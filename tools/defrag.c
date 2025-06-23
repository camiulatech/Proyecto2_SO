// Inclusión de bibliotecas estándar
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../include/bwfs.h"

#define BLOCK_SIZE 125000

extern Superblock sb;
extern Inode inodos[MAX_FILES];
extern uint8_t bitmap[MAX_BLOCKS];

// Funciones para leer y escribir estructuras desde PNGs
void read_struct_from_png(const char *filename, uint8_t *buffer, size_t size);
void write_struct_to_png(const char *filename, const uint8_t *buffer, size_t size);

// Busca una secuencia contigua de bloques libres de longitud `quantity` y devuelve el índice del primer bloque contiguo encontrado, o -1 si no hay suficientes
int search_free_blocks(int quantity) {
    int cont = 0;
    for (int i = 3; i < sb.total_blocks; i++) { // Saltar bloques de metadatos
        if (bitmap[i] == 0) { // Bloque libre
            cont++;
            if (cont == quantity) return i - quantity + 1; // Encontró espacio contiguo suficiente
        } else {
            cont = 0; // Reiniciar contador si el bloque está ocupado
        }
    }
    return -1; // No hay bloques contiguos disponibles
}

// Lee un bloque desde disco (PNG) a memoria (buffer)
void read_block(const char *folder, int id, uint8_t *buf) {
    char filename[256];
    snprintf(filename, sizeof(filename), "%s/block_%04d.png", folder, id);
    read_struct_from_png(filename, buf, BLOCK_SIZE);
}

// Escribe un bloque desde memoria (buffer) a disco (PNG)
void write_block(const char *folder, int id, uint8_t *buf) {
    char filename[256];
    snprintf(filename, sizeof(filename), "%s/block_%04d.png", folder, id);
    write_struct_to_png(filename, buf, BLOCK_SIZE);
}

// Función principal de desfragmentación del FS
int main(int argc, char *argv[]) {
    // Verificar que se pase una carpeta como argumento
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <carpeta_fs>\n", argv[0]);
        return 1;
    }

    const char *folder = argv[1];

    // Construir rutas a los archivos de metadatos
    char path_sb[256], path_inodes[256], path_bitmap[256];
    snprintf(path_sb, sizeof(path_sb), "%s/block_0000.png", folder);
    snprintf(path_inodes, sizeof(path_inodes), "%s/block_0001.png", folder);
    snprintf(path_bitmap, sizeof(path_bitmap), "%s/block_0002.png", folder);

    // Cargar estructuras desde disco
    read_struct_from_png(path_sb, (uint8_t*)&sb, sizeof(sb));
    read_struct_from_png(path_inodes, (uint8_t*)inodos, sizeof(inodos));
    read_struct_from_png(path_bitmap, bitmap, sizeof(bitmap));

    int processed_files = 0;

    // Recorre todos los inodos para procesar archivos (no directorios)
    for (int i = 0; i < MAX_FILES; i++) {
        Inode *in = &inodos[i];
        if (!in->used || in->is_dir) continue; // Ignorar inodos no usados o directorios

        // Calcular cuántos bloques ocupa este archivo
        int blocks_used = (in->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        if (blocks_used <= 1) continue; // No necesita desfragmentarse

        // Verificar si ya están contiguos
        bool contiguo = true;
        for (int j = 1; j < blocks_used; j++) {
            if (in->block_pointers[j] != in->block_pointers[j - 1] + 1) {
                contiguo = false;
                break;
            }
        }
        if (contiguo) continue; // Ya está desfragmentado

        // Buscar bloques contiguos libres suficientes
        int new_start = search_free_blocks(blocks_used);
        if (new_start == -1) continue; // No hay espacio para desfragmentar

        // Copiar contenido de los bloques actuales a los nuevos bloques
        for (int j = 0; j < blocks_used; j++) {
            uint8_t buf[BLOCK_SIZE];
            read_block(folder, in->block_pointers[j], buf);
            write_block(folder, new_start + j, buf);
        }

        // Marcar bloques antiguos como libres en el bitmap
        for (int j = 0; j < blocks_used; j++) {
            bitmap[in->block_pointers[j]] = 0;
        }

        // Actualizar punteros de bloques del inodo y marcar nuevos bloques como usados
        for (int j = 0; j < blocks_used; j++) {
            in->block_pointers[j] = new_start + j;
            bitmap[new_start + j] = 1;
        }

        processed_files++;
    }

    // Guardar estructuras actualizadas en disco
    write_struct_to_png(path_inodes, (uint8_t *)inodos, sizeof(inodos));
    write_struct_to_png(path_bitmap, bitmap, sizeof(bitmap));

    // Imprimir resumen
    printf("Desfragmentación completada. Archivos procesados: %d\n", processed_files);
    return 0;
}