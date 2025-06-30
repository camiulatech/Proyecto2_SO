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

#define FS_SIZE_BYTES (FS_SIZE_MB * 1024 * 1024)

#define BITS_PER_BLOCK (IMG_WIDTH * IMG_HEIGHT)
#define BYTES_PER_BLOCK (BITS_PER_BLOCK / 8)

#define USABLE_BLOCKS ((FS_SIZE_BYTES + BYTES_PER_BLOCK - 1) / BYTES_PER_BLOCK)
#define TOTAL_BLOCKS (USABLE_BLOCKS + 3)  // +3 para metadatos

// Crea un bloque de datos completamente blanco como imagen PNG en escala de grises
void create_blank_png(const char *filename) {
    // Abre el archivo en modo binario para escritura
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        // Si no se pudo abrir el archivo, se muestra el error y se termina
        perror("Error creando PNG");
        exit(1);
    }

    // Crea la estructura principal de escritura PNG (libpng)
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    // Crea la estructura de información del PNG
    png_infop info_ptr = png_create_info_struct(png_ptr);

    // Verifica que ambas estructuras se hayan creado correctamente
    if (!png_ptr || !info_ptr) {
        fprintf(stderr, "Error inicializando libpng\n");
        exit(1);
    }

    // Configura el manejador de errores de libpng
    if (setjmp(png_jmpbuf(png_ptr))) {
        // Si ocurre un error al escribir el PNG, se limpia y se cierra el archivo
        fprintf(stderr, "Error escribiendo PNG\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        exit(1);
    }

    // Inicializa libpng con el archivo de salida
    png_init_io(png_ptr, fp);

    // Define los parámetros del encabezado del PNG:
    // - IMG_WIDTH x IMG_HEIGHT: dimensiones de la imagen (en píxeles)
    // - 1 bit por píxel (blanco o negro)
    // - Escala de grises (sin color)
    // - Sin entrelazado ni filtros especiales
    png_set_IHDR(png_ptr, info_ptr, IMG_WIDTH, IMG_HEIGHT,
                 1, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    // Desactiva la compresión para que la imagen sea escrita de forma simple
    png_set_compression_level(png_ptr, 0);

    // Escribe la cabecera del archivo PNG
    png_write_info(png_ptr, info_ptr);

    // Calcula la cantidad de bytes por fila (redondeando bits a bytes)
    size_t row_bytes = (IMG_WIDTH + 7) / 8;

    // Reserva memoria para una fila de la imagen
    png_bytep row = (png_bytep)malloc(row_bytes);

    // Llena la fila con 0xFF (todos los bits en 1 = blanco en 1-bit grayscale)
    memset(row, 0xFF, row_bytes);

    // Escribe todas las filas (todas son iguales) para crear una imagen blanca
    for (int y = 0; y < IMG_HEIGHT; y++)
        png_write_row(png_ptr, row);

    // Libera memoria de la fila
    free(row);

    // Finaliza la escritura del PNG
    png_write_end(png_ptr, NULL);

    // Libera las estructuras de libpng
    png_destroy_write_struct(&png_ptr, &info_ptr);

    // Cierra el archivo
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