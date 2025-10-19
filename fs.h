#ifndef FS_H
#define FS_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>

#define DISK_NAME "disk.dat"
#define DISK_SIZE_MB 64
#define MAX_INODES 128
#define BLOCK_SIZE 512
#define BLOCKS_PER_INODE 12
#define MAX_BLOCKS ((DISK_SIZE_MB * 1024 * 1024) / BLOCK_SIZE)
#define MAX_NAMESIZE 32

#define ROOT_INODE 0

typedef enum {
    FILE_REGULAR,
    FILE_DIRECTORY,
    FILE_ANY
} inode_type_t;
typedef enum {
    PERM_NONE  = 0,        // 000 000 000
    PERM_EXEC  = 1 << 0,   // 001
    PERM_WRITE = 1 << 1,   // 010
    PERM_READ  = 1 << 2,   // 100

    PERM_RX    = PERM_READ | PERM_EXEC,    
    PERM_RWX   = PERM_READ | PERM_WRITE | PERM_EXEC,   // 111

    PERM_ALL   = (PERM_RWX << 6) | (PERM_RWX << 3) | PERM_RWX
} permission_t;

typedef struct {
    inode_type_t type;
    char name[MAX_NAMESIZE];
    char creator[MAX_NAMESIZE];
    char owner[MAX_NAMESIZE];
    uint32_t size;
    time_t creation_date;       
    time_t modification_date;   
    permission_t permissions;
    uint32_t blocks[BLOCKS_PER_INODE];
    uint32_t next_inode;
    uint32_t link_target_index;     
} inode_t;

typedef struct {
    char name[MAX_NAMESIZE];
    uint32_t inode_index;
} dir_entry_t;

typedef struct {
    char name[MAX_NAMESIZE];
    inode_type_t type;
    char creator[MAX_NAMESIZE];
    char owner[MAX_NAMESIZE];
    uint32_t size;
    time_t creation_date;       
    time_t modification_date;   
    uint16_t permissions;
    uint32_t inode_index;
} fs_entry_t;

typedef struct {
    fs_entry_t *entries;
    int count;
} fs_dir_list_t;

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
int dirFindEntry(int dir_inode, const char *name, inode_type_t type, int *out_inode);
int dirAddEntry(int dir_inode, const char *name, inode_type_t type, int inode_index);
int dirRemoveEntry(int dir_inode, const char *name, inode_type_t type);

/* Permissões */
int hasPermission(const inode_t *inode, const char *username, permission_t perm);

/* Manipulação de conteúdos */
int createDirectory(int parent_inode, const char *name, const char *user);
int deleteDirectory(int parent_inode, const char *name, const char *user);
int createFile(int parent_inode, const char *name, const char *user);
int deleteFile(int parent_inode, const char *name, const char *user);
fs_dir_list_t listElements(int parent_inode);
int addContentToFile(int parent_inode, const char *name, const char *content, const char *user);
ssize_t getFileSize(int parent_inode, const char *name);
int readContentFromFile(int parent_inode, const char *name, char *buffer, size_t buffer_size, size_t *out_bytes, const char *user);

int resolvePath(const char *path, int *inode_out);

int createSymlink(int parent_inode, int target_index, const char *link_name, inode_type_t type, const char *user);

int cmd_cd();
int cmd_mkdir();
int cmd_touch();
int cmd_echo_arrow();
int cmd_echo_arrow_arrow();
int cmd_cat();
int cmd_cp();
int cmd_mv();
int cmd_ln();
int cmd_ls();
int cmd_rm();
int cmd_rmdir();

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
