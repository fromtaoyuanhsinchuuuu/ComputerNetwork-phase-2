#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "config.h"

int c_receiver_port;
int conn_fd;
char buf[BUFFER_SIZE];

void show_main_menu();
void show_logged_in_menu();
void send_register();
void send_login();
void *c_receiver(void *arg);
void handle_logged_in_interface(const char *username);

int main() {
    printf("Enter receiver port: ");
    scanf("%d", &c_receiver_port);

    conn_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn_fd < 0) ERR_EXIT("socket");

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &servaddr.sin_addr) <= 0) ERR_EXIT("Invalid IP");

    if (connect(conn_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
        ERR_EXIT("connect");

    char server_response[BUFFER_SIZE];
    recv(conn_fd, server_response, sizeof(server_response), 0);
    if (strcmp(server_response, QUEUE_FULL) == 0) {
        printf("Server response: queue full\n");
        close(conn_fd);
        exit(0);
    } else if (strcmp(server_response, ACCEPT_TASK) == 0) {
        printf("Server response: accept task\n");
        pthread_t receiver_thread;
        pthread_create(&receiver_thread, NULL, c_receiver, NULL);
    }

    while (1) {
        show_main_menu();
        int choice;
        printf("Enter your choice: ");
        scanf("%d", &choice);

        switch (choice) {
            case 1:
                send_register();
                break;
            case 2:
                send_login();
                break;
            case 3:
                printf("Exiting...\n");
                close(conn_fd);
                exit(0);
            default:
                printf("Unknown choice. Please try again.\n");
        }
    }

    return 0;
}

void show_main_menu() {
    printf("\nPlease choose one of the following options:\n");
    printf("1. Register\n");
    printf("2. Login\n");
    printf("3. Exit\n");
}

void show_logged_in_menu() {
    printf("\nChoose an action:\n");
    printf("1. Show online users\n");
    printf("2. Send message\n");
    printf("3. Log out\n");
}

void send_register() {
    char name[MAX_NAME + 1];
    printf("Enter your username (max %d characters): ", MAX_NAME);
    scanf("%s", name);

    snprintf(buf, sizeof(buf), "register:%s", name);
    send(conn_fd, buf, strlen(buf), 0);

    memset(buf, 0, sizeof(buf));
    recv(conn_fd, buf, sizeof(buf), 0);
    printf("Server response: %s\n", buf);
}

void send_login() {
    char name[MAX_NAME + 1];
    printf("Enter your username: ");
    scanf("%s", name);

    snprintf(buf, sizeof(buf), "login:%s", name);
    send(conn_fd, buf, strlen(buf), 0);

    memset(buf, 0, sizeof(buf));
    recv(conn_fd, buf, sizeof(buf), 0);

    if (strcmp(buf, ASK_RCVR_PORT) == 0) {
        snprintf(buf, sizeof(buf), "%d", c_receiver_port);
        send(conn_fd, buf, strlen(buf), 0);

        memset(buf, 0, sizeof(buf));
        recv(conn_fd, buf, sizeof(buf), 0);
        if (strcmp(buf, LOGIN_SUCCESS) == 0) {
            printf("Login successful!\n");
            handle_logged_in_interface(name);
        } else {
            printf("Login failed: %s\n", buf);
        }
    } else {
        printf("Login failed: %s\n", buf);
    }
}

void *c_receiver(void *arg) {
    printf("Receiver thread started on port %d\n", c_receiver_port);
    while (1) {
        char message[BUFFER_SIZE];
        memset(message, 0, sizeof(message));
        recv(conn_fd, message, sizeof(message), 0);
        printf("[Server]: %s\n", message);
    }
    return NULL;
}

void handle_logged_in_interface(const char *username) {
    while (1) {
        show_logged_in_menu();
        int choice;
        printf("Enter your choice: ");
        scanf("%d", &choice);

        if (choice == 1) {
            send(conn_fd, SHOW_LIST, strlen(SHOW_LIST), 0);
            memset(buf, 0, sizeof(buf));
            recv(conn_fd, buf, sizeof(buf), 0);
            printf("Online users: %s\n", buf);
        } else if (choice == 2) {
            char message[MAX_MES];
            printf("Enter your message: ");
            scanf(" %[^]", message);

            snprintf(buf, sizeof(buf), "message:%s:%s", username, message);
            send(conn_fd, buf, strlen(buf), 0);
        } else if (choice == 3) {
            send(conn_fd, LOGOUT, strlen(LOGOUT), 0);
            printf("Logged out successfully.\n");
            break;
        } else {
            printf("Unknown choice. Please try again.\n");
        }
    }
}
