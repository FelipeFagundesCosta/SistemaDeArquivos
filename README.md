# Sistema de Arquivos Baseado em I-nodes

## Objetivo
Desenvolver um sistema de arquivos persistente que simule operações típicas de UNIX (`touch`, `mkdir`, `ls`, `cat`, etc.), utilizando i-nodes e um disco virtual armazenado em arquivo binário (`disk.dat`).

---

## Estrutura do Projeto

meufs/
│
├── fs.h # Cabeçalho: structs e protótipos
├── fs.c # Implementação das funções do sistema de arquivos
└── main.c # Ponto de entrada do programa


---

## Checkpoints de Desenvolvimento

### ✅ Etapa 1 – Criar disco virtual
- [x] Criar arquivo `disk.dat` com 64 MB
- [x] Inicializar função `init_fs()` em `fs.c`
- [x] Chamar `init_fs()` em `main.c` e validar retorno

**Objetivo**: Ter um disco virtual criado e acessível.

---

### ✅ Etapa 2 – Definir e gravar superbloco
- [x] Criar `superblock_t` em `fs.h`
- [x] Inicializar valores (`magic`, `disk_size`, `block_size`)
- [x] Gravar superbloco no início do disco (`fwrite`)

**Objetivo**: Disco formatado com metadados iniciais.

---

### ⏳ Etapa 3 – Implementar bitmaps
- Criar bitmap de blocos (1 bit = bloco livre/ocupado)
- Criar bitmap de i-nodes (1 bit = i-node livre/ocupado)
- Inicializar ambos no disco após o superbloco
- Funções auxiliares:
  - `allocate_block()`
  - `free_block()`
  - `allocate_inode()`
  - `free_inode()`

**Objetivo**: Gerenciar alocação de blocos e i-nodes de forma persistente.

---

### ⏳ Etapa 4 – Definir estrutura de i-node
- Estrutura `inode_t` com:
  - Nome do arquivo/diretório
  - Dono / Criador
  - Tamanho do arquivo
  - Data de criação/modificação
  - Permissões
  - Apontadores para blocos de dados
  - Apontador para i-node extra (se necessário)
- Reservar espaço para tabela de i-nodes no disco
- Funções auxiliares para ler/escrever i-nodes

**Objetivo**: Representar arquivos e diretórios no FS.

---

### ⏳ Etapa 5 – Criar operações de arquivos
- `touch arquivo`
- `rm arquivo`
- `echo "conteúdo" > arquivo`
- `echo "conteúdo" >> arquivo`
- `cat arquivo`
- `cp arquivo1 arquivo2`
- `mv arquivo1 arquivo2`
- `ln -s arquivoOriginal link`

**Objetivo**: Manipular arquivos usando i-nodes e blocos de dados.

---

### ⏳ Etapa 6 – Criar operações de diretórios
- `mkdir diretorio`
- `rmdir diretorio`
- `ls diretorio`
- `cd diretorio`
- `mv diretorio1 diretorio2`
- `ln -s diretorioOriginal link`

**Objetivo**: Manipular hierarquia de diretórios com links simbólicos.

---

### ⏳ Etapa 7 – Caminhos absolutos e relativos
- Implementar interpretação de caminhos:
  - Absolutos (`/home/user/file.txt`)
  - Relativos (`../arquivo.txt`)
- Implementar diretórios especiais:
  - `.` (diretório atual)
  - `..` (diretório pai)

**Objetivo**: Navegação completa no FS.

---

### ⏳ Etapa 8 – Persistência total
- Garantir que todas as alterações em arquivos e diretórios sejam gravadas no disco
- Implementar função `mount_fs()` para ler superbloco, bitmaps e tabela de i-nodes ao iniciar o programa
- Função `unmount_fs()` opcional para sincronizar dados antes de encerrar

**Objetivo**: Sistema de arquivos persistente entre execuções.

---

### ⏳ Etapa 9 – Testes finais e documentação
- Criar scripts de teste para cada comando
- Documentar todas as funções no código (`Doxygen` style opcional)
- Instruções de uso no README

**Objetivo**: Projeto completo, testado e documentado.

---

## ⚡ Dicas de Implementação
- Sempre atualizar bitmaps ao alocar/liberar blocos e i-nodes
- Cada i-node pode ter múltiplos blocos, use ponteiros diretos e indiretos se necessário
- Comece pelas operações mais simples (`touch`, `mkdir`) e evolua
- Teste cada passo antes de passar para o próximo

---

## 📌 Resultado Final Esperado
Um programa em C capaz de:
- Criar e formatar um disco virtual
- Manipular arquivos e diretórios usando i-nodes
- Persistir todas as alterações
- Interpretar caminhos absolutos e relativos
- Suportar links simbólicos
