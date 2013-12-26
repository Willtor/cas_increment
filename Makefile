# -*- tab-width: 8 ; indent-tabs-mode: t -*-
TARGETS=\
state_machine \
 #Blank line

default: $(TARGETS)
GCC=g++

OPTFLAGS=-O3
CFLAGS=$(OPTFLAGS) -W -Wall -lrt -std=c99
LDFLAGS= -ldl

frob: frob.c
	$(GCC) $(CFLAGS) $< -o $@

borf: borf.c
	$(GCC) $(CFLAGS) $< -o $@

misc: misc.c
	$(GCC) $(CFLAGS) $< -o $@

cache: cache.c
	$(GCC) -o $@ $(CFLAGS) $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(TARGETS) *~

