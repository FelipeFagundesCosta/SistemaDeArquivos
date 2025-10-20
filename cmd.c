// cmd.c
#include "fs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_INPUT 256

int main() {
    int current_inode = 0; // inode raiz
    char user[] = "root";  // usuário fixo para testes
    char input[MAX_INPUT];

    if (access(DISK_NAME, F_OK) == 0) {
    // Disco existe → montar
    if (mount_fs() != 0) {
        fprintf(stderr, "Erro ao montar o filesystem!\n");
        return -1;
    }
    } else {
        // Disco não existe → criar
        if (init_fs() != 0) {
            fprintf(stderr, "Erro ao inicializar o filesystem!\n");
            return -1;
        }
    }

    printf("MiniFS Terminal. Digite 'exit' para sair.\n");

    while (1) {
        printf("[%s]> ", inode_table[current_inode].name); 
        if (!fgets(input, MAX_INPUT, stdin)) break;

        // Remove \n final
        input[strcspn(input, "\n")] = 0;

        // Sair
        if (strcmp(input, "exit") == 0) break;

        // Parse do comando
        char *cmd = strtok(input, " ");
        char *arg1 = strtok(NULL, " ");
        char *arg2 = strtok(NULL, " ");
        char *arg3 = strtok(NULL, " ");
        char *arg4 = strtok(NULL, " ");

        if (!cmd) continue;
        

        if (strcmp(cmd, "cd") == 0 && arg1) {
            cmd_cd(&current_inode, arg1);
        }
        else if (strcmp(cmd, "mkdir") == 0 && arg1) {
            cmd_mkdir(current_inode, arg1, user);
        }
        else if (strcmp(cmd, "touch") == 0 && arg1) {
            cmd_touch(current_inode, arg1, user);
        }
        else if (strcmp(cmd, "rm") == 0 && arg1) {
            cmd_rm(current_inode, arg1, user);
        }
        else if (strcmp(cmd, "rmdir") == 0 && arg1) {
            cmd_rmdir(current_inode, arg1, user);
        }
        else if (strcmp(cmd, "echo") == 0 && arg1) {
            char *filename = arg1;
            char *redir = arg2; // > ou >>
            char *content = arg3;

            if (redir && filename && content) {
                if (strcmp(redir, ">") == 0) {
                    cmd_echo_arrow(current_inode, filename, content, user);
                }
                else if (strcmp(redir, ">>") == 0) {
                    cmd_echo_arrow_arrow(current_inode, filename, content, user);
                }
            } else {
                printf("Uso: echo arquivo >|>> conteudo\n");
            }
            
        }
        else if (strcmp(cmd, "cat") == 0 && arg1) {
            cmd_cat(current_inode, arg1, user);
        }
        else if (strcmp(cmd, "ls") == 0) {
            cmd_ls(current_inode, arg1? arg1 : ".", user);
        }
        else if (strcmp(cmd, "cp") == 0 && arg1 && arg2) {
            cmd_cp(current_inode, ".", arg1, ".", arg2, user);
        }
        else if (strcmp(cmd, "mv") == 0 && arg1 && arg2) {
            cmd_mv(current_inode, ".", arg1, ".", arg2, user);
        }
        else if (strcmp(cmd, "ln") == 0 && arg1 && strcmp(arg1, "-s") == 0 && arg2 && arg3) {
            cmd_ln_s(current_inode, ".", arg2, ".", arg3, user);
        }
        else {
            printf("Comando não reconhecido\n");
        }
    }

    printf("Saindo...\n");
    unmount_fs();
    return 0;
}

