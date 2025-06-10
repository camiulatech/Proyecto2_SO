// mkfs/mkfs.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <png.h>
#include "../include/bwfs.h"

#define FS_MAGIC 0xBEEF2025
#define IMG_WIDTH 1000
#define IMG_HEIGHT 1000

#define FS_SIZE_MB 5
#define FS_SIZE_BYTES (FS_SIZE_MB * 1024 * 1024)  // 5 MB

#define BITS_PER_BLOCK (IMG_WIDTH * IMG_HEIGHT)
#define BYTES_PER_BLOCK (BITS_PER_BLOCK / 8)  // 125000 bytes por imagen (bloque)

#define USABLE_BLOCKS ((FS_SIZE_BYTES + BYTES_PER_BLOCK - 1) / BYTES_PER_BLOCK)
#define TOTAL_BLOCKS (USABLE_BLOCKS)



void create_blank_png(const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Error creando archivo PNG");
        exit(1);
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info_ptr = png_create_info_struct(png_ptr);

    if (!png_ptr || !info_ptr) {
        fprintf(stderr, "Error inicializando libpng\n");
        exit(1);
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "Error durante la escritura del PNG\n");
        exit(1);
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, IMG_WIDTH, IMG_HEIGHT,
                 8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);

    png_bytep row = (png_bytep) malloc(IMG_WIDTH * sizeof(png_byte));
    memset(row, 255, IMG_WIDTH);  // píxeles blancos

    for (int y = 0; y < IMG_HEIGHT; y++)
        png_write_row(png_ptr, row);

    png_write_end(png_ptr, NULL);
    free(row);
    fclose(fp);
    png_destroy_write_struct(&png_ptr, &info_ptr);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s carpeta_destino/\n", argv[0]);
        return 1;
    }

    char *folder = argv[1];
    mkdir(folder, 0777);

    char path[256];

    // Crear imágenes PNG
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        snprintf(path, sizeof(path), "%s/block_%04d.png", folder, i);
        create_blank_png(path);
    }

    // Crear archivo de metadata
    snprintf(path, sizeof(path), "%s/metadata.dat", folder);
    FILE *meta = fopen(path, "wb");

    Superblock sb = {
        .magic_number = FS_MAGIC,
        .total_blocks = TOTAL_BLOCKS,
        .used_blocks = 0,
        .inode_start = sizeof(Superblock),
        .bitmap_start = sizeof(Superblock) + sizeof(Inode) * MAX_FILES
    };

    fwrite(&sb, sizeof(Superblock), 1, meta);

    Inode inode = {0};
    for (int i = 0; i < MAX_FILES; i++) {
        fwrite(&inode, sizeof(Inode), 1, meta);
    }

    unsigned char zero = 0;
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        fwrite(&zero, 1, 1, meta);
    }

    fclose(meta);
    printf("FS de %d MB creado en %s con:\n", FS_SIZE_MB, folder);
    printf("- Bloques de datos útiles: %d\n", USABLE_BLOCKS);
    printf("- Total de imágenes PNG creadas: %d\n", TOTAL_BLOCKS);
    return 0;
}
