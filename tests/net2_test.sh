#!/bin/sh
# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

d=tests/net2
fail=0
base=$(cat tests/os/base.files.tmp)
port=$((41000 + $$ % 2000))
./tdisk -o $d/a.tmp.img -s 65536 -b os/boot/boot.bin -k os/kernel/kernel.tex $base $d/a.conf:/etc/net.conf > /dev/null || exit 1
./tdisk -o $d/b.tmp.img -s 65536 -b os/boot/boot.bin -k os/kernel/kernel.tex $base $d/b.conf:/etc/net.conf $d/hello.txt:/var/www/hello.txt > /dev/null || exit 1
./tswitch $port &
sw=$!
sleep 1
start=$(date +%s)
timeout 60 ./tortoise -m 32 -N udp:$((port + 1)):127.0.0.1:$port -M 02:00:00:00:00:0a -W $d/a.tmp.pcap -r rom/rom.bin -D $d/a.tmp.img -i $d/a.in > $d/a.tmp.out 2>&1 &
a=$!
timeout 60 ./tortoise -m 32 -N udp:$((port + 2)):127.0.0.1:$port -M 02:00:00:00:00:0b -r rom/rom.bin -D $d/b.tmp.img -i $d/b.in > $d/b.tmp.out 2>&1 &
b=$!
wait $a
ra=$?
wait $b
rb=$?
kill $sw 2> /dev/null
wait $sw 2> /dev/null
secs=$(($(date +%s) - start))
check() {
	if grep -q "$2" $d/$1.tmp.out; then
		echo "net2: $1 shows '$2'"
	else
		echo "net2: FAIL, $1 does not show '$2'"
		fail=1
	fi
}
[ $ra -eq 0 ] || { echo "net2: FAIL, machine a exited with $ra"; fail=1; }
[ $rb -eq 0 ] || { echo "net2: FAIL, machine b exited with $rb"; fail=1; }
check a '^alpha$'
check b '^beta$'
check a '64 bytes from 10.0.0.2: icmp_seq=1'
check a '3 packets transmitted, 3 received'
check a 'hello from the httpd on beta'
check a '^from beta$'
check b '^from alpha$'
check a '10.0.0.2 *02:00:00:00:00:0b *eth0'
check a '^imported hello from the httpd on beta'
check a '^hello from listen1 on beta$'
if command -v tcpdump > /dev/null 2>&1; then
	if tcpdump -r $d/a.tmp.pcap > $d/tcpdump.tmp 2>&1 && grep -q 'ICMP echo request' $d/tcpdump.tmp; then
		echo "net2: tcpdump reads the pcap file"
	else
		echo "net2: FAIL, tcpdump cannot read the pcap file"
		fail=1
	fi
else
	echo "net2: tcpdump not installed, pcap check skipped"
fi
if [ $secs -gt 60 ]; then
	echo "net2: FAIL, took $secs seconds"
	fail=1
fi
if [ $fail -ne 0 ]; then
	echo "--- machine a"
	cat $d/a.tmp.out
	echo "--- machine b"
	cat $d/b.tmp.out
	exit 1
fi
rm -f $d/*.tmp*
echo "net2: ok ($secs s)"
