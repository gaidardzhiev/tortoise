# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

mkdir -p /tmp/g
cd /tmp/g
touch a.c b.c c.h .hidden.c
echo *.c
echo ?.h
echo [ab].c
echo "*.c" '*.c'
echo *.none
echo /tmp/g/*.h
files=$(ls)
echo command substitution: $files
echo nested $(echo inner $(echo deeper))
echo "quoted $(echo  spaced   out)"
count=$(ls | wc -l)
echo count $count
