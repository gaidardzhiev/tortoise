/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <pwd.h>

int main(void) {
	struct passwd *pw = getpwuid(geteuid());
	if (pw) printf("%s\n", pw->pw_name);
	else printf("%d\n", geteuid());
	return 0;
}
