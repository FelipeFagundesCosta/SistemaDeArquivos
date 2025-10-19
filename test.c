#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void printDirList(fs_dir_list_t list) {
    printf("=== Listagem de diretório (%d itens) ===\n", list.count);
    for (int i = 0; i < list.count; i++) {
        fs_entry_t *e = &list.entries[i];
        printf("[%d] %s\tTipo: %s\tTamanho: %u\tCriador: %s\tOwner: %s\n",
               e->inode_index,
               e->name,
               (e->type == FILE_DIRECTORY) ? "DIR" :
               (e->type == FILE_REGULAR) ? "FILE" : "SYMLINK",
               e->size,
               e->creator,
               e->owner);
    }
    printf("=======================================\n");
}

int main() {
    const char *user = "tester";

    if (init_fs() != 0) {
        fprintf(stderr, "Erro ao inicializar FS\n");
        return 1;
    }

    printf("=== Criando diretórios encadeados ~/a/b/c/d ===\n");
    int inode_a, inode_b, inode_c, inode_d;
    createDirectory(ROOT_INODE, "a", user);
    dirFindEntry(ROOT_INODE, "a", FILE_DIRECTORY, &inode_a);

    createDirectory(inode_a, "b", user);
    dirFindEntry(inode_a, "b", FILE_DIRECTORY, &inode_b);

    createDirectory(inode_b, "c", user);
    dirFindEntry(inode_b, "c", FILE_DIRECTORY, &inode_c);

    createDirectory(inode_c, "d", user);
    dirFindEntry(inode_c, "d", FILE_DIRECTORY, &inode_d);

    printf("=== Criando arquivos grandes em ~/a/b/c/d ===\n");
    createFile(inode_d, "bigfile.txt", user);

    size_t big_size = BLOCK_SIZE * BLOCKS_PER_INODE * 2; // Ocupa 2 inodes
    char *big_content = malloc(big_size + 1);
    for (size_t i = 0; i < big_size; i++) big_content[i] = 'X' + (i % 26);
    big_content[big_size] = '\0';

    addContentToFile(inode_d, "bigfile.txt", big_content, user);

    // --- Testando resolvePath absoluto ---
    int inode_resolved;
    if (resolvePath("~/a/b/c/d/bigfile.txt", &inode_resolved) == 0) {
        printf("resolvePath absoluto encontrou inode %d\n", inode_resolved);
    } else {
        printf("resolvePath absoluto falhou\n");
    }

    // --- Testando resolvePath relativo ---
    if (resolvePath("c/d/bigfile.txt", &inode_resolved) == 0) {
        printf("resolvePath relativo encontrou inode %d\n", inode_resolved);
    } else {
        printf("resolvePath relativo falhou\n");
    }

    // --- Lendo o arquivo grande ---
    size_t read_bytes;
    if (readContentFromFile(inode_d, "bigfile.txt", big_content, big_size + 1, &read_bytes, user) == 0) {
        printf("Leitura de bigfile.txt: %zu bytes\n", read_bytes);
    }

    // --- Testando symlink ---
    printf("=== Criando symlink 'link_to_bigfile' em ~/a/b/c/d apontando para bigfile.txt ===\n");
    int bigfile_inode;
    dirFindEntry(inode_d, "bigfile.txt", FILE_REGULAR, &bigfile_inode);

    if (createSymlink(inode_d, bigfile_inode, "link_to_bigfile", FILE_REGULAR, user) == 0) {
        printf("Symlink criado com sucesso!\n");

        int symlink_inode;
        if (dirFindEntry(inode_d, "link_to_bigfile", FILE_REGULAR, &symlink_inode) == 0) {
            printf("Leitura via symlink: inode %d\n", symlink_inode);

            // Lendo conteúdo através do symlink
            int target_inode = inode_table[symlink_inode].link_target_index;
            size_t symlink_bytes;
            if (readContentFromFile(inode_d, "link_to_bigfile", big_content, big_size + 1, &symlink_bytes, user) == 0) {
                printf("Conteúdo via symlink lido: %zu bytes\n", symlink_bytes);
            }
        }
    }

    free(big_content);

    printf("=== Criando múltiplos arquivos e subdiretórios em ~/a ===\n");
    for (int i = 1; i <= 10; i++) {
        char fname[32], dname[32];
        snprintf(fname, sizeof(fname), "file%d.txt", i);
        createFile(inode_a, fname, user);

        snprintf(dname, sizeof(dname), "subdir%d", i);
        createDirectory(inode_a, dname, user);
    }

    fs_dir_list_t list_a = listElements(inode_a);
    printDirList(list_a);
    free(list_a.entries);

    printf("=== Finalizando FS ===\n");
    unmount_fs();

    return 0;
}
