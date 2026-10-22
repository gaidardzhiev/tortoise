# Copyright (C) 2026 Ivan Gaydardzhiev
# Licensed under the GPL-3.0-only

x=value
echo 'single $x "quoted"'
echo "double $x 'inner'"
echo \$x escaped \"quote\" back\\slash
echo "escapes in double: \$x \" \\ done"
echo a\ b "c  d" 'e  f'
echo "embedded
newline"
echo it'''s' "con"cat'en'ated
echo ~ ~/sub
