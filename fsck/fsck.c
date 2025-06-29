#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <png.h>
#include <libgen.h>
#include "../include/bwfs.h"

extern Superblock sb;
extern Inode inodos[MAX_FILES];
extern uint8_t bitmap[MAX_BLOCKS];

int main(int argc, char *argv[]) {
    // Verifica argumentos
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <carpeta_fs>\n", argv[0]);
        return 1;
    }

    // Construye rutas a los archivos de metadatos
    char path_sb[256], path_inodes[256], path_bitmap[256];
    snprintf(path_sb, sizeof(path_sb), "%s/block_0000.png", argv[1]);
    snprintf(path_inodes, sizeof(path_inodes), "%s/block_0001.png", argv[1]);
    snprintf(path_bitmap, sizeof(path_bitmap), "%s/block_0002.png", argv[1]);

    // Lee los metadatos desde los archivos PNG
    read_struct_from_png(path_sb, (uint8_t*)&sb, sizeof(sb));
    read_struct_from_png(path_inodes, (uint8_t*)inodos, sizeof(inodos));
    read_struct_from_png(path_bitmap, bitmap, sizeof(bitmap));

    printf("Verificando consistencia de BWFS en %s...\n", argv[1]);

    // Inicializa contadores y estructuras para el análisis
    int errors = 0;
    int used_blocks[MAX_BLOCKS] = {0};
    int marked_blocks = 0;
    int referenced_blocks = 0;

    // Recorre los inodos y verifica bloques
    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used) continue;

        // Verifica cada bloque usado por el inodo
        for (int j = 0; j < MAX_BLOCKS_PER_FILE; j++) {
            int b = inodos[i].block_pointers[j];
            if (b > 0) {
                used_blocks[b]++;
                referenced_blocks++;
                if (bitmap[b] == 0) {
                    printf("Inodo %d usa bloque %d pero no está marcado en bitmap.\n", i, b);
                    errors++;
                }
            }
        }

        // Verifica el parent_inode
        if ((inodos[i].parent_inode < -1 || inodos[i].parent_inode >= MAX_FILES) && i > 0) {
            printf("Inodo %d tiene parent_inode inválido (%d).\n", i, inodos[i].parent_inode);
            errors++;
        }
    }

    // Verifica bloques marcados en el bitmap
    for (int i = 3; i < sb.total_blocks; i++) {
        if (bitmap[i]) {
            marked_blocks++;
            if (used_blocks[i] == 0) {
                printf("Bloque %d marcado en bitmap pero no referenciado por ningún inodo.\n", i);
            }
        }
    }

    // Busca nombres duplicados en el mismo directorio
    int duplicates = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used) continue;
        for (int j = i + 1; j < MAX_FILES; j++) {
            if (inodos[j].used &&
                inodos[i].parent_inode == inodos[j].parent_inode &&
                strcmp(inodos[i].name, inodos[j].name) == 0) {
                printf("Nombres duplicados '%s' en el mismo directorio (inodos %d y %d).\n", inodos[i].name, i, j);
                errors++;
                duplicates++;
            }
        }
    }

    // Contar estadisticas finales
    int used_inodes = 0;
    int marked_blocks_not_used = 0;
    int used_blocks_not_marked = 0;

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used) used_inodes++;
    }

    for (int i = 3; i < sb.total_blocks; i++) {
        if (bitmap[i] && used_blocks[i] == 0) marked_blocks_not_used++;
        if (!bitmap[i] && used_blocks[i] > 0) used_blocks_not_marked++;
    }

    printf("\nResumen de Estado de BWFS\n");
    printf("────────────────────────────────────────\n");
    printf("Inodos usados:            %d\n", used_inodes);
    printf("Inodos libres:            %d\n", MAX_FILES - used_inodes);
    printf("Bloques referenciados:    %d\n", referenced_blocks);
    printf("Bloques marcados (bitmap): %d\n", marked_blocks);
    printf("Bloques marcados no usados: %d\n", marked_blocks_not_used);
    printf("Bloques usados no marcados: %d\n", used_blocks_not_marked);
    printf("Nombres duplicados:        %d\n", duplicates);
    printf("Errores detectados:        %d\n", errors);

    // Retorna 1 si hay errores, 0 si todo está bien
    return errors ? 1 : 0;
}