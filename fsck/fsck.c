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
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <carpeta_fs>\n", argv[0]);
        return 1;
    }

    char path_sb[256], path_inodes[256], path_bitmap[256];
    snprintf(path_sb, sizeof(path_sb), "%s/block_0000.png", argv[1]);
    snprintf(path_inodes, sizeof(path_inodes), "%s/block_0001.png", argv[1]);
    snprintf(path_bitmap, sizeof(path_bitmap), "%s/block_0002.png", argv[1]);

    read_struct_from_png(path_sb, (uint8_t*)&sb, sizeof(sb));
    read_struct_from_png(path_inodes, (uint8_t*)inodos, sizeof(inodos));
    read_struct_from_png(path_bitmap, bitmap, sizeof(bitmap));

    printf("🧪 Verificando consistencia de BWFS en %s...\n", argv[1]);

    // Variables de chequeo
    int errores = 0;
    int bloques_usados[MAX_BLOCKS] = {0};
    int bloques_marcados = 0;
    int bloques_referenciados = 0;

    // 1. Revisar inodos y bloques referenciados
    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used) continue;

        for (int j = 0; j < 8; j++) {
            int b = inodos[i].block_pointers[j];
            if (b > 0) {
                bloques_usados[b]++;
                bloques_referenciados++;
                if (bitmap[b] == 0) {
                    printf("❌ Inodo %d usa bloque %d pero no está marcado en bitmap.\n", i, b);
                    errores++;
                }
            }
        }

        if (inodos[i].parent_inode < -1 || inodos[i].parent_inode >= MAX_FILES) {
            printf("❌ Inodo %d tiene parent_inode inválido (%d).\n", i, inodos[i].parent_inode);
            errores++;
        }
    }

    // 2. Revisar bitmap vs inodos
    for (int i = 3; i < sb.total_blocks; i++) {
        if (bitmap[i]) {
            bloques_marcados++;
            if (bloques_usados[i] == 0) {
                printf("⚠️  Bloque %d marcado en bitmap pero no referenciado por ningún inodo.\n", i);
            }
        }
    }

    // 3. Duplicados en el mismo directorio
    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used) continue;
        for (int j = i + 1; j < MAX_FILES; j++) {
            if (inodos[j].used &&
                inodos[i].parent_inode == inodos[j].parent_inode &&
                strcmp(inodos[i].name, inodos[j].name) == 0) {
                printf("❌ Nombres duplicados '%s' en el mismo directorio (inodos %d y %d).\n", inodos[i].name, i, j);
                errores++;
            }
        }
    }

    // Resultado final
    printf("\n📊 Resumen:\n");
    printf(" - Bloques marcados en bitmap: %d\n", bloques_marcados);
    printf(" - Bloques referenciados por inodos: %d\n", bloques_referenciados);
    printf(" - Errores encontrados: %d\n", errores);

    return errores ? 1 : 0;
}

/*
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <carpeta_fs>\n", argv[0]);
        return 1;
    }

    char path_sb[256], path_inodes[256], path_bitmap[256];
    snprintf(path_sb, sizeof(path_sb), "%s/block_0000.png", argv[1]);
    snprintf(path_inodes, sizeof(path_inodes), "%s/block_0001.png", argv[1]);
    snprintf(path_bitmap, sizeof(path_bitmap), "%s/block_0002.png", argv[1]);

    read_struct_from_png(path_sb, (uint8_t*)&sb, sizeof(sb));
    read_struct_from_png(path_inodes, (uint8_t*)inodos, sizeof(inodos));
    read_struct_from_png(path_bitmap, bitmap, sizeof(bitmap));

    printf("🧪 Verificando consistencia de BWFS en %s...\n", argv[1]);

    int errores = 0;
    int bloques_usados[MAX_BLOCKS] = {0};
    int bloques_marcados = 0;
    int bloques_referenciados = 0;

    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used) continue;

        for (int j = 0; j < 8; j++) {
            int b = inodos[i].block_pointers[j];
            if (b > 0) {
                bloques_usados[b]++;
                bloques_referenciados++;
                if (bitmap[b] == 0) {
                    printf("❌ Inodo %d usa bloque %d pero no está marcado en bitmap.\n", i, b);
                    errores++;
                }
            }
        }

        if (inodos[i].parent_inode < -1 || inodos[i].parent_inode >= MAX_FILES) {
            printf("❌ Inodo %d tiene parent_inode inválido (%d).\n", i, inodos[i].parent_inode);
            errores++;
        }
    }

    for (int i = 3; i < sb.total_blocks; i++) {
        if (bitmap[i]) {
            bloques_marcados++;
            if (bloques_usados[i] == 0) {
                printf("⚠️  Bloque %d marcado en bitmap pero no referenciado por ningún inodo.\n", i);
            }
        }
    }

    int duplicados = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used) continue;
        for (int j = i + 1; j < MAX_FILES; j++) {
            if (inodos[j].used &&
                inodos[i].parent_inode == inodos[j].parent_inode &&
                strcmp(inodos[i].name, inodos[j].name) == 0) {
                printf("❌ Nombres duplicados '%s' en el mismo directorio (inodos %d y %d).\n", inodos[i].name, i, j);
                errores++;
                duplicados++;
            }
        }
    }

    int inodos_usados = 0;
    int bloques_marcados_no_usados = 0;
    int bloques_usados_no_marcados = 0;

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used) inodos_usados++;
    }

    for (int i = 3; i < sb.total_blocks; i++) {
        if (bitmap[i] && bloques_usados[i] == 0) bloques_marcados_no_usados++;
        if (!bitmap[i] && bloques_usados[i] > 0) bloques_usados_no_marcados++;
    }

    printf("\n📊 Resumen de Estado de BWFS\n");
    printf("────────────────────────────────────────\n");
    printf("🗂️  Inodos usados:            %d\n", inodos_usados);
    printf("📁 Inodos libres:            %d\n", MAX_FILES - inodos_usados);
    printf("📦 Bloques referenciados:    %d\n", bloques_referenciados);
    printf("🧮 Bloques marcados (bitmap): %d\n", bloques_marcados);
    printf("⚠️  Bloques marcados no usados: %d\n", bloques_marcados_no_usados);
    printf("❌ Bloques usados no marcados: %d\n", bloques_usados_no_marcados);
    printf("📛 Nombres duplicados:        %d\n", duplicados);
    printf("🔴 Errores detectados:        %d\n", errores);

    return errores ? 1 : 0;
}
*/

