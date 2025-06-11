#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <png.h>
#include <stdint.h>

#include "../include/bwfs.h"

#define FS_MAGIC 0xBEEF2025
#define IMG_WIDTH 1000
#define IMG_HEIGHT 1000

#define FS_SIZE_MB 5
#define FS_SIZE_BYTES (FS_SIZE_MB * 1024 * 1024)

#define BITS_PER_BLOCK (IMG_WIDTH * IMG_HEIGHT)
#define BYTES_PER_BLOCK (BITS_PER_BLOCK / 8)

#define USABLE_BLOCKS ((FS_SIZE_BYTES + BYTES_PER_BLOCK - 1) / BYTES_PER_BLOCK)
#define TOTAL_BLOCKS (USABLE_BLOCKS + 3)  // +3 para metadatos

// Función general para escribir cualquier estructura en un bloque PNG
void write_struct_to_png(const char *filename, const uint8_t *data, size_t size) {
    if (size > BYTES_PER_BLOCK) {
        fprintf(stderr, "Estructura excede tamaño del bloque (%lu > %d bytes)\n", size, BYTES_PER_BLOCK);
        exit(1);
    }

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("fopen");
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
                row[x] = bit ? 0x00 : 0xFF;
            } else {
                row[x] = 0xFF;
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

// Crea un bloque de datos completamente blanco
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
    memset(row, 255, IMG_WIDTH);

    for (int y = 0; y < IMG_HEIGHT; y++)
        png_write_row(png_ptr, row);

    free(row);
    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
}

// Programa principal
int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s <carpeta_destino>\n", argv[0]);
        return 1;
    }

    const char *folder = argv[1];
    mkdir(folder, 0777);

    char path[256];

    // Inicializar estructuras
    Superblock sb = {
        .magic_number = FS_MAGIC,
        .total_blocks = TOTAL_BLOCKS,
        .used_blocks = 3,  // 0, 1, 2 ya ocupados
        .inode_start = 1,
        .bitmap_start = 2
    };

    Inode inodos[MAX_FILES] = {0};
    uint8_t bitmap[TOTAL_BLOCKS] = {0};
    bitmap[0] = 1;  // superbloque
    bitmap[1] = 1;  // inodos
    bitmap[2] = 1;  // bitmap

    // Guardar superbloque
    snprintf(path, sizeof(path), "%s/block_0000.png", folder);
    write_struct_to_png(path, (uint8_t*)&sb, sizeof(sb));

    // Guardar inodos
    snprintf(path, sizeof(path), "%s/block_0001.png", folder);
    write_struct_to_png(path, (uint8_t*)inodos, sizeof(inodos));

    // Guardar bitmap
    snprintf(path, sizeof(path), "%s/block_0002.png", folder);
    write_struct_to_png(path, bitmap, sizeof(bitmap));

    // Crear bloques vacíos para datos
    for (int i = 3; i < TOTAL_BLOCKS; i++) {
        snprintf(path, sizeof(path), "%s/block_%04d.png", folder, i);
        create_blank_png(path);
    }

    printf("FS de %d MB creado exitosamente en %s\n", FS_SIZE_MB, folder);
    printf("- Total bloques: %d\n", TOTAL_BLOCKS);
    printf("- Bloques de datos disponibles: %d\n", USABLE_BLOCKS);
    return 0;
}