#include "fs.h"

int init_fs(void) {
    FILE *disk = fopen(DISK_NAME, "wb");
    if (disk == NULL) {
        perror("Erro ao criar arquivo de disco");
        return -1;
    }

    long length = DISK_SIZE_MB * 1024 * 1024;    

    if (ftruncate(fileno(disk), length) != 0) {
        perror("Erro ao definir tamanho do disco");
        fclose(disk);
        return -1;
    }

    // Criação e gravação do superbloco
    superblock_t sb;
    sb.magic = 0x12345678;   // valor fixo usado para identificar o FS
    sb.disk_size = length;
    sb.block_size = BLOCK_SIZE;    // 4 KB por bloco, por exemplo

    fwrite(&sb, sizeof(superblock_t), 1, disk);

    unsigned char block_bitmap[MAX_BLOCKS / 8] = {0};
    unsigned char inode_bitmap[MAX_INODES / 8] = {0};

    for (int i = 0; i < META_BLOCKS; i++){
        int byte_index = i / 8;
        int bit_index = i % 8;

        block_bitmap[byte_index] |= (1 << bit_index);
    } // Marca os primeiros META_BLOCKS blocos como ocupados (superbloco + bitmaps + i-nodes)


    fwrite(block_bitmap, sizeof(block_bitmap), 1, disk);
    fwrite(inode_bitmap, sizeof(inode_bitmap), 1, disk);

    printf("Disco virtual '%s' criado com %ld bytes (%.2f MB)\n",
           DISK_NAME, length, (float) length / (1024 * 1024));

    fclose(disk);
    return 0;
}

int allocateBlock(FILE *disk){

}

void freeBlock(FILE *disk, int block_index){

}

int allocateInode(FILE *disk){

}

void freeInode(FILE *disk, int inode_index){

}