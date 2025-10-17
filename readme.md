# TORTOISE CPU Emulator

## Overview
TORTOISE is a simple CPU emulator implemented in C designed for educational purposes and to demonstrate the fundamentals of CPU architecture and instruction execution. The emulator models a minimalist 16bit CPU with a small set of instructions, a fixed size memory space, and general purpose registers.

---

## Architecture

### CPU Structure
- **Registers:** 8 general-purpose 16bit registers (R0–R7).
- **Program Counter (PC):** 32bit register pointing to the current instruction in memory, allowing addressing 64KB memory space.
- **Stack Pointer (SP):** 32bit register managing the call stack in memory.
- **Memory:** 64KB byte addressable memory representing RAM.
- **Halted Flag:** Indicates whether the CPU is halted.

### Instruction Set
The emulator supports a set of instructions facilitating data movement, arithmetic, logical operations, control flow, and program termination:

| Opcode  | Mnemonic | Description                               |
|---------|----------|-------------------------------------------|
| 0x00    | NOP      | No operation                              |
| 0x01    | LOAD     | Load immediate 16bit value into register  |
| 0x02    | ADD      | Add immediate 16bit value to register     |
| 0x03    | STORE    | Store register value into memory          |
| 0x04    | JMP      | Unconditional jump to memory address      |
| 0x05    | JZ       | Jump if register is zero                  |
| 0x06    | AND      | Bitwise AND immediate with register       |
| 0x07    | OR       | Bitwise OR immediate with register        |
| 0x08    | XOR      | Bitwise XOR immediate with register       |
| 0x09    | NOT      | Bitwise NOT of register                   |
| 0x0A    | CALL     | Call subroutine at memory address         |
| 0x0B    | RET      | Return from subroutine                    |
| 0x0C    | IN       | Input from device (simulated)             |
| 0x0D    | OUT      | Output to device (simulated)              |
| 0xFF    | HALT     | Stop execution                            |

---

### Fetch Decode Execute Cycle

The CPU operates by repeatedly performing the following steps:

1. **Fetch:** The instruction byte is read from memory at the address indicated by the PC.
2. **Decode:** The opcode and operands (register indices, immediate values, addresses) are parsed.
3. **Execute:** The instruction modifies CPU state by changing registers, memory, or program counter.
4. **Advance:** The PC is updated to point to the next instruction unless altered by control flow instructions (e.g., JMP, CALL, RET).

Instructions use fixed size formats for simplicity of decoding.

---

## Project Structure

- `src/`  
  Contains the CPU emulator source files:  
  - `cpu.c`: Implementation of CPU registers, memory, and instruction execution logic  
  - `main.c`: Entrypoint initializing the CPU and starting execution  
- `include/`  
  Header files exposing CPU API and data structures:  
  - `cpu.h`  
- `tests/`  
  Contains test programs and unit tests:  
  - `test_cpu.c`: Basic and extended instruction tests including arithmetic, logical, and control flow operations
- `assembler/`  
  Contains the assembler source and test assembly files:  
  - `assembler.c`: A simple assembler converting human readable assembly into binary machine code compatible with TORTOISE CPU  
  - `.asm` files containing assembly source programs  
- `Makefile`  
  Build instructions for the emulator, assembler, and tests 

---

## Usage

Build the emulator using:
```
make
```

Run the emulator starting the `tortoise` executable with your program loaded.

Run comprehensive CPU tests with:

```
make run_test
```
The tests include:
- A simple test verifying `LOAD`, `ADD`, and `HALT` instructions.
- An extended test covering logical bitwise operations (`AND`, `OR`, `XOR`, `NOT`), subroutine call/return, and correct register and memory behavior.

Build the assembler:
```
make assembler
```

Run assembler test:
```
make run_asm_test
```

---

## Assembler

- The TORTOISE assembler is a command line tool written in C that converts human readable assembly language programs into binary machine code executable by the TORTOISE CPU emulator.
- It supports the full instruction set of the emulator, including data movement (`LOAD`, `STORE`), arithmetic (`ADD`), logical operations (`AND`, `OR`, `XOR`, `NOT`), control flow (`JMP`, `JZ`, `CALL`, `RET`), I/O instructions (`IN`, `OUT`), and program termination (`HALT`). 
- The assembler reads assembly source files line by line, ignoring comments and whitespace, and parses instructions and operands which can be registers or immediate numeric values (in decimal or hexadecimal).
- It outputs machine code in a compact binary format ready for loading into emulator memory.
- This allows writing readable assembly programs for TORTOISE, which can then be assembled and executed on the emulator, providing a complete flow from source code to CPU execution.
- The assembler is designed to be simple, easy to understand, and easily extensible for adding new instructions or features.

### Assembler Test

The assembler code was executed on the emulator to validate the correct functioning and execution of each instruction. The following assembler source code was used for this verification:
```
LOAD R0, 0x0F0F
LOAD R1, 0x3333
AND R0, 0x00FF
OR R1, 0x00CC
XOR R0, 0x00FF
NOT R1
HALT
```

Upon assembling the code and loading the resulting binary into the emulator, the subsequent output illustrates the CPU executing each instruction in a step by step manner.
```
make run_asm_test
./as assembler/logic.asm assembler/logic.bin
23 bytes written to assembler/logic.bin
./tortoise assembler/logic.bin
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

- Initial values loaded into registers R0 and R1.
- Bitwise AND on R0 masks the bits correctly.
- Bitwise OR on R1 properly sets bits.
- XOR operation toggles bits in R0 as expected.
- NOT operation inverts bits in R1 correctly.
- The program halts after executing all instructions.

The log provides a detailed record of register values before and after each logical operation, demonstrating conformity with the expected results and thereby validating the correct functionality of both the assembler and the CPU emulator. The compiled machine code is successfully assembled into a binary file, which is subsequently loaded into the emulator's memory where the instructions execute as intended.

### Truth Table Test

The `assembler/truth.asm` code was written to test the truth table for two input bits stored in registers R0 and R1. It outputs the inputs and the results of logical operations AND, OR, XOR, and NOT on the inputs.

After assembling and loading the binary into the emulator, the `tortoise` CPU executes each instruction sequentially and outputs the input bits followed by the results of the logical operations. The expected outputs correspond exactly to the truth table for two boolean inputs:

- Inputs A and B are output first for each test case.
- The AND result outputs 1 only if both A and B are 1, else 0.
- The OR result outputs 1 if either A or B is 1, else 0.
- The XOR result outputs 1 if A and B differ, else 0.
- The NOT result outputs the bitwise complement of input A.
- The sequence repeats for all four input combinations (00, 01, 10, 11) before halting.

This test verifies the correct functioning of bitwise instructions, register loads, and output operations on the emulator, demonstrating both the assembler’s correctness in encoding instructions and the CPU's ability to execute them precisely.

### Bitwise Masking Test

The `assembler/bitwise_masking.asm` program was designed to verify the correct execution of bitwise AND and XOR instructions. It first loads the value 0xABCD into register R0, applies an AND operation with 0x00FF which masks the higher byte leaving the lower byte intact, and then applies an XOR with 0x00FF to toggle bits in the masked value. After executing these instructions sequentially, the emulator outputs the resulting R0 value.

This test demonstrates the precise functionality of bitwise masking and toggling operations, confirming that the emulator correctly processes immediate bitwise instructions, modifies register contents appropriately, and produces the correct final output.

### OR Chain Test

The `assembler/or_chain.asm` source tests the behavior of the OR instruction chained consecutively. It loads 0x0001 into R0, performs an OR with 0x0004 updating R0 to 0x0005, then another OR with 0x0008 resulting in 0x000D. The final value of R0 is output before halting.

This program validates that multiple OR operations can be combined to cumulatively set bits in a register, reflecting the emulator's capability to correctly handle sequential logical OR instructions and update registers appropriately.

### Bitwise NOT Toggle Test

The `assembler/toggle_bits.asm` assembly verifies the NOT instruction functionality. The program loads R0 with 0x0F0F and applies the bitwise NOT operation, flipping each bit in R0 to yield 0xF0F0. This inverted value is then output before halting.

This test confirms the emulator's proper implementation of the NOT instruction, ensuring correct bitwise inversion in a 16-bit register and verifying that the output instruction correctly reports the resultant value.

### XOR Flip Flop Test

The `assembler/xor_flip_flop.asm` program tests the XOR instruction’s ability to toggle bits against a fixed mask. Initially, R0 is loaded with the alternating bit pattern 0xAAAA, and then XORed with 0xFFFF which flips all bits, resulting in 0x5555. The final register state is output before halting.

This test validates that the emulator's XOR opcode correctly toggles bits in a register when combined with an immediate operand, demonstrating accurate execution of bitwise logical instructions and output handling.

## Extending TORTOISE

The emulator is structured to encourage experimentation and learning. Possible extensions include:

- Adding more instructions such as multiplication, division, or bit shifts.
- Implementing interrupt handling and I/O devices beyond simple simulated input/output.
- ~~Developing a simple assembler and loader for human readable program writing.~~ [DONE]
- Supporting larger memory sizes or different data widths (e.g., 32bit registers).
- ~~Fix compiler warnings.~~ [DONE]

---

## Learning Goals

- Gain understanding of CPU architecture components and their interaction.
- Practical experience writing an emulator and instruction decoding.
- Insight into instruction set design including logical and control flow operations.
- Observe real time instruction execution and CPU state mutation debugging.

---

## License

This project is provided under the GPL3 License.

---
