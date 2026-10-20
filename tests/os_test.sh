#!/bin/sh
cp disk.img tests/os/disk.tmp
./tortoise $NOCACHE -m 16 -t -x 2000000000 -r rom/rom.bin -D tests/os/disk.tmp -i tests/os/input.txt > tests/os/out.tmp
rc=$?
rm -f tests/os/disk.tmp
if [ $rc -ne 0 ]; then
	echo "os test: exit code $rc"
	cat tests/os/out.tmp
	exit 1
fi
if ! cmp -s tests/os/out.tmp tests/os/expected.txt; then
	echo "os test: output differs"
	diff tests/os/out.tmp tests/os/expected.txt
	exit 1
fi
rm -f tests/os/out.tmp
echo "os test: ok"
