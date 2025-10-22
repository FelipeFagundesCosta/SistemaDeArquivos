#include "fs.h"
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>

/* ---- Variáveis globais ---- */
unsigned char *block_bitmap = NULL;
unsigned char *inode_bitmap = NULL;
inode_t *inode_table = NULL;
FILE *disk = NULL;

/* Layout do FS */
off_t off_block_bitmap = 0;
off_t off_inode_bitmap = 0;
off_t off_inode_table = 0;
off_t off_data_region = 0;

size_t computed_block_bitmap_bytes = 0;
size_t computed_inode_bitmap_bytes = 0;
size_t computed_inode_table_bytes = 0;
uint32_t computed_meta_blocks = 0;
uint32_t computed_data_blocks = 0;

/* ---- Funções utilitárias ---- */
size_t inode_bitmap_bytes(void) { return (MAX_INODES + 7) / 8; }
size_t inode_table_bytes(void) { return MAX_INODES * sizeof(inode_t); }
size_t block_bitmap_bytes(void) { return computed_block_bitmap_bytes; }

/* ---- Calcula layout do FS ---- */
static void compute_layout(void) {
    size_t inode_bmap_bytes = inode_bitmap_bytes();
    size_t inode_tbl_bytes = inode_table_bytes();

    /* Primeiro assumimos todos os blocos de dados disponíveis */
    size_t data_blocks = MAX_BLOCKS;

    /* Calcula bytes do bitmap de blocos */
    size_t bmap_bytes = (data_blocks + 7) / 8;

    /* Computa tamanhos */
    computed_block_bitmap_bytes = bmap_bytes;
    computed_inode_bitmap_bytes = inode_bmap_bytes;
    computed_inode_table_bytes = inode_tbl_bytes;

    /* Número de blocos ocupados pela meta-região */
    computed_meta_blocks = (computed_block_bitmap_bytes +
                            computed_inode_bitmap_bytes +
                            computed_inode_table_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

    /* Blocos de dados efetivos */
    computed_data_blocks = MAX_BLOCKS - computed_meta_blocks;

    /* Offsets */
    off_block_bitmap = sizeof(fs_header_t);
    off_inode_bitmap = off_block_bitmap + computed_block_bitmap_bytes;
    off_inode_table = off_inode_bitmap + computed_inode_bitmap_bytes;
    off_data_region = off_inode_table + computed_inode_table_bytes;
    off_data_region = ((off_data_region + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
}

/* ---- Persiste um inode específico no disco ---- */
void sync_inode(int inode_num) {
    if (!disk || !inode_table) return;
    fseek(disk, off_inode_table + inode_num * sizeof(inode_t), SEEK_SET);
    fwrite(&inode_table[inode_num], sizeof(inode_t), 1, disk);
    fflush(disk);
}

/* ---- Inicializa um novo filesystem ---- */
int init_fs(void) {
    if (access(DISK_NAME, F_OK) == 0) {
        printf("[INFO] Disco existente detectado. Montando FS...\n");
        return mount_fs();
    }

    printf("[INFO] Inicializando novo filesystem...\n");
    disk = fopen(DISK_NAME, "wb+");
    if (!disk) { perror("Erro ao criar disco"); return -1; }

    ftruncate(fileno(disk), DISK_SIZE_MB * 1024 * 1024);
    compute_layout();

    block_bitmap = calloc(1, computed_block_bitmap_bytes);
    inode_bitmap = calloc(1, computed_inode_bitmap_bytes);
    inode_table = calloc(MAX_INODES, sizeof(inode_t));
    if (!block_bitmap || !inode_bitmap || !inode_table) {
        perror("Erro ao alocar memória para FS");
        fclose(disk);
        return -1;
    }

    /* Cria diretório raiz */
    int root_inode = allocateInode();
    inode_table[root_inode].type = FILE_DIRECTORY;
    inode_table[root_inode].size = 0;
    inode_table[root_inode].creation_date = time(NULL);
    inode_table[root_inode].modification_date = time(NULL);
    inode_table[root_inode].permissions = PERM_ALL;
    strcpy(inode_table[root_inode].name, "~");
    strcpy(inode_table[root_inode].owner, "root");
    dirAddEntry(ROOT_INODE, ".", FILE_DIRECTORY, ROOT_INODE);
    dirAddEntry(ROOT_INODE, "..", FILE_DIRECTORY, ROOT_INODE);
    sync_inode(root_inode);

    /* Escreve header no disco */
    fs_header_t header = {0};
    header.magic = FS_MAGIC;
    header.block_bitmap_bytes = computed_block_bitmap_bytes;
    header.inode_bitmap_bytes = computed_inode_bitmap_bytes;
    header.inode_table_bytes = computed_inode_table_bytes;
    header.meta_blocks = computed_meta_blocks;
    header.data_blocks = computed_data_blocks;
    header.off_block_bitmap = off_block_bitmap;
    header.off_inode_bitmap = off_inode_bitmap;
    header.off_inode_table = off_inode_table;
    header.off_data_region = off_data_region;

    fseek(disk, 0, SEEK_SET);
    fwrite(&header, sizeof(header), 1, disk);
    fflush(disk);

    /* Escreve bitmaps e tabela de inodes */
    // bitmap de blocos
    fseek(disk, off_block_bitmap, SEEK_SET);
    fwrite(block_bitmap, 1, computed_block_bitmap_bytes, disk);

    // bitmap de inodes
    fseek(disk, off_inode_bitmap, SEEK_SET);
    fwrite(inode_bitmap, 1, computed_inode_bitmap_bytes, disk);

    // tabela de inodes
    fseek(disk, off_inode_table, SEEK_SET);
    fwrite(inode_table, 1, computed_inode_table_bytes, disk);

    printf("[INFO] Filesystem criado com sucesso.\n");
    return 0;
}

/* ---- Monta filesystem existente ---- */
int mount_fs(void) {
    printf("[INFO] Montando filesystem existente...\n");
    disk = fopen(DISK_NAME, "rb+");
    if (!disk) { perror("Erro ao abrir disco"); return -1; }

    fs_header_t header;
    fseek(disk, 0, SEEK_SET);
    if (fread(&header, sizeof(header), 1, disk) != 1) {
        fprintf(stderr, "Erro ao ler header do FS.\n");
        fclose(disk);
        return -1;
    }

    if (header.magic != FS_MAGIC) {
        fprintf(stderr, "Disco inválido ou corrompido.\n");
        fclose(disk);
        return -1;
    }

    /* Restaura variáveis globais */
    computed_block_bitmap_bytes = header.block_bitmap_bytes;
    computed_inode_bitmap_bytes = header.inode_bitmap_bytes;
    computed_inode_table_bytes = header.inode_table_bytes;
    computed_meta_blocks = header.meta_blocks;
    computed_data_blocks = header.data_blocks;
    off_block_bitmap = header.off_block_bitmap;
    off_inode_bitmap = header.off_inode_bitmap;
    off_inode_table = header.off_inode_table;
    off_data_region = header.off_data_region;

    /* Aloca memória */
    block_bitmap = malloc(computed_block_bitmap_bytes);
    inode_bitmap = malloc(computed_inode_bitmap_bytes);
    inode_table = malloc(computed_inode_table_bytes);
    if (!block_bitmap || !inode_bitmap || !inode_table) {
        perror("Erro ao alocar memória para FS");
        fclose(disk);
        return -1;
    }

    /* Lê conteúdo do disco */
    // bitmap de blocos
    fseek(disk, off_block_bitmap, SEEK_SET);
    fread(block_bitmap, 1, computed_block_bitmap_bytes, disk);

    // bitmap de inodes
    fseek(disk, off_inode_bitmap, SEEK_SET);
    fread(inode_bitmap, 1, computed_inode_bitmap_bytes, disk);

    // tabela de inodes
    fseek(disk, off_inode_table, SEEK_SET);
    fread(inode_table, 1, computed_inode_table_bytes, disk);


    printf("[INFO] Filesystem montado com sucesso!\n");
    return 0;
}

/* ---- Sincroniza FS inteiro ---- */
int sync_fs(void) {
    if (!disk || !block_bitmap || !inode_bitmap || !inode_table) return -1;
    fseek(disk, off_block_bitmap, SEEK_SET);
    fwrite(block_bitmap, 1, computed_block_bitmap_bytes, disk);

    fseek(disk, off_inode_bitmap, SEEK_SET);
    fwrite(inode_bitmap, 1, computed_inode_bitmap_bytes, disk);

    fseek(disk, off_inode_table, SEEK_SET);
    fwrite(inode_table, 1, computed_inode_table_bytes, disk);

    fflush(disk);
    fsync(fileno(disk));

    return 0;
}

/* ---- Desmonta FS ---- */
int unmount_fs(void) {
    sync_fs();
    free(block_bitmap); block_bitmap = NULL;
    free(inode_bitmap); inode_bitmap = NULL;
    free(inode_table); inode_table = NULL;
    if (disk) { fclose(disk); disk = NULL; }
    return 0;
}

/* ----- Utilitarios --------*/




const char *format_time(time_t t, char *buf, size_t buflen) {
    if (!buf || buflen == 0) return NULL;
    struct tm tm;
    if (localtime_r(&t, &tm) == NULL) {
        buf[0] = '\0';
        return buf;
    }
    strftime(buf, buflen, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}




int show_inode_info(int inode_index) {
    if (!inode_table) return -1;
    if (inode_index < 0 || inode_index >= MAX_INODES) return -1;

    inode_t *ino = &inode_table[inode_index];
    char ctime_buf[64] = {0}, mtime_buf[64] = {0};
    format_time(ino->creation_date, ctime_buf, sizeof(ctime_buf));
    format_time(ino->modification_date, mtime_buf, sizeof(mtime_buf));

    const char *type_str = "unknown";
    if (ino->type == FILE_REGULAR) type_str = "regular file";
    else if (ino->type == FILE_DIRECTORY) type_str = "directory";
    else if (ino->type == FILE_SYMLINK) type_str = "symlink";

    // Monta string de permissões rwxrwxrwx
    char perm_str[10] = "---------";
    int pos = 0;
    for (int who = 6; who >= 0; who -= 3) {
        perm_str[pos++] = (ino->permissions & (PERM_READ << who)) ? 'r' : '-';
        perm_str[pos++] = (ino->permissions & (PERM_WRITE << who)) ? 'w' : '-';
        perm_str[pos++] = (ino->permissions & (PERM_EXEC << who)) ? 'x' : '-';
    }
    perm_str[9] = '\0';

    printf("Inode %d:\n", inode_index);
    printf("  name: %s\n", ino->name);
    printf("  type: %s\n", type_str);
    printf("  creator: %s\n", ino->creator);
    printf("  owner: %s\n", ino->owner);
    printf("  size: %u bytes\n", ino->size);
    printf("  permissions: %s (0%o)\n", perm_str, (unsigned)ino->permissions);
    printf("  created: %s\n", ctime_buf);
    printf("  modified: %s\n", mtime_buf);
    if (ino->type == FILE_SYMLINK) {
        printf("  symlink -> inode %u\n", ino->link_target_index);
    }
    printf("  blocks:");
    for (int i = 0; i < BLOCKS_PER_INODE; ++i) {
        if (ino->blocks[i] != 0)
            printf(" %u", ino->blocks[i]);
    }
    if (ino->next_inode != 0) printf("  (next inode: %u)", ino->next_inode);
    printf("\n");

    return 0;
}



/* ---- alocação ---- */
int allocateBlock(void) {
    for (uint32_t i = 0; i < computed_data_blocks; i++){
        uint32_t byte = i / 8;
        uint8_t bit = i % 8;

        if ((block_bitmap[byte] & (1 << bit)) == 0) {
            block_bitmap[byte] |= (1 << bit);
            return i;
        }
    }
    return -1;
}

void freeBlock(int block_index) {
    if (block_index >= 0 && block_index < (int)computed_data_blocks) {
        uint32_t byte = block_index / 8;
        uint8_t bit = block_index % 8;
        block_bitmap[byte] &= ~(1 << bit);
    }
}

int allocateInode(void) {
    for (uint32_t i = 0; i < MAX_INODES; i++) {
        uint32_t byte = i / 8;
        uint8_t bit = i % 8;

        if ((inode_bitmap[byte] & (1 << bit)) == 0) {
            inode_bitmap[byte] |= (1 << bit);
            memset(&inode_table[i], 0, sizeof(inode_t));
            inode_table[i].next_inode = 0;
            return i;
        }
    }
    return -1;
}

void freeInode(int inode_index) {
    if (inode_index >= 0 && inode_index < MAX_INODES){
        uint32_t byte = inode_index / 8;
        uint8_t bit = inode_index % 8;
        inode_bitmap[byte] &= ~(1 << bit);
        inode_table[inode_index].type = 0;
        memset(inode_table[inode_index].blocks, 0, sizeof(inode_table[inode_index].blocks));
        inode_table[inode_index].next_inode = 0;
    }
}

/* ---- leitura e escrita ---- */
int readBlock(uint32_t block_index, void *buffer){
    if (!disk || block_index >= computed_data_blocks) return -1;
    off_t offset = off_data_region + (off_t)block_index * BLOCK_SIZE;
    fseek(disk, offset, SEEK_SET);
    size_t read_bytes = fread(buffer, 1, BLOCK_SIZE, disk);
    return (read_bytes == BLOCK_SIZE) ? 0 : -1;
}

int writeBlock(uint32_t block_index, const void *buffer){
    if (!disk || block_index >= computed_data_blocks) return -1;
    off_t offset = off_data_region + (off_t)block_index * BLOCK_SIZE;
    fseek(disk, offset, SEEK_SET);
    size_t written_bytes = fwrite(buffer, 1, BLOCK_SIZE, disk);
    fflush(disk);
    fsync(fileno(disk));
    return (written_bytes == BLOCK_SIZE) ? 0 : -1;
}

/* ---- diretórios ---- */
int dirFindEntry(int dir_inode, const char *name, inode_type_t type, int *out_inode) {
    if (dir_inode < 0 || dir_inode >= MAX_INODES || !name || !out_inode) 
        return -1;
    

    if (strlen(name) >= sizeof(((dir_entry_t*)0)->name)) {
        // nome muito grande para o campo do dir_entry_t
        return -1;
    }

    int current_inode = dir_inode;

    while (current_inode >= 0) {
        inode_t *dir = &inode_table[current_inode];
        if (dir->type != FILE_DIRECTORY) return -1;        

        for (int i = 0; i < BLOCKS_PER_INODE; i++) {
            if (dir->blocks[i] == 0) continue;

            // buffer alocado dinamicamente para não sobrecarregar a pilha
            dir_entry_t *buffer = malloc(BLOCK_SIZE);
            if (!buffer) return -1;

            if (readBlock(dir->blocks[i], buffer) != 0) {
                free(buffer);
                return -1;
            }

            int entries = BLOCK_SIZE / sizeof(dir_entry_t);
            for (int j = 0; j < entries; j++) {
                if (strcmp(buffer[j].name, name) == 0 &&
                    (inode_table[buffer[j].inode_index].type == type || type == FILE_SYMLINK || type == FILE_ANY)) {
                        
                    *out_inode = buffer[j].inode_index;
                    free(buffer);
                    return 0;
                }
            }

            free(buffer);
        }

        if (dir->next_inode == 0)
            break;

        current_inode = dir->next_inode;
    }

    return -1;
}


int dirAddEntry(int dir_inode, const char *name, inode_type_t type, int inode_index) {
    if (dir_inode < 0 || dir_inode >= MAX_INODES || !name)
        return -1;

    // evita duplicados
    int found;
    if (dirFindEntry(dir_inode, name, type, &found) == 0)
        return -1;

    int current_inode = dir_inode;

    while (current_inode >= 0) {
        inode_t *dir = &inode_table[current_inode];
        if (dir->type != FILE_DIRECTORY)
            return -1;

        // aloca buffers no heap
        dir_entry_t *buffer = malloc(BLOCK_SIZE);
        dir_entry_t *empty = malloc(BLOCK_SIZE);
        if (!buffer || !empty) {
            free(buffer);
            free(empty);
            return -1;
        }
        memset(empty, 0, BLOCK_SIZE);

        // tenta colocar em todos os blocos existentes
        for (int i = 2; i < BLOCKS_PER_INODE; i++) {
            if (dir->blocks[i] == 0) {
                // se bloco não existe, aloca
                int new_block = allocateBlock();
                if (new_block < 0) {
                    free(buffer);
                    free(empty);
                    return -1;
                }
                dir->blocks[i] = new_block;
                if (writeBlock(new_block, empty) != 0) {
                    free(buffer);
                    free(empty);
                    return -1;
                }
            }

            if (readBlock(dir->blocks[i], buffer) != 0) {
                free(buffer);
                free(empty);
                return -1;
            }

            for (int j = 0; j < BLOCK_SIZE / sizeof(dir_entry_t); j++) {
                if (buffer[j].inode_index == 0) {
                    strncpy(buffer[j].name, name, sizeof(buffer[j].name) - 1);
                    buffer[j].name[sizeof(buffer[j].name) - 1] = '\0';
                    buffer[j].inode_index = inode_index;

                    if (writeBlock(dir->blocks[i], buffer) != 0) {
                        free(buffer);
                        free(empty);
                        return -1;
                    }

                    dir->size += sizeof(dir_entry_t);
                    dir->modification_date = time(NULL);

                    free(buffer);
                    free(empty);
                    return 0;
                }
            }
        }

        // todos os blocos do inode cheio → cria next_inode
        if (dir->next_inode == 0) {
            int next = allocateInode();
            if (next < 0) {
                free(buffer);
                free(empty);
                return -1;
            }

            inode_t *next_inode = &inode_table[next];
            memset(next_inode, 0, sizeof(inode_t));
            next_inode->type = FILE_DIRECTORY;

            int new_block = allocateBlock();
            if (new_block < 0) {
                free(buffer);
                free(empty);
                return -1;
            }

            next_inode->blocks[0] = new_block;
            if (writeBlock(new_block, empty) != 0) {
                free(buffer);
                free(empty);
                return -1;
            }

            dir->next_inode = next;
        }

        free(buffer);
        free(empty);
        current_inode = dir->next_inode;
    }

    return -1;
}




int dirRemoveEntry(int dir_inode, const char *name, inode_type_t type) {
    if (dir_inode < 0 || dir_inode >= MAX_INODES || !name)
        return -1;

    int current_inode = dir_inode;

    while (current_inode >= 0) {
        inode_t *dir = &inode_table[current_inode];
        if (dir->type != FILE_DIRECTORY)
            return -1;

        // buffer dinâmico
        dir_entry_t *buffer = malloc(BLOCK_SIZE);
        if (!buffer)
            return -1;

        for (int i = 0; i < BLOCKS_PER_INODE; i++) {
            uint32_t block_index = dir->blocks[i];
            if (block_index == 0)
                continue;

            if (readBlock(block_index, buffer) != 0) {
                free(buffer);
                return -1;
            }

            for (int j = 0; j < BLOCK_SIZE / sizeof(dir_entry_t); j++) {
                if (buffer[j].inode_index != 0 && strcmp(buffer[j].name, name) == 0) {
                    int target_inode = buffer[j].inode_index;

                    // limpa entrada
                    buffer[j].inode_index = 0;
                    buffer[j].name[0] = '\0';

                    if (writeBlock(block_index, buffer) != 0) {
                        free(buffer);
                        return -1;
                    }

                    // limpa dados do inode alvo
                    inode_t *target = &inode_table[target_inode];
                    for (int k = 0; k < BLOCKS_PER_INODE; k++) {
                        if (target->blocks[k] != 0) {
                            freeBlock(target->blocks[k]);
                            target->blocks[k] = 0;
                        }
                    }
                    freeInode(target_inode);

                    inode_table[dir_inode].size -= sizeof(dir_entry_t);
                    inode_table[dir_inode].modification_date = time(NULL);

                    free(buffer);
                    return 0;
                }
            }
        }

        free(buffer);
        current_inode = dir->next_inode;
    }

    return -1;
}


int hasPermission(const inode_t *inode, const char *username, permission_t perm) {
    if (strcmp(inode->owner, username) == 0) {
        return ((inode->permissions >> 6) & PERM_RWX) & perm;
    } else {
        return (inode->permissions & PERM_RWX) & perm;
    }
}


int createDirectory(int parent_inode, const char *name, const char *user){
    if (parent_inode < 0 || parent_inode >= MAX_INODES || !name || !user) return -1;
    int dummy_output;
    if (dirFindEntry(parent_inode, name, FILE_DIRECTORY, &dummy_output) == 0) return -1;

    inode_t *parent= &inode_table[parent_inode];
    if (parent_inode != ROOT_INODE){
    if (!hasPermission(parent, user, PERM_WRITE)) return -1;
    }

    int new_inode_index = allocateInode();
    if (new_inode_index < 0) return -1;

    inode_t *new_inode = &inode_table[new_inode_index];

    time_t now = time(NULL);

    new_inode->type = FILE_DIRECTORY;
    strncpy(new_inode->name, name, MAX_NAMESIZE-1);
    new_inode->name[MAX_NAMESIZE-1] = '\0';
    new_inode->creation_date = now;
    new_inode->modification_date = now;
    new_inode->size = 0;
    strncpy(new_inode->creator, user, MAX_NAMESIZE-1);
    new_inode->creator[MAX_NAMESIZE-1] = '\0';
    strncpy(new_inode->owner, user, MAX_NAMESIZE-1);
    new_inode->owner[MAX_NAMESIZE-1] = '\0';
    new_inode->permissions = PERM_RWX << 6 | PERM_RX << 3 | PERM_RX;
    new_inode->link_target_index = -1;

    int block = allocateBlock();
    if (block < 0) return -1;
    new_inode->blocks[0] = block;

    dir_entry_t entries[BLOCK_SIZE / sizeof(dir_entry_t)] = {0};

    strncpy(entries[0].name, ".", sizeof(entries[0].name));
    entries[0].inode_index = new_inode_index;
    strncpy(entries[1].name, "..", sizeof(entries[1].name));
    entries[1].inode_index = parent_inode;

    if (writeBlock(block, entries) != 0) return -1;
    
    if (dirAddEntry(parent_inode, name, FILE_DIRECTORY, new_inode_index) != 0) return -1;
    sync_fs();
    return 0;
}

int deleteDirectory(int parent_inode, const char *name, const char *user){
    if (parent_inode < 0 || parent_inode >= MAX_INODES || !name) return -1;
    int target_inode;
    if (dirFindEntry(parent_inode, name, FILE_DIRECTORY, &target_inode) != 0) return -1;

    if (parent_inode == ROOT_INODE) return -1;
    inode_t *inode = &inode_table[target_inode];
    if (!hasPermission(inode, user, PERM_WRITE)) return -1;

    inode_t *target = &inode_table[target_inode];
    if (target->type != FILE_DIRECTORY) return -1;

    for (int i = 0; i < BLOCKS_PER_INODE; i++) {
        if (target->blocks[i] == 0) continue;
        dir_entry_t entries[BLOCK_SIZE / sizeof(dir_entry_t)];
        if (readBlock(target->blocks[i], entries) != 0) return -1;
        for (int j = 0; j < BLOCK_SIZE / sizeof(dir_entry_t); j++) {
            if (entries[j].inode_index != 0 &&
                strcmp(entries[j].name, ".") != 0 &&
                strcmp(entries[j].name, "..") != 0) {
                return -1;
            }
        }
    }

    if (dirRemoveEntry(parent_inode, name, FILE_DIRECTORY) != 0) return -1;
    freeInode(target_inode);
    sync_fs();
    return 0;

}

int createFile(int parent_inode, const char *name, const char *user){
    if (parent_inode < 0 || parent_inode >= MAX_INODES || !name) return -1;
    int dummy_output;
    if (dirFindEntry(parent_inode, name, FILE_REGULAR, &dummy_output) == 0) return -1;

    inode_t *parent= &inode_table[parent_inode];
    if (parent_inode != ROOT_INODE){
    if (!hasPermission(parent, user, PERM_WRITE)) return -1;
    }

    int new_inode_index = allocateInode();
    if (new_inode_index < 0) return -1;
    inode_t *new_inode = &inode_table[new_inode_index];

    time_t now = time(NULL);

    new_inode->type = FILE_REGULAR;
    strncpy(new_inode->name, name, MAX_NAMESIZE-1);
    new_inode->name[MAX_NAMESIZE-1] = '\0';
    new_inode->creation_date = now;
    new_inode->modification_date = now;
    new_inode->size = 0;
    strncpy(new_inode->creator, user, MAX_NAMESIZE-1);
    new_inode->creator[MAX_NAMESIZE-1] = '\0';
    strncpy(new_inode->owner, user, MAX_NAMESIZE-1);
    new_inode->owner[MAX_NAMESIZE-1] = '\0';
    new_inode->permissions = PERM_RWX << 6 | PERM_RX << 3 | PERM_RX;
    new_inode->link_target_index = -1;

    if (dirAddEntry(parent_inode, name, FILE_REGULAR, new_inode_index) != 0) return -1;
    sync_fs();
    return 0;
}

int deleteFile(int parent_inode, const char *name, const char *user){
    if (parent_inode < 0 || parent_inode >= MAX_INODES || !name) return -1;
    int target_inode;
    if (dirFindEntry(parent_inode, name, FILE_REGULAR, &target_inode) == -1) return -1;

    inode_t *target = &inode_table[target_inode];
    if (!hasPermission(target, user, PERM_WRITE)) return -1;

    if (target->type != FILE_REGULAR && target->type != FILE_SYMLINK) return -1;

    for (int i = 0; i < BLOCKS_PER_INODE; i++) {
        if (target->blocks[i] != 0)
            freeBlock(target->blocks[i]);
    }

    if (dirRemoveEntry(parent_inode, name, target->type) == -1) return -1;
    freeInode(target_inode);
    sync_fs();
    return 0;
}

fs_dir_list_t listElements(int parent_inode) {
    fs_dir_list_t result = {NULL, 0};

    if (parent_inode < 0 || parent_inode >= MAX_INODES) return result;

    inode_t *dir = &inode_table[parent_inode];
    int depth = 0;
    while (dir->type == FILE_SYMLINK){
        dir = &inode_table[dir->link_target_index];
        if (++depth > 16) return result;
    }
    if (dir->type != FILE_DIRECTORY) return result;

    int capacity = 16; // capacidade inicial do array
    fs_entry_t *temp = calloc(capacity, sizeof(fs_entry_t));
    if (!temp) return result;

    int count = 0;
    int current_inode = parent_inode;

    while (current_inode >= 0) {
        dir = &inode_table[current_inode];

        for (int i = 0; i < BLOCKS_PER_INODE; i++) {
            uint32_t block_index = dir->blocks[i];
            if (block_index == 0) continue;

            dir_entry_t entries[BLOCK_SIZE / sizeof(dir_entry_t)];
            if (readBlock(block_index, entries) != 0) continue;

            for (int j = 0; j < BLOCK_SIZE / sizeof(dir_entry_t); j++) {
                if (entries[j].inode_index == 0) continue;
                if (strcmp(entries[j].name, ".") == 0 || strcmp(entries[j].name, "..") == 0) continue;

                inode_t *child = &inode_table[entries[j].inode_index];

                if (count >= capacity) {
                    capacity *= 2;
                    fs_entry_t *new_temp = realloc(temp, capacity * sizeof(fs_entry_t));
                    if (!new_temp) {
                        free(temp);
                        result.entries = NULL;
                        result.count = 0;
                        return result;
                    }
                    temp = new_temp;
                }

                fs_entry_t *entry = &temp[count++];
                entry->inode_index = entries[j].inode_index;
                entry->type = child->type;

                strncpy(entry->name, child->name, MAX_NAMESIZE - 1);
                entry->name[MAX_NAMESIZE - 1] = '\0';

                strncpy(entry->creator, child->creator, MAX_NAMESIZE - 1);
                entry->creator[MAX_NAMESIZE - 1] = '\0';

                strncpy(entry->owner, child->owner, MAX_NAMESIZE - 1);
                entry->owner[MAX_NAMESIZE - 1] = '\0';

                entry->size = child->size;
                entry->permissions = child->permissions;
                entry->creation_date = child->creation_date;
                entry->modification_date = child->modification_date;
            }
        }

        if (dir->next_inode == 0) break;
        current_inode = dir->next_inode;
    }

    result.entries = temp;
    result.count = count;
    return result;
}

int addContentToInode(int inode_number, const char *data, size_t data_size, const char *user) {
    if (!data || !user) return -1;

    inode_t *inode = &inode_table[inode_number];

    // Permissão de escrita
    if (!hasPermission(inode, user, PERM_WRITE)) return -1;

    size_t offset = inode->size; // onde começar a escrever
    size_t written = 0;
    inode_t *current = inode;

    // Vai até o último inode encadeado
    while (current->next_inode != 0) {
        current = &inode_table[current->next_inode];
    }

    while (written < data_size) {
        // Procura por um bloco vazio ou parcialmente usado
        int block_idx = -1;
        for (int i = 0; i < BLOCKS_PER_INODE; i++) {
            if (current->blocks[i] == 0) {
                block_idx = i;
                break;
            }
        }

        // Se não houver bloco disponível, criar novo inode
        if (block_idx == -1) {
            int new_inode = allocateInode();
            if (new_inode < 0) return -1; // sem inodes disponíveis
            current->next_inode = new_inode;
            current = &inode_table[new_inode];
            block_idx = 0;
        }

        // Aloca novo bloco se necessário
        if (current->blocks[block_idx] == 0) {
            int new_block = allocateBlock();
            if (new_block <= 0) return -1; // sem blocos disponíveis
            current->blocks[block_idx] = new_block;
        }

        // Escreve no bloco
        char block_buffer[BLOCK_SIZE] = {0};
        size_t to_write = BLOCK_SIZE;
        if (data_size - written < to_write) to_write = data_size - written;

        memcpy(block_buffer, data + written, to_write);
        if (writeBlock(current->blocks[block_idx], block_buffer) != 0) return -1;

        written += to_write;
        offset += to_write;
    }

    inode->size = offset; // atualiza tamanho do arquivo
    sync_fs();
    return 0;
}


ssize_t getFileSize(int parent_inode, const char *name) {
    int file_inode_index;
    if (dirFindEntry(parent_inode, name, FILE_REGULAR, &file_inode_index) != 0)
        return -1;

    inode_t *inode = &inode_table[file_inode_index];
    if (!inode) return -1;

    return inode->size;
}

int readContentFromInode(int inode_number, char *buffer, size_t buffer_size, size_t *out_bytes, const char *user) {
    if (!buffer || !out_bytes || !user) return -1;

    int target_inode = inode_number;
    int depth = 0;

    // Segue links simbólicos, com limite de 16
    while (inode_table[target_inode].type == FILE_SYMLINK) {
        target_inode = inode_table[target_inode].link_target_index;
        if (++depth > 16) return -1; // evita loop infinito
    }

    inode_t *inode = &inode_table[target_inode];
    if (!inode || !hasPermission(inode, user, PERM_READ)) return -1;

    size_t total_size = inode->size;
    if (buffer_size < total_size + 1) return -1; // espaço para '\0'

    size_t offset = 0;
    inode_t *current = inode;

    while (current) {
        for (int i = 0; i < BLOCKS_PER_INODE; i++) {
            if (current->blocks[i] == 0) continue;

            char block_buffer[BLOCK_SIZE];
            if (readBlock(current->blocks[i], block_buffer) != 0) return -1;

            size_t to_copy = BLOCK_SIZE;
            if (offset + to_copy > total_size) to_copy = total_size - offset;

            memcpy(buffer + offset, block_buffer, to_copy);
            offset += to_copy;

            if (offset >= total_size) break; // já leu todo o arquivo
        }

        if (offset >= total_size) break;

        if (current->next_inode != 0) {
            current = &inode_table[current->next_inode];
        } else {
            current = NULL;
        }
    }

    buffer[offset] = '\0';
    *out_bytes = offset;
    return 0;
}

int resolvePath(const char *path, int current_inode, int *inode_out) {
    if (!path || !inode_out) return -1;

    int current = current_inode;

    // Caminho absoluto
    if (path[0] == '~') {
        current = ROOT_INODE;
        path++; // pula o '~'
        if (*path == '/') path++; // pula barra inicial
    }

    char token[256];
    const char *p = path;
    while (*p) {
        int i = 0;
        while (*p && *p != '/') token[i++] = *p++;
        token[i] = '\0';
        if (*p == '/') p++; // pula barra

        if (strcmp(token, ".") == 0 || token[0] == '\0') continue;

        if (strcmp(token, "..") == 0) {
            int parent_inode;
            if (dirFindEntry(current, "..", FILE_DIRECTORY, &parent_inode) != 0) return -1;
            current = parent_inode;
            continue;
        }
        
        int next_inode;
        const char *next_slash = strchr(p, '/');
        int type = next_slash ? FILE_DIRECTORY : FILE_ANY;

        if (dirFindEntry(current, token, type, &next_inode) != 0) return -1;

        int depth = 0;
        while (inode_table[next_inode].type == FILE_SYMLINK) {
            next_inode = inode_table[next_inode].link_target_index;
            if (++depth > 16) return -1; // evita loops
        }

        current = next_inode;
    }

    *inode_out = current;
    return 0;
}


int createSymlink(int parent_inode, int target_index, const char *link_name, const char *user) {
    // 1. Verifica se link_name já existe
    int dummy_output;
    if (!dirFindEntry(parent_inode, link_name, FILE_SYMLINK, &dummy_output)) return -1; // erro, já existe
    

    inode_t *parent = &inode_table[parent_inode];
    if (!hasPermission(parent, user, PERM_WRITE)) return -1;

    // 2. Aloca um novo i-node
    int inode_index = allocateInode();
    inode_t *inode = &inode_table[inode_index];
    if (!inode) return -1;

    // 3. Preenche campos
    strncpy(inode->name, link_name, MAX_NAMESIZE-1);
    inode->name[MAX_NAMESIZE-1] = '\0';
    inode->type = FILE_SYMLINK;
    inode->size = 0;
    inode->link_target_index = target_index;
    inode->creation_date = time(NULL);
    inode->modification_date = inode->creation_date;
    strncpy(inode->creator, user, MAX_NAMESIZE-1);
    inode->creator[MAX_NAMESIZE-1] = '\0';
    strncpy(inode->owner, user, MAX_NAMESIZE-1);
    inode->owner[MAX_NAMESIZE-1] = '\0';
    inode->permissions = inode_table[target_index].permissions;

    if (dirAddEntry(parent_inode, link_name, FILE_SYMLINK, inode_index) != 0){
        freeInode(inode_index);
        return -1;
    }

    sync_inode(inode_index);
    return 0;
}

int createDirectoriesRecursively(const char *path, int current_inode, const char *user) {
    if (!path || !user) return -1;
    if (path[0] == '\0') return -1;

    // Se path == "." nada a fazer
    if (strcmp(path, ".") == 0) return 0;

    // Vamos caminhar token por token, tentando resolver cada nível e criando quando não existir.
    int cur = current_inode;

    // Se começa com '~' (você já trata isso no resolvePath), trate como absoluto
    const char *p = path;
    if (p[0] == '~') {
        cur = ROOT_INODE;
        p++;
        if (*p == '/') p++;
    }

    char token[256];
    while (*p) {
        int i = 0;
        // extrai token até '/' ou fim
        while (*p && *p != '/' && i < (int)sizeof(token)-1) token[i++] = *p++;
        token[i] = '\0';
        if (*p == '/') p++; // pula '/'

        if (token[0] == '\0' || strcmp(token, ".") == 0) continue;
        if (strcmp(token, "..") == 0) {
            int parent_inode;
            if (dirFindEntry(cur, "..", FILE_DIRECTORY, &parent_inode) != 0) return -1;
            cur = parent_inode;
            continue;
        }

        int next_inode;
        // tentamos achar token no diretório atual (aceitamos FILE_DIRECTORY ou FILE_SYMLINK -> seguido)
        if (dirFindEntry(cur, token, FILE_DIRECTORY, &next_inode) != 0) {
            // não existe -> criar diretório aqui
            if (createDirectory(cur, token, user) != 0) {
                return -1;
            }
            // recuperar inode do diretório criado
            if (dirFindEntry(cur, token, FILE_DIRECTORY, &next_inode) != 0) return -1;
        }

        // se for symlink, resolva link_target_index (resolvePath já faz isso, mas como estamos passo a passo:)
        int depth = 0;
        while (inode_table[next_inode].type == FILE_SYMLINK) {
            next_inode = inode_table[next_inode].link_target_index;
            if (++depth > 16) return -1;
        }

        cur = next_inode;
    }

    return 0;
}

/* ---- Comandos de FS ---- */

// cd
int cmd_cd(int *current_inode, const char *path) {
    if (!current_inode || !path) return -1;

    int target_inode;
    if (resolvePath(path, *current_inode, &target_inode) != 0) return -1;

    inode_t *inode = &inode_table[target_inode];
    if (inode->type != FILE_DIRECTORY) return -1;

    *current_inode = target_inode;
    return 0;
}

// Função auxiliar para separar caminho e último segmento
static void splitPath(const char *full_path, char *dir_path, char *base_name) {
    const char *last_slash = strrchr(full_path, '/');
    if (last_slash) {
        size_t dir_len = last_slash - full_path;
        strncpy(dir_path, full_path, dir_len);
        dir_path[dir_len] = '\0';
        strcpy(base_name, last_slash + 1);
    } else {
        strcpy(dir_path, "."); // diretório atual
        strcpy(base_name, full_path);
    }
}

// mkdir com criação recursiva (estilo cp)
int cmd_mkdir(int current_inode, const char *full_path, const char *user) {
    if (!full_path || !user) return -1;

    char dir_path[256], name[256];
    splitPath(full_path, dir_path, name);

    int parent_inode;
    if (resolvePath(dir_path, current_inode, &parent_inode) != 0) {
        if (createDirectoriesRecursively(dir_path, current_inode, user) != 0) return -1;
        if (resolvePath(dir_path, current_inode, &parent_inode) != 0) return -1;
    }

    return createDirectory(parent_inode, name, user);
}

// touch com criação recursiva
int cmd_touch(int current_inode, const char *full_path, const char *user) {
    if (!full_path || !user) return -1;

    char dir_path[256], name[256];
    splitPath(full_path, dir_path, name);

    int parent_inode;
    if (resolvePath(dir_path, current_inode, &parent_inode) != 0) {
        if (createDirectoriesRecursively(dir_path, current_inode, user) != 0) return -1;
        if (resolvePath(dir_path, current_inode, &parent_inode) != 0) return -1;
    }

    return createFile(parent_inode, name, user);
}

// echo > (sobrescreve conteúdo) com criação recursiva
int cmd_echo_arrow(int current_inode, const char *full_path, const char *content, const char *user) {
    if (!full_path || !content || !user) return -1;

    char dir_path[256], name[256];
    splitPath(full_path, dir_path, name);

    int parent_inode;
    if (resolvePath(dir_path, current_inode, &parent_inode) != 0) {
        if (createDirectoriesRecursively(dir_path, current_inode, user) != 0) return -1;
        if (resolvePath(dir_path, current_inode, &parent_inode) != 0) return -1;
    }

    int inode_index;
    if (dirFindEntry(parent_inode, name, FILE_REGULAR, &inode_index) != 0) {
        if (createFile(parent_inode, name, user) != 0) return -1;
        if (dirFindEntry(parent_inode, name, FILE_REGULAR, &inode_index) != 0) return -1;
    }

    inode_t *inode = &inode_table[inode_index];
    inode->size = 0;
    inode->next_inode = 0;
    for (int i = 0; i < BLOCKS_PER_INODE; i++) inode->blocks[i] = 0;

    return addContentToInode(inode_index, content, strlen(content), user);
}

// echo >> (anexa conteúdo) com criação recursiva
int cmd_echo_arrow_arrow(int current_inode, const char *full_path, const char *content, const char *user) {
    if (!full_path || !content || !user) return -1;


    char dir_path[256], name[256];
    splitPath(full_path, dir_path, name);

    int parent_inode;
    if (resolvePath(dir_path, current_inode, &parent_inode) != 0) {
        if (createDirectoriesRecursively(dir_path, current_inode, user) != 0) return -1;
        if (resolvePath(dir_path, current_inode, &parent_inode) != 0) return -1;
    }

    int inode_index;
    if (dirFindEntry(parent_inode, name, FILE_REGULAR, &inode_index) != 0) {
        if (createFile(parent_inode, name, user) != 0) return -1;
        if (dirFindEntry(parent_inode, name, FILE_REGULAR, &inode_index) != 0) return -1;
    }

    return addContentToInode(inode_index, content, strlen(content), user);
}


// cat
int cmd_cat(int current_inode, const char *path, const char *user) {
    if (!path || !user) return -1;

    // resolve o inode do arquivo
    int target_inode;
    if (resolvePath(path, current_inode, &target_inode) != 0)
        return -1;

    // procura o inode pelo indice
    inode_t *inode = &inode_table[target_inode];
    if (!inode || inode->type != FILE_REGULAR) {
        fprintf(stderr, "Erro: %s não é um arquivo regular.\n", path);
        return -1;
    }

    // assegura que há permissão para leitura
    if (!hasPermission(inode, user, PERM_READ)) {
        fprintf(stderr, "Erro: permissão negada para %s.\n", path);
        return -1;
    }

    size_t filesize = inode->size;
    if (filesize == 0) return 0; // arquivo vazio

    char *buffer = malloc(filesize + 1);
    if (!buffer) return -1;

    // Lê arquivo
    size_t bytes_read = 0;
    if (readContentFromInode(target_inode, buffer, filesize + 1, &bytes_read, user) != 0) {
        free(buffer);
        return -1;
    }

    buffer[bytes_read] = '\0';
    printf("%s\n", buffer);

    free(buffer);
    return 0;
}

// Versão robusta de cmd_cp.
// src_path/dst_path podem ser "." para indicar diretório atual.
// src_name / dst_name podem conter barras (ex: "subdir/file") — nesse caso a parte de diretório será usada.
int cmd_cp(int current_inode, const char *src_path, const char *src_name,
           const char *dst_path, const char *dst_name, const char *user) {
    if (!src_name || !dst_name || !user) return -1;

    int src_parent_inode = current_inode;
    int dst_parent_inode = current_inode;
    const char *src_base = src_name;
    const char *dst_base = dst_name;

    // Se src_name contem '/', separe dir e base
    char tmpbuf[256];
    const char *slash = strrchr(src_name, '/');
    if (slash) {
        size_t dirlen = slash - src_name;
        if (dirlen >= sizeof(tmpbuf)) return -1;
        strncpy(tmpbuf, src_name, dirlen);
        tmpbuf[dirlen] = '\0';
        src_base = slash + 1;
        if (resolvePath(tmpbuf, current_inode, &src_parent_inode) != 0) return -1;
    } else {
        // se foi passado um src_path != "." resolva-o
        if (src_path && src_path[0] != '\0' && strcmp(src_path, ".") != 0) {
            if (resolvePath(src_path, current_inode, &src_parent_inode) != 0) return -1;
        }
    }

    // Processa dst: se dst_name contem '/', separe dir e base
    slash = strrchr(dst_name, '/');
    if (slash) {
        size_t dirlen = slash - dst_name;
        if (dirlen >= sizeof(tmpbuf)) return -1;
        strncpy(tmpbuf, dst_name, dirlen);
        tmpbuf[dirlen] = '\0';
        dst_base = slash + 1;

        // tenta resolver o diretório; se não existir, tenta criar recursivamente
        if (resolvePath(tmpbuf, current_inode, &dst_parent_inode) != 0) {
            if (createDirectoriesRecursively(tmpbuf, current_inode, user) != 0) return -1;
            if (resolvePath(tmpbuf, current_inode, &dst_parent_inode) != 0) return -1;
        }
    } else {
        // se foi passado um dst_path != "." resolva-o (p.ex.: cp arq dir/arq2 onde dst_path veio como "dir")
        if (dst_path && dst_path[0] != '\0' && strcmp(dst_path, ".") != 0) {
            if (resolvePath(dst_path, current_inode, &dst_parent_inode) != 0) {
                // tentar criar o caminho de destino
                if (createDirectoriesRecursively(dst_path, current_inode, user) != 0) return -1;
                if (resolvePath(dst_path, current_inode, &dst_parent_inode) != 0) return -1;
            }
        }
    }

    // Agora temos src_parent_inode + src_base e dst_parent_inode + dst_base
    int src_file_inode;
    if (dirFindEntry(src_parent_inode, src_base, FILE_REGULAR, &src_file_inode) != 0) {
        // tenta também aceitar symlink/any (se quiser copiar links) → mas por enquanto requer arquivo regular
        return -1;
    }

    inode_t *src_inode = &inode_table[src_file_inode];
    if (!src_inode) return -1;

    // Lê o conteúdo diretamente por inode
    char *buffer = malloc(src_inode->size + 1);
    if (!buffer) return -1;
    size_t bytes_read = 0;
    if (readContentFromInode(src_file_inode, buffer, src_inode->size + 1, &bytes_read, user) != 0) {
        free(buffer);
        return -1;
    }

    // Cria arquivo destino se necessário
    int dst_file_inode;
    if (dirFindEntry(dst_parent_inode, dst_base, FILE_REGULAR, &dst_file_inode) != 0) {
        if (createFile(dst_parent_inode, dst_base, user) != 0) {
            free(buffer);
            return -1;
        }
        if (dirFindEntry(dst_parent_inode, dst_base, FILE_REGULAR, &dst_file_inode) != 0) {
            free(buffer);
            return -1;
        }
    } else {
        // Se o arquivo já existe, precisamos sobrescrever: zera inode antes de escrever
        inode_t *dst_inode = &inode_table[dst_file_inode];
        dst_inode->size = 0;
        dst_inode->next_inode = 0;
        for (int i = 0; i < BLOCKS_PER_INODE; ++i) dst_inode->blocks[i] = 0;
    }

    // Escreve no inode destino usando addContentToInode
    int res = addContentToInode(dst_file_inode, buffer, bytes_read, user);
    free(buffer);
    return res;
}



// mv (move)
int cmd_mv(int current_inode, const char *src_path, const char *src_name,
           const char *dst_path, const char *dst_name, const char *user) {
    // Copia o arquivo
    if (cmd_cp(current_inode, src_path, src_name, dst_path, dst_name, user) != 0) return -1;

    // Apaga o arquivo de origem
    int src_parent_inode;
    if (resolvePath(src_path, current_inode, &src_parent_inode) != 0) return -1;

    return deleteFile(src_parent_inode, src_name, user);
}


// ln -s (symlink)
int cmd_ln_s(int current_inode,
             const char *target_path,
             const char *link_path,
             const char *user) {
    if (!target_path || !link_path || !user)
        return -1;

    int target_index;
    if (resolvePath(target_path, current_inode, &target_index) != 0) return -1;
    


    char link_dir[256];
    char link_name[256];
    splitPath(link_path, link_dir, link_name);

    int link_dir_index;

    // --- 2. Garante que o diretório do link exista ---
    // Tenta resolver o link_path normalmente
    if (resolvePath(link_dir, current_inode, &link_dir_index) != 0) {
        // Se falhar, cria diretórios recursivamente
        if (createDirectoriesRecursively(link_dir, current_inode, user) != 0)
            return -1;

        // Tenta resolver novamente agora que o caminho existe
        if (resolvePath(link_dir, current_inode, &link_dir_index) != 0)
            return -1;
    }

    // --- 3. Cria o link simbólico ---
    createSymlink(link_dir_index, target_index, link_name, user);
}


// ls
int cmd_ls(int current_inode, const char *path, const char *user, int info_arg) {
    if (!user) return -1;

    // checa se o caminho existe
    int target_inode = current_inode;
    if (path && strlen(path) > 0) {
        if (resolvePath(path, current_inode, &target_inode) != 0) {
            printf("ls: caminho não encontrado: %s\n", path);
            return -1;
        }
    }
    
    inode_t *dir_inode = &inode_table[target_inode];

    // itera sobre cada next dentro do inode
    do {
        // itera sobre cada bloco dentro do inode do diretório
        for (int block_idx = 0; block_idx < BLOCKS_PER_INODE; block_idx++) {
            if (dir_inode->blocks[block_idx] == 0) continue;
            
            dir_entry_t *entries = malloc(BLOCK_SIZE);
            if (readBlock(dir_inode->blocks[block_idx], entries) != 0) {
                free(entries);
                return -1;
            }
            
            int entries_per_block = BLOCK_SIZE / sizeof(dir_entry_t);
            for (int entry_idx = 0; entry_idx < entries_per_block; entry_idx++) {
                if (entries[entry_idx].inode_index == 0)
                    continue;

                inode_t *entry_inode = &inode_table[entries[entry_idx].inode_index];
                

                // Caso utilize o argumento para emular o ls - l
                if (info_arg) {
                    // Formata permissões (rwxrwxrwx)
                    char perm_str[10] = "---------";
                    for (int who = 6; who >= 0; who -= 3) {
                        perm_str[8-who-2] = (entry_inode->permissions & (PERM_READ << who)) ? 'r' : '-';
                        perm_str[8-who-1] = (entry_inode->permissions & (PERM_WRITE << who)) ? 'w' : '-';
                        perm_str[8-who] = (entry_inode->permissions & (PERM_EXEC << who)) ? 'x' : '-';
                    }

                    // Formata datas
                    char ctime_buf[32], mtime_buf[32];
                    format_time(entry_inode->creation_date, ctime_buf, sizeof(ctime_buf));
                    format_time(entry_inode->modification_date, mtime_buf, sizeof(mtime_buf));

                    // Determina tipo de arquivo
                    char type = '-';
                    if (entry_inode->type == FILE_DIRECTORY) type = 'd';
                    else if (entry_inode->type == FILE_SYMLINK) type = 'l';

                    printf("%c%s %8s %8s %8lu %s %s", 
                        type,
                        perm_str,
                        entry_inode->owner,
                        entry_inode->creator,
                        (unsigned long)entry_inode->size,
                        mtime_buf, 
                        entry_inode->name
                    );

                    // Se for link simbólico, mostra o alvo
                    if (entry_inode->type == FILE_SYMLINK) {
                        printf(" -> %s", inode_table[entry_inode->link_target_index].name);
                    }
                    printf("\n");
                }
                else {
                    printf("%s     %s\n", entry_inode->type == FILE_REGULAR? "-f" : "-d", entry_inode->name);
                }
            }

            free(entries);
        }
        dir_inode = &inode_table[dir_inode->next_inode];
    } while (dir_inode->next_inode != 0);
    return 0;
}

int cmd_remove(int current_inode, const char *filepath, const char *user, int remove_dir) {
    if (!filepath || !user) return -1;

    char path_copy[1024];
    strncpy(path_copy, filepath, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    

    char parent_path[1024];
    char name[MAX_NAMESIZE];

    // Encontra a última barra para separar caminho/nome
    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        // caso n tenha barra é o diretorio atual
        strncpy(parent_path, ".", sizeof(parent_path) - 1);
        parent_path[sizeof(parent_path) - 1] = '\0';
        strncpy(name, path_copy, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    } else {
        // Extrai o nome apos a última barra
        strncpy(name, last_slash + 1, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        // Se a barra for a primeira posição, o pai é raiz "~"
        if (last_slash == path_copy) {
            strncpy(parent_path, "~", sizeof(parent_path) - 1);
            parent_path[sizeof(parent_path) - 1] = '\0';
        } else {
            // Caso contrário copia a parte anterior como parent_path
            size_t len = last_slash - path_copy;
            if (len >= sizeof(parent_path)) return -1;
            strncpy(parent_path, path_copy, len);
            parent_path[len] = '\0';
        }
    }
    
    // resolve o inode do diretorio pai
    int parent_inode;
    if (resolvePath(parent_path, current_inode, &parent_inode) != 0) {
        if (remove_dir)
            printf("rmdir: diretório não encontrado: %s\n", parent_path);
        else
            printf("Arquivo não encontrado\n");
        return -1;
    }

    // Procura o arquivo com o nome dentro do diretório pai
    int target_inode;
    if (dirFindEntry(parent_inode, name, FILE_ANY, &target_inode) != 0) {
        if (remove_dir)
            printf("rmdir: não existe o diretório: %s\n", filepath);
        else
            printf("Arquivo não encontrado\n");
        return -1;
    }

    inode_t *target = &inode_table[target_inode];

    // Verifica conforme o tipo de rm (e.g. rm ou rmdir)
    if (remove_dir) {
        if (target->type != FILE_DIRECTORY) {
            printf("rmdir: não é um diretório: %s\n", filepath);
            return -1;
        }
        if (deleteDirectory(parent_inode, name, user) != 0) {
            printf("rmdir: não foi possível remover '%s'\n", filepath);
            return -1;
        }
        return 0;
    } else {
        if (target->type == FILE_DIRECTORY) {
            printf("rm: não é possível remover '%s': é um diretório\n", filepath);
            return -1;
        }
        if (deleteFile(parent_inode, name, user) != 0) {
            printf("Erro ao remover arquivo: %s\n", filepath);
            return -1;
        }
        return 0;
    }
}

int cmd_rm(int current_inode, const char *filepath, const char *user) {
    return cmd_remove(current_inode, filepath, user, 0);
}

int cmd_rmdir(int current_inode, const char *filepath, const char *user) {
    return cmd_remove(current_inode, filepath, user, 1);
}