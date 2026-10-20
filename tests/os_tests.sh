#!/bin/sh
fail=0
base=$(cat tests/os/base.files.tmp)
for in in tests/os/*.in; do
	n=$(basename $in .in)
	img=tests/os/$n.tmp.img
	extra=""
	[ -f tests/os/$n.files ] && extra=$(cat tests/os/$n.files)
	files=""
	for w in $base; do
		keep=1
		case $w in
		*:/*)
			p=${w#*:}
			p=${p%%:*}
			for x in $extra; do
				case $x in
				*:"$p" | *:"$p":*) keep=0 ;;
				esac
			done
			;;
		esac
		[ $keep = 1 ] && files="$files $w"
	done
	./tdisk -o $img -s 65536 -b os/boot/boot.bin -k os/kernel/kernel.tex $files $extra > /dev/null || { echo "$n: image failed"; fail=1; continue; }
	disk2=""
	if [ -f tests/os/$n.disk2 ]; then
		./tdisk -o tests/os/$n.tmp2.img -s 8192 $(cat tests/os/$n.disk2) > /dev/null || { echo "$n: second image failed"; fail=1; continue; }
		disk2="-E tests/os/$n.tmp2.img"
	fi
	start=$(date +%s)
	./tortoise $NOCACHE -m 32 -t -x 4000000000 -r rom/rom.bin -D $img $disk2 -i $in > tests/os/$n.tmp.out
	rc=$?
	secs=$(( $(date +%s) - start ))
	if [ $rc -ne 0 ]; then
		echo "$n: exit code $rc"
		fail=1
	elif [ ! -f tests/os/$n.expected ]; then
		echo "$n: no expected output"
		fail=1
	elif ! cmp -s tests/os/$n.tmp.out tests/os/$n.expected; then
		echo "$n: output differs"
		diff tests/os/$n.tmp.out tests/os/$n.expected | head -20
		fail=1
	else
		echo "$n: ok (${secs}s)"
	fi
	if [ $secs -gt 60 ]; then
		echo "$n: took ${secs}s, more than 60"
		fail=1
	fi
	rm -f $img tests/os/$n.tmp2.img
done
exit $fail
