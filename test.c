#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    printf("=== Inicializando FS ===\n");
    if (init_fs() != 0) {
        fprintf(stderr, "Erro ao inicializar FS\n");
        return 1;
    }

    printf("=== Criando diretórios /dir1 e /dir2 ===\n");
    if (createDirectory(ROOT_INODE, "dir1") != 0) printf("Falha ao criar dir1\n");
    if (createDirectory(ROOT_INODE, "dir2") != 0) printf("Falha ao criar dir2\n");

    printf("=== Criando arquivos file1.txt e file2.txt em /dir1 ===\n");
    int dir1_inode;
    if (dirFindEntry(ROOT_INODE, "dir1", FILE_DIRECTORY,&dir1_inode) != 0) {
        fprintf(stderr, "dir1 não encontrado\n");
        return 1;
    }

    if (createFile(dir1_inode, "file1.txt") != 0) printf("Falha ao criar file1.txt\n");
    if (createFile(dir1_inode, "file2.txt") != 0) printf("Falha ao criar file2.txt\n");

    printf("=== Listando ROOT ===\n");
    fs_dir_list_t root_list = listElements(ROOT_INODE);
    printDirList(root_list);
    free(root_list.entries);

    printf("=== Listando /dir1 ===\n");
    fs_dir_list_t dir1_list = listElements(dir1_inode);
    printDirList(dir1_list);
    free(dir1_list.entries);

    printf("=== Criando mais arquivos em /dir1 para testar next_inode ===\n");
    for (int i = 3; i <= 20; i++) {
        char fname[32];
        snprintf(fname, sizeof(fname), "file%d.txt", i);
        if (createFile(dir1_inode, fname) != 0) printf("Falha ao criar %s\n", fname);
    }

    printf("=== Listando /dir1 novamente ===\n");
    dir1_list = listElements(dir1_inode);
    printDirList(dir1_list);
    free(dir1_list.entries);

    printf("=== Deletando file1.txt e file2.txt de /dir1 ===\n");
    deleteFile(dir1_inode, "file1.txt");
    deleteFile(dir1_inode, "file2.txt");

    printf("=== Listando /dir1 após deleções ===\n");
    dir1_list = listElements(dir1_inode);
    printDirList(dir1_list);
    free(dir1_list.entries);

    printf("=== Tentando deletar /dir1 (deve falhar porque ainda tem arquivos) ===\n");
    if (deleteDirectory(ROOT_INODE, "dir1") != 0) {
        printf("Deleção de /dir1 falhou como esperado\n");
    }

    printf("=== Deletando todos arquivos restantes de /dir1 ===\n");
    dir1_list = listElements(dir1_inode);
    for (int i = 0; i < dir1_list.count; i++) {
        deleteFile(dir1_inode, dir1_list.entries[i].name);
    }
    free(dir1_list.entries);

    printf("=== Agora deletando /dir1 ===\n");
    if (deleteDirectory(ROOT_INODE, "dir1") == 0) {
        printf("/dir1 deletado com sucesso\n");
    }

    printf("=== Listando ROOT após deleção de /dir1 ===\n");
    root_list = listElements(ROOT_INODE);
    printDirList(root_list);
    free(root_list.entries);

    printf("=== Finalizando FS ===\n");
    unmount_fs();
    return 0;
}
