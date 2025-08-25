CC=gcc
CFLAGS=-Wall -Wextra -std=c11 -g
INCLUDES=-Iinclude
SRC=src/main.c src/cpu.c
OBJ=$(SRC:.c=.o)
TARGET=tortoise

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_BIN)

TEST_SRC = tests/test_cpu.c src/cpu.c
TEST_BIN = test_cpu

test: $(TEST_SRC)
	$(CC) $(CFLAGS) -Iinclude -o $(TEST_BIN) $(TEST_SRC)

run_test: test
	./$(TEST_BIN)

.PHONY: all clean test
