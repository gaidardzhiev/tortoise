# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

cd /tmp
pwd
mkdir -p /tmp/a/b
cd a/b
pwd
cd -
cd
echo home is $(pwd)
HOME=/tmp/a
cd
pwd
cd /
echo "alpha beta gamma" > /tmp/line.txt
read x y < /tmp/line.txt
echo x=$x y=$y
read one < /tmp/line.txt
echo one=$one
printf_missing=1
umask 077
umask
touch /tmp/masked
stat /tmp/masked | grep Access: | head -n 1
umask 022
echo 'echo sourced with $1' > /tmp/src.sh
. /tmp/src.sh dot
source /tmp/src.sh source
set -x
echo traced
set +x
echo not traced
eval 'echo evaluated $x'
test -d /tmp && echo test -d
[ -f /tmp/line.txt ] && echo bracket -f
[ ! -e /nowhere ] && echo bracket not
[ abc != abd -a 1 -le 2 ] && echo and/or
test -z "" && test -n x && echo empty and nonempty
(exit 7); echo subshell exit $?
sh -c 'set -e; echo before; false; echo not reached'
echo set -e status $?
exec sh -c 'echo exec replaced the shell'
echo not reached
