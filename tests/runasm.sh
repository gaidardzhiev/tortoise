#!/bin/sh
n=$1
l=$2
shift 2
./as assembler/${n}_test.asm assembler/${n}_test.bin > /dev/null || { echo "$n: assembly failed"; exit 1; }
out=$(./tortoise $NOCACHE "$@" assembler/${n}_test.bin)
rc=$?
if [ $rc -ne 0 ]; then
	echo "$n: exit code $rc, output '$out'"
	exit 1
fi
if [ "$out" != "$l" ]; then
	echo "$n: output '$out' expected '$l'"
	exit 1
fi
echo "$n: $l ok"
