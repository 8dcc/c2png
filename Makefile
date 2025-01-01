
CC=gcc
CFLAGS=-Wall -Wextra
LDLIBS=-lm -lpng

SRC=main.c highlight.c hashtable.c
OBJ=$(addprefix obj/, $(addsuffix .o, $(SRC)))

BIN=c2png

PREFIX=/usr/local
BINDIR=$(PREFIX)/bin

#-------------------------------------------------------------------------------

.PHONY: all clean install

all: $(BIN)

clean:
	rm -f $(OBJ)
	rm -f $(BIN)

install: $(BIN)
	install -D -m 755 $^ -t $(DESTDIR)$(BINDIR)

#-------------------------------------------------------------------------------

txt2png: CFLAGS += -DDISABLE_SYNTAX_HIGHLIGHT

c2png txt2png: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

obj/%.c.o : src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
