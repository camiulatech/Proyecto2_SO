# Compilador y opciones
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude
CFLAGS_MOUNT = -Wall -Wextra -std=gnu99 -Iinclude
LDFLAGS_MKFS = -lpng
LDFLAGS_MOUNT = `pkg-config fuse3 --cflags --libs` -lpng

# Archivos fuente
MKFS_SRC = mkfs/mkfs.c
MOUNT_SRC = mount/mount.c

# Ejecutables
MKFS_BIN = mkfs.bwfs
MOUNT_BIN = mount.bwfs

.PHONY: all clean

# Compilación por defecto
all: $(MKFS_BIN) $(MOUNT_BIN)

# Compilar mkfs
$(MKFS_BIN): $(MKFS_SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_MKFS)

# Compilar mount con -std=gnu99
$(MOUNT_BIN): $(MOUNT_SRC)
	$(CC) $(CFLAGS_MOUNT) -o $@ $< $(LDFLAGS_MOUNT)

# Limpiar binarios
clean:
	rm -f $(MKFS_BIN) $(MOUNT_BIN)
