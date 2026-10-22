# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

if true; then echo if then; fi
if false; then echo wrong; else echo if else; fi
if false; then echo wrong; elif true; then echo elif taken; else echo wrong; fi
if test 3 -gt 2 && [ "a" = "a" ]; then echo test and bracket; fi
false || echo or runs
true && echo and runs
false && echo never
true || echo never
! false && echo negated
i=0
while [ $i != 3 ]; do echo while $i; case $i in 0) i=1 ;; 1) i=2 ;; 2) i=3 ;; esac; done
n=0
for w in a b c; do n=$w; done; echo for ended with $n
until test -f /tmp/flag; do echo until creates flag; touch /tmp/flag; done
for f in 1 2 3 4 5; do
	if [ $f = 2 ]; then continue; fi
	if [ $f = 4 ]; then break; fi
	echo loop $f
done
for word in apple banana cherry date; do
	case $word in
	a*) echo $word starts with a ;;
	b*|c*) echo $word starts with b or c ;;
	*) echo $word is something else ;;
	esac
done
case "x.c" in *.h) echo header ;; *.c) echo source ;; esac
case "[" in "[") echo quoted pattern ;; esac
echo one; echo two
{ echo group one; echo group two; } > /tmp/group.txt
cat /tmp/group.txt
(cd /tmp; echo subshell in $(pwd))
echo still in $(pwd)
