#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../include/bwfs_ops.h"   // ya incluye "bwfs.h"

/* ──────────────────────── main ──────────────────────── */
int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr,"Uso: %s <carpeta_fs> <punto_montaje>\n", argv[0]);
        return 1;
    }

    /* Resuelve ruta absoluta de la carpeta donde viven los PNG */
    if (!realpath(argv[1], mount_folder)) {
        perror("realpath");
        return 1;
    }

    /* Verifica que el punto de montaje exista y sea directorio */
    struct stat st;
    if (stat(argv[2], &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr,"Error: '%s' no es un directorio válido.\n", argv[2]);
        return 1;
    }

    cargar_metadatos();

    /* Lanza FUSE en foreground (-f) */
    char *fuse_args[] = { argv[0], "-f", argv[2] };
    int    argc_fuse  = sizeof(fuse_args)/sizeof(fuse_args[0]);
    return fuse_main(argc_fuse, fuse_args, &fs_oper, NULL);
}