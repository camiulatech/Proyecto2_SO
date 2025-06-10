#define FUSE_USE_VERSION 31

#include <time.h>         // intenta primero con esta
#include <sys/stat.h>

#ifndef _STRUCT_TIMESPEC

#define _STRUCT_TIMESPEC
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#endif

#include <fuse3/fuse.h>


#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

// Atributos mínimos para el directorio raíz
static int fs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;  // directorio con permisos rwxr-xr-x
        stbuf->st_nlink = 2;
        return 0;
    }
    if (strcmp(path, "/hola.txt") == 0) {
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = 1;
        stbuf->st_size = 13;  // tamaño simulado
        return 0;
    }
    

    return -ENOENT;
}

// Solo muestra "." y ".." al hacer ls
static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    filler(buf, "hola.txt", NULL, 0, 0);  // <-- archivo simulado

    return 0;
}

static struct fuse_operations fs_oper = {
    .getattr = fs_getattr,
    .readdir = fs_readdir,
};

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <punto_de_montaje>\n", argv[0]);
        return 1;
    }

    return fuse_main(argc, argv, &fs_oper, NULL);
}
