# Compilador y opciones
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude
LDFLAGS = -lpng

# Archivos fuente y binarios
MKFS_SRC = mkfs/mkfs.c
MKFS_BIN = mkfs.bwfs

.PHONY: all clean

# Compilación por defecto
all: $(MKFS_BIN)

# Regla para compilar mkfs.bwfs
$(MKFS_BIN): $(MKFS_SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Limpiar binarios generados
clean:
	rm -f $(MKFS_BIN)
