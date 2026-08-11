#define _POSIX_C_SOURCE 200809L

#include "estado_compartilhado.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BACKLOG 16
#define MAX_LINHA 256
#define SHM_PADRAO "/reservas_shm"

static volatile sig_atomic_t parar = 0;

static estado_t *estado = NULL;

static void tratar_sinal(int sinal)
{
    (void)sinal;
    parar = 1;
}

static int enviar_linha(
    int fd,
    const char *linha
)
{
    size_t total = strlen(linha);
    size_t enviado = 0;

    while (enviado < total) {
        ssize_t n = send(
            fd,
            linha + enviado,
            total - enviado,
            MSG_NOSIGNAL
        );

        if (n < 0) {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (n == 0)
            return -1;

        enviado += (size_t)n;
    }

    return 0;
}

/*
 * Lê exatamente uma linha.
 *
 * Retorno:
 *  1 = linha recebida
 *  0 = conexão fechada
 * -1 = erro
 */
static int receber_linha(
    int fd,
    char *linha,
    size_t capacidade
)
{
    size_t pos = 0;

    while (pos + 1 < capacidade) {
        char c;

        ssize_t n = recv(fd, &c, 1, 0);

        if (n == 0)
            return 0;

        if (n < 0) {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (c == '\n') {
            linha[pos] = '\0';
            return 1;
        }

        if (c != '\r')
            linha[pos++] = c;
    }

    /*
     * Linha muito grande.
     * Descarta até encontrar '\n'.
     */
    while (1) {
        char c;

        ssize_t n = recv(fd, &c, 1, 0);

        if (n <= 0)
            return n == 0 ? 0 : -1;

        if (c == '\n')
            break;
    }

    snprintf(
        linha,
        capacidade,
        "__LINHA_MUITO_GRANDE__"
    );

    return 1;
}

static int processar(
    int fd,
    const char *linha
)
{
    char comando[16];

    if (sscanf(linha, "%15s", comando) != 1)
        return enviar_linha(
            fd,
            "ERR malformed\n"
        );

    /* LIST */
    if (strcmp(comando, "LIST") == 0) {
        char extra;

        if (sscanf(
                linha,
                "%*s %c",
                &extra) == 1) {

            return enviar_linha(
                fd,
                "ERR malformed\n"
            );
        }

        estado_snapshot_t snapshot;

        if (estado_snapshot(
                estado,
                &snapshot) != 0) {

            return enviar_linha(
                fd,
                "ERR internal\n"
            );
        }

        char resposta[80];

        int pos = snprintf(
            resposta,
            sizeof(resposta),
            "MAP "
        );

        for (size_t i = 0;
             i < snapshot.quantidade;
             ++i) {

            resposta[pos++] =
                snapshot.ocupado[i] ? '1' : '0';
        }

        resposta[pos++] = '\n';
        resposta[pos] = '\0';

        return enviar_linha(fd, resposta);
    }

    /* RESERVE */
    if (strcmp(comando, "RESERVE") == 0) {
        int id;
        char titular[ESTADO_MAX_TITULAR + 1];
        char extra;

        int n = sscanf(
            linha,
            "%*s %d %32s %c",
            &id,
            titular,
            &extra
        );

        if (n != 2)
            return enviar_linha(
                fd,
                "ERR malformed\n"
            );

        int rc = estado_reservar(
            estado,
            id,
            titular
        );

        if (rc == 0)
            return enviar_linha(fd, "OK\n");

        if (rc == 1)
            return enviar_linha(fd, "TAKEN\n");

        if (rc == 2)
            return enviar_linha(fd, "INVALID\n");

        return enviar_linha(
            fd,
            "ERR internal\n"
        );
    }

    /* CANCEL */
    if (strcmp(comando, "CANCEL") == 0) {
        int id;
        char extra;

        int n = sscanf(
            linha,
            "%*s %d %c",
            &id,
            &extra
        );

        if (n != 1)
            return enviar_linha(
                fd,
                "ERR malformed\n"
            );

        int rc = estado_cancelar(
            estado,
            id
        );

        if (rc == 0)
            return enviar_linha(fd, "OK\n");

        if (rc == 1)
            return enviar_linha(fd, "FREE\n");

        if (rc == 2)
            return enviar_linha(fd, "INVALID\n");

        return enviar_linha(
            fd,
            "ERR internal\n"
        );
    }

    /* STATUS */
    if (strcmp(comando, "STATUS") == 0) {
        int id;
        char extra;
        char titular[ESTADO_MAX_TITULAR + 1];

        int n = sscanf(
            linha,
            "%*s %d %c",
            &id,
            &extra
        );

        if (n != 1)
            return enviar_linha(
                fd,
                "ERR malformed\n"
            );

        int rc = estado_status(
            estado,
            id,
            titular,
            sizeof(titular)
        );

        if (rc == 0)
            return enviar_linha(fd, "FREE\n");

        if (rc == 1) {
            char resposta[64];

            snprintf(
                resposta,
                sizeof(resposta),
                "TAKEN %s\n",
                titular
            );

            return enviar_linha(
                fd,
                resposta
            );
        }

        if (rc == 2)
            return enviar_linha(
                fd,
                "INVALID\n"
            );

        return enviar_linha(
            fd,
            "ERR internal\n"
        );
    }

    /* QUIT */
    if (strcmp(comando, "QUIT") == 0) {
        char extra;

        if (sscanf(
                linha,
                "%*s %c",
                &extra) == 1) {

            return enviar_linha(
                fd,
                "ERR malformed\n"
            );
        }

        enviar_linha(fd, "BYE\n");

        return 1;
    }

    return enviar_linha(
        fd,
        "ERR unknown_command\n"
    );
}

static void *atender_cliente(void *arg)
{
    int fd = *(int *)arg;

    free(arg);

    char linha[MAX_LINHA];

    while (!parar) {
        int rc = receber_linha(
            fd,
            linha,
            sizeof(linha)
        );

        if (rc <= 0)
            break;

        if (strcmp(
                linha,
                "__LINHA_MUITO_GRANDE__") == 0) {

            enviar_linha(
                fd,
                "ERR line_too_long\n"
            );

            continue;
        }

        int terminou = processar(
            fd,
            linha
        );

        if (terminou == 1)
            break;

        if (terminou < 0)
            break;
    }

    close(fd);

    return NULL;
}

static int criar_servidor(int porta)
{
    int fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (fd == -1)
        return -1;

    int reutilizar = 1;

    setsockopt(
        fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reutilizar,
        sizeof(reutilizar)
    );

    struct sockaddr_in endereco;

    memset(
        &endereco,
        0,
        sizeof(endereco)
    );

    endereco.sin_family = AF_INET;
    endereco.sin_addr.s_addr =
        htonl(INADDR_ANY);
    endereco.sin_port =
        htons((uint16_t)porta);

    if (bind(
            fd,
            (struct sockaddr *)&endereco,
            sizeof(endereco)
        ) == -1) {

        close(fd);
        return -1;
    }

    if (listen(fd, BACKLOG) == -1) {
        close(fd);
        return -1;
    }

    return fd;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(
            stderr,
            "Uso: %s <porta> [nome_shm]\n",
            argv[0]
        );

        return EXIT_FAILURE;
    }

    char *fim;
    long porta = strtol(
        argv[1],
        &fim,
        10
    );

    if (*argv[1] == '\0' ||
        *fim != '\0' ||
        porta < 1 ||
        porta > 65535) {

        fprintf(
            stderr,
            "Porta invalida.\n"
        );

        return EXIT_FAILURE;
    }

    const char *nome_shm =
        argc == 3
        ? argv[2]
        : SHM_PADRAO;

    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = tratar_sinal;

    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /*
     * Somente o servidor cria a SHM.
     */
    estado = estado_criar(
        nome_shm,
        ESTADO_MAX_RECURSOS
    );

    if (estado == NULL) {
        fprintf(
            stderr,
            "Erro ao criar SHM '%s'.\n",
            nome_shm
        );

        return EXIT_FAILURE;
    }

    int servidor_fd =
        criar_servidor((int)porta);

    if (servidor_fd == -1) {
        perror("socket/bind/listen");

        estado_destruir(estado);
        estado_fechar(estado);

        return EXIT_FAILURE;
    }

    printf(
        "Servidor na porta %ld, SHM %s\n",
        porta,
        nome_shm
    );

    while (!parar) {
        struct sockaddr_in cliente;
        socklen_t tamanho =
            sizeof(cliente);

        int cliente_fd = accept(
            servidor_fd,
            (struct sockaddr *)&cliente,
            &tamanho
        );

        if (cliente_fd == -1) {
            if (errno == EINTR)
                continue;

            perror("accept");
            break;
        }

        int *fd = malloc(sizeof(*fd));

        if (fd == NULL) {
            enviar_linha(
                cliente_fd,
                "ERR server_busy\n"
            );

            close(cliente_fd);
            continue;
        }

        *fd = cliente_fd;

        pthread_t thread;

        if (pthread_create(
                &thread,
                NULL,
                atender_cliente,
                fd
            ) != 0) {

            free(fd);

            enviar_linha(
                cliente_fd,
                "ERR server_busy\n"
            );

            close(cliente_fd);
            continue;
        }

        /*
         * A thread é independente.
         *
         * O estado só é destruído depois que o processo
         * deixar de aceitar conexões e as threads forem
         * encerradas.
         */
        pthread_detach(thread);
    }

    close(servidor_fd);

    /*
     * O shutdown das threads é feito pelo fechamento
     * das conexões no fluxo de encerramento.
     *
     * Como as threads são detached, damos um pequeno
     * período para elas terminarem antes da destruição.
     */
    sleep(1);

    estado_destruir(estado);
    estado_fechar(estado);
    estado = NULL;

    return EXIT_SUCCESS;
}
