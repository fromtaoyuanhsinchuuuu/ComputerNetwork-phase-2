all:
	gcc -o server server.c config.c -lssl -lcrypto -lpthread
	gcc -o client client.c config.c -lssl -lcrypto -lpthread `pkg-config --cflags --libs gtk+-3.0`

clean:
	rm client server

server_ssl:
	openssl req -x509 -nodes -days 365 -newkey rsa:2048 -keyout server.key -out server.crt

