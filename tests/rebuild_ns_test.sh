#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

d=tests/rebuild
img=$d/ns.tmp.img
fail=0
mkdir -p $d/nsobj.tmp
cp src.img $img
printf '%s\n' 'cd /usr/src' 'mkdir -p /usr/obj' 'rfork n' 'ramfs9 -m /usr/obj' 'ns | grep usr' 'sh build.sh' 'echo stage one status $?' 'bind -b /usr/obj/tools /bin' 'bind -b /usr/obj/bin /bin' 'bind -b /usr/obj/lib /lib' 'unmount /usr/obj' 'sh build.sh' 'echo stage two status $?' 'ns | grep usr' 'shutdown 0' > $d/ns.in.tmp
start=$(date +%s)
./tortoise -m 64 -t -c -x 900000000000 -r rom/rom.bin -D $img -i $d/ns.in.tmp > $d/ns.out.tmp 2> $d/ns.count.tmp
rc=$?
secs=$(($(date +%s) - start))
echo "rebuild_ns: both builds ran in $secs seconds, $(cat $d/ns.count.tmp)"
if [ $rc -ne 0 ] || ! grep -q '^stage one status 0$' $d/ns.out.tmp || ! grep -q '^stage two status 0$' $d/ns.out.tmp; then
	echo "rebuild_ns: FAIL, a build inside TORTOISE did not finish (exit $rc)"
	cat $d/ns.out.tmp
	exit 1
fi
grep '^bind\|^mount' $d/ns.out.tmp | sed 's/^/rebuild_ns: /'
if ! grep -q '^mount -c socket:.* /usr/obj$' $d/ns.out.tmp; then
	echo "rebuild_ns: FAIL, /usr/obj was not on ramfs9 for the first build"
	fail=1
fi
n=0
same() {
	n=$((n + 1))
	if ! ./tdisk -x $img $2 $d/nsobj.tmp/file > /dev/null 2>&1; then
		echo "rebuild_ns: FAIL, $2 is missing"
		fail=1
	elif ! cmp -s $1 $d/nsobj.tmp/file; then
		echo "rebuild_ns: FAIL, $2 differs from $1"
		fail=1
	fi
}
for f in lib/os/*.o; do same $f /usr/obj/$f; done
for f in lib/bare/*.o; do same $f /usr/obj/$f; done
for t in as tlink tcc0; do same os/tools/$t /usr/obj/tools/$t; done
for f in os/kernel/*.o; do same $f /usr/obj/kernel/$(basename $f); done
same os/kernel/kernel.tex /usr/obj/kernel/kernel.tex
same os/boot/boot.bin /usr/obj/boot/boot.bin
for p in $PROGS; do same os/bin/$p /usr/obj/bin/$p; done
echo "rebuild_ns: compared $n files with the host build"
if [ $fail -ne 0 ]; then
	exit 1
fi
rm -rf $d/ns.*.tmp $d/ns.tmp.img $d/nsobj.tmp
echo "rebuild_ns: ok"
