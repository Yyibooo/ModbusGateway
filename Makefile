CC = gcc
CFLAGS = -Wall -Wextra -g
TARGETS = echo_server echo_client

.PHONY: all clean

all: $(TARGETS)

echo_server: echo_server.c
	$(CC) $(CFLAGS) echo_server.c -o echo_server

echo_client: echo_client.c
	$(CC) $(CFLAGS) echo_client.c -o echo_client

clean:
	rm -f $(TARGETS)
