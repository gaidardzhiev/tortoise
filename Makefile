CC:=$(shell command -v musl-gcc 2>/dev/null || command -v gcc 2>/dev/null || command -v tcc 2>/dev/null || command -v clang 2>/dev/null)
CFLAGS=-Wall -Wextra -std=c11 -g -static
INCLUDES=-Iinclude
SRC=src/main.c src/cpu.c
OBJ=$(SRC:.c=.o)
TARGET=tortoise

ifeq ($(strip $(CC)),)
CC=cc
endif

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_BIN) $(ASM_BIN) assembler/*.bin

TEST_SRC=tests/test_cpu.c src/cpu.c
TEST_BIN=test_cpu

test: $(TEST_SRC)
	$(CC) $(CFLAGS) -Iinclude -o $(TEST_BIN) $(TEST_SRC)

run_test: test
	./$(TEST_BIN)

ASM_SRC=assembler/assembler.c
ASM_BIN=as

assembler: $(ASM_SRC)
	$(CC) $(CFLAGS) -o $(ASM_BIN) $(ASM_SRC)

run_asm_test:
	./$(ASM_BIN) assembler/logic.asm assembler/logic.bin
	./$(TARGET) assembler/logic.bin

.PHONY: all clean test assembler
