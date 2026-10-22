# Topica's portable core, tested on the host: make test (CC=cc by default).
CC ?= cc
CFLAGS = -std=c89 -pedantic -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
CORE = ../core/textconv.c ../core/frontmatter.c ../core/undo.c ../core/post.c

test: test_core
	./test_core

test_core: test_core.c $(CORE) ../core/*.h
	$(CC) $(CFLAGS) -o $@ test_core.c $(CORE)

clean:
	rm -rf test_core test_core.dSYM

.PHONY: test clean
