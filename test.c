#include <stdio.h>
#include <stdlib.h>
#include "fs.h"

void print_fs_status(void) {
    printf("=== FS STATUS ===\n");
    printf("Meta blocks: %u\n", computed_meta_blocks);
    printf("Data blocks: %u\n", computed_data_blocks);
    printf("Block bitmap bytes: %zu\n", computed_block_bitmap_bytes);
    printf("Inode bitmap bytes: %zu\n", computed_inode_bitmap_bytes);
    printf("Inode table bytes: %zu\n", computed_inode_table_bytes);
    printf("Offsets:\n");
    printf("  block_bitmap: %lld\n", (long long)offset_block_bitmap());
    printf("  inode_bitmap: %lld\n", (long long)offset_inode_bitmap());
    printf("  inode_table: %lld\n", (long long)offset_inode_table());
    printf("  data_region: %lld\n", (long long)offset_data_region());
    printf("================\n");
}

int main() {
    printf("TEST: Inicializando FS...\n");
    if(init_fs() != 0) {
        fprintf(stderr, "Falha ao inicializar FS!\n");
        return 1;
    }

    print_fs_status();

    printf("TEST: Montando FS...\n");
    if(mount_fs() != 0) {
        fprintf(stderr, "Falha ao montar FS!\n");
        return 1;
    }

    print_fs_status();

    // Teste rápido de bitmaps e inodes
    printf("Verificando bitmaps e inodes zerados...\n");

    int block_zero = 1;
    for(size_t i=0;i<computed_block_bitmap_bytes;i++){
        if(block_bitmap[i]!=0){
            block_zero=0;
            break;
        }
    }
    printf("Block bitmap zerado: %s\n", block_zero?"SIM":"NAO");

    int inode_zero = 1;
    for(size_t i=0;i<computed_inode_bitmap_bytes;i++){
        if(inode_bitmap[i]!=0){
            inode_zero=0;
            break;
        }
    }
    printf("Inode bitmap zerado: %s\n", inode_zero?"SIM":"NAO");

    int inodes_empty = 1;
    for(size_t i=0;i<MAX_INODES;i++){
        if(inode_table[i].size!=0){
            inodes_empty=0;
            break;
        }
    }
    printf("Inode table zerada: %s\n", inodes_empty?"SIM":"NAO");

    printf("TEST: Sincronizando FS...\n");
    if(sync_fs()!=0){
        fprintf(stderr,"Falha ao sincronizar FS!\n");
        return 1;
    }

    printf("TEST: Desmontando FS...\n");
    if(unmount_fs()!=0){
        fprintf(stderr,"Falha ao desmontar FS!\n");
        return 1;
    }

    printf("TEST: Montando novamente FS...\n");
    if(mount_fs()!=0){
        fprintf(stderr,"Falha ao montar FS novamente!\n");
        return 1;
    }

    print_fs_status();
    printf("TEST: Tudo OK!\n");

    unmount_fs();
    return 0;
}
