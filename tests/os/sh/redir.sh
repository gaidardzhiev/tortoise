# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

echo to file > /tmp/r.txt
echo appended >> /tmp/r.txt
cat < /tmp/r.txt
cat /nonexistent 2> /tmp/err.txt
echo error went to file: $(cat /tmp/err.txt)
sh -c 'echo out; echo err >&2' > /tmp/both.txt 2>&1
cat /tmp/both.txt
sh -c 'echo to stderr >&2' 2>/dev/null
echo silenced
exec 3< /tmp/r.txt
read first 0<&3
read second <&3
echo from fd 3: $first / $second
echo hello 1>/tmp/one.txt; cat /tmp/one.txt
echo pipes | cat | tr a-z A-Z
echo one two three | wc -w
