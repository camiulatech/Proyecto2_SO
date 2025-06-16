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
Superblock sb;
Inode inodos[MAX_FILES];
char mount_folder[256];
uint8_t bitmap[MAX_BLOCKS];

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

// --------------------- DECLARACIONES FUSE -------------------------
static int fs_getattr(const char *, struct stat *, struct fuse_file_info *);
static int fs_readdir(const char *, void *, fuse_fill_dir_t, off_t, struct fuse_file_info *, enum fuse_readdir_flags);
static int fs_create(const char *, mode_t, struct fuse_file_info *);
static int fs_read(const char *, char *, size_t, off_t, struct fuse_file_info *);
static int fs_write(const char *, const char *, size_t, off_t, struct fuse_file_info *);
static int fs_unlink(const char *);
static int fs_mkdir(const char *, mode_t);
static int fs_rmdir(const char *);
static int fs_opendir(const char *, struct fuse_file_info *);
static int fs_rename(const char *, const char *, unsigned int);
static int fs_statfs(const char *, struct statvfs *);
static int fs_fsync(const char *, int, struct fuse_file_info *);
static int fs_access(const char *, int);
static int fs_flush(const char *, struct fuse_file_info *);
static off_t fs_lseek(const char *, off_t, int, struct fuse_file_info *);

// --------------------- TABLA DE OPERACIONES FUSE -------------------------
struct fuse_operations fs_oper = {
    .getattr = fs_getattr,
    .readdir = fs_readdir,
    .read = fs_read,
    .write = fs_write,
    .create = fs_create,
    .unlink = fs_unlink,
    .mkdir = fs_mkdir,
    .rmdir = fs_rmdir,
    .opendir = fs_opendir,
    .rename = fs_rename,
    .statfs = fs_statfs,
    .fsync = fs_fsync,
    .access = fs_access,
    .flush = fs_flush,
    .lseek = fs_lseek,
};

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
        for (int i = 0; i < MAX_FILES; i++) {
            if (inodos[i].used && strcmp(path + 1, inodos[i].name) == 0) {
                stbuf->st_mode = (inodos[i].is_dir ? S_IFDIR : S_IFREG) | 0755;
                stbuf->st_nlink = 1;
                stbuf->st_size = inodos[i].size;
                return 0;
            }
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
            inodos[i].is_dir = 0;  // 👈 Es un archivo regular
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

static int fs_unlink(const char *path) {
    const char *name = path + 1;

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(inodos[i].name, name) == 0) {
            if (inodos[i].is_dir) {
                return -EISDIR;
            }

            for (int j = 0; j < 8; j++) {
                int b = inodos[i].block_pointers[j];
                if (b > 0) bitmap[b] = 0;
            }

            inodos[i].used = 0;
            memset(inodos[i].name, 0, sizeof(inodos[i].name));
            memset(inodos[i].block_pointers, 0, sizeof(inodos[i].block_pointers));
            inodos[i].size = 0;
            inodos[i].is_dir = 0;

            char pathi[256], pathbm[256];
            snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
            snprintf(pathbm, sizeof(pathbm), "%s/block_0002.png", mount_folder);
            write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));
            write_struct_to_png(pathbm, bitmap, sizeof(bitmap));

            return 0;
        }
    }

    return -ENOENT;
}

static int fs_mkdir(const char *path, mode_t mode) {
    (void)mode;
    const char *name = path + 1;

    // Verificar si ya existe
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(inodos[i].name, name) == 0)
            return -EEXIST;
    }

    // Buscar inodo libre
    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used) {
            inodos[i].used = 1;
            inodos[i].is_dir = 1;  // 👈 IMPORTANTE: marcar como directorio
            strncpy(inodos[i].name, name, sizeof(inodos[i].name) - 1);
            inodos[i].size = 0;
            memset(inodos[i].block_pointers, 0, sizeof(inodos[i].block_pointers));

            // Guardar inodos
            char pathi[256];
            snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
            write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

            return 0;
        }
    }

    return -ENOSPC;
}

static int fs_rmdir(const char *path) {
    const char *name = path + 1;

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(inodos[i].name, name) == 0) {
            if (!inodos[i].is_dir) {
                return -ENOTDIR;  // No es un directorio
            }

            // Verificar si está vacío
            for (int j = 0; j < MAX_FILES; j++) {
                if (inodos[j].used && strcmp(inodos[j].name, name) != 0) {
                    // Esto asume que no hay jerarquía. Si hubiera, se verificaría con path completo.
                    // Si tuvieras subdirectorios deberías verificar si "inodos[j].name" empieza con "name/"
                    return -ENOTEMPTY;
                }
            }

            // Borrar el directorio
            inodos[i].used = 0;
            memset(&inodos[i], 0, sizeof(Inode));

            // Guardar inodos
            char pathi[256];
            snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
            write_struct_to_png(pathi, (uint8_t *)inodos, sizeof(inodos));

            return 0;
        }
    }

    return -ENOENT;
}

static int fs_opendir(const char *path, struct fuse_file_info *fi) {
    // Solo permitimos abrir el directorio raíz por ahora
    if (strcmp(path, "/") == 0) {
        return 0;
    }

    // Verificamos si el path es un directorio válido en los inodos
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && inodos[i].is_dir && strcmp(path + 1, inodos[i].name) == 0) {
            return 0;
        }
    }

    return -ENOENT;
}

static int fs_rename(const char *from, const char *to, unsigned int flags) {
    (void) flags;

    const char *old_name = from + 1;
    const char *new_name = to + 1;

    // Verificar si ya existe un archivo con el nuevo nombre
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(inodos[i].name, new_name) == 0)
            return -EEXIST;
    }

    // Buscar el archivo original
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(inodos[i].name, old_name) == 0) {
            strncpy(inodos[i].name, new_name, sizeof(inodos[i].name) - 1);

            // Guardar cambios
            char pathi[256];
            snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
            write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

            return 0;
        }
    }

    return -ENOENT;
}

static int fs_statfs(const char *path, struct statvfs *st) {
    (void)path;
    st->f_bsize = BLOCK_SIZE;
    st->f_frsize = BLOCK_SIZE;
    st->f_blocks = sb.total_blocks;

    // contar bloques libres
    int libres = 0;
    for (int i = 0; i < sb.total_blocks; i++) {
        if (bitmap[i] == 0)
            libres++;
    }
    st->f_bfree = libres;
    st->f_bavail = libres;

    st->f_files = MAX_FILES;

    // ✅ calcular nodos libres
    int inodos_libres = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used)
            inodos_libres++;
    }
    st->f_ffree = inodos_libres;
    st->f_favail = inodos_libres;

    st->f_namemax = 255;
    return 0;
}

static int fs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi) {
    (void) isdatasync;
    (void) fi;

    const char *file_name = path + 1;

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(inodos[i].name, file_name) == 0) {
            printf("🟢 fs_fsync ejecutado para '%s'\n", file_name);

            // Forzar escritura de los metadatos principales
            char path_sb[256], path_inodes[256], path_bitmap[256];

            snprintf(path_sb, sizeof(path_sb), "%s/block_0000.png", mount_folder);
            snprintf(path_inodes, sizeof(path_inodes), "%s/block_0001.png", mount_folder);
            snprintf(path_bitmap, sizeof(path_bitmap), "%s/block_0002.png", mount_folder);

            write_struct_to_png(path_sb, (const uint8_t *) &sb, sizeof(sb));
            write_struct_to_png(path_inodes, (const uint8_t *) inodos, sizeof(inodos));
            write_struct_to_png(path_bitmap, bitmap, sizeof(bitmap));

            return 0;
        }
    }

    printf("🔴 fs_fsync: archivo '%s' no encontrado\n", file_name);
    return -ENOENT;
}

static int fs_access(const char *path, int mask) {
    (void) mask;

    printf("[access] Verificando acceso a: %s\n", path);

    // Verificar si es el directorio raíz
    if (strcmp(path, "/") == 0) {
        printf("[access] Es el directorio raíz.\n");
        return 0;
    }

    const char *name = path + 1;

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(inodos[i].name, name) == 0) {
            printf("[access] Archivo o directorio '%s' encontrado (inode %d).\n", name, i);
            return 0;
        }
    }

    printf("[access] '%s' NO encontrado.\n", name);
    return -ENOENT;
}

static int fs_flush(const char *path, struct fuse_file_info *fi) {
    (void)fi;

    const char *name = path + 1;
    printf("[flush] Se cerró el descriptor para %s\n", name);

    // No realizamos acciones adicionales porque ya se sincroniza con write/fsync
    return 0;
}

static off_t fs_lseek(const char *path, off_t off, int whence, struct fuse_file_info *fi) {
    (void)fi;

    const char *name = path + 1;
    printf("lseek(): path=%s, offset=%ld, whence=%d\n", name, off, whence);

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(inodos[i].name, name) == 0) {
            off_t result = 0;

            switch (whence) {
                case SEEK_SET:
                    result = off;
                    break;
                case SEEK_CUR:
                    result = fi->fh + off;
                    break;
                case SEEK_END:
                    result = inodos[i].size + off;
                    break;
                default:
                    return -EINVAL;
            }

            if (result < 0 || result > inodos[i].size)
                return -EINVAL;

            return result;
        }
    }

    return -ENOENT;
}
