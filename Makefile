CC = gcc
CFLAGS = -Wall -Wextra -pthread
INCLUDE = -Iinclude

SERVER_SRC = server.c src/send_recv_all.c src/stampa_delimitatore.c
CLIENT_SRC = client.c src/send_recv_all.c src/stampa_delimitatore.c

SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)

all: server client

server: $(SERVER_OBJ)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $(SERVER_OBJ)

client: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $(CLIENT_OBJ)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

clean:
	rm -f server client $(SERVER_OBJ) $(CLIENT_OBJ)
