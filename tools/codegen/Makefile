# Build the C code generators and the Jamal preprocessor - replacements for
# the Perl scripts.
# GCC 13 accepts -std=c2x (the C23 draft); GCC 14+ also accepts -std=c23.
CC     ?= gcc
CFLAGS ?= -std=c2x -O2 -Wall -Wextra
BIN    := ../../bin/tools

all: $(BIN)/generrh $(BIN)/lmt_make $(BIN)/headerer $(BIN)/syntaxer $(BIN)/jamal $(BIN)/setup

$(BIN)/lmt_make: lmt_make.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN)/generrh: generrh.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN):
	mkdir -p $(BIN)

$(BIN)/headerer: headerer.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN)/syntaxer: syntaxer.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN)/jamal: jamal.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN)/setup: setup.c setup_data.h setup_text.h | $(BIN)
	$(CC) $(CFLAGS) -I. -o $@ setup.c
