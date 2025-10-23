# Sistema de Arquivos Baseado em I-nodes

## Objetivo
Desenvolver um sistema de arquivos persistente que simule operações típicas de UNIX (touch, mkdir, ls, cat, etc.), utilizando i-nodes e um disco virtual armazenado em arquivo binário (disk.dat).

---

## Como Executar

1.  *Clone e acesse o repositório:*
    ```
    git clone https://github.com/FelipeFagundesCosta/SistemaDeArquivos
    cd SistemaDeArquivos
    ```

    

3.  *Compile no Terminal Linux:*
    ```
    gcc cmd.c fs.c -o cmd
    ```
    

4.  *Execute o programa principal:*
    ```
    ./cmd
    ```
    
5.  *Execute os comandos após a criação de seu disco:*
    
    - Na primeira execução, o programa criará automaticamente um arquivo de disco (disk.dat).

    - Nas execuções seguintes, ele montará o disco existente.

    - Você pode agora usar os comandos do sistema de arquivos (lista com comandos já implementados na seção [Comandos Implementados](#comandos)).

---
    

## Estrutura do Projeto

SistemaDeArquivos/

│

├── fs.h # Cabeçalho: structs e protótipos

├── fs.c # Implementação das funções do sistema de arquivos

└── cmd.c # Ponto de entrada do programa

---
<a id="comandos"></a>
## Comandos Implementados

O sistema de arquivos suporta os seguintes comandos:

### cd [diretório]

Muda o diretório atual para o especificado.
Exemplo:
```
cd /home/user/docs
```
### mkdir [diretório]

Cria um novo diretório. Suporta criação recursiva de caminhos.
Exemplo:
```
mkdir /home/user/docs/projetos
```
### touch [arquivo]

Cria um novo arquivo vazio. Também cria diretórios necessários caso não existam.
Exemplo:
```
touch /home/user/docs/arquivo.txt
```
### rm [arquivo]

Remove um arquivo do sistema. Não funciona para diretórios.
Exemplo:
```
rm /home/user/docs/arquivo.txt
```
### rmdir [diretório]

Remove um diretório vazio. Não remove arquivos dentro dele.
Exemplo:
```
rmdir /home/user/docs/antigo
```
### clear

Limpa o terminal.
Exemplo:
```
clear
```
### echo [conteúdo] > [arquivo]

Sobrescreve o conteúdo de um arquivo com o texto fornecido. Cria o arquivo se não existir.
Exemplo:
```
echo "Olá mundo" > /home/user/docs/ola.txt
```
### echo [conteúdo] >> [arquivo]

Anexa conteúdo ao final de um arquivo existente. Cria o arquivo se não existir.
Exemplo:
```
echo "Segunda linha" >> /home/user/docs/ola.txt
```
### cat [arquivo]

Exibe o conteúdo de um arquivo.
Exemplo:
```
cat /home/user/docs/ola.txt
```
### ls [opções] [diretório]

Lista o conteúdo de um diretório.

-l mostra informações detalhadas (permissões, proprietário, tamanho, data).
Exemplo:
```
ls
ls -l /home/user/docs
```
### cp [arquivo_origem] [arquivo_destino]

Copia um arquivo para outro caminho ou nome.
Exemplo:
```
cp arquivo.txt copia_arquivo.txt
```
### mv [arquivo_origem] [arquivo_destino]

Move ou renomeia um arquivo.
Exemplo:
```
mv arquivo.txt antigo_arquivo.txt
```
### ln -s [arquivo_alvo] [link]

Cria um link simbólico apontando para outro arquivo ou diretório.
Exemplo:
```
ln -s /home/user/docs/arquivo.txt link_para_arquivo.txt
```
### su [usuário]

Muda o usuário atual do sistema de arquivos.
Exemplo:
```
su novouser
```
### unlink [link]

Remove um link simbólico.
Exemplo:
```
unlink link_para_arquivo.txt
```
### df

Exibe informações sobre o uso do sistema de arquivos (número de blocos, usados, disponíveis, percentual).
Exemplo:
```
df
```

## Resultado Final 
Um programa em C capaz de:
- Criar e montar um disco virtual
- Manipular arquivos e diretórios usando i-nodes
- Persistir todas as alterações
- Interpretar caminhos absolutos e relativos
- Suportar links simbólicos
