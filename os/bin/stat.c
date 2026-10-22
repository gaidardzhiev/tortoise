/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

static const char *kind(unsigned m) {
	if (S_ISREG(m)) return "regular file";
	if (S_ISDIR(m)) return "directory";
	if (S_ISLNK(m)) return "symbolic link";
	if (S_ISCHR(m)) return "character device";
	if (S_ISFIFO(m)) return "fifo";
	return "socket";
}

int main(int argc, char **argv) {
	int i, rc = 0;
	for (i = 1; i < argc; i++) {
		struct stat st;
		if (lstat(argv[i], &st) < 0) {
			fprintf(stderr, "stat: cannot stat %s\n", argv[i]);
			rc = 1;
			continue;
		}
		printf("  File: %s\n", argv[i]);
		printf("  Size: %u\tBlocks: %u\t%s\n", st.st_size, st.st_blocks, kind(st.st_mode));
		printf("Device: %x\tInode: %u\tLinks: %u\n", st.st_dev, st.st_ino, st.st_nlink);
		printf("Access: %04o\tUid: %u\tGid: %u\n", st.st_mode & 07777, st.st_uid, st.st_gid);
		printf("Access: %u\nModify: %u\nChange: %u\n", st.st_atime, st.st_mtime, st.st_ctime);
	}
	return rc;
}
