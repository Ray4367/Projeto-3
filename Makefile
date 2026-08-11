CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -O2
LDFLAGS = -pthread

TARGETS = servidor cliente inspetor

all: $(TARGETS)

servidor: servidor.o estado_compartilhado.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

cliente: cliente.o
	$(CC) $(CFLAGS) -o $@ $^

inspetor: inspetor.o estado_compartilhado.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

servidor.o: servidor.c estado_compartilhado.h
	$(CC) $(CFLAGS) -c servidor.c

cliente.o: cliente.c
	$(CC) $(CFLAGS) -c cliente.c

inspetor.o: inspetor.c estado_compartilhado.h
	$(CC) $(CFLAGS) -c inspetor.c

estado_compartilhado.o: estado_compartilhado.c estado_compartilhado.h
	$(CC) $(CFLAGS) -c estado_compartilhado.c

clean:
	rm -f *.o $(TARGETS)

.PHONY: all clean
