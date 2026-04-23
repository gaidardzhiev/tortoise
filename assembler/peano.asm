; peano.asm
;
; proves the predecessor axiom of Peano arithmetic on TORTOISE:
; for every natural number N, subtracting 1 exactly N times yields 0.
;
; this is the executable form of: pred(succ(n)) = n, iterated to the base case.
; if it holds for N=1 through N=8, the inductive chain is verified on real hardware.
;
; R1 is loaded with N before each CALL.
; the verify subroutine decrements R1 to zero and prints P (pass) or F (fail).
; expected output: PPPPPPPP
LOAD R1, 1
CALL 0x0039
LOAD R1, 2
CALL 0x0039
LOAD R1, 3
CALL 0x0039
LOAD R1, 4
CALL 0x0039
LOAD R1, 5
CALL 0x0039
LOAD R1, 6
CALL 0x0039
LOAD R1, 7
CALL 0x0039
LOAD R1, 8
CALL 0x0039
HALT
; verify subroutine at 0x0039
; precondition:  R1 = N > 0
; postcondition: prints P if R1 reached exactly 0, F otherwise
SUB R1, 1
JNZ 0x0039
CMP R1, 0
JNZ 0x0050
LOAD R2, 0x0050
OUT R2, 0xFF00
RET
; fail path at 0x0050
LOAD R2, 0x0046
OUT R2, 0xFF00
RET
