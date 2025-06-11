#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <png.h>
#include "../include/bwfs.h"

#define IMG_WIDTH 1000
#define IMG_HEIGHT 1000
#define BLOCK_SIZE 1024

// Función para escribir una estructura en una imagen PNG (igual que mkfs)
void write_struct_to_png(const char *filename, const uint8_t *data, size_t size) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!png_ptr || !info_ptr) {
        fclose(fp);
        fprintf(stderr, "Error inicializando libpng\n");
        exit(1);
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fclose(fp);
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fprintf(stderr, "Error escribiendo PNG\n");
        exit(1);
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, IMG_WIDTH, IMG_HEIGHT, 8,
                 PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
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

int main() {
    // Crear archivo hola.txt en el primer inodo
    Inode inodos[MAX_FILES] = {0};

    FILE *fp = fopen("fs_data/block_0001.png", "rb");
    if (!fp) {
        perror("Error abriendo block_0001.png");
        return 1;
    }
    fread(inodos, sizeof(inodos), 1, fp);
    fclose(fp);

    // Inodo 0 -> hola.txt
    strcpy(inodos[0].name, "hola.txt");
    inodos[0].used = 1;
    inodos[0].size = 12;
    inodos[0].block_pointers[0] = 3;  // usar block_0003.png

    // Guardar inodos actualizados
    write_struct_to_png("fs_data/block_0001.png", (uint8_t *)inodos, sizeof(inodos));

    // Escribir contenido en el bloque 0003
    uint8_t contenido[BLOCK_SIZE] = {0};
    strcpy((char *)contenido, "¡Hola BWFS!\n");
    write_struct_to_png("fs_data/block_0003.png", contenido, BLOCK_SIZE);

    printf("Archivo 'hola.txt' inyectado exitosamente.\n");
    return 0;
}