
CC=gcc
CFLAGS=-Wall -Wextra
LDFLAGS=-lpng

OBJ_FILES=main.c.o
OBJS=$(addprefix obj/, $(OBJ_FILES))

BIN=c2png

#-------------------------------------------------------------------------------

.PHONY: clean all

all: $(BIN)

clean:
	rm -f $(OBJS)
	rm -f $(BIN)

#-------------------------------------------------------------------------------

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

obj/%.c.o : src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
