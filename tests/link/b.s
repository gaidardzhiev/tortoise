.text
.globl puts
puts:
LOAD32 R7, 0xF0002000
.loop:
LDB R0, [R1]
CMP32 R0, 0
BEQ .done
STW [R7], R0
ADD32 R1, 1
JMPREL .loop
.done:
RET32
.data
.globl counter, msg2
counter:
.dword 42
msg2:
.asciiz "hello from b\n"
