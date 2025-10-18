#include "fs.h"
#include <stdio.h>
#include <string.h>

void test_filesystem() {
    printf("=== Testando Filesystem ===\n");

    if (init_fs() == 0) printf("[PASS] init_fs()\n"); else printf("[FAIL] init_fs()\n");
    if (mount_fs() == 0) printf("[PASS] mount_fs()\n"); else printf("[FAIL] mount_fs()\n");

    int root_inode = allocateInode();
    if (root_inode >= 0) {
        inode_table[root_inode].type = FILE_DIRECTORY;
        for (int i=0;i<BLOCKS_PER_INODE;i++) inode_table[root_inode].blocks[i]=0;
        printf("[PASS] allocateInode()\n");
    } else printf("[FAIL] allocateInode()\n");

    int file_inode = allocateInode();
    if (file_inode >= 0) printf("[PASS] allocateBlock()\n"); else printf("[FAIL] allocateBlock()\n");

    int block_index = allocateBlock();
    if (block_index >= 0) printf("[PASS] allocateBlock()\n"); else printf("[FAIL] allocateBlock()\n");

    if (dirAddEntry(root_inode, "arquivo", file_inode) == 0) printf("[PASS] dirAddEntry() arquivo\n"); else printf("[FAIL] dirAddEntry() arquivo\n");

    int found;
    if (dirFindEntry(root_inode, "arquivo", &found) == 0) printf("[PASS] dirFindEntry() arquivo\n"); else printf("[FAIL] dirFindEntry() arquivo\n");

    if (dirRemoveEntry(root_inode, "arquivo") == 0) printf("[PASS] dirRemoveEntry() arquivo\n"); else printf("[FAIL] dirRemoveEntry() arquivo\n");

    if (dirFindEntry(root_inode, "arquivo", &found) != 0) printf("[PASS] dirFindEntry() após remoção\n"); else printf("[FAIL] dirFindEntry() após remoção\n");

    char buffer[BLOCK_SIZE];
    memset(buffer, 'A', BLOCK_SIZE);
    if (writeBlock(block_index, buffer) == 0) printf("[PASS] writeBlock()\n"); else printf("[FAIL] writeBlock()\n");

    char readbuf[BLOCK_SIZE];
    if (readBlock(block_index, readbuf) == 0 && memcmp(buffer, readbuf, BLOCK_SIZE)==0)
        printf("[PASS] readBlock()\n"); else printf("[FAIL] readBlock()\n");

    unmount_fs();
}

int main() {
    test_filesystem();
    return 0;
}
