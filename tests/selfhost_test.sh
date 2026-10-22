#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

d=tests/selfhost
cp selfhost.img $d/disk.tmp
./tortoise $NOCACHE -m 16 -t -x 4000000000 -r rom/rom.bin -D $d/disk.tmp -i $d/input.txt > $d/out.tmp
rc=$?
fail=0
if [ $rc -ne 0 ]; then
	echo "selfhost: exit code $rc"
	fail=1
elif ! cmp -s $d/out.tmp $d/expected.txt; then
	echo "selfhost: console output differs"
	diff $d/out.tmp $d/expected.txt
	fail=1
fi
./tdisk -x $d/disk.tmp /home/hello.s $d/inside.tmp.s || fail=1
./tcc0 -S $d/hello.c -o $d/host.tmp.s || fail=1
if ! cmp -s $d/inside.tmp.s $d/host.tmp.s; then
	echo "selfhost: assembly generated inside TORTOISE differs from the host compiler"
	fail=1
fi
rm -f $d/*.tmp $d/*.tmp.s
[ $fail -eq 0 ] && echo "selfhost: ok"
exit $fail
