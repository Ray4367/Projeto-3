#define _POSIX_C_SOURCE 200809L

#include "estado_compartilhado.h"

#include <stdio.h>
#include <stdlib.h>

#define SHM_PADRAO "/reservas_shm"

int main(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(
            stderr,
            "Uso: %s [nome_shm]\n",
            argv[0]
        );

        return EXIT_FAILURE;
    }

    const char *nome =
        argc == 2
        ? argv[1]
        : SHM_PADRAO;

    estado_t *estado =
        estado_anexar(nome);

    if (estado == NULL) {
        fprintf(
            stderr,
            "Nao foi possivel anexar a SHM '%s'.\n",
            nome
        );

        return EXIT_FAILURE;
    }

    estado_snapshot_t snapshot;

    if (estado_snapshot(
            estado,
            &snapshot) != 0) {

        fprintf(
            stderr,
            "Erro ao obter snapshot.\n"
        );

        estado_fechar(estado);

        return EXIT_FAILURE;
    }

    printf(
        "Snapshot: %zu recursos\n",
        snapshot.quantidade
    );

    for (size_t i = 0;
         i < snapshot.quantidade;
         ++i) {

        if (snapshot.ocupado[i]) {
            printf(
                "[%02zu] OCUPADO - %s\n",
                i,
                snapshot.titular[i]
            );
        } else {
            printf(
                "[%02zu] LIVRE\n",
                i
            );
        }
    }

    estado_fechar(estado);

    return EXIT_SUCCESS;
}
