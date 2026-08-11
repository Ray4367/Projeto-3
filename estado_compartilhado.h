/* estado_compartilhado.h */

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

estado_t *estado_criar(
    const char *nome,
    size_t quantidade
);

estado_t *estado_anexar(
    const char *nome
);

void estado_fechar(
    estado_t *estado
);

int estado_destruir(
    estado_t *estado
);

int estado_reservar(
    estado_t *estado,
    int id,
    const char *titular
);

int estado_cancelar(
    estado_t *estado,
    int id
);

int estado_status(
    estado_t *estado,
    int id,
    char *titular,
    size_t tamanho_titular
);

int estado_snapshot(
    estado_t *estado,
    estado_snapshot_t *snapshot
);

#endif
