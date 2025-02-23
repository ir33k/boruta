.POSIX:

CC = cc
CFLAGS += -std=c99 -pedantic -Wall -Wextra -Wshadow
CFLAGS += -Wswitch-enum -Wmissing-declarations -Wno-deprecated-declarations
CFLAGS += -ggdb

.PHONY: all tests

all: test boruta
test: boruta.t

# main

boruta: boruta.o main.c
	$(CC) $(CFLAGS) -o $@ $^

# tests

boruta.t: boruta.t.c
	$(CC) $(CFLAGS) -o $@ $^
	chmod +x $@
	./$@

# libs

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $< -o $@
