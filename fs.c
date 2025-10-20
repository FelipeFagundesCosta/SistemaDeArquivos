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
    strcpy(inode_table[root_inode].name, "/");
    strcpy(inode_table[root_inode].owner, "root");
    inode_table[root_inode].blocks[0] = allocateBlock();
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
                if (buffer[j].inode_index != 0 &&
                    strcmp(buffer[j].name, name) == 0 &&
                    (inode_table[buffer[j].inode_index].type == type || type == FILE_ANY|| inode_table[buffer[j].inode_index].link_target_index != -1)) {
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
        for (int i = 0; i < BLOCKS_PER_INODE; i++) {
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

    if (target->type != FILE_REGULAR) return -1;

    for (int i = 0; i < BLOCKS_PER_INODE; i++) {
        if (target->blocks[i] != 0)
            freeBlock(target->blocks[i]);
    }

    if (dirRemoveEntry(parent_inode, name, FILE_REGULAR) == -1) return -1;
    freeInode(target_inode);
    sync_fs();
    return 0;
}

fs_dir_list_t listElements(int parent_inode) {
    fs_dir_list_t result = {NULL, 0};

    if (parent_inode < 0 || parent_inode >= MAX_INODES) return result;

    inode_t *dir = &inode_table[parent_inode];
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

int addContentToFile(int parent_inode, const char *name, const char *content, const char *user) {
    if (!name || !content || parent_inode < 0 || parent_inode >= MAX_INODES) return -1;

    int target_inode;
    if (dirFindEntry(parent_inode, name, FILE_REGULAR, &target_inode) != 0) return -1;

    if (inode_table[target_inode].link_target_index != -1){
        target_inode = inode_table[target_inode].link_target_index;
    }

    inode_t *inode = &inode_table[target_inode];
    if (!hasPermission(inode, user, PERM_WRITE)) return -1;
    

    inode_t *file_inode = &inode_table[target_inode];

    // --- Liberar blocos antigos ---
    int current_inode_idx = target_inode;
    while (current_inode_idx != 0) {
        inode_t *cur_inode = &inode_table[current_inode_idx];

        for (int i = 0; i < BLOCKS_PER_INODE; i++) {
            if (cur_inode->blocks[i] != 0) {
                freeBlock(cur_inode->blocks[i]);
                cur_inode->blocks[i] = 0;
            }
        }

        int next = cur_inode->next_inode;
        if (current_inode_idx != target_inode && next != 0) freeInode(current_inode_idx);
        cur_inode->next_inode = 0;
        current_inode_idx = next;
    }

    file_inode->size = 0;
    file_inode->modification_date = time(NULL);

    // --- Escrever novo conteúdo ---
    size_t content_len = strlen(content);
    size_t written = 0;
    inode_t *cur_inode = file_inode;

    while (written < content_len) {
        for (int i = 0; i < BLOCKS_PER_INODE && written < content_len; i++) {
            if (cur_inode->blocks[i] == 0) {
                int new_block = allocateBlock();
                if (new_block < 0) return -1;
                cur_inode->blocks[i] = new_block;
            }

            char buffer[BLOCK_SIZE] = {0};
            size_t chunk_size = (content_len - written > BLOCK_SIZE) ? BLOCK_SIZE : (content_len - written);
            memcpy(buffer, content + written, chunk_size);

            if (writeBlock(cur_inode->blocks[i], buffer) != 0) return -1;
            written += chunk_size;
        }

        // precisa de um novo inode?
        if (written < content_len) {
            int next_inode_idx = allocateInode();
            if (next_inode_idx < 0) return -1;

            inode_t *next_inode = &inode_table[next_inode_idx];
            memset(next_inode, 0, sizeof(inode_t));
            next_inode->type = FILE_REGULAR;

            cur_inode->next_inode = next_inode_idx;
            cur_inode = next_inode;
        }
    }

    file_inode->size = content_len;
    file_inode->modification_date = time(NULL);
    return sync_fs();
}

ssize_t getFileSize(int parent_inode, const char *name) {
    int file_inode_index;
    if (dirFindEntry(parent_inode, name, FILE_REGULAR, &file_inode_index) != 0)
        return -1;

    inode_t *inode = &inode_table[file_inode_index];
    if (!inode) return -1;

    return inode->size;
}

int readContentFromFile(int parent_inode, const char *name, char *buffer, size_t buffer_size, size_t *out_bytes, const char *user) {
    if (!name || !buffer || !out_bytes) return -1;

    int target_inode;
    if (dirFindEntry(parent_inode, name, FILE_REGULAR, &target_inode) != 0) {
        *out_bytes = 0;
        return -1;
    }

    if (inode_table[target_inode].link_target_index != -1){
        target_inode = inode_table[target_inode].link_target_index;
    }

    inode_t *inode = &inode_table[target_inode];
    if (!hasPermission(inode, user, PERM_READ)) return -1;

    if (!inode) {
        *out_bytes = 0;
        return -1;
    }

    size_t total_size = inode->size;
    if (buffer_size < total_size + 1) return -1;

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
        }

        if (current->next_inode != 0) {
            current = &inode_table[current->next_inode];
        } else {
            current = NULL;
        }
    }

    buffer[total_size] = '\0';
    *out_bytes = total_size;
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
        while (inode_table[next_inode].link_target_index != -1) {
            next_inode = inode_table[next_inode].link_target_index;
            if (++depth > 16) return -1; // evita loops
        }

        current = next_inode;
    }

    *inode_out = current;
    return 0;
}


int createSymlink(int parent_inode, int target_index, const char *link_name, inode_type_t type, const char *user) {
    // 1. Verifica se link_name já existe
    int dummy_output;
    if (dirFindEntry(parent_inode, link_name, type, &dummy_output)) return -1; // erro, já existe

    inode_t *parent = &inode_table[parent_inode];
    if (!hasPermission(parent, user, PERM_WRITE)) return -1;

    // 2. Aloca um novo i-node
    int inode_index = allocateInode();
    inode_t *inode = &inode_table[inode_index];
    if (!inode) return -1;

    // 3. Preenche campos
    strncpy(inode->name, link_name, MAX_NAMESIZE-1);
    inode->name[MAX_NAMESIZE-1] = '\0';
    inode->type = type;
    inode->size = 0;
    inode->link_target_index = target_index;
    inode->creation_date = time(NULL);
    inode->modification_date = inode->creation_date;
    strncpy(inode->creator, user, MAX_NAMESIZE-1);
    inode->creator[MAX_NAMESIZE-1] = '\0';
    strncpy(inode->owner, user, MAX_NAMESIZE-1);
    inode->owner[MAX_NAMESIZE-1] = '\0';
    inode->permissions = inode_table[target_index].permissions;

    if (dirAddEntry(parent_inode, link_name, type, inode_index) != 0){
        freeInode(inode_index);
        return -1;
    }

    sync_inode(inode_index);
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

// mkdir
int cmd_mkdir(int current_inode, const char *path, const char *name, const char *user) {
    if (!path || !name || !user) return -1;

    int parent_inode;
    if (resolvePath(path, current_inode, &parent_inode) != 0) return -1;

    return createDirectory(parent_inode, name, user);
}

// touch
int cmd_touch(int current_inode, const char *path, const char *name, const char *user) {
    if (!path || !name || !user) return -1;

    int parent_inode;
    if (resolvePath(path, current_inode, &parent_inode) != 0) return -1;

    return createFile(parent_inode, name, user);
}

// echo >
int cmd_echo_arrow(int current_inode, const char *path, const char *name, const char *content, const char *user) {
    if (!path || !name || !content || !user) return -1;

    int parent_inode;
    if (resolvePath(path, current_inode, &parent_inode) != 0) return -1;

    int inode_index;
    if (dirFindEntry(parent_inode, name, FILE_REGULAR, &inode_index) != 0) {
        if (createFile(parent_inode, name, user) != 0) return -1;
    }

    return addContentToFile(parent_inode, name, content, user);
}

// echo >>
int cmd_echo_arrow_arrow(int current_inode, const char *path, const char *name, const char *content, const char *user) {
    if (!path || !name || !content || !user) return -1;

    int parent_inode;
    if (resolvePath(path, current_inode, &parent_inode) != 0) return -1;

    int inode_index;
    size_t current_size = 0;

    if (dirFindEntry(parent_inode, name, FILE_REGULAR, &inode_index) != 0) {
        if (createFile(parent_inode, name, user) != 0) return -1;
        inode_index = -1;
    }

    inode_t *inode = (inode_index >= 0) ? &inode_table[inode_index] : NULL;
    if (inode) current_size = inode->size;

    size_t content_size = strlen(content);
    size_t buffer_size = current_size + content_size + 1;
    char *buffer = malloc(buffer_size);
    if (!buffer) return -1;

    size_t bytes_read = 0;
    if (inode && readContentFromFile(parent_inode, name, buffer, buffer_size, &bytes_read, user) != 0) {
        free(buffer);
        return -1;
    }

    memcpy(buffer + bytes_read, content, content_size);
    buffer[bytes_read + content_size] = '\0';

    int res = addContentToFile(parent_inode, name, buffer, user);
    free(buffer);
    return res;
}

// cp (copy)
int cmd_cp(int current_inode, const char *src_path, const char *src_name,
           const char *dst_path, const char *dst_name, const char *user) {
    if (!src_path || !src_name || !dst_path || !dst_name || !user) return -1;

    int src_inode;
    if (resolvePath(src_path, current_inode, &src_inode) != 0) return -1;

    int file_inode;
    if (dirFindEntry(src_inode, src_name, FILE_REGULAR, &file_inode) != 0) return -1;

    int dst_inode;
    if (resolvePath(dst_path, current_inode, &dst_inode) != 0) return -1;

    inode_t *inode = &inode_table[file_inode];
    char *buffer = malloc(inode->size + 1);
    if (!buffer) return -1;

    size_t bytes_read;
    if (readContentFromFile(src_inode, src_name, buffer, inode->size + 1, &bytes_read, user) != 0) {
        free(buffer);
        return -1;
    }

    if (createFile(dst_inode, dst_name, user) != 0) {
        free(buffer);
        return -1;
    }

    int result = addContentToFile(dst_inode, dst_name, buffer, user);
    free(buffer);
    return result;
}

// mv (move)
int cmd_mv(int current_inode, const char *src_path, const char *src_name,
           const char *dst_path, const char *dst_name, const char *user) {
    if (cmd_cp(current_inode, src_path, src_name, dst_path, dst_name, user) != 0) return -1;

    int src_inode;
    if (resolvePath(src_path, current_inode, &src_inode) != 0) return -1;

    return deleteFile(src_inode, src_name, user);
}

// ln -s (symlink)
int cmd_ln_s(int current_inode, const char *target_path, const char *target_name,
             const char *link_path, const char *link_name, const char *user) {
    if (!target_path || !target_name || !link_path || !link_name || !user) return -1;

    int target_dir;
    if (resolvePath(target_path, current_inode, &target_dir) != 0) return -1;

    int target_inode;
    if (dirFindEntry(target_dir, target_name, FILE_ANY, &target_inode) != 0) return -1;

    int link_dir;
    if (resolvePath(link_path, current_inode, &link_dir) != 0) return -1;

    inode_t *inode = &inode_table[target_inode];
    return createSymlink(link_dir, target_inode, link_name, inode->type, user);
}


int cmd_ls(int current_inode, const char *path) {
    if (strcmp(path, ".") == 0) {
        inode_t inode = inode_table[current_inode];
        while (1)
        {   
            for (int element = 0; element <= BLOCKS_PER_INODE; element++) {
                switch (inode_table[inode.blocks[element]].type)
                {
                case FILE_DIRECTORY:
                    printf("DIR:  %s ", inode_table[inode.blocks[element]].name);
                    break;
                case FILE_REGULAR:
                    printf("FILE: %s ", inode_table[inode.blocks[element]].name);
                    break;
                default:
                    break;
                }
                // if (inode.blocks[element] != 0) {
                //     printf("%s  ", inode_table[inode.blocks[element]].name);
                // }
            }
            uint32_t next = inode.next_inode;
            if (next != 0) {
                inode = inode_table[next];
            }
            else break;
        }
        printf("\n");
    }
}

int cmd_rm(int current_inode, const char *filepath, int recursive, const char *user) {
    if (recursive) {
        return -1;
    }
    else {
        int inode_index;
        if (resolvePath(filepath, current_inode, &inode_index) != 0) return -1;
        inode_t *inode = &inode_table[inode_index];
        deleteFile(inode_index, inode->name, user);
    }
}

