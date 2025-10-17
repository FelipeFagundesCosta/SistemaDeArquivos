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

/* ---- calcula layout de forma determinística ---- */
static int compute_layout(void) {
    size_t inode_bmap_bytes = inode_bitmap_bytes();
    size_t inode_tbl_bytes = inode_table_bytes();

    size_t bmap_bytes = 0;
    uint32_t meta_blocks = 0;

    /* aproximação inicial */
    size_t inode_meta_bytes = inode_bmap_bytes + inode_tbl_bytes;
    meta_blocks = ( ((MAX_BLOCKS + 7)/8 + inode_meta_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE );

    /* bytes do bitmap de blocos considerando somente blocos de dados */
    uint32_t data_blocks = MAX_BLOCKS - meta_blocks;
    bmap_bytes = (data_blocks + 7) / 8;

    /* recalcula meta_blocks exato */
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

/* ---- placeholders ---- */
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
    }
}
