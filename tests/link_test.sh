#!/bin/sh
d=tests/link
./as -c $d/a.s -o $d/a.tmp.o > /dev/null || exit 1
./as -c $d/b.s -o $d/b.tmp.o > /dev/null || exit 1
./tlink -T 0x8000 -o $d/ab.tmp.tex $d/a.tmp.o $d/b.tmp.o || exit 1
out=$(./tortoise $NOCACHE $d/ab.tmp.tex)
rc=$?
./tlink -b -T 0x0 -o $d/ab.tmp.bin $d/a.tmp.o $d/b.tmp.o || exit 1
out2=$(./tortoise $NOCACHE $d/ab.tmp.bin)
rc2=$?
rm -f $d/*.tmp.*
want=$(printf 'hello from a\nhello from b')
if [ $rc -ne 0 ] || [ "$out" != "$want" ] || [ $rc2 -ne 0 ] || [ "$out2" != "$want" ]; then
	echo "link: rc=$rc '$out' rc2=$rc2 '$out2'"
	exit 1
fi
echo "link: ok"
