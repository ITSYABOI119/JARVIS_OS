# Makefile for IPC Ring Buffer Testing
# Week 4: Lock-Free SPSC Ring Buffer Implementation

CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c11 -pthread
LDFLAGS = -lm -lrt -pthread

# Source files
SRCS = ring_buffer.c test_ipc.c
OBJS = $(SRCS:.c=.o)

# Target executable
TARGET = test_ipc

# Build rules
.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $@..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: $@"

%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	@echo ""
	@echo "========================================="
	@echo "  JARVIS Phase 1 - Week 4 IPC Testing"
	@echo "  Target: <100μs median latency"
	@echo "  Baseline: 54μs (Phase 0 validation)"
	@echo "========================================="
	@echo ""
	./$(TARGET)
	@echo ""
	@echo "========================================="
	@echo "  Test complete!"
	@echo "========================================="

clean:
	@echo "Cleaning build artifacts..."
	rm -f $(OBJS) $(TARGET)
	@echo "Clean complete."

# Dependencies
ring_buffer.o: ring_buffer.c ring_buffer.h
test_ipc.o: test_ipc.c ring_buffer.h
