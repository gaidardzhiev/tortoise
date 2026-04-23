# TORTOISE CPU Emulator

## Overview
TORTOISE is a simple CPU emulator implemented in C designed for educational purposes and to demonstrate the fundamentals of CPU architecture and instruction execution. The emulator models a minimalist 16bit CPU with a flags register, a fixed size memory space, and general purpose registers.

## Architecture

### CPU Structure
- **Registers:** 8 general purpose 16bit registers (R0, R7).
- **Program Counter (PC):** 32bit register pointing to the current instruction in memory, allowing addressing of the full 64KB memory space.
- **Stack Pointer (SP):** 32bit register managing the call stack in memory.
- **Flags Register:** 8bit register updated by arithmetic instructions, carrying Zero (Z), Carry (C), Sign (S), and Overflow (V) flags.
- **Memory:** 64KB byte addressable memory representing RAM.
- **Halted Flag:** Indicates whether the CPU is halted.

### Instruction Set
The emulator supports a set of instructions facilitating data movement, arithmetic, logical operations, control flow, and program termination:

| Opcode | Mnemonic | Description                                                   |
|--------|----------|---------------------------------------------------------------|
| 0x00   | NOP      | No operation                                                  |
| 0x01   | LOAD     | Load immediate 16bit value into register                      |
| 0x02   | ADD      | Add immediate 16bit value to register                         |
| 0x03   | STORE    | Store register value into memory                              |
| 0x04   | JMP      | Unconditional jump to memory address                          |
| 0x05   | JZ       | Jump if zero flag is set                                      |
| 0x06   | AND      | Bitwise AND immediate with register                           |
| 0x07   | OR       | Bitwise OR immediate with register                            |
| 0x08   | XOR      | Bitwise XOR immediate with register                           |
| 0x09   | NOT      | Bitwise NOT of register                                       |
| 0x0A   | CALL     | Call subroutine at memory address                             |
| 0x0B   | RET      | Return from subroutine                                        |
| 0x0C   | IN       | Input from device (simulated)                                 |
| 0x0D   | OUT      | Output to device (simulated)                                  |
| 0x0E   | SUB      | Subtract immediate 16bit value from register                  |
| 0x0F   | CMP      | Compare register against immediate, set flags, discard result |
| 0x10   | JC       | Jump if carry flag is set                                     |
| 0x11   | JN       | Jump if sign flag is set (negative)                           |
| 0x12   | JO       | Jump if overflow flag is set                                  |
| 0x13   | JNZ      | Jump if zero flag is not set                                  |
| 0xFF   | HALT     | Stop execution                                                |

### Flags Register
ADD and SUB update all four flags after every execution. CMP performs a subtraction, updates flags, and discards the result, making it the standard way to drive conditional jumps without modifying register state.

**Z (Zero):** Set when the result is 0x0000.
**C (Carry):** Set on unsigned borrow in subtraction, or unsigned overflow in addition.
**S (Sign):** Set when bit 15 of the result is 1.
**V (Overflow):** Set when the operation overflows the signed 16bit range.

### Fetch Decode Execute Cycle

The CPU operates by repeatedly performing the following steps:

1. **Fetch:** The instruction byte is read from memory at the address indicated by the PC.
2. **Decode:** The opcode and operands (register indices, immediate values, addresses) are parsed.
3. **Execute:** The instruction modifies CPU state by changing registers, memory, flags, or program counter.
4. **Advance:** The PC is updated to point to the next instruction unless altered by control flow instructions (JMP, CALL, RET, or a taken conditional jump).

Instructions use fixed size formats for simplicity of decoding.

## Project Structure

`src/` contains the CPU emulator source files. `cpu.c` implements CPU registers, flags, memory, and instruction execution logic. `main.c` is the entrypoint initializing the CPU and starting execution.

`include/` contains header files exposing the CPU API and data structures, including the flag bit definitions (FLAG_Z, FLAG_C, FLAG_S, FLAG_V).

`tests/` contains unit tests. `test_cpu.c` covers basic and extended instruction tests including arithmetic, logical, and control flow operations.

`assembler/` contains the assembler source and all assembly test programs. `assembler.c` converts human readable assembly into binary machine code. The `.asm` files are the assembly source programs.

`Makefile` provides build targets for the emulator, assembler, and all tests.

## Usage

Build the emulator:
```
make
```

Run the emulator with a binary program:
```
./tortoise program.bin
```

Run the unit tests:
```
make run_test
```

Build the assembler:
```
make assembler
```

Run all assembler tests:
```
make run_asm_test
```

## Assembler

The TORTOISE assembler is a command line tool written in C that converts human readable assembly language programs into binary machine code executable by the TORTOISE CPU emulator. It supports the full instruction set of the emulator, including data movement (LOAD, STORE), arithmetic (ADD, SUB, CMP), logical operations (AND, OR, XOR, NOT), control flow (JMP, JZ, JC, JN, JO, JNZ, CALL, RET), I/O instructions (IN, OUT), and program termination (HALT).

The assembler reads assembly source files line by line, ignoring comments (`;`) and blank lines, and parses instructions and operands which can be registers or immediate numeric values in decimal or hexadecimal. It outputs machine code in a compact binary format ready for loading into emulator memory.

### Logic Test

`assembler/logic.asm` tests the core bitwise instructions in sequence. It loads values into R0 and R1, then applies AND, OR, XOR, and NOT operations, verifying register state at each step.

```
LOAD R0, 0x0F0F
LOAD R1, 0x3333
AND R0, 0x00FF
OR R1, 0x00CC
XOR R0, 0x00FF
NOT R1
HALT
```

```
23 bytes written to assembler/logic.bin
cpu_init: PC=0x0 SP=0x10000
loaded 23 bytes into memory
executing opcode 0x1 at PC=0x0
executing opcode 0x1 at PC=0x4
executing opcode 0x6 at PC=0x8
AND before: R0=0xF0F val=0xFF
AND after: R0=0xF
executing opcode 0x7 at PC=0xC
OR before: R1=0x3333 val=0xCC
OR after: R1=0x33FF
executing opcode 0x8 at PC=0x10
XOR before: R0=0xF val=0xFF
XOR after: R0=0xF0
executing opcode 0x9 at PC=0x14
NOT before: R1=0x33FF
NOT after: R1=0xCC00
executing opcode 0xFF at PC=0x16
```

Initial values are loaded into R0 and R1. AND masks the lower byte of R0 correctly. OR sets bits in R1 as expected. XOR toggles bits in R0 as expected. NOT inverts R1 correctly. The program halts cleanly after all instructions.

### Truth Table Test

`assembler/truth.asm` verifies the truth table for two input bits stored in R0 and R1. It outputs inputs and results of AND, OR, XOR, and NOT across all four input combinations (00, 01, 10, 11) before halting. The AND result is 1 only when both inputs are 1, OR is 1 when either input is 1, XOR is 1 when inputs differ, and NOT outputs the bitwise complement of input A.

### Bitwise Masking Test

`assembler/bitwise_masking.asm` verifies AND and XOR chained together. It loads 0xABCD into R0, applies AND with 0x00FF to isolate the lower byte (yielding 0xCD), then XORs with 0x00FF to toggle those bits (yielding 0x32). The result is output before halting.

### OR Chain Test

`assembler/or_chain.asm` tests consecutive OR operations. It loads 0x0001 into R0, ORs with 0x0004 yielding 0x0005, then ORs with 0x0008 yielding 0x000D. This validates that OR operations accumulate bit sets correctly across multiple instructions.

### Bitwise NOT Toggle Test

`assembler/toggle_bits.asm` verifies the NOT instruction. R0 is loaded with 0x0F0F and NOT is applied, flipping all 16 bits to yield 0xF0F0. The result is output before halting.

### XOR Flip Flop Test

`assembler/xor_flip_flop.asm` tests XOR against a full mask. R0 is loaded with the alternating pattern 0xAAAA and XORed with 0xFFFF, yielding 0x5555. This confirms that XOR correctly toggles every bit when the mask is all ones.

### Flags Test

`assembler/flags_test.asm` exercises the flags register and all conditional jump instructions. It runs four independent tests in sequence and outputs one character per test to confirm each flag path was taken correctly.

**Test 1, JNZ countdown loop.** R0 is loaded with 3. The loop prints `1`, subtracts 1 from R0, and jumps back while the zero flag is not set. It iterates exactly three times, printing `111`, then falls through and prints `A` to confirm loop exit.

**Test 2, JN on negative result.** R1 is loaded with 5 and compared against 10 using CMP. Since 5 < 10, the subtraction borrows and sets the sign flag (S). JN is taken, printing `B`.

**Test 3, JO on signed overflow.** R2 is loaded with 0x7FFF (the maximum positive signed 16bit value) and 1 is added. The result wraps to 0x8000, setting the overflow flag (V). JO is taken, printing `C`.

**Test 4, JZ on equal comparison.** R3 is loaded with 0x00AA and compared against 0x00AA using CMP. The result is zero, setting the zero flag (Z). JZ is taken, printing `D`.

The expected output is `111ABCD`. The three `1`s confirm the loop body ran exactly three times before the zero flag terminated it. Each letter confirms one flag and its corresponding conditional jump worked correctly.

```
95 bytes written to assembler/flags_test.bin
cpu_init: PC=0x0 SP=0x10000
loaded 95 bytes into memory
...
SUB: R0=0x3 - 0x1 = 0x2 flags=0x0
...
SUB: R0=0x2 - 0x1 = 0x1 flags=0x0
...
SUB: R0=0x1 - 0x1 = 0x0 flags=0x1
...
CMP: R1=0x5 vs 0xA flags=0x6
...
CMP: R3=0xAA vs 0xAA flags=0x1
...
executing opcode 0xFF at PC=0x5E
```

flags=0x1 on the final SUB confirms Z was set. flags=0x6 on the CMP confirms both C and S were set for the underflow case. flags=0x1 on the equal CMP confirms Z was set. All four conditional jumps resolved correctly.

### Peano Arithmetic Test

`assembler/peano.asm` proves the predecessor axiom of Peano arithmetic on TORTOISE. The axiom states that for every natural number N, applying the predecessor function N times starting from N reaches zero. This is the foundation that all natural number arithmetic is built on.

The program calls a shared verify subroutine for N=1 through N=8. R1 is loaded with N before each CALL. The subroutine decrements R1 by 1 in a loop until the zero flag is set, then CMP confirms R1 is exactly zero. It prints P (pass) or F (fail) and returns. Expected output is PPPPPPPP.

The trace shows each case reducing strictly one step at a time, hitting exactly zero, never overshooting. The CMP after the inner loop is the falsifiability trap, if SUB or JNZ had a bug that caused it to skip zero the flag would not be set and you would see F. Eight P's means eight independent termination proofs, one per starting value, all witnessed by the hardware.

The stack returns to SP=0x10000 after every RET confirming no frame leaks. The inner loop runs exactly N iterations for each N, visible in the SUB trace. The program halts cleanly at 0x38.

```
./as assembler/peano.asm peano.bin
./tortoise peano.bin
```

`OUT` on TORTOISE writes a single byte directly to stdout with no newline appended, so the P character prints flush against the next line of the execution trace, appearing as `Pexecuting opcode 0xB at PC=0x4F`. All eight P's are present and correct in the output, one per CALL, each immediately followed by the RET trace. A future update will add a newline after each OUT by loading 0x000A into a register and calling OUT a second time, which will make the results cleanly readable without the trace noise.

## Extending TORTOISE

The emulator is structured to encourage experimentation and learning. Possible extensions include:

- ~~Developing a simple assembler and loader for human readable program writing.~~
- ~~Fix compiler warnings.~~
- ~~Flags register with SUB, CMP, and conditional jumps (JC, JN, JO, JNZ).~~
- Adding shift instructions (SHL, SHR) to complete the ALU.
- Adding multiplication and division instructions.
- Implementing a two-pass assembler to support named labels in assembly source.
- Implementing interrupt handling and I/O devices beyond simple simulated input/output.
- Supporting larger memory sizes or different data widths (32bit registers).

## Learning Goals

Gain understanding of CPU architecture components and their interaction.
Practical experience writing an emulator and instruction decoding.
Insight into instruction set design including arithmetic, flags, logical, and control flow operations.
Observe real time instruction execution and CPU state mutation debugging.
Understand how flags registers drive conditional execution in real hardware.

## License

This project is provided under the GPL3 License.
