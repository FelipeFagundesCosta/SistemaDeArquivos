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

typedef struct {
    uint32_t magic; // identificador do FS
    uint32_t block_bitmap_bytes;
    uint32_t inode_bitmap_bytes;
    uint32_t inode_table_bytes;
    uint32_t meta_blocks;
    uint32_t data_blocks;
    uint32_t off_block_bitmap;
    uint32_t off_inode_bitmap;
    uint32_t off_inode_table;
    uint32_t off_data_region;
} fs_header_t;

#define FS_MAGIC 0xF5F5F5F5


typedef enum {
    FILE_REGULAR,
    FILE_DIRECTORY,
    FILE_SYMLINK,
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

/* Utilitarios */
const char *format_time(time_t t, char *buf, size_t buflen);
int show_inode_info(int inode_index);

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
int addContentToInode(int inode_number, const char *data, size_t data_size, const char *user);
int readContentFromInode(int inode_number, char *buffer, size_t buffer_size, size_t *out_bytes, const char *user);

int resolvePath(const char *path, int current_inode, int *inode_out);
int createDirectoriesRecursively(const char *path, int current_inode, const char *user);
static void splitPath(const char *full_path, char *dir_path, char *base_name);

int createSymlink(int parent_inode, int target_index, const char *link_name, const char *user);

// core utils
int cmd_cd(int *current_inode, const char *path);
int cmd_cat(int current_inode, const char *path, const char *user);
int cmd_mkdir(int current_inode, const char *fullpath, const char *user);
int cmd_touch(int current_inode, const char *fullpath, const char *user);
int cmd_echo_arrow(int current_inode, const char *fullpath, const char *content, const char *user);
int cmd_echo_arrow_arrow(int current_inode, const char *fullpath, const char *content, const char *user);

int cmd_cp(int current_inode, const char *src_path, const char *src_name,
           const char *dst_path, const char *dst_name, const char *user);
int cmd_mv(int current_inode, const char *src_path, const char *src_name,
           const char *dst_path, const char *dst_name, const char *user);
int cmd_ln_s(int current_inode, const char *target_path, const char *link_path, const char *user);
int cmd_ls(int current_inode, const char *path, const char *user, int info_args);
int cmd_rm(int current_inode, const char *filepath, const char *user);
int cmd_rmdir(int current_inode, const char *filepath, const char *user);

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

extern off_t off_data_region;


#endif /* FS_H */
