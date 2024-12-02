# Makefile for server and client programs

# Compiler and flags
CC = gcc
CFLAGS = -pthread -Wall -Wextra

# Targets
SERVER = server
CLIENT = client

# Source files
SERVER_SRC = server.c
CLIENT_SRC = client.c

# Default target
all: $(SERVER) $(CLIENT)

# Compile server
$(SERVER): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC)

# Compile client
$(CLIENT): $(CLIENT_SRC)
	$(CC) -o $(CLIENT) $(CLIENT_SRC)

# Clean up generated files
clean:
	rm -f $(SERVER) $(CLIENT)

# Phony targets
.PHONY: all clean

