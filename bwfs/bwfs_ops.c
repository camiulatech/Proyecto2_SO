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

// --------------------- GLOBALS -------------------------
Superblock sb;
Inode inodos[MAX_FILES];
char mount_folder[256];
uint8_t bitmap[MAX_BLOCKS];
uint16_t used_bytes[MAX_BLOCKS];

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

    // 🔧 Inicializar used_bytes[] de forma inferida desde los inodos
    memset(used_bytes, 0, sizeof(used_bytes));

    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used || inodos[i].is_dir) continue;

        int size_restante = inodos[i].size;

        for (int j = 0; j < 8 && size_restante > 0; j++) {
            int block_id = inodos[i].block_pointers[j];
            int offset = inodos[i].block_offsets[j];

            if (block_id == 0) continue;

            int bytes_en_bloque = (size_restante > BLOCK_SIZE - offset) ? (BLOCK_SIZE - offset) : size_restante;
            int nuevo_total = offset + bytes_en_bloque;

            if (nuevo_total > used_bytes[block_id]) {
                used_bytes[block_id] = nuevo_total;
            }

            size_restante -= bytes_en_bloque;
        }
    }
}

// --------------------- DECLARACIONES FUSE -------------------------
static int fs_getattr(const char *, struct stat *, struct fuse_file_info *);
static int fs_readdir(const char *, void *, fuse_fill_dir_t, off_t, struct fuse_file_info *, enum fuse_readdir_flags);
static int fs_create(const char *, mode_t, struct fuse_file_info *);
static int fs_read(const char *, char *, size_t, off_t, struct fuse_file_info *);
static int fs_write(const char *, const char *, size_t, off_t, struct fuse_file_info *);
static int fs_open(const char *, struct fuse_file_info *);
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
    .open = fs_open,
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

// Divide un path como "/a/b/c.txt" y recorre los inodos jerárquicamente
int buscar_inodo_por_ruta(const char *path) {
    if (strcmp(path, "/") == 0) return -1;  // raíz especial

    // Copia mutable del path
    char ruta_copia[256];
    strncpy(ruta_copia, path, sizeof(ruta_copia));
    ruta_copia[sizeof(ruta_copia)-1] = '\0';

    char *token;
    char *rest = ruta_copia;

    int actual = -1;  // -1 = raíz
    token = strtok(rest, "/");

    while (token != NULL) {
        int encontrado = 0;

        for (int i = 0; i < MAX_FILES; i++) {
            if (inodos[i].used && strcmp(inodos[i].name, token) == 0 && inodos[i].parent_inode == actual) {
                actual = i;
                encontrado = 1;
                break;
            }
        }

        if (!encontrado) {
            return -ENOENT;
        }

        token = strtok(NULL, "/");
    }

    return actual;  // Índice del inodo final
}

// --------------------- FUSE OPERATIONS -------------------------
static int fs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi;
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;  // rwxr-xr-x
        stbuf->st_nlink = 2;
        return 0;
    }

    int idx = buscar_inodo_por_ruta(path);
    if (idx >= 0) {
        if (inodos[idx].is_dir) {
            stbuf->st_mode = S_IFDIR | 0755;   // directorio: rwxr-xr-x
            stbuf->st_nlink = 2;
        } else {
            stbuf->st_mode = S_IFREG | 0644;   // archivo: rw-r--r--
            stbuf->st_nlink = 1;
            stbuf->st_size = inodos[idx].size;
        }
        return 0;
    }

    return -ENOENT;
}

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;

    // Obtener el índice del directorio actual (raíz = -1)
    int dir_idx = (strcmp(path, "/") == 0) ? -1 : buscar_inodo_por_ruta(path);
    if (dir_idx == -ENOENT || (dir_idx >= 0 && !inodos[dir_idx].is_dir))
    return -ENOENT;

    // Agrega "." y ".."
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    // Recorre todos los inodos y lista los que tengan como padre al actual
    for (int i = 0; i < MAX_FILES; i++) {
    if (inodos[i].used && inodos[i].parent_inode == dir_idx) {
    // Debug opcional
    printf("[readdir] Listando: %s (inode %d, padre %d)\n", inodos[i].name, i, inodos[i].parent_inode);

    filler(buf, inodos[i].name, NULL, 0, 0);
    }
    }

    return 0;
}

static int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void)mode; (void)fi;

    char copia1[256], copia2[256];
    strncpy(copia1, path, sizeof(copia1));
    strncpy(copia2, path, sizeof(copia2));
    copia1[sizeof(copia1) - 1] = '\0';
    copia2[sizeof(copia2) - 1] = '\0';

    char *nombre = basename(copia1);
    char *padre = dirname(copia2);

    int padre_idx = buscar_inodo_por_ruta(padre);
    if (padre_idx == -ENOENT || (padre_idx >= 0 && !inodos[padre_idx].is_dir))
        return -ENOENT;

    // Verificar si ya existe en ese directorio
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(inodos[i].name, nombre) == 0 && inodos[i].parent_inode == padre_idx)
            return -EEXIST;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used) {
            inodos[i].used = 1;
            inodos[i].is_dir = 0;
            inodos[i].parent_inode = padre_idx;
            strncpy(inodos[i].name, nombre, sizeof(inodos[i].name) - 1);
            inodos[i].size = 0;
            memset(inodos[i].block_offsets, 0, sizeof(inodos[i].block_offsets));

            char pathi[256];
            snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
            write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

            return 0;
        }
    }

    return -ENOSPC;
}

int find_block_with_space(size_t size) {
    for (int i = 3; i < sb.total_blocks; i++) {
        if (bitmap[i] == 1 && (BLOCK_SIZE - used_bytes[i]) >= size) {
            return i;
        }
    }
    return -1;
}

int allocate_new_block() {
    for (int i = 3; i < sb.total_blocks; i++) {
        if (bitmap[i] == 0) {
            bitmap[i] = 1;
            used_bytes[i] = 0;
            return i;
        }
    }
    return -1;
}

static int fs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi;

    int inodo_idx = buscar_inodo_por_ruta(path);
    if (inodo_idx < 0 || inodos[inodo_idx].is_dir) return -ENOENT;

    Inode *inodo = &inodos[inodo_idx];

    // Si se sobreescribe desde cero, limpiar bloques usados por este archivo
    if (offset == 0) {
        for (int j = 0; j < 8; j++) {
            int b = inodo->block_pointers[j];
            if (b > 0) {
                // No reducir used_bytes[b], otros archivos pueden usarlo
                // Solo limpiar punteros de este inodo
                inodo->block_pointers[j] = 0;
                inodo->block_offsets[j] = 0;
            }
        }
        inodo->size = 0;
    }

    size_t total_written = 0;
    for (int i = 0; i < 8 && total_written < size; i++) {
        int espacio_necesario = size - total_written;

        // Buscar bloque con espacio suficiente
        int elegido = -1;
        for (int b = 3; b < sb.total_blocks; b++) {
            if (BLOCK_SIZE - used_bytes[b] >= espacio_necesario) {
                elegido = b;
                break;
            }
        }

        // Si no se encontró, buscar bloque libre
        if (elegido == -1) {
            for (int b = 3; b < sb.total_blocks; b++) {
                if (bitmap[b] == 0) {
                    elegido = b;
                    bitmap[b] = 1;
                    sb.used_blocks++;
                    break;
                }
            }
        }

        if (elegido == -1) return -ENOSPC;

        // Leer contenido actual del bloque
        uint8_t tmp[BLOCK_SIZE];
        char pathb[256];
        snprintf(pathb, sizeof(pathb), "%s/block_%04d.png", mount_folder, elegido);
        read_struct_from_png(pathb, tmp, BLOCK_SIZE);

        int offset_bloque = used_bytes[elegido];
        int bytes_a_copiar = BLOCK_SIZE - offset_bloque;
        if (bytes_a_copiar > espacio_necesario) bytes_a_copiar = espacio_necesario;

        memcpy(tmp + offset_bloque, buf + total_written, bytes_a_copiar);
        write_struct_to_png(pathb, tmp, BLOCK_SIZE);

        // Registrar en inodo
        inodo->block_pointers[i] = elegido;
        inodo->block_offsets[i] = offset_bloque;
        used_bytes[elegido] += bytes_a_copiar;

        total_written += bytes_a_copiar;
    }

    if ((size_t)(offset + total_written) > (size_t)inodo->size)
        inodo->size = offset + total_written;

    // Persistir cambios
    char pathi[256], pathbm[256];
    snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
    snprintf(pathbm, sizeof(pathbm), "%s/block_0002.png", mount_folder);
    write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));
    write_struct_to_png(pathbm, bitmap, sizeof(bitmap));

    return total_written;
}

static int fs_read(const char *path, char *buf, size_t size, off_t offset,
    struct fuse_file_info *fi) {
    (void)fi;

    int idx = buscar_inodo_por_ruta(path);
    if (idx < 0 || inodos[idx].is_dir) return -ENOENT;

    if ((size_t)offset >= inodos[idx].size)
        return 0;

    size_t bytes_to_read = (offset + size > inodos[idx].size)
        ? (inodos[idx].size - offset) : size;

    size_t total_read = 0;
    size_t logical_offset = offset;

    for (int i = 0; i < 8 && total_read < bytes_to_read; i++) {
        int block_id = inodos[idx].block_pointers[i];
        int block_offset = inodos[idx].block_offsets[i];
        if (block_id == 0) break;

        if (logical_offset >= BLOCK_SIZE) {
            logical_offset -= BLOCK_SIZE;
            continue;
        }

        uint8_t temp_block[BLOCK_SIZE];
        read_data_block(block_id, temp_block, BLOCK_SIZE);

        size_t read_offset = block_offset + logical_offset;
        size_t available = BLOCK_SIZE - read_offset;
        size_t to_copy = bytes_to_read - total_read;
        if (to_copy > available)
            to_copy = available;

        memcpy(buf + total_read, temp_block + read_offset, to_copy);
        total_read += to_copy;
        logical_offset = 0;
    }

    return total_read;
}

static int fs_open(const char *path, struct fuse_file_info *fi) {
    (void)fi;

    int idx = buscar_inodo_por_ruta(path);
    if (idx >= 0 && !inodos[idx].is_dir) {
        return 0;  // OK: es archivo regular
    }

    return -ENOENT;
}

static int fs_unlink(const char *path) {
    int idx = buscar_inodo_por_ruta(path);
    if (idx < 0)
        return -ENOENT;

    if (inodos[idx].is_dir)
        return -EISDIR;  // No se puede eliminar directorios con unlink

    // Liberar bloques de datos
    for (int j = 0; j < 8; j++) {
        int b = inodos[idx].block_pointers[j];
        used_bytes[b] = 0;
        if (b > 0) bitmap[b] = 0;
    }

    // Limpiar inodo
    inodos[idx].used = 0;
    memset(inodos[idx].name, 0, sizeof(inodos[idx].name));
    memset(inodos[idx].block_pointers, 0, sizeof(inodos[idx].block_pointers));
    inodos[idx].size = 0;
    inodos[idx].is_dir = 0;
    inodos[idx].parent_inode = -1;

    // Guardar cambios
    char pathi[256], pathbm[256];
    snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
    snprintf(pathbm, sizeof(pathbm), "%s/block_0002.png", mount_folder);
    write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));
    write_struct_to_png(pathbm, bitmap, sizeof(bitmap));

    return 0;
}

static int fs_mkdir(const char *path, mode_t mode) {
    (void)mode;

    char copia1[256], copia2[256];
    strncpy(copia1, path, sizeof(copia1));
    strncpy(copia2, path, sizeof(copia2));
    copia1[sizeof(copia1)-1] = '\0';
    copia2[sizeof(copia2)-1] = '\0';
    
    char *nombre = basename(copia1);
    char *padre = dirname(copia2);

    int padre_idx = buscar_inodo_por_ruta(padre);
    if (padre_idx == -ENOENT || (padre_idx >= 0 && !inodos[padre_idx].is_dir)) {
        return -ENOENT;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(inodos[i].name, nombre) == 0 && inodos[i].parent_inode == padre_idx)
            return -EEXIST;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used) {
            inodos[i].used = 1;
            inodos[i].is_dir = 1;
            inodos[i].parent_inode = padre_idx;  // 👈 Enlace al padre
            strncpy(inodos[i].name, nombre, sizeof(inodos[i].name) - 1);
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

static int fs_rmdir(const char *path) {
    int idx = buscar_inodo_por_ruta(path);
    if (idx < 0)
        return -ENOENT;

    if (!inodos[idx].is_dir)
        return -ENOTDIR;

    // Verificar si el directorio está vacío
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && inodos[i].parent_inode == idx) {
            return -ENOTEMPTY;
        }
    }

    // Limpiar inodo
    inodos[idx].used = 0;
    memset(&inodos[idx], 0, sizeof(Inode));
    inodos[idx].parent_inode = -1;

    // Guardar cambios
    char pathi[256];
    snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
    write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

    return 0;
}

static int fs_opendir(const char *path, struct fuse_file_info *fi) {
    if (strcmp(path, "/") == 0) {
        return 0;  // raíz especial
    }

    int idx = buscar_inodo_por_ruta(path);
    if (idx >= 0 && inodos[idx].is_dir) {
        return 0;
    }

    return -ENOENT;
}


static int fs_rename(const char *from, const char *to, unsigned int flags) {
    (void)flags;

    // Obtener inodo original
    int origen_idx = buscar_inodo_por_ruta(from);
    if (origen_idx < 0)
        return -ENOENT;

    // Preparar nuevas partes
    char copia1[256], copia2[256];
    strncpy(copia1, to, sizeof(copia1));
    strncpy(copia2, to, sizeof(copia2));
    copia1[sizeof(copia1)-1] = '\0';
    copia2[sizeof(copia2)-1] = '\0';

    char *nuevo_nombre = basename(copia1);
    char *nuevo_padre_path = dirname(copia2);
    int nuevo_padre_idx = buscar_inodo_por_ruta(nuevo_padre_path);

    if (nuevo_padre_idx < -1 || (nuevo_padre_idx >= 0 && !inodos[nuevo_padre_idx].is_dir))
        return -ENOENT;

    // Verificar que no exista ya un archivo con el mismo nombre en el nuevo lugar
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used &&
            strcmp(inodos[i].name, nuevo_nombre) == 0 &&
            inodos[i].parent_inode == nuevo_padre_idx)
            return -EEXIST;
    }

    // Actualizar el inodo
    strncpy(inodos[origen_idx].name, nuevo_nombre, sizeof(inodos[origen_idx].name) - 1);
    inodos[origen_idx].parent_inode = nuevo_padre_idx;

    // Guardar cambios
    char pathi[256];
    snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
    write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

    return 0;
}

static int fs_statfs(const char *path, struct statvfs *st) {
    (void)path;

    st->f_bsize = BLOCK_SIZE;
    st->f_frsize = BLOCK_SIZE;
    st->f_blocks = sb.total_blocks;

    // Contar bloques libres
    int libres = 0;
    for (int i = 0; i < sb.total_blocks; i++) {
        if (bitmap[i] == 0)
            libres++;
    }

    st->f_bfree = libres;
    st->f_bavail = libres;

    // Cantidad de archivos posibles
    st->f_files = MAX_FILES;

    // Contar inodos libres
    int inodos_libres = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used)
            inodos_libres++;
    }

    st->f_ffree = inodos_libres;
    st->f_favail = inodos_libres;

    st->f_namemax = 63;  // límite real de tu campo name[64]

    return 0;
}

static int fs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi) {
    (void)isdatasync;
    (void)fi;

    int idx = buscar_inodo_por_ruta(path);
    if (idx < 0)
        return -ENOENT;

    printf("🟢 fs_fsync ejecutado para '%s'\n", path);

    char path_sb[256], path_inodes[256], path_bitmap[256];
    snprintf(path_sb, sizeof(path_sb), "%s/block_0000.png", mount_folder);
    snprintf(path_inodes, sizeof(path_inodes), "%s/block_0001.png", mount_folder);
    snprintf(path_bitmap, sizeof(path_bitmap), "%s/block_0002.png", mount_folder);

    write_struct_to_png(path_sb, (const uint8_t*)&sb, sizeof(sb));
    write_struct_to_png(path_inodes, (const uint8_t*)inodos, sizeof(inodos));
    write_struct_to_png(path_bitmap, bitmap, sizeof(bitmap));

    return 0;
}

static int fs_access(const char *path, int mask) {
    printf("[access] Verificando acceso a: %s\n", path);

    if (strcmp(path, "/") == 0) {
        printf("[access] Es el directorio raíz.\n");
        return 0;
    }

    int idx = buscar_inodo_por_ruta(path);
    if (idx >= 0) {
        printf("[access] Archivo o directorio '%s' encontrado (inode %d).\n", path, idx);

        // Opcional: aquí podrías usar mask para validar permisos específicos
        // Pero como no manejamos usuarios ni permisos reales, devolvemos 0 (OK)
        return 0;
    }

    printf("[access] '%s' NO encontrado.\n", path);
    return -ENOENT;
}

static int fs_flush(const char *path, struct fuse_file_info *fi) {
    (void)fi;

    int idx = buscar_inodo_por_ruta(path);
    if (idx < 0)
        return -ENOENT;

    printf("[flush] Se cerró el descriptor para %s (inode %d)\n", path, idx);

    // No se hace nada porque write y fsync ya guardan los cambios
    return 0;
}

static off_t fs_lseek(const char *path, off_t off, int whence, struct fuse_file_info *fi) {
    (void)fi;

    int idx = buscar_inodo_por_ruta(path);
    if (idx < 0 || inodos[idx].is_dir)
        return -ENOENT;

    off_t result = 0;

    switch (whence) {
        case SEEK_SET:
            result = off;
            break;
        case SEEK_CUR:
            result = off;  // como no usamos fi->fh como offset, lo tratamos como relativo
            break;
        case SEEK_END:
            result = inodos[idx].size + off;
            break;
        default:
            return -EINVAL;
    }

    if (result < 0 || result > inodos[idx].size)
        return -EINVAL;

    return result;
}
