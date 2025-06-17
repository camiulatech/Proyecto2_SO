#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../include/bwfs.h"

#define BLOCK_SIZE 1024

extern Superblock sb;
extern Inode inodos[MAX_FILES];
extern uint8_t bitmap[MAX_BLOCKS];

void read_struct_from_png(const char *filename, uint8_t *buffer, size_t size);
void write_struct_to_png(const char *filename, const uint8_t *buffer, size_t size);

int buscar_bloques_contiguos_libres(int cantidad) {
    int cont = 0;
    for (int i = 3; i < sb.total_blocks; i++) {
        if (bitmap[i] == 0) {
            cont++;
            if (cont == cantidad) return i - cantidad + 1;
        } else {
            cont = 0;
        }
    }
    return -1;
}

void leer_bloque(const char *folder, int id, uint8_t *buf) {
    char filename[256];
    snprintf(filename, sizeof(filename), "%s/block_%04d.png", folder, id);
    read_struct_from_png(filename, buf, BLOCK_SIZE);
}

void escribir_bloque(const char *folder, int id, uint8_t *buf) {
    char filename[256];
    snprintf(filename, sizeof(filename), "%s/block_%04d.png", folder, id);
    write_struct_to_png(filename, buf, BLOCK_SIZE);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <carpeta_fs>\n", argv[0]);
        return 1;
    }

    const char *folder = argv[1];

    char path_sb[256], path_inodes[256], path_bitmap[256];
    snprintf(path_sb, sizeof(path_sb), "%s/block_0000.png", folder);
    snprintf(path_inodes, sizeof(path_inodes), "%s/block_0001.png", folder);
    snprintf(path_bitmap, sizeof(path_bitmap), "%s/block_0002.png", folder);

    read_struct_from_png(path_sb, (uint8_t*)&sb, sizeof(sb));
    read_struct_from_png(path_inodes, (uint8_t*)inodos, sizeof(inodos));
    read_struct_from_png(path_bitmap, bitmap, sizeof(bitmap));

    int archivos_procesados = 0;

    for (int i = 0; i < MAX_FILES; i++) {
        Inode *in = &inodos[i];
        if (!in->used || in->is_dir) continue;

        int blocks_used = (in->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        if (blocks_used <= 1) continue;

        bool contiguo = true;
        for (int j = 1; j < blocks_used; j++) {
            if (in->block_pointers[j] != in->block_pointers[j - 1] + 1) {
                contiguo = false;
                break;
            }
        }
        if (contiguo) continue;

        int nuevo_inicio = buscar_bloques_contiguos_libres(blocks_used);
        if (nuevo_inicio == -1) continue;

        for (int j = 0; j < blocks_used; j++) {
            uint8_t buf[BLOCK_SIZE];
            leer_bloque(folder, in->block_pointers[j], buf);
            escribir_bloque(folder, nuevo_inicio + j, buf);
        }

        for (int j = 0; j < blocks_used; j++) {
            bitmap[in->block_pointers[j]] = 0;
        }

        for (int j = 0; j < blocks_used; j++) {
            in->block_pointers[j] = nuevo_inicio + j;
            bitmap[nuevo_inicio + j] = 1;
        }

        archivos_procesados++;
    }

    write_struct_to_png(path_inodes, (uint8_t *)inodos, sizeof(inodos));
    write_struct_to_png(path_bitmap, bitmap, sizeof(bitmap));

    printf("✅ Desfragmentación completada. Archivos procesados: %d\n", archivos_procesados);
    return 0;
}