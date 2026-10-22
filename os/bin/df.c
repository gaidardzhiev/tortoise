/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static unsigned get32(unsigned char *p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

int main(void) {
	FILE *f = fopen("/proc/mounts", "r");
	char line[256], src[128], dst[128], type[16];
	if (!f) {
		fprintf(stderr, "df: cannot read /proc/mounts\n");
		return 1;
	}
	printf("%-12s %8s %8s %8s %s\n", "Filesystem", "1K-blocks", "Used", "Free", "Mounted on");
	while (fgets(line, sizeof(line), f)) {
		unsigned char sb[512];
		int fd, ok = 0;
		unsigned total = 0, freeb = 0;
		if (sscanf(line, "%127s %127s %15s", src, dst, type) != 3) continue;
		if (!strcmp(type, "tfs")) {
			fd = open(src, O_RDONLY);
			if (fd >= 0) {
				if (lseek(fd, 1024 * 512, SEEK_SET) >= 0 && read(fd, sb, 512) == 512 && !memcmp(sb, "TFS2", 4)) {
					total = get32(sb + 4) / 2;
					freeb = get32(sb + 24) / 2;
					ok = 1;
				}
				close(fd);
			}
		}
		if (ok) printf("%-12s %8u %8u %8u %s\n", src, total, total - freeb, freeb, dst);
		else printf("%-12s %8s %8s %8s %s\n", src, "-", "-", "-", dst);
	}
	fclose(f);
	return 0;
}
