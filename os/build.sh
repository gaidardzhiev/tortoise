#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

set -e
S=/usr/src
O=/usr/obj
date=$(cat $S/os/kernel/build.date)
step() {
	echo "build: $*"
}
mkdir -p $O/lib/os $O/lib/bare $O/stage1 $O/tools $O/kernel $O/boot $O/bin
rm -f $O/lib/include
ln -s $S/lib/include $O/lib/include

step library
for f in $(cat $S/lib/SHARED); do
	tcc0 -bare -c $S/lib/$f.c -o $O/lib/bare/$f.o
	tcc0 -c $S/lib/$f.c -o $O/lib/os/$f.o
done
for f in $(cat $S/lib/OSONLY); do
	tcc0 -c $S/lib/$f.c -o $O/lib/os/$f.o
done
tcc0 -bare -c $S/lib/sys_bare.c -o $O/lib/bare/sys_bare.o
as -c $S/lib/crt0_bare.s -o $O/lib/bare/crt0_bare.o > /dev/null
as -c $S/lib/crt0.s -o $O/lib/os/crt0.o > /dev/null
as -c $S/lib/memops.s -o $O/lib/bare/memops.o > /dev/null
as -c $S/lib/memops.s -o $O/lib/os/memops.o > /dev/null

step "as and tlink"
tcc0 -L $O/lib $S/assembler/assembler.c -o $O/tools/as
tcc0 -L $O/lib $S/tools/tlink.c -o $O/tools/tlink
cp $O/tools/as $O/tools/tlink $O/stage1

step "tcc0, first with the installed tcc0"
tcc0 -L $O/lib $S/cc/main.c $S/cc/pp.c $S/cc/parse.c $S/cc/gen.c -o $O/stage1/tcc0
step "tcc0, again with the new one"
$O/stage1/tcc0 -L $O/lib $S/cc/main.c $S/cc/pp.c $S/cc/parse.c $S/cc/gen.c -o $O/tools/tcc0
CC=$O/tools/tcc0

step kernel
objs="$O/kernel/entry.o $O/kernel/memops.o"
$O/tools/as -c $S/os/kernel/entry.s -o $O/kernel/entry.o > /dev/null
$O/tools/as -c $S/os/kernel/memops.s -o $O/kernel/memops.o > /dev/null
for f in $(cat $S/os/kernel/OBJS); do
	if [ $f = proc ]; then
		$CC -L $O/lib -c -DBUILD_DATE='"'$date'"' $S/os/kernel/$f.c -o $O/kernel/$f.o
	else
		$CC -L $O/lib -c $S/os/kernel/$f.c -o $O/kernel/$f.o
	fi
	objs="$objs $O/kernel/$f.o"
done
$O/tools/tlink -T 0x20000 -e _start -o $O/kernel/kernel.tex $objs
$O/tools/as $S/os/boot/boot.asm $O/boot/boot.bin > /dev/null

step programs
for p in $(cat $S/os/bin/PROGS); do
	$CC -L $O/lib $S/os/bin/$p.c -o $O/bin/$p
done
step done
