#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <png.h>
#include <stdint.h>   // define uint8_t

#include "../include/bwfs.h"

#define FS_MAGIC 0xBEEF2025
#define IMG_WIDTH 1000
#define IMG_HEIGHT 1000

#define FS_SIZE_MB 5
#define FS_SIZE_BYTES (FS_SIZE_MB * 1024 * 1024)

#define BITS_PER_BLOCK (IMG_WIDTH * IMG_HEIGHT)
#define BYTES_PER_BLOCK (BITS_PER_BLOCK / 8)

#define USABLE_BLOCKS ((FS_SIZE_BYTES + BYTES_PER_BLOCK - 1) / BYTES_PER_BLOCK)
#define TOTAL_BLOCKS (USABLE_BLOCKS)

// -------------------------------------------------------------------

void write_metadata_to_block0(const char *filename, const uint8_t *data, size_t size) {
    if (size > BYTES_PER_BLOCK) {
        fprintf(stderr, "Metadatos exceden tamaño del bloque 0 (%lu > %d bytes)\n", size, BYTES_PER_BLOCK);
        exit(1);
    }

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("fopen block_0000.png");
        exit(1);
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!png_ptr || !info_ptr) {
        fclose(fp);
        fprintf(stderr, "Error inicializando PNG\n");
        exit(1);
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fclose(fp);
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fprintf(stderr, "Error escribiendo PNG\n");
        exit(1);
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, IMG_WIDTH, IMG_HEIGHT,
                 8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);

    png_bytep row = malloc(IMG_WIDTH);
    size_t bit_index = 0;

    for (int y = 0; y < IMG_HEIGHT; y++) {
        for (int x = 0; x < IMG_WIDTH; x++) {
            if (bit_index < size * 8) {
                uint8_t byte = data[bit_index / 8];
                uint8_t bit = (byte >> (7 - (bit_index % 8))) & 1;
                row[x] = bit ? 0x00 : 0xFF;  // 1 = negro (ocupado), 0 = blanco (libre)
            } else {
                row[x] = 0xFF;  // libre
            }
            bit_index++;
        }
        png_write_row(png_ptr, row);
    }

    free(row);
    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
}

void create_blank_png(const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Error creando PNG");
        exit(1);
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!png_ptr || !info_ptr) {
        fprintf(stderr, "Error inicializando libpng\n");
        exit(1);
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "Error escribiendo PNG\n");
        exit(1);
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, IMG_WIDTH, IMG_HEIGHT,
                 8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);

    png_bytep row = malloc(IMG_WIDTH);
    memset(row, 255, IMG_WIDTH);  // blanco

    for (int y = 0; y < IMG_HEIGHT; y++)
        png_write_row(png_ptr, row);

    free(row);
    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
}

// -------------------------------------------------------------------

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s <carpeta_destino>\n", argv[0]);
        return 1;
    }

    const char *folder = argv[1];
    mkdir(folder, 0777);

    char path[256];

    // Reservar espacio para metadatos: superbloque + inodos + bitmap
    size_t meta_size = sizeof(Superblock) + sizeof(Inode) * MAX_FILES + TOTAL_BLOCKS;
    uint8_t *metadata = calloc(meta_size, 1);

    Superblock sb = {
        .magic_number = FS_MAGIC,
        .total_blocks = TOTAL_BLOCKS,
        .used_blocks = 0,
        .inode_start = sizeof(Superblock),
        .bitmap_start = sizeof(Superblock) + sizeof(Inode) * MAX_FILES
    };

    memcpy(metadata, &sb, sizeof(Superblock));

    Inode inode = {0};
    for (int i = 0; i < MAX_FILES; i++) {
        memcpy(metadata + sizeof(Superblock) + i * sizeof(Inode), &inode, sizeof(Inode));
    }

    // bitmap ya está en 0 (calloc) → todos los bloques libres

    // Escribir metadatos en block_0000.png
    snprintf(path, sizeof(path), "%s/block_0000.png", folder);
    write_metadata_to_block0(path, metadata, meta_size);
    free(metadata);

    // Crear los bloques restantes
    for (int i = 1; i < TOTAL_BLOCKS; i++) {
        snprintf(path, sizeof(path), "%s/block_%04d.png", folder, i);
        create_blank_png(path);
    }

    printf("FS de %d MB creado en %s con:\n", FS_SIZE_MB, folder);
    printf("- Bloques de datos útiles: %d\n", USABLE_BLOCKS);
    printf("- Total de imágenes PNG creadas: %d\n", TOTAL_BLOCKS);
    return 0;
}
