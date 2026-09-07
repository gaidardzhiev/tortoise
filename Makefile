CC:=$(shell command -v musl-gcc 2>/dev/null || command -v gcc 2>/dev/null || command -v tcc 2>/dev/null || command -v clang 2>/dev/null)
CFLAGS=-Wall -Wextra -std=c11 -g -static
INCLUDES=-Iinclude
SRC=src/main.c src/cpu.c
OBJ=$(SRC:.c=.o)
TARGET=tortoise
TEST_SRC=tests/test_cpu.c src/cpu.c
TEST_BIN=test_cpu
ASM_SRC=assembler/assembler.c
ASM_BIN=as

ifeq ($(strip $(CC)),)
CC=cc
endif

all: $(TARGET) assembler test

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

test: $(TEST_SRC)
	$(CC) $(CFLAGS) -Iinclude -o $(TEST_BIN) $(TEST_SRC)

run_test: test
	./$(TEST_BIN)

assembler: $(ASM_SRC)
	$(CC) $(CFLAGS) -o $(ASM_BIN) $(ASM_SRC)

run_shift_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/shift_test.asm assembler/shift_test.bin
	./$(TARGET) assembler/shift_test.bin

run_mul_div_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/mul_div_test.asm assembler/mul_div_test.bin
	./$(TARGET) assembler/mul_div_test.bin

run_labels_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/labels_test.asm assembler/labels_test.bin
	./$(TARGET) assembler/labels_test.bin

run_interrupt_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/interrupt_test.asm assembler/interrupt_test.bin
	./$(TARGET) assembler/interrupt_test.bin

run_io_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/io_test.asm assembler/io_test.bin
	./$(TARGET) assembler/io_test.bin

run_reg32_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/reg32_test.asm assembler/reg32_test.bin
	./$(TARGET) assembler/reg32_test.bin

run_indirect_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/indirect_test.asm assembler/indirect_test.bin
	./$(TARGET) assembler/indirect_test.bin

run_directives_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/directives_test.asm assembler/directives_test.bin
	./$(TARGET) assembler/directives_test.bin

run_privilege_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/privilege_test.asm assembler/privilege_test.bin
	./$(TARGET) assembler/privilege_test.bin

run_peripherals_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/peripherals_test.asm assembler/peripherals_test.bin
	./$(TARGET) assembler/peripherals_test.bin

run_multitask_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/multitask.asm assembler/multitask.bin
	./$(TARGET) assembler/multitask.bin

run_debugger_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/shift_test.asm assembler/shift_test.bin
	printf "r\ns\nq\n" | ./$(TARGET) -d assembler/shift_test.bin

run_asm_test: $(TARGET) assembler
	./$(ASM_BIN) assembler/logic.asm assembler/logic.bin
	./$(TARGET) assembler/logic.bin
	./$(ASM_BIN) assembler/truth.asm assembler/truth.bin
	./$(TARGET) assembler/truth.bin
	./$(ASM_BIN) assembler/bitwise_masking.asm assembler/bitwise_masking.bin
	./$(TARGET) assembler/bitwise_masking.bin
	./$(ASM_BIN) assembler/or_chain.asm assembler/or_chain.bin
	./$(TARGET) assembler/or_chain.bin
	./$(ASM_BIN) assembler/toggle_bits.asm assembler/toggle_bits.bin
	./$(TARGET) assembler/toggle_bits.bin
	./$(ASM_BIN) assembler/xor_flip_flop.asm assembler/xor_flip_flop.bin
	./$(TARGET) assembler/xor_flip_flop.bin
	./$(ASM_BIN) assembler/flags_test.asm assembler/flags_test.bin
	./$(TARGET) assembler/flags_test.bin
	./$(ASM_BIN) assembler/peano.asm assembler/peano.bin
	./$(TARGET) assembler/peano.bin
	./$(ASM_BIN) assembler/shift_test.asm assembler/shift_test.bin
	./$(TARGET) assembler/shift_test.bin
	./$(ASM_BIN) assembler/mul_div_test.asm assembler/mul_div_test.bin
	./$(TARGET) assembler/mul_div_test.bin
	./$(ASM_BIN) assembler/labels_test.asm assembler/labels_test.bin
	./$(TARGET) assembler/labels_test.bin
	./$(ASM_BIN) assembler/interrupt_test.asm assembler/interrupt_test.bin
	./$(TARGET) assembler/interrupt_test.bin
	./$(ASM_BIN) assembler/io_test.asm assembler/io_test.bin
	./$(TARGET) assembler/io_test.bin
	./$(ASM_BIN) assembler/reg32_test.asm assembler/reg32_test.bin
	./$(TARGET) assembler/reg32_test.bin
	./$(ASM_BIN) assembler/indirect_test.asm assembler/indirect_test.bin
	./$(TARGET) assembler/indirect_test.bin
	./$(ASM_BIN) assembler/directives_test.asm assembler/directives_test.bin
	./$(TARGET) assembler/directives_test.bin
	./$(ASM_BIN) assembler/privilege_test.asm assembler/privilege_test.bin
	./$(TARGET) assembler/privilege_test.bin
	./$(ASM_BIN) assembler/peripherals_test.asm assembler/peripherals_test.bin
	./$(TARGET) assembler/peripherals_test.bin
	./$(ASM_BIN) assembler/multitask.asm assembler/multitask.bin
	./$(TARGET) assembler/multitask.bin

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_BIN) $(ASM_BIN) assembler/*.bin

.PHONY: all clean test assembler run_test run_asm_test run_shift_test run_mul_div_test run_labels_test run_interrupt_test run_io_test run_reg32_test run_indirect_test run_directives_test run_privilege_test run_peripherals_test run_multitask_test run_debugger_test
