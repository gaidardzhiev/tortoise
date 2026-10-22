#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

# a script with comments and arguments
echo script $0 got $# args: $*
sh -c 'echo dash c with $0 and $1' zero one
echo 'echo from a plain file' > /tmp/plain
chmod +x /tmp/plain
/tmp/plain
PATH=/bin:/tmp
plain
which plain
nosuch 2>/dev/null
echo not found status $?
exit 5
