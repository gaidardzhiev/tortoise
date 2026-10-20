#!/bin/sh
fail=0
t=tests/exit.tmp
expect() {
	want=$1
	shift
	"$@" > /dev/null 2>&1 < /dev/null
	rc=$?
	if [ $rc -ne $want ]; then
		echo "exit: '$*' gave $rc expected $want"
		fail=1
	fi
}
printf '\040\000\000\160\000\360\001\001\052\000\076\001\000\000\377' > $t
expect 42 ./tortoise $NOCACHE $t
printf '\040\000\000\160\000\360\001\001\000\000\076\001\000\000\001\002\007\000\377' > $t
expect 0 ./tortoise $NOCACHE $t
printf '\376' > $t
expect 2 ./tortoise $NOCACHE $t
printf '\040\000\000\000\000\200\075\020\000\000\377' > $t
expect 2 ./tortoise $NOCACHE $t
printf '\004\000\000' > $t
expect 124 ./tortoise $NOCACHE -x 1000 $t
expect 1 ./tortoise $NOCACHE -m 0 $t
expect 1 ./tortoise $NOCACHE -m 257 $t
expect 1 ./tortoise $NOCACHE -q $t
expect 1 ./tortoise $NOCACHE tests/does_not_exist.bin
expect 1 ./tortoise $NOCACHE
printf '\377' > $t
expect 0 ./tortoise $NOCACHE -m 256 $t
printf '\040\000\000\000\000\200\176\020\040\000\001\000\000\000\176\000\174' > $t
expect 3 ./tortoise $NOCACHE $t
printf '\040\000\001\000\000\000\176\000\201' > $t
expect 4 ./tortoise $NOCACHE $t
rm -f $t
[ $fail -eq 0 ] && echo "exit codes: ok"
exit $fail
