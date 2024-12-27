CC = gcc
CFLAGS = -Wall -Wextra -std=c99
LDFLAGS = -lssl -lcrypto -lpthread -lgtk-3 -lgdk-3 -lpangocairo-1.0 -lpango-1.0 \
          -latk-1.0 -lcairo-gobject -lcairo -lgdk_pixbuf-2.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0

# FFmpeg and SDL2 flags
AV_LIBS = -lavcodec -lavformat -lavutil -lswscale -lSDL2

# GTK flags
GTK_FLAGS = $(shell pkg-config --cflags gtk+-3.0)

all: server client

server: server.c config.c
	$(CC) $(CFLAGS) $(GTK_FLAGS) -o server server.c config.c $(LDFLAGS) $(AV_LIBS)

client: client.c config.c
	$(CC) $(CFLAGS) $(GTK_FLAGS) -o client client.c config.c $(LDFLAGS) $(AV_LIBS)

clean:
	rm -f server client *.o

server_ssl:
	openssl req -x509 -nodes -days 365 -newkey rsa:2048 -keyout server.key -out server.crt

