CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
INCLUDE = -Iinclude
SRC = src
BIN = bin

all: server client

server: $(SRC)/server.c include/chat_config.h
	$(CC) $(CFLAGS) $(INCLUDE) -o server $(SRC)/server.c

client: $(SRC)/client.c include/chat_config.h
	$(CC) $(CFLAGS) $(INCLUDE) -o client $(SRC)/client.c

clean:
	rm -f server client
	# do not remove /tmp FIFOs automatically

.PHONY: all clean
