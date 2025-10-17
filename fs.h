#ifndef FS_H
#define FS_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

#define DISK_NAME "disk.dat"
#define DISK_SIZE_MB 64
#define MAX_INODES 128
#define BLOCK_SIZE 512
#define BLOCKS_PER_INODE 12
#define MAX_BLOCKS ((DISK_SIZE_MB * 1024 * 1024) / BLOCK_SIZE)

typedef enum {
    FILE_REGULAR,
    FILE_DIRECTORY,
    FILE_SYMLINK
} inode_type_t;

typedef struct {
    inode_type_t type;
    char name[32];
    char creator[32];
    char owner[32];
    uint32_t size;
    int creation_date;       
    int modification_date;   
    uint16_t permissions;
    uint32_t blocks[BLOCKS_PER_INODE];     
    uint32_t next_inode;     
} inode_t;

typedef struct {
    char name[32];
    uint32_t inode_index;
} dir_entry_t;

/* Funções principais */
int init_fs(void);
int mount_fs(void);
int sync_fs(void);
int unmount_fs(void);

/* Alocação */
int allocateBlock(void);
void freeBlock(int block_index);
int allocateInode(void);
void freeInode(int inode_index);

/* Leitura e escrita nos blocos */
int readBlock(uint32_t block_index, void *buffer);
int writeBlock(uint32_t block_index, const void *buffer);

/* Diretórios */
int dirFindEntry(int dir_inode, const char *name, int *out_inode);
int dirAddEntry(int dir_inode, const char *name, int inode_index);
int dirRemoveEntry(int dir_inode, const char *name);

/* Layout */
size_t block_bitmap_bytes(void);
size_t inode_bitmap_bytes(void);
size_t inode_table_bytes(void);
size_t meta_region_bytes(void);
off_t  offset_block_bitmap(void);
off_t  offset_inode_bitmap(void);
off_t  offset_inode_table(void);
off_t  offset_data_region(void);

/* Variáveis globais */
extern unsigned char *block_bitmap;
extern unsigned char *inode_bitmap;
extern inode_t *inode_table;
extern FILE *disk;

/* Variáveis computadas (para testes) */
extern size_t computed_block_bitmap_bytes;
extern size_t computed_inode_bitmap_bytes;
extern size_t computed_inode_table_bytes;
extern uint32_t computed_meta_blocks;
extern uint32_t computed_data_blocks;

#endif /* FS_H */
