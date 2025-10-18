#include "fs.h"
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

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
    for (uint32_t i = 0; i < MAX_INODES; i++){
        uint32_t byte = i / 8;
        uint8_t bit = i % 8;

        if ((inode_bitmap[byte] & (1 << bit)) == 0){
            inode_bitmap[byte] |= (1 << bit);
            memset(&inode_table[i], 0, sizeof(inode_t));
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
int dirFindEntry(int dir_inode, const char *name, int *out_inode) {
    if (dir_inode < 0 || dir_inode >= MAX_INODES || !name || !out_inode) 
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
                if (buffer[j].inode_index != 0 && strcmp(buffer[j].name, name) == 0) {
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

int dirAddEntry(int dir_inode, const char *name, int inode_index){
    if (dir_inode < 0 || dir_inode >= MAX_INODES || !name || inode_index < 0 || inode_index >= MAX_INODES) return -1;

    int found;
    if (dirFindEntry(dir_inode, name, &found) == 0) return -1; // já existe

    int current_inode = dir_inode;
    while (current_inode >= 0) {
        inode_t *dir = &inode_table[current_inode];
        if (dir->type != FILE_DIRECTORY) return -1;
        dir->type = FILE_DIRECTORY; // garante tipo

        // garante pelo menos um bloco alocado
        if (dir->blocks[0] == 0) {
            int new_block = allocateBlock();
            if (new_block < 0) return -1;
            dir->blocks[0] = new_block;
            dir_entry_t empty_block[BLOCK_SIZE / sizeof(dir_entry_t)] = {0};
            if (writeBlock(new_block, empty_block) != 0) return -1;
        }

        for (int i = 0; i < BLOCKS_PER_INODE; i++) {
            uint32_t block_index = dir->blocks[i];
            if (block_index == 0) continue;

            dir_entry_t buffer[BLOCK_SIZE / sizeof(dir_entry_t)];
            if (readBlock(block_index, buffer) != 0) return -1;

            for (int j = 0; j < BLOCK_SIZE / sizeof(dir_entry_t); j++) {
                if (buffer[j].inode_index == 0) {
                    strncpy(buffer[j].name, name, sizeof(buffer[j].name)-1);
                    buffer[j].name[sizeof(buffer[j].name)-1] = '\0';
                    buffer[j].inode_index = inode_index;
                    if (writeBlock(block_index, buffer) != 0) return -1;
                    return 0;
                }
            }
        }

        // avança ou cria next_inode se necessário
        if (dir->next_inode == 0) {
            int next = allocateInode();
            if (next < 0) return -1;
            printf("passou7");
            dir->next_inode = next;
            inode_table[next].type = FILE_DIRECTORY;
        }
        current_inode = dir->next_inode;
    }

    return -1;
    printf("passou8");
}


int dirRemoveEntry(int dir_inode, const char *name){
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
                    return 0;
                }
            }
        }

        current_inode = dir->next_inode;
    }
    return -1;
}
