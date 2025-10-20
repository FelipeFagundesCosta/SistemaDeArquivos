#include "fs.h"
#include <stdio.h>
#include <string.h>

int main() {
    int current_inode = ROOT_INODE;
    size_t bytes_read;
    char buffer[256];

    printf("Inicializando filesystem...\n");
    if (init_fs() != 0) {
        printf("Erro ao inicializar o filesystem.\n");
        return 1;
    }

    printf("\n=== Teste de criação de diretórios ===\n");
    cmd_mkdir(current_inode, "", "dir1", "user");
    cmd_mkdir(current_inode, "", "dir2", "user");
    cmd_ls();

    printf("\n=== Teste de navegação de diretórios ===\n");
    cmd_cd(&current_inode, "dir1");
    cmd_mkdir(current_inode, "", "subdir1", "user");
    cmd_touch(current_inode, "", "file1.txt", "user");
    cmd_ls();

    printf("\n=== Teste de escrita e leitura em arquivo ===\n");
    cmd_echo_arrow(current_inode, "", "file1.txt", "Hello World!", "user");
    readContentFromFile(current_inode, "file1.txt", buffer, sizeof(buffer), &bytes_read, "user");
    printf("Conteúdo de file1.txt: %s\n", buffer);

    printf("\n=== Teste de concatenação de conteúdo ===\n");
    cmd_echo_arrow_arrow(current_inode, "", "file1.txt", " More text.", "user");
    readContentFromFile(current_inode, "file1.txt", buffer, sizeof(buffer), &bytes_read, "user");
    printf("Conteúdo atualizado de file1.txt: %s\n", buffer);

    printf("\n=== Teste de copiar e mover arquivos ===\n");
    cmd_cp(current_inode, "", "file1.txt", "/dir2", "copied_file.txt", "user");
    cmd_mv(current_inode, "/dir2", "copied_file.txt", "/dir2", "moved_file.txt", "user");

    printf("\n=== Teste de links simbólicos ===\n");
    cmd_ln_s(current_inode, "", "file1.txt", "", "link_to_file1", "user");
    readContentFromFile(current_inode, "link_to_file1", buffer, sizeof(buffer), &bytes_read, "user");
    printf("Conteúdo do link link_to_file1: %s\n", buffer);

    printf("\n=== Teste de deleção ===\n");
    cmd_rm(current_inode, "", "file1.txt", "user");
    cmd_rmdir(current_inode, "", "subdir1", "user");
    cmd_ls();

    printf("\n=== Voltar ao diretório raiz e listar ===\n");
    cmd_cd(&current_inode, "..");
    cmd_ls();

    printf("\nDesmontando filesystem...\n");
    unmount_fs();

    printf("Teste finalizado.\n");
    return 0;
}
