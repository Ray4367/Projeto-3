#ifndef ESTADO_COMPARTILHADO_H
#define ESTADO_COMPARTILHADO_H

#include <stddef.h>

#define ESTADO_MAX_RECURSOS 64
#define ESTADO_MAX_TITULAR 32

typedef struct estado estado_t;

typedef struct {
    size_t quantidade;
    unsigned char ocupado[ESTADO_MAX_RECURSOS];
    char titular[ESTADO_MAX_RECURSOS][ESTADO_MAX_TITULAR + 1];
} estado_snapshot_t;

/*
 * Cria e inicializa a SHM.
 *
 * Somente o servidor deve chamar esta função.
 * Retorna NULL se a SHM já existir ou ocorrer erro.
 */
estado_t *estado_criar(const char *nome, size_t quantidade);

/*
 * Anexa a uma SHM já criada.
 *
 * Não inicializa nem modifica o mutex.
 */
estado_t *estado_anexar(const char *nome);

/*
 * Desanexa deste processo.
 */
void estado_fechar(estado_t *estado);

/*
 * Destrói o mutex e remove a SHM.
 *
 * Deve ser chamado somente pelo servidor,
 * depois que todas as threads terminarem.
 */
int estado_destruir(estado_t *estado);

/*
 * RESERVE
 *
 * Retorno:
 *   0 = OK
 *   1 = TAKEN
 *   2 = INVALID
 *  -1 = erro
 */
int estado_reservar(
    estado_t *estado,
    int id,
    const char *titular
);

/*
 * CANCEL
 *
 * Retorno:
 *   0 = OK
 *   1 = FREE
 *   2 = INVALID
 *  -1 = erro
 */
int estado_cancelar(estado_t *estado, int id);

/*
 * STATUS
 *
 * Retorno:
 *   0 = FREE
 *   1 = TAKEN
 *   2 = INVALID
 *  -1 = erro
 */
int estado_status(
    estado_t *estado,
    int id,
    char *titular,
    size_t tamanho_titular
);

/*
 * Obtém uma cópia consistente de todo o estado.
 *
 * Retorno:
 *   0 = sucesso
 *  -1 = erro
 */
int estado_snapshot(
    estado_t *estado,
    estado_snapshot_t *snapshot
);

#endif
