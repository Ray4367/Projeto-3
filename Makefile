CC = gcc

CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -O2
LDFLAGS = -pthread

.PHONY: all clean

all: servidor cliente inspetor

servidor: servidor.o estado_compartilhado.o
	$(CC) $(CFLAGS) $^ -o servidor $(LDFLAGS)

cliente: cliente.o
	$(CC) $(CFLAGS) $^ -o cliente

inspetor: inspetor.o estado_compartilhado.o
	$(CC) $(CFLAGS) $^ -o inspetor $(LDFLAGS)

servidor.o: servidor.c estado_compartilhado.h
	$(CC) $(CFLAGS) -c servidor.c -o servidor.o

cliente.o: cliente.c
	$(CC) $(CFLAGS) -c cliente.c -o cliente.o

inspetor.o: inspetor.c estado_compartilhado.h
	$(CC) $(CFLAGS) -c inspetor.c -o inspetor.o

estado_compartilhado.o: estado_compartilhado.c estado_compartilhado.h
	$(CC) $(CFLAGS) -c estado_compartilhado.c -o estado_compartilhado.o

clean:
	rm -f servidor cliente inspetor *.o
