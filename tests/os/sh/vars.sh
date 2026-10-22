# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

name=world
echo hello $name ${name}!
empty=
echo "[$empty]" [$empty]
export GREETING=hi
printenv GREETING
sh -c 'echo child sees $GREETING'
unset GREETING
sh -c 'echo child sees [$GREETING]'
LOCAL=only sh -c 'echo prefix assignment $LOCAL'
echo after [$LOCAL]
echo status of false:; false; echo $?
true; echo status of true: $?
echo "the shell is $$" > /tmp/pid.txt
read pid_line < /tmp/pid.txt
case "$pid_line" in "the shell is "[0-9]*) echo dollar dollar is a number ;; esac
sleep -t 1 &
wait $!
echo background pid known: $(test -n "$!" && echo yes)
set -- one "two words" three
echo count $#
echo first $1 second $2 third $3
for a in "$@"; do echo "arg <$a>"; done
for a in $@; do echo "split <$a>"; done
echo "star <$*>"
shift
echo after shift $# $1
shift 2
echo after shift 2: $#
echo zero is $0
