CC ?= cc
CLANG_FORMAT ?= clang-format
PYTHON ?= python3
VERSION ?= V1.0
CFLAGS ?= -O2 -Wall -Wextra -std=c11
SOURCES = jed2pld.c design.c output.c opal.c cupl.c pal.c gal.c v10.c mapl.c
HEADERS = banner.h util.h design.h output.h opal.h cupl.h device_geometry.h pal.h gal.h v10.h mapl.h
ifeq ($(OS),Windows_NT)
EXEEXT ?= .exe
endif
PROGRAM = jed2pld$(EXEEXT)

all: $(PROGRAM)

$(PROGRAM): $(SOURCES) $(HEADERS) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -DJED2PLD_VERSION='"$(VERSION)"' -o $@ $(SOURCES)

clean:
	rm -f jed2pld jed2pld.exe

format:
	$(CLANG_FORMAT) -i --style=file $(SOURCES) $(HEADERS)

test: $(PROGRAM)
	$(PYTHON) tests/test_regressions.py
	$(PYTHON) tests/test_behavior.py
	$(PYTHON) tests/test_equations.py

fuzz-sanitize:
	CC="$(CC)" $(PYTHON) tests/fuzz_sanitize.py

.PHONY: all clean format test fuzz-sanitize
