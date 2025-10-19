#include "fs.h"
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

/* Globais */
unsigned char *block_bitmap = NULL;
unsigned char *inode_bitmap = NULL;
inode_t *inode_table = NULL;
FILE *disk = NULL;

/* Layout computado */
static off_t off_block_bitmap = 0;
static off_t off_inode_bitmap = 0;
static off_t off_inode_table = 0;
static off_t off_data_region = 0;

size_t computed_block_bitmap_bytes = 0;
size_t computed_inode_bitmap_bytes = 0;
size_t computed_inode_table_bytes = 0;
uint32_t computed_meta_blocks = 0;
uint32_t computed_data_blocks = 0;

/* ---- utilitárias ---- */
size_t inode_bitmap_bytes(void) { return (MAX_INODES + 7) / 8; }
size_t inode_table_bytes(void) { return MAX_INODES * sizeof(inode_t); }
size_t block_bitmap_bytes(void) { return computed_block_bitmap_bytes; }
size_t meta_region_bytes(void) { return computed_block_bitmap_bytes + computed_inode_bitmap_bytes + computed_inode_table_bytes; }
off_t offset_block_bitmap(void) { return off_block_bitmap; }
off_t offset_inode_bitmap(void) { return off_inode_bitmap; }
off_t offset_inode_table(void) { return off_inode_table; }
off_t offset_data_region(void) { return off_data_region; }

/* ---- calcula layout ---- */
static int compute_layout(void) {
    size_t inode_bmap_bytes = inode_bitmap_bytes();
    size_t inode_tbl_bytes = inode_table_bytes();

    size_t bmap_bytes = 0;
    uint32_t meta_blocks = 0;

    size_t inode_meta_bytes = inode_bmap_bytes + inode_tbl_bytes;
    meta_blocks = (((MAX_BLOCKS + 7)/8 + inode_meta_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE);

    uint32_t data_blocks = MAX_BLOCKS - meta_blocks;
    bmap_bytes = (data_blocks + 7) / 8;

    meta_blocks = (bmap_bytes + inode_meta_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

    computed_block_bitmap_bytes = bmap_bytes;
    computed_inode_bitmap_bytes = inode_bmap_bytes;
    computed_inode_table_bytes = inode_tbl_bytes;
    computed_meta_blocks = meta_blocks;
    computed_data_blocks = MAX_BLOCKS - computed_meta_blocks;

    off_block_bitmap = 0;
    off_inode_bitmap = off_block_bitmap + computed_block_bitmap_bytes;
    off_inode_table = off_inode_bitmap + computed_inode_bitmap_bytes;
    off_data_region = off_inode_table + computed_inode_table_bytes;

    return 0;
}

/* ---- init_fs ---- */
int init_fs(void) {
    disk = fopen(DISK_NAME, "wb+");
    if (!disk) { perror("init_fs"); return -1; }
    if (ftruncate(fileno(disk), DISK_SIZE_MB * 1024 * 1024) != 0) {
        perror("init_fs: ftruncate"); fclose(disk); disk=NULL; return -1;
    }

    if (compute_layout() != 0) { fclose(disk); disk=NULL; return -1; }

    block_bitmap = calloc(1, computed_block_bitmap_bytes);
    inode_bitmap = calloc(1, computed_inode_bitmap_bytes);
    inode_table = calloc(1, computed_inode_table_bytes);
    if (!block_bitmap || !inode_bitmap || !inode_table) goto fail_init;

    if (fwrite(block_bitmap, 1, computed_block_bitmap_bytes, disk) != computed_block_bitmap_bytes) goto fail_init;
    if (fwrite(inode_bitmap, 1, computed_inode_bitmap_bytes, disk) != computed_inode_bitmap_bytes) goto fail_init;
    if (fwrite(inode_table, 1, computed_inode_table_bytes, disk) != computed_inode_table_bytes) goto fail_init;

    fflush(disk);
    fsync(fileno(disk));
    rewind(disk);

    // === Criação do diretório raiz (inode 0) ===
    int root_inode = allocateInode();
    if (root_inode != ROOT_INODE) {
        fprintf(stderr, "Erro: inode raiz não está em 0!\n");
        goto fail_init;
    }

    inode_t *root = &inode_table[ROOT_INODE];
    time_t now = time(NULL);

    root->type = FILE_DIRECTORY;
    strcpy(root->name, "~");
    root->creation_date = now;
    root->modification_date = now;
    root->size = 0;

    int block = allocateBlock();
    if (block < 0) goto fail_init;
    root->blocks[0] = block;

    dir_entry_t entries[BLOCK_SIZE / sizeof(dir_entry_t)] = {0};

    // "." aponta para si mesmo, ".." também (pois root não tem pai)
    strcpy(entries[0].name, ".");
    entries[0].inode_index = ROOT_INODE;
    strcpy(entries[1].name, "..");
    entries[1].inode_index = ROOT_INODE;

    if (writeBlock(block, entries) != 0) goto fail_init;

    sync_fs();
    rewind(disk);

    return 0;

fail_init:
    free(block_bitmap); free(inode_bitmap); free(inode_table);
    fclose(disk); disk=NULL;
    return -1;
}

/* ---- mount_fs ---- */
int mount_fs(void) {
    if (disk != NULL) return 0;

    disk = fopen(DISK_NAME, "rb+");
    if (!disk) return init_fs();

    if (compute_layout() != 0) { fclose(disk); disk=NULL; return -1; }

    block_bitmap = malloc(computed_block_bitmap_bytes);
    inode_bitmap = malloc(computed_inode_bitmap_bytes);
    inode_table = malloc(computed_inode_table_bytes);
    if (!block_bitmap || !inode_bitmap || !inode_table) goto fail_mount;

    fseek(disk, off_block_bitmap, SEEK_SET);
    fread(block_bitmap, 1, computed_block_bitmap_bytes, disk);
    fread(inode_bitmap, 1, computed_inode_bitmap_bytes, disk);
    fread(inode_table, 1, computed_inode_table_bytes, disk);
    rewind(disk);
    return 0;

fail_mount:
    free(block_bitmap); free(inode_bitmap); free(inode_table);
    fclose(disk); disk=NULL;
    return -1;
}

/* ---- sync_fs ---- */
int sync_fs(void) {
    if (!disk || !block_bitmap || !inode_bitmap || !inode_table) return -1;
    fseek(disk, off_block_bitmap, SEEK_SET);
    fwrite(block_bitmap, 1, computed_block_bitmap_bytes, disk);
    fwrite(inode_bitmap, 1, computed_inode_bitmap_bytes, disk);
    fwrite(inode_table, 1, computed_inode_table_bytes, disk);
    fflush(disk);
    fsync(fileno(disk));
    rewind(disk);
    return 0;
}

/* ---- unmount_fs ---- */
int unmount_fs(void) {
    sync_fs();
    free(block_bitmap); block_bitmap=NULL;
    free(inode_bitmap); inode_bitmap=NULL;
    free(inode_table); inode_table=NULL;
    if (disk) { fclose(disk); disk=NULL; }
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
    off_t offset = offset_data_region() + (off_t)block_index * BLOCK_SIZE;
    fseek(disk, offset, SEEK_SET);
    size_t read_bytes = fread(buffer, 1, BLOCK_SIZE, disk);
    return (read_bytes == BLOCK_SIZE) ? 0 : -1;
}

int writeBlock(uint32_t block_index, const void *buffer){
    if (!disk || block_index >= computed_data_blocks) return -1;
    off_t offset = offset_data_region() + (off_t)block_index * BLOCK_SIZE;
    fseek(disk, offset, SEEK_SET);
    size_t written_bytes = fwrite(buffer, 1, BLOCK_SIZE, disk);
    fflush(disk);
    fsync(fileno(disk));
    return (written_bytes == BLOCK_SIZE) ? 0 : -1;
}

/* ---- diretórios ---- */
int dirFindEntry(int dir_inode, const char *name, inode_type_t type, int *out_inode) {
    if (dir_inode < 0 || dir_inode >= MAX_INODES || !name|| !out_inode) 
        return -1;

    int current_inode = dir_inode;

    while (current_inode >= 0) {
        inode_t *dir = &inode_table[current_inode];
        if (dir->type != FILE_DIRECTORY) return -1;

        for (int i = 0; i < BLOCKS_PER_INODE; i++) {
            if (dir->blocks[i] == 0) continue;

            dir_entry_t buffer[BLOCK_SIZE / sizeof(dir_entry_t)];
            if (readBlock(dir->blocks[i], buffer) != 0) return -1;

            for (int j = 0; j < BLOCK_SIZE / sizeof(dir_entry_t); j++) {
                if (buffer[j].inode_index != 0 && strcmp(buffer[j].name, name) == 0 && inode_table[buffer[j].inode_index].type == type) {
                    *out_inode = buffer[j].inode_index;
                    return 0;
                }
            }
        }
        if (dir->next_inode == 0){
            break;
         }
         current_inode = dir->next_inode;
    }
    return -1;
}

int dirAddEntry(int dir_inode, const char *name, inode_type_t type, int inode_index) {
    if (dir_inode < 0 || dir_inode >= MAX_INODES || !name) return -1;

    // evita duplicados
    int found;
    if (dirFindEntry(dir_inode, name, type, &found) == 0) return -1;

    int current_inode = dir_inode;

    while (current_inode >= 0) {
        inode_t *dir = &inode_table[current_inode];
        if (dir->type != FILE_DIRECTORY) return -1;

        // tenta colocar em todos os blocos existentes
        for (int i = 0; i < BLOCKS_PER_INODE; i++) {
            if (dir->blocks[i] == 0) {
                // se bloco não existe, aloca
                int new_block = allocateBlock();
                if (new_block < 0) return -1;
                dir->blocks[i] = new_block;
                dir_entry_t empty[BLOCK_SIZE / sizeof(dir_entry_t)] = {0};
                if (writeBlock(new_block, empty) != 0) return -1;
            }

            // tenta inserir neste bloco
            dir_entry_t buffer[BLOCK_SIZE / sizeof(dir_entry_t)];
            if (readBlock(dir->blocks[i], buffer) != 0) return -1;

            for (int j = 0; j < BLOCK_SIZE / sizeof(dir_entry_t); j++) {
                if (buffer[j].inode_index == 0) {
                    strncpy(buffer[j].name, name, sizeof(buffer[j].name)-1);
                    buffer[j].name[sizeof(buffer[j].name)-1] = '\0';
                    buffer[j].inode_index = inode_index;

                    if (writeBlock(dir->blocks[i], buffer) != 0) return -1;

                    dir->size += sizeof(dir_entry_t);
                    dir->modification_date = time(NULL);
                    return 0;
                }
            }
        }

        // todos os blocos do inode cheio → cria next_inode
        if (dir->next_inode == 0) {
            int next = allocateInode();
            if (next < 0) return -1;
            inode_t *next_inode = &inode_table[next];
            memset(next_inode, 0, sizeof(inode_t));
            next_inode->type = FILE_DIRECTORY;

            // aloca primeiro bloco do next_inode
            int new_block = allocateBlock();
            if (new_block < 0) return -1;
            next_inode->blocks[0] = new_block;
            dir_entry_t empty[BLOCK_SIZE / sizeof(dir_entry_t)] = {0};
            if (writeBlock(new_block, empty) != 0) return -1;

            dir->next_inode = next;
        }

        current_inode = dir->next_inode;
    }

    return -1;
}



int dirRemoveEntry(int dir_inode, const char *name, inode_type_t type){
    if (dir_inode < 0 || dir_inode >= MAX_INODES || !name) return -1;

    int current_inode = dir_inode;
    while (current_inode >= 0) {
        inode_t *dir = &inode_table[current_inode];
        if (dir->type != FILE_DIRECTORY) return -1;

        for (int i = 0; i < BLOCKS_PER_INODE; i++) {
            uint32_t block_index = dir->blocks[i];
            if (block_index == 0) continue;

            dir_entry_t buffer[BLOCK_SIZE / sizeof(dir_entry_t)];
            if (readBlock(block_index, buffer) != 0) return -1;

            for (int j = 0; j < BLOCK_SIZE / sizeof(dir_entry_t); j++) {
                if (buffer[j].inode_index != 0 && strcmp(buffer[j].name, name) == 0) {
                    int target_inode = buffer[j].inode_index;
                    buffer[j].inode_index = 0;
                    buffer[j].name[0] = '\0';
                    if (writeBlock(block_index, buffer) != 0) return -1;

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

                    return 0;
                }
            }
        }

        current_inode = dir->next_inode;
    }
    return -1;
}

int hasPermission(inode_t *inode, const char *user, char mode) {
    int perm = inode->permissions;

    if (strcmp(inode->owner, user) == 0) {
        // dono
        if (mode == 'r') return perm & 0400;
        if (mode == 'w') return perm & 0200;
        if (mode == 'x') return perm & 0100;
    } else {
        // outros
        if (mode == 'r') return perm & 0004;
        if (mode == 'w') return perm & 0002;
        if (mode == 'x') return perm & 0001;
    }
    return 0;
}


int createDirectory(int parent_inode, const char *name, const char *user){
    if (parent_inode < 0 || parent_inode >= MAX_INODES || !name || !user) return -1;
    int dummy_output;
    if (dirFindEntry(parent_inode, name, FILE_DIRECTORY, &dummy_output) == 0) return -1;

    inode_t *parent= &inode_table[parent_inode];
    if (parent_inode != ROOT_INODE){
    if (!hasPermission(parent, user, 'w')) return -1;
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
    new_inode->permissions = 0644;

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
    if (!hasPermission(inode, user, 'w')) return -1;

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
    if (!hasPermission(parent, user, 'w')) return -1;
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
    new_inode->permissions = 0644;

    if (dirAddEntry(parent_inode, name, FILE_REGULAR, new_inode_index) != 0) return -1;
    sync_fs();
    return 0;
}

int deleteFile(int parent_inode, const char *name, const char *user){
    if (parent_inode < 0 || parent_inode >= MAX_INODES || !name) return -1;
    int target_inode;
    if (dirFindEntry(parent_inode, name, FILE_REGULAR, &target_inode) == -1) return -1;

    inode_t *target = &inode_table[target_inode];
    if (!hasPermission(target, user, 'w')) return -1;

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

    inode_t *inode = &inode_table[target_inode];
    if (!hasPermission(inode, user, 'w')) return -1;
    

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

    inode_t *inode = &inode_table[target_inode];
    if (!hasPermission(inode, user, 'r')) return -1;

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

