JMP code_start
data_bytes:
.byte 10, 20, 30, 40
data_word:
.word 0x55AA
data_dword:
.dword 0x12345678
data_str:
.asciiz "HELLO"
code_start:
LOAD R0, data_bytes
LOAD R1, [R0 + 0]
LOAD R0, data_word
LOAD R2, [R0 + 0]
CMP R2, 0x55AA
JNZ dir_fail
LOAD R0, data_dword
LOAD32 R3, [R0 + 0]
CMP32 R3, 0x12345678
JNZ dir_fail
LOAD R4, 0x0044
OUT R4, 0xFF00
HALT
dir_fail:
LOAD R4, 0x0046
OUT R4, 0xFF00
HALT
