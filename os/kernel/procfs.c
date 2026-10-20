#include "kernel.h"

static unsigned procfs_dev;

static int p_lookup(Vnode *dir, const char *name, Vnode **out) {
	if (!strcmp(name, "..") || !strcmp(name, ".")) {
		*out = vref(dir);
		return 0;
	}
	return -ENOENT;
}

static int p_readdir(Vnode *v, unsigned *off, Dirent *de) {
	if (*off >= 2) return 0;
	de->inode = v->ino;
	strncpy(de->name, *off ? ".." : ".", 28);
	(*off)++;
	return 1;
}

static int p_getattr(Vnode *v, Stat2 *st) {
	st->dev = v->fs->dev;
	st->ino = v->ino;
	st->mode = S_IFDIR | 0555;
	st->nlink = 2;
	return 0;
}

FsOps procfs_ops = {
	p_lookup, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, p_readdir, p_getattr, NULL, NULL, NULL, NULL, NULL, NULL
};

int procfs_mount(Fs *fs) {
	fs->ops = &procfs_ops;
	fs->dev = (4u << 8) | procfs_dev++;
	fs->root = vget(fs, 1, VT_DIR);
	return fs->root ? 0 : -ENOMEM;
}
