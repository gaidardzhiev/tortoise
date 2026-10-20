#!/bin/sh
fail=0
for f in tests/c/*.c; do
	n=$(basename $f .c)
	if ! ./tcc0 -bare $f -o tests/c/$n.tmp.tex 2> tests/c/$n.tmp.err; then
		echo "$n: compile failed"
		cat tests/c/$n.tmp.err
		fail=1
		continue
	fi
	out=$(./tortoise $NOCACHE -x 2000000000 tests/c/$n.tmp.tex)
	rc=$?
	if [ $rc -ne 0 ] || [ "$out" != "OK" ]; then
		echo "$n: rc=$rc output: $out"
		fail=1
	else
		echo "$n: OK"
	fi
	rm -f tests/c/$n.tmp.tex tests/c/$n.tmp.err
done
exit $fail
