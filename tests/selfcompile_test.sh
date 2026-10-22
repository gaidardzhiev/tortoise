#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

d=tests/selfcompile
img=$d/disk.tmp
fail=0
mkdir -p $d
cp selfcompile.img $img
printf '%s\n' 'mkdir -p /src/stage2 /src/stage3' 'cp /bin/as /bin/tlink /src/stage2' 'cp /bin/as /bin/tlink /src/stage3' 'cd /src/cc' 'tcc0 -L /lib main.c pp.c parse.c gen.c -o /src/stage2/tcc0' 'echo stage 2 status $?' '/src/stage2/tcc0 -L /lib main.c pp.c parse.c gen.c -o /src/stage3/tcc0' 'echo stage 3 status $?' '/src/stage3/tcc0 -L /lib /src/hello.c -o /src/hello' '/src/hello' 'shutdown 0' > $d/input.tmp
start=$(date +%s)
./tortoise $NOCACHE -m 64 -t -c -x 100000000000 -r rom/rom.bin -D $img -i $d/input.tmp > $d/out.tmp 2> $d/count.tmp
rc=$?
secs=$(($(date +%s) - start))
echo "selfcompile: two generations of tcc0 built inside TORTOISE in $secs seconds, $(cat $d/count.tmp)"
if [ $rc -ne 0 ] || ! grep -q '^stage 2 status 0$' $d/out.tmp || ! grep -q '^stage 3 status 0$' $d/out.tmp; then
	echo "selfcompile: FAIL, a compilation inside TORTOISE did not finish (exit $rc)"
	cat $d/out.tmp
	exit 1
fi
./tdisk -x $img /src/stage2/tcc0 $d/stage2.tmp > /dev/null || fail=1
./tdisk -x $img /src/stage3/tcc0 $d/stage3.tmp > /dev/null || fail=1
if ! cmp -s os/tools/tcc0 $d/stage2.tmp; then
	echo "selfcompile: FAIL, the tcc0 built inside differs from os/tools/tcc0, built by the host"
	fail=1
else
	echo "selfcompile: stage 2, tcc0 built by tcc0 inside TORTOISE, equals os/tools/tcc0"
fi
if ! cmp -s $d/stage2.tmp $d/stage3.tmp; then
	echo "selfcompile: FAIL, stage 3 differs from stage 2"
	fail=1
else
	echo "selfcompile: stage 3, tcc0 built by stage 2, equals stage 2"
fi
if ! grep -q '^argc=1 len=' $d/out.tmp; then
	echo "selfcompile: FAIL, a program built by stage 3 does not run"
	fail=1
else
	echo "selfcompile: a program built by stage 3 runs"
fi
if [ $fail -ne 0 ]; then
	cat $d/out.tmp
	exit 1
fi
rm -rf $d
echo "selfcompile: ok"
