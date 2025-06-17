# ─────────── variables comunes ───────────
CC      = gcc
CFLAGS  = -Wall -Wextra -Iinclude
MKFS_LD = -lpng
MNT_LD  = `pkg-config fuse3 --cflags --libs` -lpng
FSCK_LD = -lpng

# ─────────── fuentes  ───────────
MKFS_SRC  = mkfs/mkfs.c
MOUNT_SRC = mount/mount.c
BWFS_SRC  = bwfs/bwfs_ops.c
FSCK_SRC  = fsck/fsck.c      # ← nuevo

# ─────────── ejecutables ───────────
MKFS_BIN  = mkfs.bwfs
MOUNT_BIN = mount.bwfs
FSCK_BIN  = fsck.bwfs

.PHONY: all clean

all: $(MKFS_BIN) $(MOUNT_BIN) $(FSCK_BIN)

# mkfs.bwfs
$(MKFS_BIN): $(MKFS_SRC)
	$(CC) $(CFLAGS) -o $@ $< $(MKFS_LD)

# mount.bwfs
$(MOUNT_BIN): $(MOUNT_SRC) $(BWFS_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(MNT_LD)

# fsck.bwfs
$(FSCK_BIN): $(FSCK_SRC)
	$(CC) $(CFLAGS) -o $@ $< $(FSCK_LD)

clean:
	rm -f $(MKFS_BIN) $(MOUNT_BIN) $(FSCK_BIN)