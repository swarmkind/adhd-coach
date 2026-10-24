CC ?= cc
CFLAGS = -std=c99 -Wall -Wextra -Werror -g -I../src/c/core
CORE = $(wildcard ../src/c/core/*.c)
SRC = $(CORE) $(wildcard test_*.c)
HDR = $(wildcard ../src/c/core/*.h) t.h

all: build/tests
	./build/tests

build/tests: $(SRC) $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -o $@

# The phone side: codec.js is checked against bytes the C records produce.
build/vectors: $(CORE) vectors.c $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) $(CORE) vectors.c -o $@

build/vectors.json: build/vectors
	./build/vectors > $@

js: build/vectors.json
	node ../tools/build-planner.js
	node --test 'js/*.test.js'

clean:
	rm -rf build

.PHONY: all js clean
