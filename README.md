# Sistema Cliente-Servidor de Reservas

Implementação em **C17 para Linux**, usando apenas APIs padrão/POSIX, memória compartilhada POSIX, `pthread` e sockets TCP.

## 1. Cenário

Foi escolhido o **Cenário B — Central de Reservas**.

O sistema possui **64 recursos**, numerados de `0` a `63`. Cada recurso pode estar livre ou reservado por um titular de até 32 caracteres.

O objetivo principal é garantir que **dois clientes concorrentes nunca consigam reservar o mesmo recurso**.

O estado é mantido em uma região de **memória compartilhada POSIX**, permitindo acesso concorrente por:

* múltiplas threads do servidor;
* diferentes processos;
* processo `inspetor`, que acessa diretamente a SHM.

A estrutura possui tamanho fixo e não usa alocação dinâmica dentro da SHM.

## 2. Arquivos

```text
estado_compartilhado.h   Interface da biblioteca-monitor
estado_compartilhado.c   Implementação da SHM e sincronização
servidor.c               Servidor TCP thread-por-conexão
cliente.c                Cliente TCP interativo
inspetor.c               Processo de inspeção direta da SHM
Makefile                 Compilação
README.md                Documentação
```

## 3. Restrições

* C17;
* Linux + GCC;
* compilação com `-Wall -Wextra -Wpedantic`;
* sem bibliotecas externas;
* sockets TCP POSIX;
* POSIX shared memory (`shm_open`, `ftruncate`, `mmap`);
* sincronização com `pthread_mutex_t`;
* mutex configurado como `PTHREAD_PROCESS_SHARED`;
* mutex armazenado dentro da SHM;
* arrays de tamanho fixo;
* servidor thread-por-conexão;
* nenhuma sincronização do estado fora da biblioteca-monitor.

O projeto foi mantido simples e dimensionado para aproximadamente quatro horas de trabalho individual.

## 4. Compilação

O `Makefile` possui `all` como alvo padrão:

```bash
make
```

São produzidos exatamente estes binários:

```text
servidor
cliente
inspetor
```

Para limpar:

```bash
make clean
```

Flags utilizadas:

```text
-std=c17 -Wall -Wextra -Wpedantic -O2
```

Os programas que usam pthread são ligados com:

```text
-pthread
```

## 5. Execução

### Servidor

```bash
./servidor <porta> [nome_shm]
```

Exemplo:

```bash
./servidor 5000
```

A SHM padrão é:

```text
/reservas_shm
```

Também é possível especificar:

```bash
./servidor 5000 /minhas_reservas
```

O servidor é o **único criador e proprietário** da SHM.

### Cliente

Em outro terminal:

```bash
./cliente 127.0.0.1 5000
```

O cliente é interativo e envia uma requisição por linha.

### Inspetor

O `inspetor` é um processo separado e **não utiliza sockets**:

```bash
./inspetor
```

Ou:

```bash
./inspetor /minhas_reservas
```

Ele anexa diretamente à SHM e obtém um snapshot consistente através da biblioteca.

## 6. Protocolo

Cada requisição é uma linha terminada em `\n`.

### LIST

```text
LIST
```

Resposta:

```text
MAP <64 caracteres>
```

`0` representa recurso livre e `1` representa recurso ocupado.

### RESERVE

```text
RESERVE <id> <titular>
```

Respostas:

```text
OK
TAKEN
INVALID
```

### CANCEL

```text
CANCEL <id>
```

Respostas:

```text
OK
FREE
INVALID
```

### STATUS

```text
STATUS <id>
```

Respostas:

```text
FREE
TAKEN <titular>
INVALID
```

### QUIT

```text
QUIT
```

Resposta:

```text
BYE
```

Comandos desconhecidos ou malformados retornam:

```text
ERR <motivo>
```

## 7. Biblioteca-monitor

A biblioteca `estado_compartilhado` encapsula completamente a sincronização.

Sua API expõe somente operações de domínio:

```c
estado_criar()
estado_anexar()
estado_fechar()
estado_destruir()

estado_reservar()
estado_cancelar()
estado_status()
estado_snapshot()
```

O mutex **não faz parte da API pública**.

O servidor não chama `pthread_mutex_lock()` diretamente. O inspetor também não acessa o mutex.

### Primitiva escolhida

Foi escolhido:

```c
pthread_mutex_t
```

armazenado fisicamente dentro da SHM e configurado com:

```c
pthread_mutexattr_setpshared(
    &attr,
    PTHREAD_PROCESS_SHARED
);
```

Essa escolha é adequada porque o problema possui operações curtas que precisam de exclusão mútua, especialmente:

```text
verificar se está livre
        +
marcar como ocupado
```

Essas duas operações precisam ser indivisíveis.

Por exemplo:

```text
Cliente A: RESERVE 10 Alice
Cliente B: RESERVE 10 Bob
```

Somente uma delas pode observar o recurso 10 como livre:

```text
A -> OK
B -> TAKEN
```

ou vice-versa.

Assim, a dupla reserva é impedida tanto entre threads quanto entre processos.

## 8. Inicialização da SHM

O servidor cria a SHM usando:

```c
shm_open(nome, O_CREAT | O_EXCL | O_RDWR, 0600);
```

Depois utiliza:

```text
ftruncate()
mmap()
```

para criar e mapear o segmento.

`O_EXCL` garante que somente o processo criador inicialize a SHM.

O segmento possui um estado de inicialização:

```text
0 = não inicializado
1 = inicializando
2 = pronto
```

O `inspetor` espera o estado `pronto` antes de acessar o mutex.

O inspetor **nunca reinicializa** a sincronização.

## 9. Snapshot

O comando `LIST` e o processo `inspetor` utilizam:

```c
estado_snapshot()
```

A biblioteca adquire o mutex, copia todo o estado para uma estrutura local e libera o mutex.

Assim, o processo inspetor recebe uma fotografia consistente da SHM sem manipular diretamente a sincronização.

## 10. Limpeza

O servidor trata `SIGINT`/`SIGTERM`.

O handler apenas altera uma flag; a limpeza ocorre no fluxo principal.

Ao encerrar:

```text
parar de aceitar conexões
        ↓
aguardar as threads
        ↓
destruir pthread_mutex_t
        ↓
shm_unlink()
        ↓
munmap()
        ↓
close()
```

Isso evita destruir o mutex enquanto alguma thread ainda possa estar usando o monitor.

## 11. Arquitetura

```text
Cliente 1 ──TCP──┐
Cliente 2 ──TCP──┼──> servidor
Cliente 3 ──TCP──┘       │
                          │ API
                          ▼
                  biblioteca-monitor
                          │
                   pthread_mutex_t
                  PROCESS_SHARED
                          │
                          ▼
                      POSIX SHM
                          ▲
                          │
                    estado_snapshot()
                          │
                      inspetor
```

O servidor utiliza **uma thread por conexão**.

A sincronização do estado compartilhado fica exclusivamente na biblioteca.

## 12. Bônus

O bônus de **thread pool** não foi implementado.

O servidor utiliza o modelo obrigatório:

```text
conexão aceita
     ↓
pthread criada
     ↓
processamento
     ↓
fim da conexão
```

Portanto, não existe fila de tarefas nem fila de conexões no projeto.


