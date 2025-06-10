// mkfs/mkfs.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../include/bwfs.h"

#define FS_MAGIC 0xBEEF2025

void create_blank_image(const char *filename) {
    FILE *img = fopen(filename, "wb");
    if (!img) {
        perror("Error creando imagen");
        exit(1);
    }
    unsigned char white = 0xFF;  // Todos los pixeles blancos
    for (int i = 0; i < BLOCK_SIZE; i++)
        fwrite(&white, 1, 1, img);
    fclose(img);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s folder/\n", argv[0]);
        return 1;
    }

    char *folder = argv[1];
    mkdir(folder, 0777);

    // Crear imágenes como bloques
    char path[256];
    for (int i = 0; i < MAX_BLOCKS; i++) {
        snprintf(path, sizeof(path), "%s/block_%04d.bw", folder, i);
        create_blank_image(path);
    }

    // Crear archivo de metadata con superbloque + inodos + bitmap
    snprintf(path, sizeof(path), "%s/metadata.dat", folder);
    FILE *meta = fopen(path, "wb");

    Superblock sb = {
        .magic_number = FS_MAGIC,
        .total_blocks = MAX_BLOCKS,
        .used_blocks = 0,
        .inode_start = sizeof(Superblock),
        .bitmap_start = sizeof(Superblock) + sizeof(Inode) * MAX_FILES
    };

    fwrite(&sb, sizeof(Superblock), 1, meta);

    // Inicializar i-nodos
    Inode inode = {0};
    for (int i = 0; i < MAX_FILES; i++) {
        fwrite(&inode, sizeof(Inode), 1, meta);
    }

    // Inicializar bitmap (1 byte por bloque)
    unsigned char zero = 0;
    for (int i = 0; i < MAX_BLOCKS; i++) {
        fwrite(&zero, 1, 1, meta);
    }

    fclose(meta);
    printf("FS creado exitosamente en %s\n", folder);
    return 0;
}
