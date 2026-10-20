#!/bin/sh
port=$((20000 + $$ % 20000))
./as tests/gdb_test.asm tests/gdb.tmp.bin > /dev/null || exit 1
./tortoise $NOCACHE -g $port tests/gdb.tmp.bin 2> /dev/null &
emu=$!
./tests/gdb_client $port
rc=$?
wait $emu
erc=$?
rm -f tests/gdb.tmp.bin
if [ $rc -ne 0 ] || [ $erc -ne 7 ]; then
	echo "gdb test: client $rc emulator $erc"
	exit 1
fi
exit 0
