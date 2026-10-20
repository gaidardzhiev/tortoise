#include "kernel.h"

#define CHUNK 4096

static File files[NFILE];
static Pipe pipes[NPIPE];
WaitQueue poll_wq;
static unsigned pipe_ids[NPIPE];
static unsigned next_pipe_id = 1;

int pty_lookup(Vnode *dir, const char *name, Vnode **out) {
	(void)dir;
	(void)name;
	(void)out;
	return -ENOENT;
}

int pty_read(Vnode *v, char *buf, unsigned n, File *f) {
	(void)v;
	(void)buf;
	(void)n;
	(void)f;
	return -EINVAL;
}

int pty_write(Vnode *v, const char *buf, unsigned n, File *f) {
	(void)v;
	(void)buf;
	(void)n;
	(void)f;
	return -EINVAL;
}

int pty_readdir(Vnode *v, unsigned *off, Dirent *de, unsigned first) {
	(void)v;
	(void)off;
	(void)de;
	(void)first;
	return 0;
}

int pty_getattr(Vnode *v, Stat2 *st) {
	(void)v;
	(void)st;
	return -ENOENT;
}

int pty_ioctl(Vnode *v, unsigned req, unsigned arg, File *f) {
	(void)v;
	(void)req;
	(void)arg;
	(void)f;
	return -EINVAL;
}

int pty_poll(Vnode *v, File *f) {
	(void)v;
	(void)f;
	return POLLNVAL;
}

File *file_alloc(void) {
	int i;
	for (i = 0; i < NFILE; i++)
		if (files[i].ref == 0) {
			memset(&files[i], 0, sizeof(File));
			files[i].ref = 1;
			return &files[i];
		}
	return NULL;
}

File *file_dup(File *f) {
	f->ref++;
	return f;
}

void file_close(File *f) {
	if (--f->ref > 0) return;
	if (f->type == FD_PIPE) {
		Pipe *p = f->pipe;
		if (f->readable) p->readers--;
		if (f->writable) p->writers--;
		wakeup(&p->rwq);
		wakeup(&p->wwq);
		wakeup(&poll_wq);
		if (p->readers == 0 && p->writers == 0) p->used = 0;
	} else if (f->type == FD_INODE && f->vn) {
		vput(f->vn);
	}
	f->type = FD_NONE;
	f->vn = NULL;
}

int fd_alloc_from(File *f, int min) {
	int i;
	for (i = min < 0 ? 0 : min; i < NOFILE; i++)
		if (!curproc->files[i]) {
			curproc->files[i] = f;
			curproc->fdflags[i] = 0;
			return i;
		}
	return -EMFILE;
}

int fd_alloc(File *f) {
	return fd_alloc_from(f, 0);
}

static File *getf(int fd) {
	if (fd < 0 || fd >= NOFILE) return NULL;
	return curproc->files[fd];
}

static int copy_path(char *path, unsigned up) {
	int r = copyinstr(path, up, 256);
	if (r == -EINVAL) return -ENAMETOOLONG;
	if (r == 0) return -ENOENT;
	return r < 0 ? r : 0;
}

File *open_vnode(Vnode *v, int flags) {
	File *f = file_alloc();
	if (!f) return NULL;
	f->type = FD_INODE;
	f->vn = v;
	f->flags = (unsigned)flags;
	f->readable = (flags & 3) != O_WRONLY;
	f->writable = (flags & 3) != O_RDONLY;
	f->append = (flags & O_APPEND) != 0;
	f->nonblock = (flags & O_NONBLOCK) != 0;
	return f;
}

int sys_open(unsigned upath, int flags) {
	char path[256], name[28];
	Vnode *v = NULL, *dir;
	Stat2 st;
	File *f;
	int r = copy_path(path, upath), fd, acc;
	if (r) return r;
	if (flags & O_CREAT) {
		r = vfs_parent(path, curproc->cwd, &dir, name);
		if (r) return r;
		r = dir->fs->ops->lookup(dir, name, &v);
		if (r == 0) {
			vput(v);
			v = NULL;
			if (flags & O_EXCL) {
				vput(dir);
				return -EEXIST;
			}
			vput(dir);
			r = vfs_resolve(path, curproc->cwd, !(flags & O_NOFOLLOW), &v);
			if (r) return r;
		} else if (r == -ENOENT) {
			if (!dir->fs->ops->create) {
				vput(dir);
				return -EACCES;
			}
			r = vfs_access(dir, 2);
			if (r == 0) r = dir->fs->ops->create(dir, name, 0666 & ~curproc->umask, &v);
			vput(dir);
			if (r) return r;
		} else {
			vput(dir);
			return r;
		}
	} else {
		r = vfs_resolve(path, curproc->cwd, !(flags & O_NOFOLLOW), &v);
		if (r) return r;
	}
	if (v->type == VT_LNK) {
		vput(v);
		return -ELOOP;
	}
	if ((flags & O_DIRECTORY) && v->type != VT_DIR) {
		vput(v);
		return -ENOTDIR;
	}
	if (v->type == VT_DIR && (flags & 3) != O_RDONLY) {
		vput(v);
		return -EISDIR;
	}
	acc = ((flags & 3) != O_WRONLY ? 4 : 0) | ((flags & 3) != O_RDONLY ? 2 : 0);
	if (!(flags & O_CREAT) || (flags & O_TRUNC)) {
		if ((r = vfs_access(v, acc)) != 0) {
			vput(v);
			return r;
		}
	}
	if (v->type == VT_SOCK) {
		vput(v);
		return -ENXIO;
	}
	if ((flags & O_TRUNC) && v->type == VT_REG && (flags & 3) != O_RDONLY) v->fs->ops->truncate(v, 0);
	vfs_getattr(v, &st);
	f = open_vnode(v, flags);
	if (!f) {
		vput(v);
		return -EMFILE;
	}
	fd = fd_alloc(f);
	if (fd < 0) {
		file_close(f);
		return fd;
	}
	if (flags & O_CLOEXEC) curproc->fdflags[fd] = FD_CLOEXEC;
	return fd;
}

static int pipe_read(Pipe *p, char *dst, unsigned n, int nonblock) {
	unsigned i = 0;
	while (p->nread == p->nwrite && p->writers > 0) {
		if (signal_pending()) return -EINTR;
		if (nonblock) return -EAGAIN;
		sleep_on(&p->rwq);
	}
	while (i < n && p->nread != p->nwrite) dst[i++] = p->buf[p->nread++ % PIPESIZE];
	wakeup(&p->wwq);
	wakeup(&poll_wq);
	return (int)i;
}

static int pipe_write(Pipe *p, const char *src, unsigned n, int nonblock) {
	unsigned i;
	for (i = 0; i < n; i++) {
		while (p->nwrite - p->nread == PIPESIZE) {
			if (p->readers == 0 || signal_pending()) return i ? (int)i : (p->readers ? -EINTR : -EPIPE);
			if (nonblock) return i ? (int)i : -EAGAIN;
			wakeup(&p->rwq);
			wakeup(&poll_wq);
			sleep_on(&p->wwq);
		}
		if (p->readers == 0) return i ? (int)i : -EPIPE;
		p->buf[p->nwrite++ % PIPESIZE] = src[i];
	}
	wakeup(&p->rwq);
	wakeup(&poll_wq);
	return (int)n;
}

static int seekable(File *f) {
	return f->type == FD_INODE && (f->vn->type == VT_REG || f->vn->type == VT_DIR || devfs_disk_of(f->vn) >= 0 || devfs_is_fb(f->vn) || f->vn->type == VT_LNK);
}

int sys_read(int fd, unsigned ubuf, unsigned n) {
	File *f = getf(fd);
	char *kbuf;
	int r;
	if (!f || !f->readable) return -EBADF;
	if (!user_ok(ubuf, n, 1)) return -EFAULT;
	if (n > CHUNK) n = CHUNK;
	kbuf = kmalloc(n ? n : 1);
	if (!kbuf) return -ENOMEM;
	r = do_read(f, kbuf, n);
	if (r > 0 && copyout(ubuf, kbuf, (unsigned)r)) r = -EFAULT;
	kfree(kbuf);
	return r;
}

int do_read(File *f, char *kbuf, unsigned n) {
	int r;
	if (f->type == FD_PIPE) r = pipe_read(f->pipe, kbuf, n, f->nonblock);
	else if (f->type == FD_SOCKET) r = sock_read(f, kbuf, n);
	else if (f->type == FD_INODE) {
		if (f->vn->type == VT_DIR) return -EISDIR;
		if (!f->vn->fs->ops->read) return -EINVAL;
		r = f->vn->fs->ops->read(f->vn, kbuf, f->off, n, f);
		if (r > 0 && seekable(f)) f->off += (unsigned)r;
	} else return -EBADF;
	return r;
}

int sys_write(int fd, unsigned ubuf, unsigned n) {
	File *f = getf(fd);
	char *kbuf;
	int r;
	if (!f || !f->writable) return -EBADF;
	if (!user_ok(ubuf, n, 0)) return -EFAULT;
	kbuf = kmalloc(CHUNK);
	if (!kbuf) return -ENOMEM;
	r = write_loop(f, ubuf, n, kbuf);
	kfree(kbuf);
	return r;
}

int write_loop(File *f, unsigned ubuf, unsigned n, char *kbuf) {
	unsigned done = 0;
	while (done < n) {
		unsigned k = n - done;
		int r;
		if (k > CHUNK) k = CHUNK;
		if (copyin(kbuf, ubuf + done, k)) return -EFAULT;
		if (f->type == FD_PIPE) r = pipe_write(f->pipe, kbuf, k, f->nonblock);
		else if (f->type == FD_SOCKET) r = sock_write(f, kbuf, k);
		else if (f->type == FD_INODE) {
			if (!f->vn->fs->ops->write) return -EINVAL;
			if (f->append && f->vn->type == VT_REG) {
				Stat2 st;
				vfs_getattr(f->vn, &st);
				f->off = st.size;
			}
			r = f->vn->fs->ops->write(f->vn, kbuf, f->off, k, f);
			if (r > 0 && seekable(f)) f->off += (unsigned)r;
		} else return -EBADF;
		if (r == -EPIPE) send_sigpipe();
		if (r < 0) return done ? (int)done : r;
		done += (unsigned)r;
		if ((unsigned)r < k) break;
	}
	return (int)done;
}

int sys_close(int fd) {
	File *f = getf(fd);
	if (!f) return -EBADF;
	curproc->files[fd] = NULL;
	curproc->fdflags[fd] = 0;
	file_close(f);
	return 0;
}

int sys_dup(int fd) {
	File *f = getf(fd);
	int nfd;
	if (!f) return -EBADF;
	nfd = fd_alloc(f);
	if (nfd >= 0) file_dup(f);
	return nfd;
}

int sys_dup2(int a, int b) {
	File *f = getf(a);
	if (!f || b < 0 || b >= NOFILE) return -EBADF;
	if (a == b) return b;
	if (curproc->files[b]) sys_close(b);
	curproc->files[b] = file_dup(f);
	curproc->fdflags[b] = 0;
	return b;
}

int sys_pipe(unsigned ufds) {
	Pipe *p = NULL;
	File *rf, *wf;
	int fds[2], i;
	if (!user_ok(ufds, 8, 1)) return -EFAULT;
	for (i = 0; i < NPIPE; i++)
		if (!pipes[i].used) {
			p = &pipes[i];
			pipe_ids[i] = next_pipe_id++;
			break;
		}
	if (!p) return -EMFILE;
	rf = file_alloc();
	if (!rf) return -EMFILE;
	wf = file_alloc();
	if (!wf) {
		rf->ref = 0;
		return -EMFILE;
	}
	memset(p, 0, sizeof(Pipe));
	p->used = 1;
	p->readers = 1;
	p->writers = 1;
	rf->type = wf->type = FD_PIPE;
	rf->pipe = wf->pipe = p;
	rf->readable = 1;
	wf->writable = 1;
	fds[0] = fd_alloc(rf);
	fds[1] = fds[0] < 0 ? -1 : fd_alloc(wf);
	if (fds[0] < 0 || fds[1] < 0) {
		if (fds[0] >= 0) curproc->files[fds[0]] = NULL;
		file_close(rf);
		file_close(wf);
		return -EMFILE;
	}
	copyout(ufds, fds, 8);
	return 0;
}

unsigned pipe_id(Pipe *p) {
	return pipe_ids[p - pipes];
}

int sys_readdir(int fd, unsigned udp) {
	File *f = getf(fd);
	Dirent de;
	int r;
	if (!f || f->type != FD_INODE) return -EBADF;
	if (f->vn->type != VT_DIR || !f->vn->fs->ops->readdir) return -ENOTDIR;
	memset(&de, 0, sizeof(de));
	r = f->vn->fs->ops->readdir(f->vn, &f->off, &de);
	if (r <= 0) return r;
	de.name[27] = 0;
	return copyout(udp, &de, sizeof(de)) ? -EFAULT : 1;
}

static int with_parent(unsigned upath, Vnode **dir, char *name) {
	char path[256];
	int r = copy_path(path, upath);
	if (r) return r;
	return vfs_parent(path, curproc->cwd, dir, name);
}

static int sticky_ok(Vnode *dir, Vnode *v) {
	Stat2 ds, vs;
	if (!curproc || curproc->euid == 0) return 0;
	vfs_getattr(dir, &ds);
	if (!(ds.mode & 01000)) return 0;
	vfs_getattr(v, &vs);
	if (vs.uid == curproc->euid || ds.uid == curproc->euid) return 0;
	return -EPERM;
}

int sys_mkdir(unsigned upath) {
	Vnode *dir, *v;
	char name[28];
	int r = with_parent(upath, &dir, name);
	if (r) return r;
	if (dir->fs->ops->lookup(dir, name, &v) == 0) {
		vput(v);
		vput(dir);
		return -EEXIST;
	}
	r = vfs_access(dir, 3);
	if (r == 0) r = dir->fs->ops->mkdir ? dir->fs->ops->mkdir(dir, name, 0777 & ~curproc->umask) : -EPERM;
	vput(dir);
	return r;
}

int sys_chdir(unsigned upath) {
	char path[256];
	Vnode *v;
	int r = copy_path(path, upath);
	if (r) return r;
	r = vfs_resolve(path, curproc->cwd, 1, &v);
	if (r) return r;
	if (v->type != VT_DIR) {
		vput(v);
		return -ENOTDIR;
	}
	if ((r = vfs_access(v, 1)) != 0) {
		vput(v);
		return r;
	}
	vput(curproc->cwd);
	curproc->cwd = v;
	return 0;
}

static int remove_common(unsigned upath, int mode) {
	Vnode *dir, *v;
	char name[28];
	int r = with_parent(upath, &dir, name);
	if (r) return r;
	if (!strcmp(name, ".") || !strcmp(name, "..")) {
		vput(dir);
		return -EINVAL;
	}
	r = dir->fs->ops->lookup(dir, name, &v);
	if (r) {
		vput(dir);
		return r;
	}
	if (v->mounted || (v->fs != dir->fs)) {
		vput(v);
		vput(dir);
		return -EBUSY;
	}
	if ((r = vfs_access(dir, 3)) != 0 || (r = sticky_ok(dir, v)) != 0) {
		vput(v);
		vput(dir);
		return r;
	}
	if (!dir->fs->ops->unlink) {
		vput(v);
		vput(dir);
		return -EPERM;
	}
	if (v->type == VT_DIR) {
		vput(v);
		r = mode == 1 ? -EISDIR : dir->fs->ops->rmdir(dir, name);
	} else {
		vput(v);
		r = mode == 2 ? -ENOTDIR : dir->fs->ops->unlink(dir, name);
	}
	vput(dir);
	return r;
}

int sys_unlink(unsigned upath) {
	return remove_common(upath, 0);
}

int sys_rmdir(unsigned upath) {
	return remove_common(upath, 2);
}

int sys_stat(unsigned upath, unsigned usp) {
	char path[256];
	Vnode *v;
	Stat2 s2;
	Stat st;
	int r = copy_path(path, upath);
	if (r) return r;
	r = vfs_resolve(path, curproc->cwd, 1, &v);
	if (r) return r;
	vfs_getattr(v, &s2);
	st.type = v->type == VT_DIR ? 2 : 1;
	st.size = s2.size;
	st.inode = s2.ino;
	vput(v);
	return copyout(usp, &st, sizeof(st)) ? -EFAULT : 0;
}

int sys_stat2(unsigned upath, unsigned usp, int follow) {
	char path[256];
	Vnode *v;
	Stat2 s2;
	int r = copy_path(path, upath);
	if (r) return r;
	r = vfs_resolve(path, curproc->cwd, follow, &v);
	if (r) return r;
	vfs_getattr(v, &s2);
	vput(v);
	return copyout(usp, &s2, sizeof(s2)) ? -EFAULT : 0;
}

int sys_fstat(int fd, unsigned usp) {
	File *f = getf(fd);
	Stat2 s2;
	if (!f) return -EBADF;
	memset(&s2, 0, sizeof(s2));
	if (f->type == FD_INODE) vfs_getattr(f->vn, &s2);
	else if (f->type == FD_PIPE) {
		s2.mode = S_IFIFO | 0600;
		s2.ino = pipe_id(f->pipe);
		s2.nlink = 1;
		s2.size = f->pipe->nwrite - f->pipe->nread;
	} else if (f->type == FD_SOCKET) {
		s2.mode = S_IFSOCK | 0777;
		s2.nlink = 1;
	}
	return copyout(usp, &s2, sizeof(s2)) ? -EFAULT : 0;
}

int sys_lseek(int fd, int off, int whence) {
	File *f = getf(fd);
	int base;
	if (!f) return -EBADF;
	if (!seekable(f)) return -ESPIPE;
	if (whence == 0) base = 0;
	else if (whence == 1) base = (int)f->off;
	else if (whence == 2) {
		Stat2 st;
		vfs_getattr(f->vn, &st);
		base = (int)st.size;
	} else return -EINVAL;
	if (base + off < 0) return -EINVAL;
	f->off = (unsigned)(base + off);
	return (int)f->off;
}

static int is_ancestor(Vnode *a, Vnode *d) {
	Vnode *v = vref(d);
	int depth = 0;
	while (depth++ < 256) {
		Vnode *p;
		if (v == a) {
			vput(v);
			return 1;
		}
		if (v == v->fs->root) break;
		if (v->fs->ops->lookup(v, "..", &p)) break;
		if (p == v) {
			vput(p);
			break;
		}
		vput(v);
		v = p;
	}
	vput(v);
	return 0;
}

int sys_rename(unsigned ua, unsigned ub) {
	Vnode *od, *nd, *src;
	char on[28], nn[28];
	int r = with_parent(ua, &od, on);
	if (r) return r;
	r = with_parent(ub, &nd, nn);
	if (r) {
		vput(od);
		return r;
	}
	if (od->fs != nd->fs) r = -EXDEV;
	else if (!od->fs->ops->rename) r = -EPERM;
	else if (!strcmp(on, ".") || !strcmp(on, "..") || !strcmp(nn, ".") || !strcmp(nn, "..")) r = -EINVAL;
	else if ((r = od->fs->ops->lookup(od, on, &src)) == 0) {
		Vnode *t;
		if (src->mounted) r = -EBUSY;
		else if (src->type == VT_DIR && is_ancestor(src, nd)) r = -EINVAL;
		else if ((r = vfs_access(od, 3)) == 0 && (r = vfs_access(nd, 3)) == 0 && (r = sticky_ok(od, src)) == 0) {
			if (nd->fs->ops->lookup(nd, nn, &t) == 0) {
				if (t->mounted) r = -EBUSY;
				else r = sticky_ok(nd, t);
				vput(t);
			}
			if (r == 0) r = od->fs->ops->rename(od, on, nd, nn);
		}
		vput(src);
	}
	vput(od);
	vput(nd);
	return r;
}

int sys_getcwd(unsigned ubuf, unsigned size) {
	char buf[512];
	int r = sys_getcwd_k(buf, sizeof(buf));
	if (r < 0) return r;
	if ((unsigned)r > size) return -ERANGE;
	return copyout(ubuf, buf, (unsigned)r) ? -EFAULT : r;
}

int sys_link(unsigned ua, unsigned ub) {
	char path[256], name[28];
	Vnode *v, *dir;
	int r = copy_path(path, ua);
	if (r) return r;
	r = vfs_resolve(path, curproc->cwd, 0, &v);
	if (r) return r;
	r = with_parent(ub, &dir, name);
	if (r) {
		vput(v);
		return r;
	}
	if (v->type == VT_DIR) r = -EPERM;
	else if (v->fs != dir->fs) r = -EXDEV;
	else if (!dir->fs->ops->link) r = -EPERM;
	else if ((r = vfs_access(dir, 3)) == 0) r = dir->fs->ops->link(dir, name, v);
	vput(v);
	vput(dir);
	return r;
}

int sys_symlink(unsigned ua, unsigned ub) {
	char target[256], name[28];
	Vnode *dir, *v;
	int r = copy_path(target, ua);
	if (r) return r;
	r = with_parent(ub, &dir, name);
	if (r) return r;
	if (dir->fs->ops->lookup(dir, name, &v) == 0) {
		vput(v);
		r = -EEXIST;
	} else if (!dir->fs->ops->symlink) r = -EPERM;
	else if ((r = vfs_access(dir, 3)) == 0) r = dir->fs->ops->symlink(dir, name, target);
	vput(dir);
	return r;
}

int sys_readlink(unsigned upath, unsigned ubuf, unsigned size) {
	char path[256], buf[256];
	Vnode *v;
	int r = copy_path(path, upath), n;
	if (r) return r;
	r = vfs_resolve(path, curproc->cwd, 0, &v);
	if (r) return r;
	if (v->type != VT_LNK) {
		vput(v);
		return -EINVAL;
	}
	n = v->fs->ops->readlink(v, buf, sizeof(buf));
	vput(v);
	if (n < 0) return n;
	if ((unsigned)n > size) n = (int)size;
	return copyout(ubuf, buf, (unsigned)n) ? -EFAULT : n;
}

static int trunc_vnode(Vnode *v, unsigned len) {
	if (v->type == VT_DIR) return -EISDIR;
	if (v->type != VT_REG || !v->fs->ops->truncate) return -EINVAL;
	return v->fs->ops->truncate(v, len);
}

int sys_truncate(unsigned upath, unsigned len) {
	char path[256];
	Vnode *v;
	int r = copy_path(path, upath);
	if (r) return r;
	r = vfs_resolve(path, curproc->cwd, 1, &v);
	if (r) return r;
	r = vfs_access(v, 2);
	if (r == 0) r = trunc_vnode(v, len);
	vput(v);
	return r;
}

int sys_ftruncate(int fd, unsigned len) {
	File *f = getf(fd);
	if (!f || f->type != FD_INODE) return -EBADF;
	if (!f->writable) return -EINVAL;
	return trunc_vnode(f->vn, len);
}

int sys_utime(unsigned upath, unsigned tp) {
	char path[256];
	Vnode *v;
	unsigned t[2];
	int r = copy_path(path, upath);
	if (r) return r;
	if (tp) {
		if (copyin(t, tp, 8)) return -EFAULT;
	} else {
		t[0] = t[1] = vfs_now();
	}
	r = vfs_resolve(path, curproc->cwd, 1, &v);
	if (r) return r;
	if (tp) {
		Stat2 st;
		vfs_getattr(v, &st);
		if (curproc->euid && st.uid != curproc->euid) r = -EPERM;
	} else if (curproc->euid) {
		Stat2 st;
		vfs_getattr(v, &st);
		if (st.uid != curproc->euid && vfs_access(v, 2)) r = -EACCES;
	}
	if (r == 0) r = v->fs->ops->setattr ? v->fs->ops->setattr(v, SA_TIMES, t[0], t[1]) : -EPERM;
	vput(v);
	return r;
}

int sys_fcntl(int fd, int cmd, unsigned arg) {
	File *f = getf(fd);
	int nfd;
	if (!f) return -EBADF;
	switch (cmd) {
	case F_DUPFD:
		if (arg >= NOFILE) return -EINVAL;
		nfd = fd_alloc_from(f, (int)arg);
		if (nfd >= 0) file_dup(f);
		return nfd;
	case F_GETFD:
		return curproc->fdflags[fd];
	case F_SETFD:
		curproc->fdflags[fd] = (unsigned char)(arg & FD_CLOEXEC);
		return 0;
	case F_GETFL:
		return (int)((f->readable && f->writable ? O_RDWR : f->writable ? O_WRONLY : O_RDONLY) | (f->append ? O_APPEND : 0) | (f->nonblock ? O_NONBLOCK : 0));
	case F_SETFL:
		f->append = (arg & O_APPEND) != 0;
		f->nonblock = (arg & O_NONBLOCK) != 0;
		return 0;
	}
	return -EINVAL;
}

static int poll_one(File *f, int events) {
	int m;
	if (!f) return POLLNVAL;
	if (f->type == FD_PIPE) {
		Pipe *p = f->pipe;
		m = 0;
		if (f->readable) {
			if (p->nread != p->nwrite) m |= POLLIN;
			if (p->writers == 0) m |= POLLHUP | POLLIN;
		}
		if (f->writable) {
			if (p->readers == 0) m |= POLLERR;
			else if (p->nwrite - p->nread < PIPESIZE) m |= POLLOUT;
		}
	} else if (f->type == FD_SOCKET) {
		m = sock_poll(f);
	} else if (f->type == FD_INODE && f->vn->fs->ops->poll) {
		m = f->vn->fs->ops->poll(f->vn, f);
	} else {
		m = POLLIN | POLLOUT;
	}
	return m & (events | POLLERR | POLLHUP | POLLNVAL);
}

static void poll_timeout(void *arg) {
	Proc *p = arg;
	p->timer_fired = 1;
	if (p->state == P_SLEEPING && p->chan == &poll_wq) wakeup(&poll_wq);
}

int sys_poll(unsigned ufds, unsigned n, int timeout) {
	short pf[64 * 4];
	unsigned i;
	int id = 0, cons = 0;
	if (n > 64) return -EINVAL;
	if (n && copyin(pf, ufds, n * 8)) return -EFAULT;
	for (i = 0; i < n; i++) {
		int fd = *(int *)&pf[i * 4];
		File *f = getf(fd);
		if (f && f->type == FD_INODE && f->vn->type == VT_CHR && f->vn->fs->ops->poll == devfs_ops.poll) cons = 1;
	}
	curproc->timer_fired = 0;
	if (timeout > 0) id = timer_add((unsigned)timeout, poll_timeout, curproc);
	for (;;) {
		int ready = 0;
		for (i = 0; i < n; i++) {
			int fd = *(int *)&pf[i * 4];
			short ev = pf[i * 4 + 2];
			int m = fd < 0 ? 0 : poll_one(getf(fd), ev);
			pf[i * 4 + 3] = (short)m;
			if (m) ready++;
		}
		if (ready || timeout == 0 || curproc->timer_fired) {
			if (id && !curproc->timer_fired) timer_cancel(id);
			if (n && copyout(ufds, pf, n * 8)) return -EFAULT;
			return ready;
		}
		if (signal_pending()) {
			if (id) timer_cancel(id);
			return -EINTR;
		}
		if (cons) console_poll_hold(1);
		sleep_on(&poll_wq);
		if (cons) console_poll_hold(0);
	}
}

int sys_ioctl(int fd, unsigned req, unsigned arg) {
	File *f = getf(fd);
	if (!f) return -EBADF;
	if (f->type == FD_SOCKET) return sock_ioctl(f, req, arg);
	if (f->type != FD_INODE || !f->vn->fs->ops->ioctl) return -ENOTTY;
	return f->vn->fs->ops->ioctl(f->vn, req, arg, f);
}

int sys_mount(unsigned us, unsigned ut, unsigned uty) {
	char s[256], t[256], ty[16];
	int r;
	if (curproc->euid) return -EPERM;
	if ((r = copy_path(s, us)) != 0 || (r = copy_path(t, ut)) != 0) return r;
	r = copyinstr(ty, uty, sizeof(ty));
	if (r < 0) return r;
	return vfs_mount(s, t, ty);
}

int sys_umount(unsigned ut) {
	char t[256];
	int r;
	if (curproc->euid) return -EPERM;
	if ((r = copy_path(t, ut)) != 0) return r;
	return vfs_umount(t);
}

static int chmod_vnode(Vnode *v, unsigned mode) {
	Stat2 st;
	vfs_getattr(v, &st);
	if (curproc->euid && st.uid != curproc->euid) return -EPERM;
	if (curproc->euid && (mode & 02000) && !in_group(st.gid)) mode &= ~02000u;
	if (!v->fs->ops->setattr) return -EPERM;
	return v->fs->ops->setattr(v, SA_MODE, mode & 07777, 0);
}

int sys_chmod(unsigned upath, unsigned mode) {
	char path[256];
	Vnode *v;
	int r = copy_path(path, upath);
	if (r) return r;
	r = vfs_resolve(path, curproc->cwd, 1, &v);
	if (r) return r;
	r = chmod_vnode(v, mode);
	vput(v);
	return r;
}

int sys_fchmod(int fd, unsigned mode) {
	File *f = getf(fd);
	if (!f) return -EBADF;
	if (f->type != FD_INODE) return -EINVAL;
	return chmod_vnode(f->vn, mode);
}
