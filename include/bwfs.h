// include/bwfs.h
#ifndef BWFS_H
#define BWFS_H

#define FS_SIZE_MB 60            // Tamaño del sistema de archivos en MB
#define BLOCK_SIZE 125000       // Simulado: 1 bloque = 1 imagen = 1024 bytes
#define MAX_BLOCKS 1024         // Máximo de bloques por FS
#define MAX_FILES (BLOCK_SIZE / sizeof(Inode)) // Máximo de archivos por FS

#define MAX_BLOCKS_PER_FILE ((FS_SIZE_MB * 1024 * 1024) / BLOCK_SIZE) // Máximo de bloques por archivo

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
    char name[128];
    int size;
    int fragment_order[MAX_BLOCKS_PER_FILE]; // Indica el orden lógico de los bloques
    int block_pointers[MAX_BLOCKS_PER_FILE]; // Soporte para archivos de hasta 8 bloques
    int block_offsets[MAX_BLOCKS_PER_FILE]; // Nuevos offsets para cada bloque
    int parent_inode;
} Inode;

void read_struct_from_png(const char *filename, uint8_t *buffer, size_t size);
void write_struct_to_png(const char *filename, const uint8_t *data, size_t size);
void read_data_block(int block_id, uint8_t *buffer, size_t size);

#endif
