# Projeto-3

Cenário escolhido: **Cenário B - Central de Reservas**

O sistema representa um conjunto fixo de 64 recursos, numerados de `0` a `63`.
Cada recurso pode estar:

- Livre; 
- Ocupado por um titular.

Um cliente pode reservar ou cancelar um recurso e consultar seu estado.

O principal requisito de concorrência é impedir que dois clientes reservem
simultaneamente o mesmo recurso.

O estado dos recursos não fica no servidor em memória privada. Ele fica em
uma região de memória compartilhada POSIX, permitindo que:

- várias threads do servidor acessem o mesmo estado;
- diferentes processos acessem o mesmo estado;
- o processo `inspetor` consulte diretamente a SHM.

A estrutura da SHM possui tamanho fixo e não utiliza alocação dinâmica.

O projeto possui os seguintes arquivos:

- `estado_compartilhado.h` — interface pública do monitor;
- `estado_compartilhado.c` — implementação do monitor e da SHM;
- `servidor.c` — servidor TCP thread-por-conexão;
- `cliente.c` — cliente TCP interativo;
- `inspetor.c` — processo que acessa diretamente a SHM;
- `Makefile` — compilação;
- `README.md` — documentação.

Requisitos:

- Linux;
- GCC;
- C17;
- APIs POSIX;
- pthreads.

Não são utilizadas bibliotecas externas.

A compilação é feita com:

```bash
make
