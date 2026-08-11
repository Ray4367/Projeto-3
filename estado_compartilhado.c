/* estado_compartilhado.c */

#define _POSIX_C_SOURCE 200809L

#include "estado_compartilhado.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define ESTADO_NAO_INICIALIZADO 0u
#define ESTADO_INICIALIZANDO    1u
#define ESTADO_PRONTO           2u

#define ESTADO_MAGIC   UINT64_C(0x5245534552564153)
#define ESTADO_VERSION 1u

typedef struct {
    uint32_t inicializacao;
    uint32_t quantidade;

    uint64_t magic;
    uint32_t version;

    pthread_mutex_t mutex;

    unsigned char ocupado[ESTADO_MAX_RECURSOS];

    char titular[
        ESTADO_MAX_RECURSOS
    ][ESTADO_MAX_TITULAR + 1];

} estado_shm_t;

struct estado {
    int fd;
    estado_shm_t *shm;
    int dono;
    char nome[256];
};

static int nome_valido(const char *nome)
{
    if (nome == NULL)
        return 0;

    if (nome[0] != '/')
        return 0;

    if (strlen(nome) >= sizeof(((estado_t *)0)->nome))
        return 0;

    return 1;
}

static int mutex_inicializar(pthread_mutex_t *mutex)
{
    pthread_mutexattr_t attr;

    if (pthread_mutexattr_init(&attr) != 0)
        return -1;

    if (pthread_mutexattr_setpshared(
            &attr,
            PTHREAD_PROCESS_SHARED
        ) != 0) {

        pthread_mutexattr_destroy(&attr);
        return -1;
    }

    if (pthread_mutex_init(mutex, &attr) != 0) {
        pthread_mutexattr_destroy(&attr);
        return -1;
    }

    pthread_mutexattr_destroy(&attr);

    return 0;
}

estado_t *estado_criar(
    const char *nome,
    size_t quantidade
)
{
    if (!nome_valido(nome))
        return NULL;

    if (quantidade == 0 ||
        quantidade > ESTADO_MAX_RECURSOS)
        return NULL;

    int fd = shm_open(
        nome,
        O_CREAT | O_EXCL | O_RDWR,
        0600
    );

    if (fd == -1)
        return NULL;

    if (ftruncate(
            fd,
            (off_t)sizeof(estado_shm_t)
        ) == -1) {

        close(fd);
        shm_unlink(nome);
        return NULL;
    }

    estado_shm_t *shm = mmap(
        NULL,
        sizeof(estado_shm_t),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    );

    if (shm == MAP_FAILED) {
        close(fd);
        shm_unlink(nome);
        return NULL;
    }

    estado_t *estado = calloc(
        1,
        sizeof(*estado)
    );

    if (estado == NULL) {
        munmap(
            shm,
            sizeof(estado_shm_t)
        );

        close(fd);
        shm_unlink(nome);

        return NULL;
    }

    estado->fd = fd;
    estado->shm = shm;
    estado->dono = 1;

    strncpy(
        estado->nome,
        nome,
        sizeof(estado->nome) - 1
    );

    /*
     * O segmento acabou de ser criado.
     * O valor 0 significa que a inicialização ainda
     * não foi publicada.
     */
    __atomic_store_n(
        &shm->inicializacao,
        ESTADO_INICIALIZANDO,
        __ATOMIC_RELEASE
    );

    shm->quantidade = (uint32_t)quantidade;
    shm->magic = ESTADO_MAGIC;
    shm->version = ESTADO_VERSION;

    memset(
        shm->ocupado,
        0,
        sizeof(shm->ocupado)
    );

    memset(
        shm->titular,
        0,
        sizeof(shm->titular)
    );

    if (mutex_inicializar(&shm->mutex) != 0) {
        munmap(
            shm,
            sizeof(estado_shm_t)
        );

        close(fd);
        shm_unlink(nome);
        free(estado);

        return NULL;
    }

    /*
     * O mutex e o restante do estado já estão prontos.
     * Somente agora outros processos podem anexar e usar
     * a SHM.
     */
    __atomic_store_n(
        &shm->inicializacao,
        ESTADO_PRONTO,
        __ATOMIC_RELEASE
    );

    return estado;
}

estado_t *estado_anexar(const char *nome)
{
    if (!nome_valido(nome))
        return NULL;

    int fd = shm_open(
        nome,
        O_RDWR,
        0600
    );

    if (fd == -1)
        return NULL;

    estado_shm_t *shm = mmap(
        NULL,
        sizeof(estado_shm_t),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    );

    if (shm == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    /*
     * Pode ocorrer de o processo anexar à SHM depois do
     * shm_open(), mas antes de o servidor publicar
     * ESTADO_INICIALIZANDO.
     *
     * Portanto, os estados 0 e 1 são ambos estados de
     * espera. Nunca tocamos no mutex enquanto a SHM não
     * estiver publicada como pronta.
     */
    while (1) {
        uint32_t inicializacao =
            __atomic_load_n(
                &shm->inicializacao,
                __ATOMIC_ACQUIRE
            );

        if (inicializacao == ESTADO_PRONTO)
            break;

        if (inicializacao != ESTADO_NAO_INICIALIZADO &&
            inicializacao != ESTADO_INICIALIZANDO) {

            munmap(
                shm,
                sizeof(estado_shm_t)
            );

            close(fd);

            return NULL;
        }

        sched_yield();
    }

    /*
     * Depois de ESTADO_PRONTO, o servidor já publicou
     * magic, version, quantidade e inicializou o mutex.
     */
    if (shm->magic != ESTADO_MAGIC ||
        shm->version != ESTADO_VERSION ||
        shm->quantidade == 0 ||
        shm->quantidade > ESTADO_MAX_RECURSOS) {

        munmap(
            shm,
            sizeof(estado_shm_t)
        );

        close(fd);

        return NULL;
    }

    estado_t *estado = calloc(
        1,
        sizeof(*estado)
    );

    if (estado == NULL) {
        munmap(
            shm,
            sizeof(estado_shm_t)
        );

        close(fd);

        return NULL;
    }

    estado->fd = fd;
    estado->shm = shm;
    estado->dono = 0;

    strncpy(
        estado->nome,
        nome,
        sizeof(estado->nome) - 1
    );

    return estado;
}

void estado_fechar(estado_t *estado)
{
    if (estado == NULL)
        return;

    munmap(
        estado->shm,
        sizeof(estado_shm_t)
    );

    close(estado->fd);

    free(estado);
}

int estado_destruir(estado_t *estado)
{
    if (estado == NULL || !estado->dono)
        return -1;

    int erro = 0;

    if (pthread_mutex_destroy(
            &estado->shm->mutex
        ) != 0) {

        erro = 1;
    }

    if (shm_unlink(estado->nome) == -1) {
        erro = 1;
    }

    return erro ? -1 : 0;
}

int estado_reservar(
    estado_t *estado,
    int id,
    const char *titular
)
{
    if (estado == NULL || titular == NULL)
        return -1;

    if (id < 0 ||
        (size_t)id >= estado->shm->quantidade)
        return 2;

    size_t tamanho = strlen(titular);

    if (tamanho == 0 ||
        tamanho > ESTADO_MAX_TITULAR)
        return -1;

    if (pthread_mutex_lock(
            &estado->shm->mutex
        ) != 0) {

        return -1;
    }

    if (estado->shm->ocupado[id]) {
        pthread_mutex_unlock(
            &estado->shm->mutex
        );

        return 1;
    }

    estado->shm->ocupado[id] = 1;

    memcpy(
        estado->shm->titular[id],
        titular,
        tamanho + 1
    );

    pthread_mutex_unlock(
        &estado->shm->mutex
    );

    return 0;
}

int estado_cancelar(
    estado_t *estado,
    int id
)
{
    if (estado == NULL)
        return -1;

    if (id < 0 ||
        (size_t)id >= estado->shm->quantidade)
        return 2;

    if (pthread_mutex_lock(
            &estado->shm->mutex
        ) != 0) {

        return -1;
    }

    if (!estado->shm->ocupado[id]) {
        pthread_mutex_unlock(
            &estado->shm->mutex
        );

        return 1;
    }

    estado->shm->ocupado[id] = 0;
    estado->shm->titular[id][0] = '\0';

    pthread_mutex_unlock(
        &estado->shm->mutex
    );

    return 0;
}

int estado_status(
    estado_t *estado,
    int id,
    char *titular,
    size_t tamanho_titular
)
{
    if (estado == NULL)
        return -1;

    if (id < 0 ||
        (size_t)id >= estado->shm->quantidade)
        return 2;

    if (pthread_mutex_lock(
            &estado->shm->mutex
        ) != 0) {

        return -1;
    }

    if (!estado->shm->ocupado[id]) {
        pthread_mutex_unlock(
            &estado->shm->mutex
        );

        return 0;
    }

    if (titular != NULL && tamanho_titular > 0) {
        strncpy(
            titular,
            estado->shm->titular[id],
            tamanho_titular - 1
        );

        titular[tamanho_titular - 1] = '\0';
    }

    pthread_mutex_unlock(
        &estado->shm->mutex
    );

    return 1;
}

int estado_snapshot(
    estado_t *estado,
    estado_snapshot_t *snapshot
)
{
    if (estado == NULL || snapshot == NULL)
        return -1;

    if (pthread_mutex_lock(
            &estado->shm->mutex
        ) != 0) {

        return -1;
    }

    snapshot->quantidade =
        estado->shm->quantidade;

    memcpy(
        snapshot->ocupado,
        estado->shm->ocupado,
        sizeof(snapshot->ocupado)
    );

    memcpy(
        snapshot->titular,
        estado->shm->titular,
        sizeof(snapshot->titular)
    );

    pthread_mutex_unlock(
        &estado->shm->mutex
    );

    return 0;
}
