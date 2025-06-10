CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude
MKFS_SRC = mkfs/mkfs.c
MKFS_BIN = mkfs.bwfs

.PHONY: all clean

all: $(MKFS_BIN)

$(MKFS_BIN): $(MKFS_SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(MKFS_BIN)
