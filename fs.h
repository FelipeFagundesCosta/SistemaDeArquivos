#ifndef FS_H
#define FS_H

#include <stdio.h>
#include <stdint.h>  // para uint32_t, uint64_t etc.
#include <unistd.h>  // para ftruncate()
#include <stdlib.h>

// Nome e tamanho do disco virtual
#define DISK_NAME "disk.dat"
#define DISK_SIZE_MB 64
#define MAX_INODES 128          // número total de i-nodes
#define BLOCK_SIZE 512          // tamanho de cada bloco em bytes
#define MAX_BLOCKS (DISK_SIZE_MB*1024*1024/BLOCK_SIZE)

#define META_BLOCKS (1 \
    + ((MAX_BLOCKS / 8 + BLOCK_SIZE - 1) / BLOCK_SIZE) \
    + ((MAX_INODES / 8 + BLOCK_SIZE - 1) / BLOCK_SIZE) \
    + ((sizeof(inode_t) * MAX_INODES + BLOCK_SIZE - 1) / BLOCK_SIZE))


// Estrutura do superbloco
typedef struct {
    uint32_t magic;          // número mágico de identificação do sistema de arquivos
    uint64_t disk_size;      // tamanho total do disco em bytes
    uint32_t block_size;     // tamanho de cada bloco (por exemplo, 4 KB)
} superblock_t;


typedef struct {
    char name[32];
    char creator[32];
    char owner[32];
    uint32_t size;
    int creation_date; //AnoMesDiaHoraMinutoSegundo
    int modification_date; //AnoMesDiaHoraMinutoSegundo
    uint16_t permissions;
    uint32_t blocks[12];  // 12 blocos diretos para simplicidade
    uint32_t next_inode;  // se o arquivo precisar de mais i-nodes
} inode_t;

// Protótipo da função de inicialização do sistema de arquivos
int allocateBlock(FILE *disk);
void freeBlock(FILE *disk, int block_index);
int allocateInode(FILE *disk);
void freeInode(FILE *disk, int inode_index);


#endif
