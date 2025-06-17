#ifndef BWFS_OPS_H
#define BWFS_OPS_H

#include <fuse3/fuse.h>
#include "bwfs.h"

extern struct fuse_operations fs_oper;
extern char mount_folder[256];
extern Superblock sb;
extern Inode inodos[MAX_FILES];
extern uint8_t bitmap[MAX_BLOCKS];

void cargar_metadatos(void);

#endif