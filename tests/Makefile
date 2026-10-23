# Topica's portable core, tested on the host: make test (CC=cc by default).
CC ?= cc
CFLAGS = -std=c89 -pedantic -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
CORE = ../core/textconv.c ../core/frontmatter.c ../core/undo.c ../core/post.c ../core/find.c ../core/prefs.c ../core/recovery.c ../core/mdscan.c ../core/mdpage.c ../core/layout.c ../core/mdhtml.c ../core/vi.c ../core/dict.c ../core/spell.c ../core/publish.c

test: test_core
	./test_core

test_core: test_core.c $(CORE) ../core/*.h
	$(CC) $(CFLAGS) -o $@ test_core.c $(CORE)

# core/vi.c against vim itself (needs vim)
vi-oracle: vi_run
	python3 vi_oracle.py

vi_run: vi_run.c ../core/vi.c ../core/undo.c ../core/*.h
	$(CC) $(CFLAGS) -o $@ vi_run.c ../core/vi.c ../core/undo.c

clean:
	rm -rf test_core test_core.dSYM vi_run vi_run.dSYM

.PHONY: test clean vi-oracle
