#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

t=tests/rebuild
mkdir -p $t
echo "$SHARED" | tr ' ' '\n' | grep . > $t/SHARED.tmp
echo "$OSONLY" | tr ' ' '\n' | grep . > $t/OSONLY.tmp
echo "$KERNEL_C" | tr ' ' '\n' | grep . > $t/OBJS.tmp
echo "$PROGS" | tr ' ' '\n' | grep . > $t/PROGS.tmp
for f in $(find lib -maxdepth 1 -name '*.c' -o -maxdepth 1 -name '*.s' | sort) $(find lib/include -name '*.h' | sort) assembler/assembler.c tools/tlink.c $(ls cc/*.c cc/*.h) $(ls os/kernel/*.c os/kernel/*.h os/kernel/*.s) os/kernel/build.date os/boot/boot.asm $(ls os/bin/*.c os/bin/*.h) $(ls os/etc/*); do
	printf '%s:/usr/src/%s ' $f $f
done
for h in $(cd lib/include && find . -name '*.h' | sort); do
	h=${h#./}
	printf 'lib/include/%s:/lib/include/%s ' $h $h
done
printf '%s ' "$t/SHARED.tmp:/usr/src/lib/SHARED" "$t/OSONLY.tmp:/usr/src/lib/OSONLY" "$t/OBJS.tmp:/usr/src/os/kernel/OBJS" "$t/PROGS.tmp:/usr/src/os/bin/PROGS" "os/build.sh:/usr/src/build.sh:0755"
echo
