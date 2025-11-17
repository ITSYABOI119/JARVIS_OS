# JARVIS AI-OS Decision Cache - Makefile
# Phase 1 Week 3

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -g
LDFLAGS = -lrt

TARGET = test_cache
SOURCES = decision_cache.c cache_patterns.c test_cache.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = decision_cache.h

.PHONY: all clean test run

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Built $(TARGET)"

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	@echo "Running decision cache tests..."
	@./$(TARGET)

run: test

clean:
	rm -f $(TARGET) $(OBJECTS)
	@echo "✓ Cleaned build artifacts"

# Quick build and test
quick: clean all test

# Help
help:
	@echo "JARVIS Decision Cache Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all    - Build test program (default)"
	@echo "  test   - Build and run tests"
	@echo "  clean  - Remove build artifacts"
	@echo "  quick  - Clean, build, and test"
	@echo ""
	@echo "Usage:"
	@echo "  make        # Build"
	@echo "  make test   # Build and run tests"
	@echo "  make quick  # Clean build and test"
