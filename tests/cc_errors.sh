#!/bin/sh
fail=0
d=tests/cce.tmp
mkdir -p $d
expect() {
	printf "$2" > $d/e.c
	if ./tcc0 -bare -S $d/e.c -o $d/e.s 2> $d/err; then
		echo "cc_errors: '$2' compiled but should fail"
		fail=1
	elif ! grep -q "e.c:$1: error: $3" $d/err; then
		echo "cc_errors: '$2' wrong message: $(cat $d/err)"
		fail=1
	fi
}
expect 1 'float x;\n' 'floating point is not supported'
expect 1 'double f(void);\n' 'floating point is not supported'
expect 1 'long long x;\n' 'long long is not supported'
expect 2 'struct s {\nint a : 3;\n};\n' 'bitfields are not supported'
expect 1 'int f(a) int a; { return a; }\n' 'parameter type expected'
expect 2 'int f(int n) {\nint a[n];\nreturn 0; }\n' 'constant expression required'
expect 2 'int main(void) {\nreturn y;\n}\n' "undeclared identifier 'y'"
expect 1 '#error stop here\n' '#error stop here'
expect 1 '#include "nothere.h"\n' "cannot find include file 'nothere.h'"
expect 3 'int main(void) {\nint x;\nx = 1 +;\n}\n' 'expression expected'
expect 1 'int x = 1; int x = 2;\n' "redefinition of 'x'"
expect 1 '#if 1\nint x;\n' 'unterminated #if'
expect 1 'char c = 1.5;\n' 'invalid'
rm -rf $d
[ $fail -eq 0 ] && echo "cc_errors: ok"
exit $fail
