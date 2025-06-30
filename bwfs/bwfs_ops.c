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
char mount_folder[1000];
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

// Carga los metadatos del sistema de archivos desde los PNGs
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

    // Recorrer los inodos y reconstruir regiones y uso de bytes
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

// --------------------- LECTURA Y ESCRITURA DE PNGS -------------------------
// Leer una estructura desde un PNG y mapearla a un buffer
void read_struct_from_png(const char *filename, uint8_t *buffer, size_t buffer_size_bytes) {
    FILE *fp = fopen(filename, "rb");

    // Verificar si el archivo se abrió correctamente
    if (!fp) {
        perror("Error abriendo PNG para lectura");
        exit(1);
    }

    // Crear estructuras de lectura PNG
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fprintf(stderr, "Error creando png_structp para lectura\n");
        fclose(fp);
        exit(1);
    }

    // Crear estructura de información PNG
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        fprintf(stderr, "Error creando png_infop para lectura\n");
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(fp);
        exit(1);
    }

    // Configurar el manejador de errores
    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "Error leyendo PNG\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        exit(1);
    }

    // Inicializar la lectura del PNG
    png_init_io(png_ptr, fp);
    png_read_info(png_ptr, info_ptr);

    // Verificar que la imagen tenga las dimensiones y tipo de color esperados
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

    // Asignar memoria para la fila de lectura
    size_t row_bytes = (1000 + 7) / 8;
    png_bytep row = (png_bytep) malloc(row_bytes);
    if (!row) {
        fprintf(stderr, "Error: No se pudo asignar memoria para la fila.\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        exit(1);
    }

    // Leer los datos de la imagen PNG
    memset(buffer, 0, buffer_size_bytes);
    size_t output_bit_index = 0;

    // Leer cada fila de la imagen PNG
    for (int y = 0; y < 1000; y++) {
        png_read_row(png_ptr, row, NULL);

        // Procesar cada byte de la fila
        for (int i = 0; i < row_bytes; i++) {
            uint8_t current_byte_from_png = row[i];

            // Procesar cada bit del byte actual
            for (int bit_in_byte = 0; bit_in_byte < 8; bit_in_byte++) {
                if (output_bit_index >= buffer_size_bytes * 8) {
                    break;
                }

                // Extraer el bit correspondiente del byte actual
                uint8_t pixel_bit_from_png = (current_byte_from_png >> (7 - bit_in_byte)) & 1;
                uint8_t mapped_output_bit = (pixel_bit_from_png == 0) ? 1 : 0; // Mapeamos 0 a 1 (blanco) y 1 a 0

                // Almacenar el bit en el buffer de salida
                if (mapped_output_bit == 1) { // Si el bit de salida mapeado es 1, establecerlo en el buffer
                    buffer[output_bit_index / 8] |= (1 << (7 - (output_bit_index % 8)));
                }
                // Si mapped_output_bit es 0, no hacemos nada, ya que el buffer se inicializó a 0.

                output_bit_index++;
            }
            // Si hemos alcanzado el tamaño del buffer, salimos del bucle
            if (output_bit_index >= buffer_size_bytes * 8) break;
        }
    }

    // Liberar memoria y destruir estructuras PNG
    free(row);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);
}

// Escribe una estructura en un PNG, mapeando los bits de entrada
void write_struct_to_png(const char *filename, const uint8_t *data, size_t data_size_bytes) {
    FILE *fp = fopen(filename, "wb");

    // Verificar si el archivo se abrió correctamente
    if (!fp) {
        perror("Error abriendo archivo para escritura");
        exit(1);
    }

    // Crear estructuras de escritura PNG
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fprintf(stderr, "Error creando png_structp para escritura\n");
        fclose(fp);
        exit(1);
    }

    // Crear estructura de información PNG
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        fprintf(stderr, "Error creando png_infop para escritura\n");
        png_destroy_write_struct(&png_ptr, NULL); // El segundo parámetro debe ser NULL si info_ptr no se creó
        fclose(fp);
        exit(1);
    }

    // Configurar el manejador de errores
    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "Error escribiendo PNG\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        exit(1);
    }

    // Inicializar la escritura del PNG
    png_init_io(png_ptr, fp);

    // Configurar los parámetros de la imagen PNG
    png_set_IHDR(png_ptr, info_ptr, 1000, 1000,
                 1, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_set_compression_level(png_ptr, 0);  // Sin compresión para mantener la estructura simple

    png_write_info(png_ptr, info_ptr);      // Escribir la cabecera PNG

    // Asignar memoria para la fila de escritura
    size_t row_bytes = (1000 + 7) / 8;
    png_bytep row = (png_bytep)malloc(row_bytes);
    if (!row) {
        fprintf(stderr, "Error: No se pudo asignar memoria para la fila.\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        exit(1);
    }

    size_t data_bit_index = 0;

    // Inicializar la fila con 0xFF (todos los bits en 1, que es blanco en PNG)
    for (int y = 0; y < 1000; y++) {
        memset(row, 0xFF, row_bytes);

        // Procesar cada bit de la fila
        for (int x = 0; x < 1000; x++) {
            if (data_bit_index < data_size_bytes * 8) {
                uint8_t byte_from_data = data[data_bit_index / 8];
                uint8_t input_bit = (byte_from_data >> (7 - (data_bit_index % 8))) & 1;

                // Si input_bit es 1, establecer el bit correspondiente en la fila a 0 (negro)
                if (input_bit == 1) {
                    row[x / 8] &= ~(1 << (7 - (x % 8)));
                }
                // Si input_bit es 0, el píxel permanece 1 (blanco), que es el valor predeterminado.
            } else {
                // Si ya no hay más 'data' de entrada, los píxeles restantes se mantendrán en blanco
            }
            data_bit_index++;
        }
        png_write_row(png_ptr, row);
    }

    // Libera la memoria y destruye las estructuras png
    free(row);
    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
}

// Lee un bloque de datos desde un archivo PNG
void read_data_block(int block_id, uint8_t *buffer, size_t size) {
    char path[1000];
    snprintf(path, sizeof(path), "%s/block_%04d.png", mount_folder, block_id);
    read_struct_from_png(path, buffer, size);
}

// Divide un path como "/a/b/c.txt" y recorre los inodos jerárquicamente
int search_inodo_by_path(const char *path) {
    if (strcmp(path, "/") == 0) return -1;  // raíz especial

    // Copia mutable del path
    char ruta_copia[1000];
    strncpy(ruta_copia, path, sizeof(ruta_copia));
    ruta_copia[sizeof(ruta_copia)-1] = '\0';

    char *token;
    char *rest = ruta_copia;

    // Comenzar desde la raíz
    int actual = -1;  // -1 = raíz
    token = strtok(rest, "/");

    // Recorre cada token del path
    while (token != NULL) {
        int encontrado = 0;

        // Si estamos en la raíz, buscamos desde el índice 0
        for (int i = 0; i < MAX_FILES; i++) {
            // Si estamos en la raíz, buscamos desde el índice 0, si lo encontramos, actualizamos
            if (inodos[i].used && strcmp(inodos[i].name, token) == 0 && inodos[i].parent_inode == actual) {
                actual = i;
                encontrado = 1;
                break;
            }
        }

        // Si no encontramos el token, retornamos error
        if (!encontrado) {
            return -ENOENT;
        }

        // Avanzar al siguiente token
        token = strtok(NULL, "/");
    }

    return actual;  // Índice del inodo final
}

// --------------------- FUSE OPERATIONS -------------------------
// Obtiene los atributos de un archivo o directorio
static int fs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi;
    // Inicializar stbuf a cero
    memset(stbuf, 0, sizeof(struct stat));

    // Si es la raíz, asignar atributos de directorio
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;  // rwxr-xr-x
        stbuf->st_nlink = 2;
        printf("[getattr] Accediendo a raíz: %s\n", path);
        return 0;
    }

    // Buscar el inodo correspondiente al path
    int idx = search_inodo_by_path(path);
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

    // Si no se encontró el inodo, retornar error
    printf("[getattr] No encontrado: %s\n", path);
    return -ENOENT;
}

// Lee el contenido de un directorio
static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;

    // Obtener el índice del directorio actual (raíz = -1)
    int dir_idx = (strcmp(path, "/") == 0) ? -1 : search_inodo_by_path(path);
    if (dir_idx == -ENOENT || (dir_idx >= 0 && !inodos[dir_idx].is_dir))
    return -ENOENT;

    // Agrega "." y ".."
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    // Recorre todos los inodos y lista los que tengan como padre al actual
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && inodos[i].parent_inode == dir_idx) {
        printf("[readdir] Listando: %s (inode %d, padre %d)\n", inodos[i].name, i, inodos[i].parent_inode);

        filler(buf, inodos[i].name, NULL, 0, 0);
        }
    }

    return 0;
}

// Crea un nuevo archivo o sobrescribe uno existente
static int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void)mode; (void)fi;

    char copia1[1000], copia2[1000];
    strncpy(copia1, path, sizeof(copia1));
    strncpy(copia2, path, sizeof(copia2));
    copia1[sizeof(copia1) - 1] = '\0';
    copia2[sizeof(copia2) - 1] = '\0';

    char *nombre = basename(copia1);
    char *padre = dirname(copia2);

    int padre_idx = search_inodo_by_path(padre);
    // Verificar si el directorio padre existe y es un directorio
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
            char pathi[1000];
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

            char pathi[1000];
            snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
            write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

            printf("[create] Archivo creado: %s (inode %d)\n", inodos[i].name, i);
            return 0;
        }
    }

    printf("[create] No hay espacio para crear un nuevo archivo\n");
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

// Permite la escritura de un archivo, escribiendo datos en bloques
static int fs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi;

    int inodo_idx = search_inodo_by_path(path);
    if (inodo_idx < 0 || inodos[inodo_idx].is_dir) return -ENOENT;

    Inode *inodo = &inodos[inodo_idx];

    // Si se está escribiendo desde cero, limpiar estado anterior
    if (offset == 0) {
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
        int selected = -1;
        int offset_in_block = -1;

        // Paso 1: Buscar bloque con espacio restante al final
        for (int b = 3; b < sb.total_blocks; b++) {
            if (bitmap[b] == 0) continue;

            // Calcular la posición final ocupada en este bloque
            int max_end = 0;
            for (int r = 0; r < MAX_REGIONES; r++) {
                if (regions_blocks[b][r].size > 0) {
                    int end = regions_blocks[b][r].offset + regions_blocks[b][r].size;
                    if (end > max_end) max_end = end;
                }
            }

            // Si hay espacio, lo usamos
            if (BLOCK_SIZE - max_end >= 1) {
                selected = b;
                offset_in_block = max_end;
                break;
            }
        }

        // Paso 2: Si no se encontró bloque con espacio, buscar nuevo
        if (selected == -1) {
            for (int b = 3; b < sb.total_blocks; b++) {
                if (bitmap[b] == 0) {
                    selected = b;
                    bitmap[b] = 1;
                    offset_in_block = 0;
                    sb.used_blocks++;
                    break;
                }
            }
        }

        // Fallo si no hay ningún bloque disponible
        if (selected == -1) {
            printf("[write] No se pudo encontrar un bloque adecuado\n");
            return -ENOSPC;
        }

        // Calcular cuántos bytes escribir en esta iteración
        int bytes_to_write = BLOCK_SIZE - offset_in_block;
        if (bytes_to_write > (int)(size - total_written))
            bytes_to_write = size - total_written;

        // Leer el contenido actual del bloque elegido
        uint8_t tmp[BLOCK_SIZE];
        char pathb[1000];
        snprintf(pathb, sizeof(pathb), "%s/block_%04d.png", mount_folder, selected);
        read_struct_from_png(pathb, tmp, BLOCK_SIZE);

        // Escribir los nuevos datos
        memcpy(tmp + offset_in_block, buf + total_written, bytes_to_write);
        write_struct_to_png(pathb, tmp, BLOCK_SIZE);

        // Buscar slot libre en los arrays del inodo
        // Verificar si el bloque ya estaba asignado al inodo
        int slot = -1;
        for (int j = 0; j < MAX_BLOCKS_PER_FILE; j++) {
            if (inodo->block_pointers[j] == selected) {
                slot = j; // Ya estaba asignado, solo actualizamos offset si queremos
                break;
            }
        }

        // Si no estaba asignado, buscar un slot nuevo
        if (slot == -1) {
            for (int j = 0; j < MAX_BLOCKS_PER_FILE; j++) {
                if (inodo->block_pointers[j] == 0) {
                    slot = j;
                    inodo->block_pointers[slot] = selected;
                    printf("[write] Asignando nuevo bloque %d al inodo %d (slot %d)\n", selected, inodo_idx, slot);
                    break;
                }
            }
        }

        // Si no hay espacio, error
        if (slot == -1) {
            printf("[write] Error: inodo %d ya alcanzó el máximo de bloques permitidos\n", inodo_idx);
            return -ENOSPC;
        }

        // Registrar asignación
        inodo->block_pointers[slot] = selected;
        inodo->block_offsets[slot] = offset_in_block;
        inodo->fragment_order[slot] = offset + total_written;

        // Registrar región del bloque
        for (int r = 0; r < MAX_REGIONES; r++) {
            if (regions_blocks[selected][r].size == 0) {
                regions_blocks[selected][r].inodo_id = inodo_idx;
                regions_blocks[selected][r].offset = offset_in_block;
                regions_blocks[selected][r].size = bytes_to_write;
                break;
            }
        }

        // Actualizar uso del bloque
        if (used_bytes[selected] < offset_in_block + bytes_to_write)
            used_bytes[selected] = offset_in_block + bytes_to_write;

        printf("[write] Escribiendo %zu bytes en bloque %d, offset %d\n",
               bytes_to_write, selected, offset_in_block);

        total_written += bytes_to_write;
    }

    // Actualizar tamaño lógico del archivo si creció
    if ((size_t)(offset + total_written) > (size_t)inodo->size)
        inodo->size = offset + total_written;

    // Guardar estructuras actualizadas en disco
    char pathi[1000], pathbm[1000];
    snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
    snprintf(pathbm, sizeof(pathbm), "%s/block_0002.png", mount_folder);
    write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));
    write_struct_to_png(pathbm, bitmap, sizeof(bitmap));

    return total_written;
}

// Permite leer un archivo, leyendo datos desde bloques asignados
static int fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi;

    // Verificar si el archivo existe y es regular
    int idx = search_inodo_by_path(path);
    if (idx < 0 || inodos[idx].is_dir) return -ENOENT;

    // Verificar si el offset es válido
    if ((size_t)offset >= inodos[idx].size) {
        printf("[read] Offset %zu fuera de rango para archivo %s\n", offset, inodos[idx].name);
        return 0;
    }

    //Se obtiene el tamaño a leer, asegurando que no exceda el tamaño del archivo
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

    // Arreglo para almacenar fragmentos encontrados
    Fragment frags[MAX_BLOCKS_PER_FILE];
    int frag_count = 0;

    //Recorrer los bloques del inodo y llenar la estructura de fragmentos
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

        // 
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

// Abre un archivo si existe y no es un directorio
static int fs_open(const char *path, struct fuse_file_info *fi) {
    (void)fi;

    int idx = search_inodo_by_path(path);
    if (idx >= 0 && !inodos[idx].is_dir)
        return 0;  // Éxito

    return -ENOENT;  // Archivo no encontrado o es un directorio
}

// Elimina un archivo del sistema
static int fs_unlink(const char *path) {
    int idx = search_inodo_by_path(path);
    if (idx < 0 || inodos[idx].is_dir) return -ENOENT;  // Archivo no encontrado o es un directorio

    // Limpiar referencias del inodo en las regiones de bloques
    for (int b = 0; b < sb.total_blocks; b++) {
        for (int r = 0; r < MAX_REGIONES; r++) {
            if (regions_blocks[b][r].inodo_id == idx) {
                regions_blocks[b][r].inodo_id = -1;
                regions_blocks[b][r].size = 0;
                regions_blocks[b][r].offset = 0;
            }
        }
    }

    // Liberar bloques que ya no contienen regiones activas
    for (int j = 0; j < MAX_BLOCKS_PER_FILE; j++) {
        int blk = inodos[idx].block_pointers[j];
        if (blk > 2 && blk < sb.total_blocks) {
            bool in_use = false;
            for (int x = 0; x < MAX_REGIONES; x++) {
                if (regions_blocks[blk][x].size > 0) {
                    in_use = true;
                    break;
                }
            }
            if (!in_use) {
                bitmap[blk] = 0;
                used_bytes[blk] = 0;
            }
        }
    }

    // Marcar el inodo como no usado
    inodos[idx].used = 0;

    // Guardar cambios en disco
    char pathi[1000];
    snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
    write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

    printf("[unlink] Archivo eliminado: %s (inode %d)\n", inodos[idx].name, idx);
    return 0;
}

// Crea un nuevo directorio en el sistema de archivos
static int fs_mkdir(const char *path, mode_t mode) {
    (void)mode;  // El modo no se usa en este sistema

    // Copiar path para manipularlo
    char copy1[1000], copy2[1000];
    strncpy(copy1, path, sizeof(copy1));
    strncpy(copy2, path, sizeof(copy2));
    copy1[sizeof(copy1)-1] = '\0';
    copy2[sizeof(copy2)-1] = '\0';

    // Separar en nombre del nuevo directorio y ruta del padre
    char *name = basename(copy1);
    char *parent = dirname(copy2);

    // Buscar el inodo del directorio padre
    int parent_idx = search_inodo_by_path(parent);
    if (parent_idx == -ENOENT || (parent_idx >= 0 && !inodos[parent_idx].is_dir)) {
        return -ENOENT;  // No existe o no es un directorio
    }

    // Verificar si ya existe un archivo/directorio con ese nombre en ese padre
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used &&
            strcmp(inodos[i].name, name) == 0 &&
            inodos[i].parent_inode == parent_idx) {
            return -EEXIST;  // Ya existe con ese nombre
        }
    }

    // Buscar un inodo libre para crear el directorio
    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used) {
            inodos[i].used = 1;
            inodos[i].is_dir = 1;
            inodos[i].parent_inode = parent_idx;
            strncpy(inodos[i].name, name, sizeof(inodos[i].name) - 1);
            inodos[i].size = 0;
            memset(inodos[i].block_pointers, 0, sizeof(inodos[i].block_pointers));

            // Guardar cambios en disco
            char pathi[1000];
            snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
            write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

            return 0;  // Éxito
        }
    }

    return -ENOSPC;  // No hay espacio para más inodos
}

static int fs_rmdir(const char *path) {
    // Buscar índice del inodo por la ruta
    int idx = search_inodo_by_path(path);
    if (idx < 0) {
        // No se encontró el inodo
        printf("[rmdir] No encontrado: %s\n", path);
        return -ENOENT;
    }

    if (!inodos[idx].is_dir) {
        // El inodo no es un directorio
        printf("[rmdir] No es un directorio: %s\n", path);
        return -ENOTDIR;
    }

    // Verificar que el directorio esté vacío
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used && inodos[i].parent_inode == idx) {
            // El directorio no está vacío
            printf("[rmdir] Directorio no vacío: %s\n", path);
            return -ENOTEMPTY;
        }
    }

    // Limpiar y marcar el inodo como libre
    inodos[idx].used = 0;
    memset(&inodos[idx], 0, sizeof(Inode));
    inodos[idx].parent_inode = -1;

    // Guardar los cambios en el almacenamiento persistente
    char pathi[1000];
    snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
    write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

    // Confirmación de eliminación
    printf("[rmdir] Directorio eliminado: %s (inode %d)\n", inodos[idx].name, idx);
    return 0;
}

static int fs_opendir(const char *path, struct fuse_file_info *fi) {
    // Permitir acceso a la raíz directamente
    if (strcmp(path, "/") == 0) {
        printf("[opendir] Accediendo a raíz: %s\n", path);
        return 0;
    }

    // Buscar el inodo correspondiente a la ruta
    int idx = search_inodo_by_path(path);
    // Verificar que exista y sea un directorio
    if (idx >= 0 && inodos[idx].is_dir) {
        printf("[opendir] Accediendo a inodo %d: %s\n", idx, inodos[idx].name);
        return 0;
    }

    // No existe o no es directorio
    printf("[opendir] No encontrado o no es directorio: %s\n", path);
    return -ENOENT;
}

static int fs_rename(const char *from, const char *to, unsigned int flags) {
    (void)flags;

    // Buscar inodo del archivo/directorio origen
    int origin_idx = search_inodo_by_path(from);
    if (origin_idx < 0) {
        printf("[rename] No encontrado: %s\n", from);
        return -ENOENT;
    }

    // Copiar la ruta destino para manipular nombre y directorio padre
    char copy1[1000], copy2[1000];
    strncpy(copy1, to, sizeof(copy1));
    strncpy(copy2, to, sizeof(copy2));
    copy1[sizeof(copy1)-1] = '\0';
    copy2[sizeof(copy2)-1] = '\0';

    // Extraer nuevo nombre y ruta padre destino
    char *new_name = basename(copy1);
    char *new_parent_path = dirname(copy2);
    int new_parent_idx = search_inodo_by_path(new_parent_path);

    // Verificar que el nuevo padre exista y sea directorio
    if (new_parent_idx < -1 || (new_parent_idx >= 0 && !inodos[new_parent_idx].is_dir))
        return -ENOENT;

    // Comprobar que no exista un archivo con el mismo nombre en destino
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodos[i].used &&
            strcmp(inodos[i].name, new_name) == 0 &&
            inodos[i].parent_inode == new_parent_idx) {
                printf("[rename] Ya existe un archivo con ese nombre: %s\n", new_name);
                return -EEXIST;
        }
    }

    // Actualizar nombre y padre en el inodo origen
    strncpy(inodos[origin_idx].name, new_name, sizeof(inodos[origin_idx].name) - 1);
    inodos[origin_idx].parent_inode = new_parent_idx;

    // Guardar cambios persistentes
    char pathi[1000];
    snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
    write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));

    printf("[rename] Archivo renombrado: %s -> %s\n", from, to);
    return 0;
}

static int fs_statfs(const char *path, struct statvfs *st) {
    (void)path; // No se usa el path en esta función

    // Configurar tamaño de bloque y total de bloques del FS
    st->f_bsize = BLOCK_SIZE;
    st->f_frsize = BLOCK_SIZE;
    st->f_blocks = sb.total_blocks;

    // Contar bloques libres en el bitmap
    int free_blocks = 0;
    for (int i = 0; i < sb.total_blocks; i++) {
        if (bitmap[i] == 0)
            free_blocks++;
    }

    st->f_bfree = free_blocks;   // bloques libres totales
    st->f_bavail = free_blocks;  // bloques disponibles para usuario

    // Número máximo de archivos (inodos)
    st->f_files = MAX_FILES;

    // Contar inodos libres
    int free_inodes = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (!inodos[i].used)
            free_inodes++;
    }

    st->f_ffree = free_inodes;   // inodos libres totales
    st->f_favail = free_inodes;  // inodos disponibles para usuario

    st->f_namemax = 63;  // longitud máxima del nombre (name[64])

    // Imprimir resumen de la info del sistema de archivos
    printf("[statfs] Información del sistema de archivos:\n");
    printf("  Tamaño de bloque: %zu\n", st->f_bsize);
    printf("  Bloques totales: %lu\n", st->f_blocks);
    printf("  Bloques libres: %lu\n", st->f_bfree);
    printf("  Inodos totales: %d\n", MAX_FILES);
    printf("  Inodos libres: %d\n", free_inodes);
    return 0;
}

static int fs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi) {
    (void)isdatasync;  // Parámetro no usado
    (void)fi;          // Parámetro no usado

    // Buscar inodo correspondiente al path
    int idx = search_inodo_by_path(path);
    if (idx < 0) {
        printf("[fsync] No encontrado: %s\n", path);
        return -ENOENT;
    }

    printf("[fsync] ejecutado para '%s'\n", path);

    // Construir rutas de los bloques del sistema de archivos
    char path_sb[1000], path_inodes[1000], path_bitmap[1000];
    snprintf(path_sb, sizeof(path_sb), "%s/block_0000.png", mount_folder);
    snprintf(path_inodes, sizeof(path_inodes), "%s/block_0001.png", mount_folder);
    snprintf(path_bitmap, sizeof(path_bitmap), "%s/block_0002.png", mount_folder);

    // Guardar estructuras del superbloque, inodos y bitmap a disco
    write_struct_to_png(path_sb, (const uint8_t*)&sb, sizeof(sb));
    write_struct_to_png(path_inodes, (const uint8_t*)inodos, sizeof(inodos));
    write_struct_to_png(path_bitmap, bitmap, sizeof(bitmap));

    printf("[fsync] completado para '%s'\n", path);
    return 0;
}

static int fs_access(const char *path, int mask) {
    printf("[access] Verificando acceso a: %s\n", path);

    // Permitir acceso directo a la raíz
    if (strcmp(path, "/") == 0) {
        printf("[access] Es el directorio raíz.\n");
        return 0;
    }

    // Buscar inodo asociado al path
    int idx = search_inodo_by_path(path);
    if (idx >= 0) {
        printf("[access] Archivo o directorio '%s' encontrado (inode %d).\n", path, idx);

        // No se validan permisos reales, siempre OK si existe
        return 0;
    }

    // No existe el archivo o directorio
    printf("[access] '%s' NO encontrado.\n", path);
    return -ENOENT;
}

static int fs_flush(const char *path, struct fuse_file_info *fi) {
    (void)fi;  // Parámetro no usado

    // Buscar inodo asociado a la ruta
    int idx = search_inodo_by_path(path);
    if (idx < 0) {
        printf("[flush] No encontrado: %s\n", path);
        return -ENOENT;
    }

    // Confirmar cierre de descriptor de archivo
    printf("[flush] Se cerró el descriptor para %s (inode %d)\n", path, idx);

    // No se realiza acción adicional, write/fsync ya guardan cambios
    return 0;
}

static off_t fs_lseek(const char *path, off_t off, int whence, struct fuse_file_info *fi) {
    (void)fi; // No se usa el file info

    // Buscar inodo y verificar que sea archivo regular
    int idx = search_inodo_by_path(path);
    if (idx < 0 || inodos[idx].is_dir) {
        printf("[lseek] No encontrado o no es archivo regular: %s\n", path);
        return -ENOENT;
    }

    off_t result = 0;

    // Calcular nuevo offset según 'whence'
    switch (whence) {
        case SEEK_SET:
            result = off;
            break;
        case SEEK_CUR:
            result = off; // No se usa offset actual, se interpreta relativo
            break;
        case SEEK_END:
            result = inodos[idx].size + off;
            break;
        default:
            printf("[lseek] Whence no válido: %d\n", whence);
            return -EINVAL;
    }

    // Validar que el nuevo offset esté dentro del tamaño del archivo
    if (result < 0 || result > inodos[idx].size) {
        printf("[lseek] Offset fuera de rango: %ld para archivo %s (size %zu)\n", result, path, inodos[idx].size);
        return -EINVAL;
    }

    printf("[lseek] Nuevo offset: %ld para archivo %s\n", result, path);
    return result;
}

static int fs_truncate(const char *path, off_t size) {
    // Buscar inodo del archivo
    int inodo_idx = search_inodo_by_path(path);
    if (inodo_idx < 0 || inodos[inodo_idx].is_dir) {
        printf("[truncate] No encontrado o no es archivo regular: %s\n", path);
        return -ENOENT;
    }

    Inode *inodo = &inodos[inodo_idx];

    if (size == 0) {
        // Truncar a cero: liberar todas las regiones y resetear punteros
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
        // Recortar archivo: ajustar tamaños de fragmentos y liberar posteriores
        size_t remaining = size;
        for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) {
            if (inodo->block_pointers[i] == 0) continue;

            int block_id = inodo->block_pointers[i];
            int offset_bloque = inodo->block_offsets[i];

            for (int r = 0; r < MAX_REGIONES; r++) {
                if (regions_blocks[block_id][r].inodo_id == inodo_idx &&
                    regions_blocks[block_id][r].offset == offset_bloque) {
                    int bytes_in_frag = regions_blocks[block_id][r].size;

                    if (remaining >= bytes_in_frag) {
                        remaining -= bytes_in_frag;
                    } else {
                        // Truncar dentro del fragmento actual
                        regions_blocks[block_id][r].size = remaining;
                        used_bytes[block_id] = offset_bloque + remaining;
                        remaining = 0;
                    }

                    if (remaining == 0) {
                        // Liberar fragmentos posteriores
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
        // Expandir archivo llenando con ceros
        size_t to_fill = size - inodo->size;
        char *zero_buffer = calloc(to_fill, 1);
        if (!zero_buffer) {
            printf("[expand] Error al asignar memoria\n");
            return -ENOMEM;
        }
        fs_write(path, zero_buffer, to_fill, inodo->size, NULL);
        free(zero_buffer);
    }

    // Guardar cambios en disco
    char pathi[1000], pathbm[1000];
    snprintf(pathi, sizeof(pathi), "%s/block_0001.png", mount_folder);
    snprintf(pathbm, sizeof(pathbm), "%s/block_0002.png", mount_folder);
    write_struct_to_png(pathi, (uint8_t*)inodos, sizeof(inodos));
    write_struct_to_png(pathbm, bitmap, sizeof(bitmap));

    printf("[fsync] Cambios guardados en disco\n");
    return 0;
}