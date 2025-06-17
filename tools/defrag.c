#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "bwfs_ops.h" // Asegurate que esto incluya inodos[], bitmap[] y funciones como write_struct_to_png

#define MAX_INODES 128
#define BLOCK_SIZE 1048576 // 1MB (ajusta si usás otro valor)

extern Inode inodos[];
extern uint8_t *bitmap;
extern Superblock superblock;

int buscar_bloques_contiguos_libres(uint8_t *bitmap, int total, int cantidad) {
    int cont = 0;
    for (int i = 0; i < total; i++) {
        if (bitmap[i] == 0) {
            cont++;
            if (cont == cantidad) return i - cantidad + 1;
        } else {
            cont = 0;
        }
    }
    return -1;
}

void leer_bloque(int id, uint8_t *buf) {
    char filename[64];
    sprintf(filename, "block_%04d.png", id);
    FILE *f = fopen(filename, "rb");
    if (!f) return;
    fread(buf, 1, BLOCK_SIZE, f);
    fclose(f);
}

void escribir_bloque(int id, uint8_t *buf) {
    char filename[64];
    sprintf(filename, "block_%04d.png", id);
    FILE *f = fopen(filename, "wb");
    if (!f) return;
    fwrite(buf, 1, BLOCK_SIZE, f);
    fclose(f);
}

int main() {
    for (int i = 0; i < MAX_INODES; i++) {
        Inode *in = &inodos[i];
        if (!in->used || in->is_dir) continue;

        int blocks_used = (in->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        if (blocks_used <= 1) continue;

        // ¿Ya está contiguo?
        bool contiguo = true;
        for (int j = 1; j < blocks_used; j++) {
            if (in->block_pointers[j] != in->block_pointers[j - 1] + 1) {
                contiguo = false;
                break;
            }
        }
        if (contiguo) continue;

        int nuevo_inicio = buscar_bloques_contiguos_libres(bitmap, superblock.total_blocks, blocks_used);
        if (nuevo_inicio == -1) continue;

        // Copiar datos
        for (int j = 0; j < blocks_used; j++) {
            uint8_t buf[BLOCK_SIZE];
            leer_bloque(in->block_pointers[j], buf);
            escribir_bloque(nuevo_inicio + j, buf);
        }

        // Liberar bloques viejos
        for (int j = 0; j < blocks_used; j++) {
            bitmap[in->block_pointers[j]] = 0;
        }

        // Marcar nuevos y actualizar punteros
        for (int j = 0; j < blocks_used; j++) {
            in->block_pointers[j] = nuevo_inicio + j;
            bitmap[nuevo_inicio + j] = 1;
        }
    }

    write_struct_to_png("block_0001.png", (uint8_t *)inodos, sizeof(Inode) * MAX_INODES);
    write_struct_to_png("block_0002.png", bitmap, superblock.total_blocks);

    printf("Desfragmentación finalizada exitosamente.\n");
    return 0;
}
