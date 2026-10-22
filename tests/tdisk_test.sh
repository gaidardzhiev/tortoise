#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

d=tests/td.tmp
rm -rf $d
mkdir -p $d
fail=0
printf 'small file\n' > $d/a.txt
: > $d/empty.txt
dd if=/dev/urandom of=$d/big.bin bs=1024 count=300 2> /dev/null
dd if=/dev/urandom of=$d/mid.bin bs=512 count=13 2> /dev/null
./tdisk -o $d/disk.img -s 4096 -b os/boot/boot.bin -k os/kernel/kernel.tex $d/a.txt:/a.txt $d/empty.txt:/x/y/empty.txt $d/big.bin:/x/big.bin $d/mid.bin:/x/y/z/mid.bin || fail=1
./tdisk -l $d/disk.img > $d/list.txt || fail=1
cat > $d/want.txt <<'LIST'
      11  /a.txt
     dir  /x/
     dir  /x/y/
       0  /x/y/empty.txt
     dir  /x/y/z/
    6656  /x/y/z/mid.bin
  307200  /x/big.bin
LIST
if ! cmp -s $d/list.txt $d/want.txt; then
	echo "tdisk: listing differs"
	diff $d/list.txt $d/want.txt
	fail=1
fi
for f in a.txt:/a.txt empty.txt:/x/y/empty.txt big.bin:/x/big.bin mid.bin:/x/y/z/mid.bin; do
	h=${f%%:*}
	p=${f#*:}
	./tdisk -x $d/disk.img $p $d/out.bin || fail=1
	if ! cmp -s $d/$h $d/out.bin; then
		echo "tdisk: $p differs after extraction"
		fail=1
	fi
done
if ./tdisk -x $d/disk.img /nothere $d/out.bin 2> /dev/null; then
	echo "tdisk: extracting a missing file succeeded"
	fail=1
fi
if ./tdisk -o $d/disk2.img -s 4096 -b os/boot/boot.bin -k os/kernel/kernel.tex $d/a.txt:/a.txt $d/a.txt:/a.txt 2> /dev/null; then
	echo "tdisk: duplicate file accepted"
	fail=1
fi
dd if=$d/disk.img bs=512 count=1 2> /dev/null | cmp -s - os/boot/boot.bin || { echo "tdisk: boot sector differs"; fail=1; }
rm -rf $d
[ $fail -eq 0 ] && echo "tdisk: ok"
exit $fail
