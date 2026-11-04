CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = echo_server

.PHONY: all clean

all: $(TARGET)

$(TARGET): echo_server.c
	$(CC) $(CFLAGS) echo_server.c -o $(TARGET)

clean:
	rm -f $(TARGET)
