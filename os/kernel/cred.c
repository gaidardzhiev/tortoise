/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

#define NOCHANGE 0xFFFFFFFFu

int sys_setuid(unsigned uid) {
	if (curproc->euid == 0) {
		curproc->uid = curproc->euid = curproc->suid = uid;
		return 0;
	}
	if (uid != curproc->uid && uid != curproc->suid) return -EPERM;
	curproc->euid = uid;
	return 0;
}

int sys_setgid(unsigned gid) {
	if (curproc->euid == 0) {
		curproc->gid = curproc->egid = curproc->sgid = gid;
		return 0;
	}
	if (gid != curproc->gid && gid != curproc->sgid) return -EPERM;
	curproc->egid = gid;
	return 0;
}

int sys_setreuid(unsigned r, unsigned e) {
	unsigned or = curproc->uid, oe = curproc->euid, os = curproc->suid;
	if (oe != 0) {
		if (r != NOCHANGE && r != or && r != oe) return -EPERM;
		if (e != NOCHANGE && e != or && e != oe && e != os) return -EPERM;
	}
	if (r != NOCHANGE) curproc->uid = r;
	if (e != NOCHANGE) curproc->euid = e;
	if (r != NOCHANGE || (e != NOCHANGE && e != or)) curproc->suid = curproc->euid;
	return 0;
}

int sys_setregid(unsigned r, unsigned e) {
	unsigned or = curproc->gid, oe = curproc->egid, os = curproc->sgid;
	if (curproc->euid != 0) {
		if (r != NOCHANGE && r != or && r != oe) return -EPERM;
		if (e != NOCHANGE && e != or && e != oe && e != os) return -EPERM;
	}
	if (r != NOCHANGE) curproc->gid = r;
	if (e != NOCHANGE) curproc->egid = e;
	if (r != NOCHANGE || (e != NOCHANGE && e != or)) curproc->sgid = curproc->egid;
	return 0;
}

int sys_getgroups(int n, unsigned ulist) {
	if (n == 0) return curproc->ngroups;
	if (n < curproc->ngroups) return -EINVAL;
	if (curproc->ngroups && copyout(ulist, curproc->groups, (unsigned)curproc->ngroups * 4)) return -EFAULT;
	return curproc->ngroups;
}

int sys_setgroups(int n, unsigned ulist) {
	unsigned g[16];
	if (curproc->euid) return -EPERM;
	if (n < 0 || n > 16) return -EINVAL;
	if (n && copyin(g, ulist, (unsigned)n * 4)) return -EFAULT;
	memcpy(curproc->groups, g, (unsigned)n * 4);
	curproc->ngroups = n;
	return 0;
}
