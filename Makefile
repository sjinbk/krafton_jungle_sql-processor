CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude
LDFLAGS ?=
SRC := src/ast.c src/error.c src/executor.c src/input.c src/main.c src/parser.c src/schema.c src/storage.c src/tokenizer.c
LIB_SRC := src/ast.c src/error.c src/executor.c src/input.c src/parser.c src/schema.c src/storage.c src/tokenizer.c
TEST_SRC := tests/test_main.c

.PHONY: all build test clean demo check sanitize

all: sqlproc

build: sqlproc

sqlproc: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS)

sqlproc_tests: $(LIB_SRC) $(TEST_SRC)
	$(CC) $(CFLAGS) $(LIB_SRC) $(TEST_SRC) -o $@ $(LDFLAGS)

test: sqlproc_tests
	./sqlproc_tests

demo: sqlproc
	bash ./scripts/run_demo.sh

check: sqlproc sqlproc_tests
	bash ./scripts/check.sh

sanitize:
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(SRC) -o sqlproc $(LDFLAGS)
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(LIB_SRC) $(TEST_SRC) -o sqlproc_tests $(LDFLAGS)

clean:
	rm -f sqlproc sqlproc_tests
