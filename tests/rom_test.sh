#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

fail=0
d=tests/rom
./as $d/bootx.asm $d/bootx.tmp > /dev/null || exit 1
cp $d/bootx.tmp $d/disk.tmp
dd if=/dev/zero bs=512 count=7 >> $d/disk.tmp 2> /dev/null
out=$(./tortoise $NOCACHE -t -x 10000000 -r rom/rom.bin -D $d/disk.tmp)
rc=$?
want=$(printf 'TORTOISE ROM\nX')
if [ $rc -ne 0 ] || [ "$out" != "$want" ]; then
	echo "rom boot: rc=$rc output '$out'"
	fail=1
else
	echo "rom boot: ok"
fi
dd if=/dev/zero bs=512 count=8 of=$d/disk.tmp 2> /dev/null
printf 'g 0\n' > $d/in.tmp
out=$(./tortoise $NOCACHE -t -x 10000000 -r rom/rom.bin -D $d/disk.tmp -i $d/in.tmp 2> /dev/null)
rc=$?
case "$out" in
"TORTOISE ROM
no boot disk
> g 0"*) echo "rom unsigned disk: ok" ;;
*) echo "rom unsigned disk: output '$out'"; fail=1 ;;
esac
./tortoise $NOCACHE -t -x 10000000 -r rom/rom.bin -i $d/monitor_input.txt > $d/out.tmp
rc=$?
if [ $rc -ne 7 ] || ! cmp -s $d/out.tmp $d/monitor_expected.txt; then
	echo "rom monitor: rc=$rc"
	diff $d/out.tmp $d/monitor_expected.txt
	fail=1
else
	echo "rom monitor: ok"
fi
rm -f $d/*.tmp
exit $fail
