#ifndef FS_H
#define FS_H

#include <stdio.h>
#include <stdint.h>  // para uint32_t, uint64_t etc.
#include <unistd.h>  // para ftruncate()
#include <stdlib.h>

// Nome e tamanho do disco virtual
#define DISK_NAME "disk.dat"
#define DISK_SIZE_MB 64
#define BLOCK_SIZE 512

// Estrutura do superbloco
typedef struct {
    uint32_t magic;          // número mágico de identificação do sistema de arquivos
    uint64_t disk_size;      // tamanho total do disco em bytes
    uint32_t block_size;     // tamanho de cada bloco (por exemplo, 4 KB)
} superblock_t;

// Protótipo da função de inicialização do sistema de arquivos
int init_fs(void);

#endif
