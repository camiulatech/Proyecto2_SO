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
#include <stdbool.h>
#include "../include/bwfs.h"

// --------------------- GLOBALS -------------------------
Superblock sb;
Inode inodos[MAX_FILES];
char mount_folder[256];
uint8_t bitmap[MAX_BLOCKS];
uint16_t used_bytes[MAX_BLOCKS];

// Máximo de regiones por bloque compartido
#define MAX_REGIONES MAX_BLOCKS_PER_FILE

typedef struct {
int inodo_id; // Índice del inodo que usa esta región
int offset; // Desplazamiento dentro del bloque
int size; // Tamaño ocupado
} UsedRegion;

// regions_blocks[bloque][i]
UsedRegion regions_blocks[MAX_BLOCKS][MAX_REGIONES];

// --------------------- FUNCIONES AUXILIARES -------------------------
void load_metadata() {
    char path[1000];
    // Leer superbloque, inodos y bitmap desde PNGs
    snprintf(path, sizeof(path), "%s/block_0000.png", mount_folder);
    read_struct_from_png(path, (uint8_t*)&sb, sizeof(Superblock));

    snprintf(path, sizeof(path), "%s/block_0001.png", mount_folder);
    read_struct_from_png(path, (uint8_t*)inodos, sizeof(inodos));

    snprintf(path, sizeof(path), "%s/block_0002.png", mount_folder);
    read_struct_from_png(path, bitmap, sizeof(bitmap));

    // Inicializar used_bytes[]
    memset(used_bytes, 0, sizeof(used_bytes));

    // Inicializar regiones por bloque
    memset(regions_blocks, 0, sizeof(regions_blocks));

    // Recorremos los inodos y reconstruimos regiones y uso de bytes
    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used || inodos[i].is_dir) continue;

        int size_remaining = inodos[i].size;

        for (int j = 0; j < MAX_BLOCKS_PER_FILE && size_remaining > 0; j++) {
            int block_id = inodos[i].block_pointers[j];
            int offset   = inodos[i].block_offsets[j];

            if (block_id == 0) continue;

            int bytes_in_block = (size_remaining > BLOCK_SIZE - offset)
                                ? (BLOCK_SIZE - offset)
                                : size_remaining;

            // Actualizar used_bytes
            int new_total = offset + bytes_in_block;
            if (new_total > used_bytes[block_id]) {
                used_bytes[block_id] = new_total;
            }

            // Registrar la región en regions_blocks
            for (int r = 0; r < MAX_REGIONES; r++) {
                if (regions_blocks[block_id][r].size == 0) {
                    regions_blocks[block_id][r].inodo_id = i;
                    regions_blocks[block_id][r].offset   = offset;
                    regions_blocks[block_id][r].size     = bytes_in_block;
                    break;
                }
            }

            size_remaining -= bytes_in_block;
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
static int fs_truncate(const char *path, off_t size);
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
    .truncate = fs_truncate,
    .opendir = fs_opendir,
    .rename = fs_rename,
    .statfs = fs_statfs,
    .fsync = fs_fsync,
    .access = fs_access,
    .flush = fs_flush,
    .lseek = fs_lseek,
    .open = fs_open,
};

void read_struct_from_png(const char *filename, uint8_t *buffer, size_t buffer_size_bytes) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Error abriendo PNG para lectura");
        exit(1);
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fprintf(stderr, "Error creando png_structp para lectura\n");
        fclose(fp);
        exit(1);
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        fprintf(stderr, "Error creando png_infop para lectura\n");
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(fp);
        exit(1);
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "Error leyendo PNG\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        exit(1);
    }

    png_init_io(png_ptr, fp);
    png_read_info(png_ptr, info_ptr);

    png_uint_32 width, height;
    int bit_depth, color_type;
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
                 NULL, NULL, NULL);

    if (width != 1000 || height != 1000 ||
        bit_depth != 1 || color_type != PNG_COLOR_TYPE_GRAY) {
        fprintf(stderr, "Error: La imagen PNG no es de %dx%d, 1 bit en escala de grises. "
                        "Dimensiones: %ux%u, Profundidad: %d, Tipo Color: %d\n",
                        1000, 1000, width, height, bit_depth, color_type);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        exit(1);
    }

    size_t row_bytes = (1000 + 7) / 8;
    png_bytep row = (png_bytep) malloc(row_bytes);
    if (!row) {
        fprintf(stderr, "Error: No se pudo asignar memoria para la fila.\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        exit(1);
    }

    memset(buffer, 0, buffer_size_bytes);
    size_t output_bit_index = 0;

    for (int y = 0; y < 1000; y++) {
        png_read_row(png_ptr, row, NULL);

        for (int i = 0; i < row_bytes; i++) {
            uint8_t current_byte_from_png = row[i];

            for (int bit_in_byte = 0; bit_in_byte < 8; bit_in_byte++) {
                if (output_bit_index >= buffer_size_bytes * 8) {
                    break;
                }

                uint8_t pixel_bit_from_png = (current_byte_from_png >> (7 - bit_in_byte)) & 1;

                // *****************************************************************
                // CAMBIO CLAVE AQUÍ: Invertir la lógica de mapeo
                // write: 1 (input_data) -> 0 (PNG_bit) (Blanco)
                //        0 (input_data) -> 1 (PNG_bit) (Negro)
                //
                // read:  0 (PNG_bit) -> 1 (output_buffer) (Blanco)
                //        1 (PNG_bit) -> 0 (output_buffer) (Negro)
                //
                // Esto se logra con un simple NOT bit a bit, o (1 - pixel_bit_from_png)
                uint8_t mapped_output_bit = (pixel_bit_from_png == 0) ? 1 : 0; // Si PNG es BLANCO (0), output es 1. Si PNG es NEGRO (1), output es 0.

                // O de forma más concisa:
                // uint8_t mapped_output_bit = !pixel_bit_from_png;

                // Almacenar el bit en el buffer de salida
                if (mapped_output_bit == 1) { // Si el bit de salida mapeado es 1, establecerlo en el buffer
                    buffer[output_bit_index / 8] |= (1 << (7 - (output_bit_index % 8)));
                }
                // Si mapped_output_bit es 0, no hacemos nada, ya que el buffer se inicializó a 0.
                // *****************************************************************

                output_bit_index++;
            }
            if (output_bit_index >= buffer_size_bytes * 8) break;
        }
    }

    free(row);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);
}

void write_struct_to_png(const char *filename, const uint8_t *data, size_t data_size_bytes) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Error abriendo archivo para escritura");
        exit(1);
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fprintf(stderr, "Error creando png_structp para escritura\n");
        fclose(fp);
        exit(1);
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        fprintf(stderr, "Error creando png_infop para escritura\n");
        png_destroy_write_struct(&png_ptr, NULL); // El segundo parámetro debe ser NULL si info_ptr no se creó
        fclose(fp);
        exit(1);
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "Error escribiendo PNG\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        exit(1);
    }

    png_init_io(png_ptr, fp);

    png_set_IHDR(png_ptr, info_ptr, 1000, 1000,
                 1, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_set_compression_level(png_ptr, 0);

    png_write_info(png_ptr, info_ptr);

    size_t row_bytes = (1000 + 7) / 8;
    png_bytep row = (png_bytep)malloc(row_bytes);
    if (!row) {
        fprintf(stderr, "Error: No se pudo asignar memoria para la fila.\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        exit(1);
    }

    size_t data_bit_index = 0;

    for (int y = 0; y < 1000; y++) {
        // *****************************************************************
        // CAMBIO CLAVE AQUÍ: Inicializar la fila a NEGRO (todos los bits a 1)
        memset(row, 0xFF, row_bytes); // Rellenar con unos (para que los píxeles sean NEGROS por defecto)
        // *****************************************************************

        for (int x = 0; x < 1000; x++) {
            if (data_bit_index < data_size_bytes * 8) {
                uint8_t byte_from_data = data[data_bit_index / 8];
                uint8_t input_bit = (byte_from_data >> (7 - (data_bit_index % 8))) & 1;

                // *****************************************************************
                // CAMBIO DE LÓGICA AQUÍ:
                // Si el bit de entrada es 1, queremos BLANCO (PNG bit 0).
                // Si el bit de entrada es 0, queremos NEGRO (PNG bit 1, que ya está por defecto).
                if (input_bit == 1) { // Si el bit de entrada es 1 (queremos BLANCO)
                    // Limpiar el bit correspondiente a 0 en el byte de la fila del PNG
                    // Esto cambia el bit de 1 (negro) a 0 (blanco)
                    row[x / 8] &= ~(1 << (7 - (x % 8)));
                }
                // Si input_bit es 0, el píxel permanece 1 (negro), que es el valor predeterminado.
                // *****************************************************************
            } else {
                // Si ya no hay más 'data' de entrada, los píxeles restantes se mantendrán
                // en NEGRO (bits 1) gracias al memset inicial con 0xFF.
            }
            data_bit_index++;
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
        printf("[getattr] Accediendo a raíz: %s\n", path);
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
        printf("[getattr] Accediendo a inodo %d: %s\n", idx, inodos[idx].name);
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
    if (padre_idx == -ENOENT || (padre_idx >= 0 && !inodos[padre_idx].is_dir)) {
        printf("[create] No encontrado o no es directorio: %s\n", padre);
        return -ENOENT;
    }

    // Verificar si ya existe en ese directorio
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(inodos[i].name, nombre) == 0 && inodos[i].parent_inode == padre_idx) {
            // Sobrescribir: limpiar contenido
            inodos[i].size = 0;
            memset(inodos[i].block_pointers, 0, sizeof(inodos[i].block_pointers));
            memset(inodos[i].block_offsets, 0, sizeof(inodos[i].block_offsets));
            memset(inodos[i].fragment_order, 0, sizeof(inodos[i].fragment_order));

            // Limpiar las regiones asignadas en bloques
            for (int b = 0; b < sb.total_blocks; b++) {
                for (int r = 0; r < MAX_REGIONES; r++) {
                    if (regions_blocks[b][r].inodo_id == i) {
                        regions_blocks[b][r].inodo_id = -1;
                        regions_blocks[b][r].offset = 0;
                        regions_blocks[b][r].size = 0;
                    }
                }
            }

            // Guardar cambios
            char pathi[256];
            snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
            write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

            printf("[create] Sobrescribiendo archivo existente: %s\n", nombre);
            return 0;
        }
    }

    // Si no existe, crearlo normalmente
    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used) {
            inodos[i].used = 1;
            inodos[i].is_dir = 0;
            inodos[i].parent_inode = padre_idx;
            strncpy(inodos[i].name, nombre, sizeof(inodos[i].name) - 1);
            inodos[i].size = 0;
            memset(inodos[i].block_pointers, 0, sizeof(inodos[i].block_pointers));
            memset(inodos[i].block_offsets, 0, sizeof(inodos[i].block_offsets));
            memset(inodos[i].fragment_order, 0, sizeof(inodos[i].fragment_order));

            char pathi[256];
            snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
            write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

            printf("[create] Archivo creado: %s (inode %d)\n", inodos[i].name, i);
            return 0;
        }
    }

    return -ENOSPC;
}

// Busca un bloque con espacio suficiente para el tamaño solicitado
int find_block_with_space(size_t size) {
    for (int i = 3; i < sb.total_blocks; i++) {
        if (bitmap[i] == 1 && (BLOCK_SIZE - used_bytes[i]) >= size) {
            return i;
        }
    }
    return -1;
}

// Busca un bloque vacío
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

    if (offset == 0) {
        // Limpiar asignaciones anteriores del inodo
        for (int b = 0; b < sb.total_blocks; b++) {
            for (int r = 0; r < MAX_REGIONES; r++) {
                if (regions_blocks[b][r].inodo_id == inodo_idx) {
                    regions_blocks[b][r].inodo_id = -1;
                    regions_blocks[b][r].size = 0;
                }
            }
        }
        memset(inodo->block_pointers, 0, sizeof(inodo->block_pointers));
        memset(inodo->block_offsets, 0, sizeof(inodo->block_offsets));
        memset(inodo->fragment_order, 0, sizeof(inodo->fragment_order));
        inodo->size = 0;
    }

    size_t total_written = 0;
    int fragment_count = 0;

    for (int i = 0; i < MAX_BLOCKS_PER_FILE && total_written < size; i++) {
        int elegido = -1;
        int offset_en_bloque = -1;

        // Buscar bloque con espacio continuo al final
        for (int b = 3; b < sb.total_blocks; b++) {
            if (bitmap[b] == 0) continue;

            int max_end = 0;
            for (int r = 0; r < MAX_REGIONES; r++) {
                if (regions_blocks[b][r].size > 0) {
                    int end = regions_blocks[b][r].offset + regions_blocks[b][r].size;
                    if (end > max_end) max_end = end;
                }
            }

            if (BLOCK_SIZE - max_end >= (int)(size - total_written)) {
                elegido = b;
                offset_en_bloque = max_end;
                break;
            }
        }

        // Si no hay bloque con suficiente espacio, usar uno nuevo
        if (elegido == -1) {
            for (int b = 3; b < sb.total_blocks; b++) {
                if (bitmap[b] == 0) {
                    elegido = b;
                    bitmap[b] = 1;
                    offset_en_bloque = 0;
                    sb.used_blocks++;
                    break;
                }
            }
        }

        if (elegido == -1) {
            printf("[write] No se pudo encontrar un bloque adecuado\n");
            return -ENOSPC;
        }

        int bytes_a_escribir = BLOCK_SIZE - offset_en_bloque;
        if (bytes_a_escribir > (int)(size - total_written))
            bytes_a_escribir = size - total_written;

        // Leer bloque actual
        uint8_t tmp[BLOCK_SIZE];
        char pathb[256];
        snprintf(pathb, sizeof(pathb), "%s/block_%04d.png", mount_folder, elegido);
        read_struct_from_png(pathb, tmp, BLOCK_SIZE);

        memcpy(tmp + offset_en_bloque, buf + total_written, bytes_a_escribir);
        write_struct_to_png(pathb, tmp, BLOCK_SIZE);

        int slot = -1;
        for (int j = 0; j < MAX_BLOCKS_PER_FILE; j++) {
            if (inodo->block_pointers[j] == 0) {
                slot = j;
                break;
            }
        }
        if (slot == -1) return -ENOSPC;

        inodo->block_pointers[slot] = elegido;
        inodo->block_offsets[slot] = offset_en_bloque;
        inodo->fragment_order[slot] = inodo->size + total_written;  // usar offset lógico


        // Imprimir debug
        printf("[write] Escribiendo %zu bytes en bloque %d, offset %d\n",
               bytes_a_escribir, elegido, offset_en_bloque);

        // Registrar región en el bloque
        for (int r = 0; r < MAX_REGIONES; r++) {
            if (regions_blocks[elegido][r].size == 0) {
                regions_blocks[elegido][r].inodo_id = inodo_idx;
                regions_blocks[elegido][r].offset = offset_en_bloque;
                regions_blocks[elegido][r].size = bytes_a_escribir;
                break;
            }
        }

        if (used_bytes[elegido] < offset_en_bloque + bytes_a_escribir)
            used_bytes[elegido] = offset_en_bloque + bytes_a_escribir;

        total_written += bytes_a_escribir;
    }

    if ((size_t)(offset + total_written) > (size_t)inodo->size)
        inodo->size = offset + total_written;

    // Persistencia
    char pathi[256], pathbm[256];
    snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
    snprintf(pathbm, sizeof(pathbm), "%s/block_0002.png", mount_folder);
    write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));
    write_struct_to_png(pathbm, bitmap, sizeof(bitmap));

    printf("[write] Escribí %zu de %zu bytes\n", total_written, size);
    return total_written;
}

static int fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi;

    int idx = buscar_inodo_por_ruta(path);
    if (idx < 0 || inodos[idx].is_dir) return -ENOENT;

    if ((size_t)offset >= inodos[idx].size) {
        printf("[read] Offset %zu fuera de rango para archivo %s\n", offset, inodos[idx].name);
        return 0;
    }

    size_t bytes_to_read = (offset + size > inodos[idx].size)
        ? (inodos[idx].size - offset) : size;

    size_t total_read = 0;
    size_t logical_offset = offset;

    // Preparar estructura auxiliar para ordenar los fragmentos
    typedef struct {
        int block_id;
        int offset;
        int length;
        int order;
    } Fragment;

    Fragment frags[MAX_BLOCKS_PER_FILE];
    int frag_count = 0;

    for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) {
        if (inodos[idx].block_pointers[i] == 0) continue;

        int blk = inodos[idx].block_pointers[i];
        int off = inodos[idx].block_offsets[i];
        int len = 0;

        // Buscar en regions_blocks la longitud real del fragmento
        for (int r = 0; r < MAX_REGIONES; r++) {
            if (regions_blocks[blk][r].inodo_id == idx && regions_blocks[blk][r].offset == off) {
                len = regions_blocks[blk][r].size;
                break;
            }
        }

        frags[frag_count].block_id = blk;
        frags[frag_count].offset = off;
        frags[frag_count].length = len;
        frags[frag_count].order = inodos[idx].fragment_order[i];
        frag_count++;
    }

    // Ordenar por orden lógico
    for (int i = 0; i < frag_count - 1; i++) {
        for (int j = i + 1; j < frag_count; j++) {
            if (frags[i].order > frags[j].order) {
                Fragment tmp = frags[i];
                frags[i] = frags[j];
                frags[j] = tmp;
            }
        }
    }

    // Leer secuencialmente cada fragmento
    for (int i = 0; i < frag_count && total_read < bytes_to_read; i++) {
        if (logical_offset >= (size_t)frags[i].length) {
            logical_offset -= frags[i].length;
            continue;
        }

        uint8_t temp_block[BLOCK_SIZE];
        read_data_block(frags[i].block_id, temp_block, BLOCK_SIZE);

        size_t read_start = frags[i].offset + logical_offset;
        size_t available = frags[i].length - logical_offset;
        size_t to_copy = (bytes_to_read - total_read < available)
                            ? (bytes_to_read - total_read) : available;

        memcpy(buf + total_read, temp_block + read_start, to_copy);
        total_read += to_copy;
        logical_offset = 0;
    }

    printf("[read] Leídos %zu bytes de %s (offset %zu)\n", total_read, inodos[idx].name, offset);
    return total_read;
}

static int fs_open(const char *path, struct fuse_file_info *fi) {
    (void)fi;

    int idx = buscar_inodo_por_ruta(path);
    if (idx >= 0 && !inodos[idx].is_dir) {
        printf("[open] Abriendo archivo: %s (inode %d)\n", inodos[idx].name, idx);
        return 0;  // OK: es archivo regular
    }

    printf("[open] No encontrado o no es archivo regular: %s\n", path);
    return -ENOENT;
}

static int fs_unlink(const char *path) {
    int idx = buscar_inodo_por_ruta(path);
    if (idx < 0 || inodos[idx].is_dir) return -ENOENT;

    // Limpiar regiones
    for (int b = 0; b < sb.total_blocks; b++) {
        for (int r = 0; r < MAX_REGIONES; r++) {
            if (regions_blocks[b][r].inodo_id == idx) {
                regions_blocks[b][r].inodo_id = -1;
                regions_blocks[b][r].size = 0;
                regions_blocks[b][r].offset = 0;
            }
        }
    }

    // Liberar bloques si ya no están en uso
    for (int j = 0; j < MAX_BLOCKS_PER_FILE; j++) {
        int blk = inodos[idx].block_pointers[j];
        if (blk > 2 && blk < sb.total_blocks) {
            bool en_uso = false;
            for (int x = 0; x < MAX_REGIONES; x++) {
                if (regions_blocks[blk][x].size > 0) {
                    en_uso = true;
                    break;
                }
            }
            if (!en_uso) {
                bitmap[blk] = 0;
                used_bytes[blk] = 0;
            }
        }
    }

    inodos[idx].used = 0;

    // Persistir cambios
    char pathi[256];
    snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
    write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

    printf("[unlink] Archivo eliminado: %s (inode %d)\n", inodos[idx].name, idx);
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
        printf("[mkdir] No encontrado o no es directorio: %s\n", padre);
        return -ENOENT;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && strcmp(inodos[i].name, nombre) == 0 && inodos[i].parent_inode == padre_idx) {
            printf("[mkdir] Ya existe un directorio con ese nombre: %s\n", nombre);
            return -EEXIST;
        }
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

            printf("[mkdir] Directorio creado: %s (inode %d)\n", inodos[i].name, i);
            return 0;
        }
    }

    printf("[mkdir] No hay espacio para crear un nuevo directorio\n");
    return -ENOSPC;
}

static int fs_rmdir(const char *path) {
    int idx = buscar_inodo_por_ruta(path);
    if (idx < 0) {
        printf("[rmdir] No encontrado: %s\n", path);
        return -ENOENT;
    }

    if (!inodos[idx].is_dir) {
        printf("[rmdir] No es un directorio: %s\n", path);
        return -ENOTDIR;
    }

    // Limpiar inodo
    inodos[idx].used = 0;
    memset(&inodos[idx], 0, sizeof(Inode));
    inodos[idx].parent_inode = -1;

    // Guardar cambios
    char pathi[256];
    snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
    write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

    printf("[rmdir] Directorio eliminado: %s (inode %d)\n", inodos[idx].name, idx);
    return 0;
}

static int fs_opendir(const char *path, struct fuse_file_info *fi) {
    if (strcmp(path, "/") == 0) {
        printf("[opendir] Accediendo a raíz: %s\n", path);
        return 0;  // raíz especial
    }

    int idx = buscar_inodo_por_ruta(path);
    if (idx >= 0 && inodos[idx].is_dir) {
        printf("[opendir] Accediendo a inodo %d: %s\n", idx, inodos[idx].name);
        return 0;
    }

    printf("[opendir] No encontrado o no es directorio: %s\n", path);
    return -ENOENT;
}

static int fs_rename(const char *from, const char *to, unsigned int flags) {
    (void)flags;

    // Obtener inodo original
    int origen_idx = buscar_inodo_por_ruta(from);
    if (origen_idx < 0) {
        printf("[rename] No encontrado: %s\n", from);
        return -ENOENT;
    }

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
            printf("[rename] Ya existe un archivo con ese nombre: %s\n", nuevo_nombre);
            return -EEXIST;
    }

    // Actualizar el inodo
    strncpy(inodos[origen_idx].name, nuevo_nombre, sizeof(inodos[origen_idx].name) - 1);
    inodos[origen_idx].parent_inode = nuevo_padre_idx;

    // Guardar cambios
    char pathi[256];
    snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
    write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

    printf("[rename] Archivo renombrado: %s -> %s\n", from, to);
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

    printf("[statfs] Información del sistema de archivos:\n");
    printf("  Tamaño de bloque: %zu\n", st->f_bsize);
    printf("  Bloques totales: %lu\n", st->f_blocks);
    printf("  Bloques libres: %lu\n", st->f_bfree);
    printf("  Bloques disponibles: %lu\n", st->f_bavail);
    printf("  Inodos totales: %d\n", MAX_FILES);
    printf("  Inodos libres: %d\n", inodos_libres);
    return 0;
}

static int fs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi) {
    (void)isdatasync;
    (void)fi;

    int idx = buscar_inodo_por_ruta(path);
    if (idx < 0) {
        printf("[fsync] No encontrado: %s\n", path);
        return -ENOENT;
    }

    printf("[fsync] ejecutado para '%s'\n", path);

    char path_sb[256], path_inodes[256], path_bitmap[256];
    snprintf(path_sb, sizeof(path_sb), "%s/block_0000.png", mount_folder);
    snprintf(path_inodes, sizeof(path_inodes), "%s/block_0001.png", mount_folder);
    snprintf(path_bitmap, sizeof(path_bitmap), "%s/block_0002.png", mount_folder);

    write_struct_to_png(path_sb, (const uint8_t*)&sb, sizeof(sb));
    write_struct_to_png(path_inodes, (const uint8_t*)inodos, sizeof(inodos));
    write_struct_to_png(path_bitmap, bitmap, sizeof(bitmap));

    printf("[fsync] completado para '%s'\n", path);
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
    if (idx < 0) {
        printf("[flush] No encontrado: %s\n", path);
        return -ENOENT;
    }

    printf("[flush] Se cerró el descriptor para %s (inode %d)\n", path, idx);

    // No se hace nada porque write y fsync ya guardan los cambios
    return 0;
}

static off_t fs_lseek(const char *path, off_t off, int whence, struct fuse_file_info *fi) {
    (void)fi;

    int idx = buscar_inodo_por_ruta(path);
    if (idx < 0 || inodos[idx].is_dir) {
        printf("[lseek] No encontrado o no es archivo regular: %s\n", path);
        return -ENOENT;
    }

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
            printf("[lseek] Whence no válido: %d\n", whence);
            return -EINVAL;
    }

    if (result < 0 || result > inodos[idx].size) {
        printf("[lseek] Offset fuera de rango: %ld para archivo %s (size %zu)\n", result, path, inodos[idx].size);
        return -EINVAL;
    }

    printf("[lseek] Nuevo offset: %ld para archivo %s\n", result, path);
    return result;
}

static int fs_truncate(const char *path, off_t size) {
    int inodo_idx = buscar_inodo_por_ruta(path);
    if (inodo_idx < 0 || inodos[inodo_idx].is_dir) {
        printf("[truncate] No encontrado o no es archivo regular: %s\n", path);
        return -ENOENT;
    }

    Inode *inodo = &inodos[inodo_idx];

    if (size == 0) {
        // Eliminar todas las regiones de este inodo
        for (int b = 0; b < sb.total_blocks; b++) {
            for (int r = 0; r < MAX_REGIONES; r++) {
                if (regions_blocks[b][r].inodo_id == inodo_idx) {
                    regions_blocks[b][r].inodo_id = -1;
                    regions_blocks[b][r].size = 0;
                }
            }
        }
        memset(inodo->block_pointers, 0, sizeof(inodo->block_pointers));
        memset(inodo->block_offsets, 0, sizeof(inodo->block_offsets));
        memset(inodo->fragment_order, 0, sizeof(inodo->fragment_order));
        inodo->size = 0;
    } else if (size < inodo->size) {
        // Recortar el archivo
        size_t remaining = size;
        for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) {
            if (inodo->block_pointers[i] == 0) continue;

            int block_id = inodo->block_pointers[i];
            int offset_bloque = inodo->block_offsets[i];
            int bytes_in_frag = 0;

            // Buscar la región correspondiente para este inodo y fragmento
            for (int r = 0; r < MAX_REGIONES; r++) {
                if (regions_blocks[block_id][r].inodo_id == inodo_idx &&
                    regions_blocks[block_id][r].offset == offset_bloque) {
                    bytes_in_frag = regions_blocks[block_id][r].size;

                    if (remaining >= bytes_in_frag) {
                        remaining -= bytes_in_frag;
                    } else {
                        // Truncar dentro de este fragmento
                        regions_blocks[block_id][r].size = remaining;
                        used_bytes[block_id] = offset_bloque + remaining;
                        remaining = 0;
                    }
                    if (remaining == 0) {
                        // Eliminar fragmentos posteriores
                        for (int j = i + 1; j < MAX_BLOCKS_PER_FILE; j++) {
                            int b2 = inodo->block_pointers[j];
                            int off2 = inodo->block_offsets[j];
                            for (int r2 = 0; r2 < MAX_REGIONES; r2++) {
                                if (regions_blocks[b2][r2].inodo_id == inodo_idx &&
                                    regions_blocks[b2][r2].offset == off2) {
                                    regions_blocks[b2][r2].inodo_id = -1;
                                    regions_blocks[b2][r2].size = 0;
                                }
                            }
                            inodo->block_pointers[j] = 0;
                            inodo->block_offsets[j] = 0;
                            inodo->fragment_order[j] = 0;
                        }
                        break;
                    }
                }
            }
        }
        inodo->size = size;
    } else if ((size_t)size > inodo->size) {
        // Expandir con ceros
        size_t to_fill = size - inodo->size;
        char *zero_buffer = calloc(to_fill, 1);  // \0-filled
        if (!zero_buffer) {
            printf("[expand] Error al asignar memoria\n");
            return -ENOMEM;
        }
        fs_write(path, zero_buffer, to_fill, inodo->size, NULL);
        free(zero_buffer);
    }

    // Guardar cambios
    char pathi[256], pathbm[256];
    snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
    snprintf(pathbm, sizeof(pathbm), "%s/block_0002.png", mount_folder);
    write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));
    write_struct_to_png(pathbm, bitmap, sizeof(bitmap));

    printf("[fsync] Cambios guardados en disco\n");
    return 0;
}