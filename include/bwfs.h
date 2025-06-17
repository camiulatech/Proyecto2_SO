// include/bwfs.h
#ifndef BWFS_H
#define BWFS_H

#define BLOCK_SIZE 1024         // Simulado: 1 bloque = 1 imagen = 1024 bytes
#define MAX_BLOCKS 1024         // Máximo de bloques por FS
#define MAX_FILES 128

// Estructura del Superbloque
typedef struct {
    int magic_number;
    int total_blocks;
    int used_blocks;
    int inode_start;
    int bitmap_start;
} Superblock;

// i-nodo simple
typedef struct {
    int used;
    uint8_t is_dir;  // 👈 Nuevo campo: 1 si es directorio, 0 si es archivo
    char name[64];
    int size;
    int block_pointers[8]; // Soporte para archivos de hasta 8 bloques
    int parent_inode;
} Inode;

#endif
