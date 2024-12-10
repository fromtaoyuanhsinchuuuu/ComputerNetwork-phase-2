#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#define CLINET
#include "config.h"

#define RESPONSE "RESPONSE:"
#define BROADCAST "BROADCAST:"
#define ERROR "ERROR:"

int bytes_received = -1;
char buf[BUFFER_SIZE];

int conn_fd = -1;

void show_menu();
void send_register(int conn_fd);
void send_login(int conn_fd);
void logged_in_interface(int conn_fd, char* username);
void handle_user_list(const char *message);
void* listen_for_server(void* arg);

int main() {
    conn_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn_fd < 0) ERR_EXIT("socket");

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &servaddr.sin_addr) <= 0) ERR_EXIT("Invalid IP");

    if (connect(conn_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) ERR_EXIT("connect");

    printf("Connected to server at port %d\n", PORT);

    char choice[BUFFER_SIZE];
    while (1) {
        show_menu();
        printf("Enter your choice: ");
        fflush(stdout);
        scanf("%15s", choice);

        if (strcmp(choice, "1") == 0) {
            send_register(conn_fd);
        } else if (strcmp(choice, "2") == 0) {
            send_login(conn_fd);
        } else if (strcmp(choice, "3") == 0) {
            printf("Exiting...\n");
            break;
        } else {
            printf("\033[0;31mUnknown choice. Please try again.\033[0m\n");
        }
    }

    close(conn_fd);
    return 0;
}

void show_menu() {
    printf("Please choose one of the following options:\n");
    printf("1. Register\n");
    printf("2. Login\n");
    printf("3. Exit\n");
}

void send_register(int conn_fd) {
    char name[MAXNAME + 1];
    printf("Enter the user name (max %d characters): ", MAXNAME);
    fflush(stdout);
    scanf("%15s", name);

    snprintf(buf, sizeof(buf), "register:%s", name);
    send(conn_fd, buf, strlen(buf), 0);

    memset(buf, 0, BUFFER_SIZE);
    bytes_received = recv(conn_fd, buf, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        if (bytes_received == 0) {
            printf("No response from server.\n");
        } else {
            perror("recv");
        }
        return;
    }
    buf[bytes_received] = '\0';

    if (!strncmp(buf, RESPONSE, strlen(RESPONSE)) && buf[strlen(RESPONSE)] == '1') {
        printf("\033[0;32m%s\033[0m\n", buf + strlen(RESPONSE) + 1);
    } else {
        printf("\033[0;31m%s\033[0m\n", buf + strlen(RESPONSE) + 1);
    }
}

void send_login(int conn_fd) {
    char name[MAXNAME + 1];
    printf("Enter the user name (max %d characters): ", MAXNAME);
    fflush(stdout);
    scanf("%15s", name);

    printf("name:%s\n", name);
    snprintf(buf, BUFFER_SIZE, "login:%s", name);
    send(conn_fd, buf, BUFFER_SIZE, 0);

    memset(buf, 0, BUFFER_SIZE);
    bytes_received = recv(conn_fd, buf, BUFFER_SIZE, 0);
    buf[bytes_received] = '\0';

    // printf("buf:%s\n", buf);
    if (!strncmp(buf, RESPONSE, strlen(RESPONSE)) && buf[strlen(RESPONSE)] == '1') {
        printf("\033[0;32m%s\033[0m\n", buf + strlen(RESPONSE) + 1);
        logged_in_interface(conn_fd, name);
    } else {
        printf("\033[0;31m%s\033[0m\n", buf + strlen(RESPONSE) + 1);
    }
}

void logged_in_interface(int conn_fd, char* username) {
    printf("Hi! %s\n", username);

    char choice[BUFFER_SIZE];
    while (1) {
        printf("Choose an action:\n");
        printf("1. Show online users\n");
        printf("2. Send message\n");
        printf("3. Log out\n");
        printf("Enter choice: ");
        fflush(stdout);
        scanf("%15s", choice);

        if (strcmp(choice, "1") == 0) {
            strncpy(choice, "1", BUFFER_SIZE);
            send(conn_fd, choice, BUFFER_SIZE, 0);

            memset(buf, 0, BUFFER_SIZE);
            bytes_received = recv(conn_fd, buf, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) {
                printf("Failed to get user list from server.\n");
                continue;
            }
            buf[bytes_received] = '\0';

            if (!strncmp(buf, "RESPONSE:USER_LIST:", strlen("RESPONSE:USER_LIST:"))) {
                handle_user_list(buf);
            } else {
                printf("Unexpected response from server: %s\n", buf);
            }
        } else if (strcmp(choice, "2") == 0) {
            char message[BUFFER_SIZE];
            printf("Enter message: ");
            fflush(stdout);
            scanf(" %[^\n]", message);

            snprintf(buf, sizeof(buf), "message:%s:%s", username, message);
            send(conn_fd, buf, strlen(buf), 0);
        } else if (strcmp(choice, "3") == 0) {
            send(conn_fd, "logout", strlen("logout"), 0);
            break;
        } else {
            printf("\033[0;31mUnknown choice. Please try again.\033[0m\n");
        }
    }
}

void handle_user_list(const char *message) {
    printf("Online users:\n");

    const char *user_list = message + strlen("RSEPONSE:USER_LIST:");
    char *token = strtok((char *)user_list, ",");
    while (token != NULL) {
        printf("- %s\n", token);
        token = strtok(NULL, ",");
    }
}
