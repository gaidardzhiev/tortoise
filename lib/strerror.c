/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>

static const char *messages[] = {
	"Success", "Operation not permitted", "No such file or directory", "No such process",
	"Interrupted system call", "Input/output error", "No such device or address",
	"Argument list too long", "Exec format error", "Bad file descriptor", "No child processes",
	"Resource temporarily unavailable", "Out of memory", "Permission denied", "Bad address",
	"Block device required", "Device or resource busy", "File exists", "Cross-device link",
	"No such device", "Not a directory", "Is a directory", "Invalid argument",
	"Too many open files in system", "Too many open files", "Not a terminal",
	"Text file busy", "File too large", "No space left on device", "Illegal seek",
	"Read-only file system", "Too many links", "Broken pipe", "Numerical argument out of domain",
	"Numerical result out of range", "Resource deadlock avoided", "File name too long",
	"No locks available", "Function not implemented", "Directory not empty",
	"Too many levels of symbolic links"
};

static const char *net_messages[] = {
	"Socket operation on non-socket", "Destination address required", "Message too long",
	"Protocol wrong type for socket", "Protocol not available", "Protocol not supported",
	"Socket type not supported", "Operation not supported", "Protocol family not supported",
	"Address family not supported by protocol", "Address already in use",
	"Cannot assign requested address", "Network is down", "Network is unreachable",
	"Network dropped connection on reset", "Software caused connection abort",
	"Connection reset by peer", "No buffer space available", "Transport endpoint is already connected",
	"Transport endpoint is not connected", "Cannot send after transport endpoint shutdown",
	"Too many references", "Connection timed out", "Connection refused", "Host is down",
	"No route to host", "Operation already in progress", "Operation now in progress"
};

char *strerror(int e) {
	static char buf[32];
	if (e >= 0 && e < (int)(sizeof(messages) / sizeof(messages[0]))) return (char *)messages[e];
	if (e >= 88 && e < 88 + (int)(sizeof(net_messages) / sizeof(net_messages[0]))) return (char *)net_messages[e - 88];
	snprintf(buf, sizeof(buf), "Unknown error %d", e);
	return buf;
}
