#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_LINHA 256
#define MAX_RESPOSTA 256

static int conectar(
    const char *host,
    const char *porta
)
{
    struct addrinfo hints;
    struct addrinfo *result;
    struct addrinfo *p;

    memset(
        &hints,
        0,
        sizeof(hints)
    );

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(
        host,
        porta,
        &hints,
        &result
    );

    if (rc != 0) {
        fprintf(
            stderr,
            "getaddrinfo: %s\n",
            gai_strerror(rc)
        );

        return -1;
    }

    int fd = -1;

    for (p = result;
         p != NULL;
         p = p->ai_next) {

        fd = socket(
            p->ai_family,
            p->ai_socktype,
            p->ai_protocol
        );

        if (fd == -1)
            continue;

        if (connect(
                fd,
                p->ai_addr,
                p->ai_addrlen
            ) == 0) {

            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);

    return fd;
}

static int enviar(
    int fd,
    const char *linha
)
{
    size_t tamanho = strlen(linha);
    size_t enviado = 0;

    while (enviado < tamanho) {
        ssize_t n = send(
            fd,
            linha + enviado,
            tamanho - enviado,
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

static int receber_linha(
    int fd,
    char *linha,
    size_t tamanho
)
{
    size_t pos = 0;

    while (pos + 1 < tamanho) {
        char c;

        ssize_t n = recv(
            fd,
            &c,
            1,
            0
        );

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
     * Descarta o restante da linha.
     */
    while (1) {
        char c;

        ssize_t n = recv(
            fd,
            &c,
            1,
            0
        );

        if (n <= 0)
            return n == 0 ? 0 : -1;

        if (c == '\n')
            break;
    }

    return -1;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(
            stderr,
            "Uso: %s <host> <porta>\n",
            argv[0]
        );

        return EXIT_FAILURE;
    }

    int fd = conectar(
        argv[1],
        argv[2]
    );

    if (fd == -1) {
        fprintf(
            stderr,
            "Nao foi possivel conectar.\n"
        );

        return EXIT_FAILURE;
    }

    char linha[MAX_LINHA];
    char resposta[MAX_RESPOSTA];

    while (1) {
        printf("> ");
        fflush(stdout);

        if (fgets(
                linha,
                sizeof(linha),
                stdin) == NULL) {

            break;
        }

        if (enviar(fd, linha) == -1) {
            fprintf(
                stderr,
                "Erro ao enviar.\n"
            );

            break;
        }

        int rc = receber_linha(
            fd,
            resposta,
            sizeof(resposta)
        );

        if (rc <= 0) {
            printf(
                "Servidor desconectado.\n"
            );

            break;
        }

        printf(
            "%s\n",
            resposta
        );

        if (strcmp(resposta, "BYE") == 0)
            break;
    }

    close(fd);

    return EXIT_SUCCESS;
}
