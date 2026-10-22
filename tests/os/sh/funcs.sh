# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

greet() {
	echo hello $1 from a function with $# arguments
}
greet world
greet a b c
twice() { "$@"; "$@"; }
twice echo again
counter=0
bump() {
	counter=x$counter
	return 3
}
bump
echo returned $? counter $counter
early() {
	echo before
	return
	echo after
}
early
nested() { greet nested; }
nested
