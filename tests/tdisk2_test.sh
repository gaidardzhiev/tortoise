#!/bin/sh
d=tests/td2.tmp
rm -rf $d
mkdir -p $d
fail=0
printf '#!/bin/sh\n' > $d/script
chmod 755 $d/script
printf 'data\n' > $d/plain
dd if=/dev/urandom of=$d/huge.bin bs=1024 count=8800 2> /dev/null
./tdisk -o $d/v2.img -s 24000 -d /home/user:0700:1000:100 -d /pub:01777 $d/script:/bin/script $d/plain:/etc/plain $d/plain:/etc/secret:0600:1000:100 -L /etc/plain:/etc/link -L ../bin/script:/etc/rel $d/huge.bin:/huge.bin || fail=1
./tdisk -ll $d/v2.img > $d/list.txt || fail=1
cat > $d/want.txt <<'LIST'
0755    0    0  3      dir  /home/
0700 1000  100  2      dir  /home/user/
1777    0    0  2      dir  /pub/
0755    0    0  2      dir  /bin/
0755    0    0  1       10  /bin/script
0755    0    0  2      dir  /etc/
0644    0    0  1        5  /etc/plain
0600 1000  100  1        5  /etc/secret
0777    0    0  1     link  /etc/link -> /etc/plain
0777    0    0  1     link  /etc/rel -> ../bin/script
0644    0    0  1  9011200  /huge.bin
LIST
if ! cmp -s $d/list.txt $d/want.txt; then
	echo "tdisk2: long listing differs"
	diff $d/list.txt $d/want.txt
	fail=1
fi
./tdisk -x $d/v2.img /huge.bin $d/out.bin || fail=1
cmp -s $d/huge.bin $d/out.bin || { echo "tdisk2: triple indirect file differs"; fail=1; }
./tdisk -x $d/v2.img /etc/secret $d/out.bin || fail=1
cmp -s $d/plain $d/out.bin || { echo "tdisk2: secret differs"; fail=1; }
if ./tdisk -x $d/v2.img /etc/link $d/out.bin 2> /dev/null; then
	echo "tdisk2: extracting a symbolic link succeeded"
	fail=1
fi
dd if=$d/v2.img bs=512 skip=1024 count=1 2> /dev/null | dd bs=4 count=1 2> /dev/null > $d/magic
printf 'TFS2' | cmp -s - $d/magic || { echo "tdisk2: no TFS2 magic"; fail=1; }
./tdisk -1 -o $d/v1.img -s 4096 $d/plain:/a/b/plain $d/script:/script || fail=1
dd if=$d/v1.img bs=512 skip=1024 count=1 2> /dev/null | dd bs=4 count=1 2> /dev/null > $d/magic
printf 'TFS1' | cmp -s - $d/magic || { echo "tdisk2: no TFS1 magic"; fail=1; }
./tdisk -l $d/v1.img > $d/list1.txt || fail=1
printf '     dir  /a/\n     dir  /a/b/\n       5  /a/b/plain\n      10  /script\n' | cmp -s - $d/list1.txt || { echo "tdisk2: TFS1 listing differs"; cat $d/list1.txt; fail=1; }
./tdisk -x $d/v1.img /a/b/plain $d/out.bin || fail=1
cmp -s $d/plain $d/out.bin || { echo "tdisk2: TFS1 extraction differs"; fail=1; }
rm -rf $d
[ $fail -eq 0 ] && echo "tdisk2: ok"
exit $fail
