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

    printf("Disco virtual '%s' criado com %ld bytes (%.2f MB)\n",
           DISK_NAME, length, (float) length / (1024 * 1024));

    fclose(disk);
    return 0;
}
