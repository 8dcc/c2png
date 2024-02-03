
CC=gcc
CFLAGS=-Wall -Wextra
LDFLAGS=-lm -lpng

OBJ_FILES=main.c.o highlight.c.o hashtable.c.o
OBJS=$(addprefix obj/, $(OBJ_FILES))

INSTALL_DIR=/usr/local/bin
BIN=c2png

#-------------------------------------------------------------------------------

.PHONY: all clean install

all: $(BIN)

clean:
	rm -f $(OBJS)
	rm -f $(BIN)

install: $(BIN)
	mkdir -p $(INSTALL_DIR)
	install -m 755 ./$(BIN) $(INSTALL_DIR)/$(BIN)

#-------------------------------------------------------------------------------

txt2png: CFLAGS += -DDISABLE_SYNTAX_HIGHLIGHT

c2png txt2png: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

obj/%.c.o : src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
