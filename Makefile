# ─────────── variables comunes ───────────
CC      = gcc
CFLAGS  = -Wall -Wextra -Iinclude
MKFS_LD = -lpng
MNT_LD  = `pkg-config fuse3 --cflags --libs` -lpng

# ─────────── fuentes  ───────────
MKFS_SRC  = mkfs/mkfs.c
MOUNT_SRC = mount/mount.c
BWFS_SRC  = bwfs/bwfs_ops.c      # <-- la ruta que faltaba

# ─────────── ejecutables ───────────
MKFS_BIN  = mkfs.bwfs
MOUNT_BIN = mount.bwfs

.PHONY: all clean

all: $(MKFS_BIN) $(MOUNT_BIN)

# mkfs.bwfs
$(MKFS_BIN): $(MKFS_SRC)
	$(CC) $(CFLAGS) -o $@ $< $(MKFS_LD)

# mount.bwfs  (mont.c + bwfs_ops.c)
$(MOUNT_BIN): $(MOUNT_SRC) $(BWFS_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(MNT_LD)

clean:
	rm -f $(MKFS_BIN) $(MOUNT_BIN)