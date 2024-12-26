# 編譯器與編譯選項
CC = gcc
CFLAGS = -Wall -Wextra -std=c99
LDFLAGS = -lavcodec -lavformat -lavutil -lswscale -lSDL2 -lpthread
TARGETS = server client

# 規則
all: $(TARGETS)

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) -o client client.c $(LDFLAGS)

clean:
	rm -f $(TARGETS)

.PHONY: all clean
