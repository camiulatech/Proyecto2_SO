#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <png.h>

#include "../include/bwfs.h"

// --------------------- GLOBALS -------------------------

static Superblock sb;
static Inode inodos[MAX_FILES];
static char mount_folder[256];

// --------------------- FUNCIONES AUXILIARES -------------------------

// Lee una estructura desde un bloque PNG
void read_struct_from_png(const char *filename, uint8_t *buffer, size_t size) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!png_ptr || !info_ptr) {
        fclose(fp);
        fprintf(stderr, "Error inicializando PNG lectura\n");
        exit(1);
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fclose(fp);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fprintf(stderr, "Error leyendo PNG\n");
        exit(1);
    }

    png_init_io(png_ptr, fp);
    png_read_info(png_ptr, info_ptr);

    int width = png_get_image_width(png_ptr, info_ptr);
    int height = png_get_image_height(png_ptr, info_ptr);

    png_bytep row = malloc(width);
    size_t bit_index = 0;
    memset(buffer, 0, size);  // limpiar buffer

    for (int y = 0; y < height; y++) {
        png_read_row(png_ptr, row, NULL);
        for (int x = 0; x < width; x++) {
            if (bit_index >= size * 8) break;
            uint8_t bit = (row[x] < 128) ? 1 : 0;
            buffer[bit_index / 8] |= (bit << (7 - (bit_index % 8)));
            bit_index++;
        }
    }

    free(row);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);
}

// Carga superbloque e inodos en memoria
void cargar_metadatos() {
    char path[256];

    snprintf(path, sizeof(path), "%s/block_0000.png", mount_folder);
    read_struct_from_png(path, (uint8_t*)&sb, sizeof(Superblock));

    snprintf(path, sizeof(path), "%s/block_0001.png", mount_folder);
    read_struct_from_png(path, (uint8_t*)inodos, sizeof(inodos));
}

// Lee contenido de un bloque de datos PNG a memoria
void read_data_block(int block_id, uint8_t *buffer, size_t size) {
    char path[256];
    snprintf(path, sizeof(path), "%s/block_%04d.png", mount_folder, block_id);
    read_struct_from_png(path, buffer, size);
}

// --------------------- FUSE: getattr -------------------------

static int fs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    // Buscar el archivo en la tabla de inodos
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(path + 1, inodos[i].name) == 0) {
            stbuf->st_mode = S_IFREG | 0644;
            stbuf->st_nlink = 1;
            stbuf->st_size = inodos[i].size;
            return 0;
        }
    }

    return -ENOENT;
}

// --------------------- FUSE: readdir -------------------------

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used)
            filler(buf, inodos[i].name, NULL, 0, 0);
    }

    return 0;
}

// --------------------- FUSE: read -------------------------

static int fs_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    (void) fi;

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(path + 1, inodos[i].name) == 0) {
            if ((size_t)offset >= inodos[i].size)
                return 0;

            size_t bytes_to_read = (offset + size > inodos[i].size) ?
                                   (inodos[i].size - offset) : size;

            size_t block_index = offset / BLOCK_SIZE;
            size_t block_offset = offset % BLOCK_SIZE;

            size_t total_read = 0;

            while (total_read < bytes_to_read && block_index < 8) {
                int block_id = inodos[i].block_pointers[block_index];
                if (block_id == 0) break;

                uint8_t temp_block[BLOCK_SIZE];
                read_data_block(block_id, temp_block, BLOCK_SIZE);

                size_t to_copy = BLOCK_SIZE - block_offset;
                if (to_copy > bytes_to_read - total_read)
                    to_copy = bytes_to_read - total_read;

                memcpy(buf + total_read, temp_block + block_offset, to_copy);

                total_read += to_copy;
                block_index++;
                block_offset = 0;
            }

            return total_read;
        }
    }

    return -ENOENT;
}

// --------------------- FUSE: operaciones -------------------------

static struct fuse_operations fs_oper = {
    .getattr = fs_getattr,
    .readdir = fs_readdir,
    .read = fs_read,
};

// --------------------- MAIN -------------------------

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <carpeta_fs> <punto_montaje>\n", argv[0]);
        return 1;
    }

    strncpy(mount_folder, argv[1], sizeof(mount_folder));
    cargar_metadatos();

    // Remover carpeta_fs del argv antes de pasarlo a FUSE
    for (int i = 1; i < argc - 1; i++)
        argv[i] = argv[i + 1];
    argc--;

    return fuse_main(argc, argv, &fs_oper, NULL);
}