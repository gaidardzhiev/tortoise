#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

fail=0
for f in tests/baseline/*.bin; do
	b=$(basename $f .bin)
	./as assembler/$b.asm tests/$b.tmp > /dev/null || { echo "$b: assembly failed"; fail=1; continue; }
	if ! cmp -s tests/$b.tmp $f; then
		echo "$b: binary differs from baseline"
		fail=1
	fi
	./tortoise $NOCACHE $f > tests/$b.out.tmp 2>/dev/null < /dev/null
	rc=$?
	if [ $rc -ne 0 ]; then
		echo "$b: exit code $rc"
		fail=1
	fi
	if ! cmp -s tests/$b.out.tmp tests/baseline/$b.out; then
		echo "$b: output differs from baseline"
		fail=1
	fi
	rm -f tests/$b.tmp tests/$b.out.tmp
done
[ $fail -eq 0 ] && echo "legacy: all binaries and outputs identical"
exit $fail
