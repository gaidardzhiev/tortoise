/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>

int main(void) {
	FILE *f = fopen("/proc/net/arp", "r");
	char line[128], a[16], m[24], i[16];
	if (!f) {
		fprintf(stderr, "arp: cannot read /proc/net/arp\n");
		return 1;
	}
	printf("%-15s %-17s %s\n", "Address", "HWaddress", "Iface");
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, "%15s %23s %15s", a, m, i) == 3) printf("%-15s %-17s %s\n", a, m, i);
	fclose(f);
	return 0;
}
