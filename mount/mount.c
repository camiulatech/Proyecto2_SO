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
static uint8_t bitmap[MAX_BLOCKS];

// --------------------- FUNCIONES AUXILIARES -------------------------
void read_struct_from_png(const char *filename, uint8_t *buffer, size_t size);
void write_struct_to_png(const char *filename, const uint8_t *data, size_t size);
void read_data_block(int block_id, uint8_t *buffer, size_t size);

void cargar_metadatos() {
    char path[256];
    snprintf(path, sizeof(path), "%s/block_0000.png", mount_folder);
    read_struct_from_png(path, (uint8_t*)&sb, sizeof(Superblock));

    snprintf(path, sizeof(path), "%s/block_0001.png", mount_folder);
    read_struct_from_png(path, (uint8_t*)inodos, sizeof(inodos));

    snprintf(path, sizeof(path), "%s/block_0002.png", mount_folder);
    read_struct_from_png(path, bitmap, sizeof(bitmap));
}

void read_struct_from_png(const char *filename, uint8_t *buffer, size_t size) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!png_ptr || !info_ptr) exit(1);
    if (setjmp(png_jmpbuf(png_ptr))) exit(1);

    png_init_io(png_ptr, fp);
    png_read_info(png_ptr, info_ptr);

    png_bytep row = malloc(1000);
    memset(buffer, 0, size);
    size_t bit_index = 0;
    for (int y = 0; y < 1000; y++) {
        png_read_row(png_ptr, row, NULL);
        for (int x = 0; x < 1000 && bit_index < size * 8; x++) {
            uint8_t bit = (row[x] < 128) ? 1 : 0;
            buffer[bit_index / 8] |= (bit << (7 - (bit_index % 8)));
            bit_index++;
        }
    }
    free(row);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);
}

void write_struct_to_png(const char *filename, const uint8_t *data, size_t size) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!png_ptr || !info_ptr) exit(1);
    if (setjmp(png_jmpbuf(png_ptr))) exit(1);

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, 1000, 1000, 8, PNG_COLOR_TYPE_GRAY,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);

    png_bytep row = malloc(1000);
    size_t bit_index = 0;
    for (int y = 0; y < 1000; y++) {
        for (int x = 0; x < 1000; x++) {
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

void read_data_block(int block_id, uint8_t *buffer, size_t size) {
    char path[256];
    snprintf(path, sizeof(path), "%s/block_%04d.png", mount_folder, block_id);
    read_struct_from_png(path, buffer, size);
}

// --------------------- FUSE OPERATIONS -------------------------
static int fs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi;
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

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

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void)offset; (void)fi; (void)flags;

    if (strcmp(path, "/") != 0) return -ENOENT;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used)
            filler(buf, inodos[i].name, NULL, 0, 0);
    }

    return 0;
}

static int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void)mode; (void)fi;
    const char *name = path + 1;

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(inodos[i].name, name) == 0)
            return -EEXIST;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used) {
            inodos[i].used = 1;
            strncpy(inodos[i].name, name, sizeof(inodos[i].name) - 1);
            inodos[i].size = 0;
            memset(inodos[i].block_pointers, 0, sizeof(inodos[i].block_pointers));

            char pathi[256];
            snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
            write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));
            return 0;
        }
    }

    return -ENOSPC;
}

static int fs_write(const char *path, const char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi) {
    (void)fi;
    const char *name = path + 1;

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(inodos[i].name, name) == 0) {
            size_t total_written = 0;
            size_t block_index = offset / BLOCK_SIZE;
            size_t block_offset = offset % BLOCK_SIZE;

            while (total_written < size && block_index < 8) {
                int block_id = inodos[i].block_pointers[block_index];
                if (block_id == 0) {
                    for (int b = 3; b < sb.total_blocks; b++) {
                        if (bitmap[b] == 0) {
                            bitmap[b] = 1;
                            inodos[i].block_pointers[block_index] = b;
                            block_id = b;
                            break;
                        }
                    }
                    if (block_id == 0) return -ENOSPC;
                }

                uint8_t temp[BLOCK_SIZE] = {0};
                read_data_block(block_id, temp, BLOCK_SIZE);

                size_t to_copy = BLOCK_SIZE - block_offset;
                if (to_copy > size - total_written) to_copy = size - total_written;

                memcpy(temp + block_offset, buf + total_written, to_copy);

                char pathb[256];
                snprintf(pathb, sizeof(pathb), "%s/block_%04d.png", mount_folder, block_id);
                write_struct_to_png(pathb, temp, BLOCK_SIZE);

                total_written += to_copy;
                block_index++;
                block_offset = 0;
            }

            if ((size_t)(offset + total_written) > (size_t)inodos[i].size)
                inodos[i].size = offset + total_written;

            char pathi[256];
            snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
            write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

            char pathbm[256];
            snprintf(pathbm, sizeof(pathbm), "%s/block_0002.png", mount_folder);
            write_struct_to_png(pathbm, bitmap, sizeof(bitmap));

            return total_written;
        }
    }

    return -ENOENT;
}

static int fs_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    (void)fi;

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(path + 1, inodos[i].name) == 0) {
            if ((size_t)offset >= inodos[i].size)
                return 0;

            size_t bytes_to_read = (offset + size > inodos[i].size)
                ? (inodos[i].size - offset) : size;

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

static struct fuse_operations fs_oper = {
    .getattr = fs_getattr,
    .readdir = fs_readdir,
    .read = fs_read,
    .write = fs_write,
    .create = fs_create
};

// --------------------- MAIN -------------------------
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <carpeta_fs> <punto_montaje>\n", argv[0]);
        return 1;
    }

    realpath(argv[1], mount_folder);
    cargar_metadatos();

    char *fuse_args[] = { argv[0], "-f", "-d", argv[2] };
    int argc_fuse = sizeof(fuse_args) / sizeof(fuse_args[0]);
    return fuse_main(argc_fuse, fuse_args, &fs_oper, NULL);
}