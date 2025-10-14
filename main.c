#include <stdio.h>
#include "fs.h"

int main(void) {
    if (init_fs() == 0) {
        printf("Sistema de arquivos inicializado com sucesso.\n");
    } else {
        printf("Falha na inicialização do sistema de arquivos.\n");
    }

    return 0;
}
