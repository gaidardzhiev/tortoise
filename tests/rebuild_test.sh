#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

d=tests/rebuild
img=$d/run.tmp.img
fail=0
mkdir -p $d/obj.tmp
cp src.img $img
printf 'cd /usr/src\nsh build.sh\necho build status $?\nkinstall /usr/obj/kernel/kernel.tex\nshutdown 0\n' > $d/in1.tmp
start=$(date +%s)
./tortoise -m 64 -t -c -x 400000000000 -r rom/rom.bin -D $img -i $d/in1.tmp > $d/out1.tmp 2> $d/count.tmp
rc=$?
secs=$(($(date +%s) - start))
echo "rebuild: build.sh ran in $secs seconds, $(cat $d/count.tmp)"
if [ $rc -ne 0 ] || ! grep -q '^build status 0$' $d/out1.tmp || ! grep -q '^kinstall: ' $d/out1.tmp; then
	echo "rebuild: FAIL, the build inside TORTOISE did not finish (exit $rc)"
	cat $d/out1.tmp
	exit 1
fi
n=0
same() {
	n=$((n + 1))
	if ! ./tdisk -x $img $2 $d/obj.tmp/file > /dev/null 2>&1; then
		echo "rebuild: FAIL, $2 is missing"
		fail=1
	elif ! cmp -s $1 $d/obj.tmp/file; then
		echo "rebuild: FAIL, $2 differs from $1"
		fail=1
	fi
}
for f in lib/os/*.o; do same $f /usr/obj/$f; done
for f in lib/bare/*.o; do same $f /usr/obj/$f; done
for t in as tlink tcc0; do same os/tools/$t /usr/obj/tools/$t; done
for f in os/kernel/*.o; do same $f /usr/obj/kernel/$(basename $f); done
same os/kernel/kernel.tex /usr/obj/kernel/kernel.tex
same os/boot/boot.bin /usr/obj/boot/boot.bin
for p in $(cat $d/PROGS.tmp); do same os/bin/$p /usr/obj/bin/$p; done
echo "rebuild: compared $n files with the host build"
printf 'echo the rebuilt kernel runs\nuname -s -r\nls /usr/obj/kernel/kernel.tex\nshutdown 0\n' > $d/in2.tmp
./tortoise -m 64 -t -x 4000000000 -r rom/rom.bin -D $img -i $d/in2.tmp > $d/out2.tmp 2>&1
rc=$?
if [ $rc -ne 0 ] || ! grep -q 'TORTOISE OS booting' $d/out2.tmp || ! grep -q '^the rebuilt kernel runs$' $d/out2.tmp || ! grep -q '^TORTOISE 3.0$' $d/out2.tmp; then
	echo "rebuild: FAIL, the installed kernel does not boot"
	cat $d/out2.tmp
	fail=1
else
	echo "rebuild: the installed kernel boots and the shell works"
fi
if [ $fail -ne 0 ]; then
	exit 1
fi
rm -rf $d/*.tmp $d/obj.tmp
echo "rebuild: ok"
