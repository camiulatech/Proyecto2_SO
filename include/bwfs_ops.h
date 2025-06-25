#ifndef BWFS_OPS_H
#define BWFS_OPS_H

#include <fuse3/fuse.h>
#include "bwfs.h"

// Estructuras globales
extern struct fuse_operations fs_oper;
extern char mount_folder[256];
extern Superblock sb;
extern Inode inodos[MAX_FILES];
extern uint8_t bitmap[MAX_BLOCKS];
extern uint16_t used_bytes[MAX_BLOCKS];

void load_metadata(void);

#endif