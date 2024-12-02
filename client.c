#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLIENT
#include "config.h"

int conn_fd = -1;

void show_menu();
void send_register(int conn_fd);
void send_login(int conn_fd);
void logged_in_interface(int conn_fd, char* username);

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
            printf("%s\n", LINE);
        }
    }

    close(conn_fd);
    return 0;
}

void show_menu() {
    printf("%s\n", LINE);

    char buf[BUFFER_SIZE] = {0};

    // Query server for registration info
    send(conn_fd, "query", strlen("query"), 0);

    // Receive server response
    int bytes_received = recv(conn_fd, buf, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        ERR_EXIT("recv");
    }
    buf[bytes_received] = '\0'; // Ensure null-termination

    // Display registration info and menu
    printf("\n%s\n", buf);
    printf("\nPlease choose one of the following options:\n");
    printf("1. Register\n");
    printf("2. Login\n");
    printf("3. Exit\n");
}

void send_register(int conn_fd) {
    char name[MAXNAME + 1];  // +1 for null terminator
    printf("Enter the user name (max %d characters): ", MAXNAME);
    fflush(stdout);
    scanf("%15s", name);

    char buf[BUFFER_SIZE];
    snprintf(buf, sizeof(buf), "register:%s", name);
    send(conn_fd, buf, strlen(buf), 0);

    int bytes_received = recv(conn_fd, buf, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        ERR_EXIT("recv");
    }
    buf[bytes_received] = '\0'; // Ensure null-termination

    printf("%s\n", LINE);
    if (buf[0] == '0') {
        printf("\033[0;31mServer response: %s\033[0m\n", buf + 1);  // Red
    } else if (buf[0] == '1') {
        printf("\033[0;32mServer response: %s\033[0m\n", buf + 1);  // Green
    }
}

void send_login(int conn_fd) {
    char name[MAXNAME + 1];
    printf("Enter the user name (max %d characters): ", MAXNAME);
    fflush(stdout);
    scanf("%15s", name);

    char buf[BUFFER_SIZE];
    snprintf(buf, sizeof(buf), "login:%s", name);
    send(conn_fd, buf, strlen(buf), 0);

    int bytes_received = recv(conn_fd, buf, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        ERR_EXIT("recv");
    }
    buf[bytes_received] = '\0'; // Ensure null-termination

    printf("%s\n", LINE);
    if (buf[0] == '0') {
        printf("\033[0;31mServer response: %s\033[0m\n", buf + 1);  // Red
    } else if (buf[0] == '1') {
        printf("\033[0;32mServer response: %s\033[0m\n", buf + 1);  // Green
        logged_in_interface(conn_fd, name);
    }
}

void logged_in_interface(int conn_fd, char* username) {
    printf("Hi! %s\n", username);

    char choice[BUFFER_SIZE];
    char message[BUFFER_SIZE];

    while (1) {
        printf("Choose an action:\n");
        printf("1. Send message\n");
        printf("2. Log out\n");
        printf("Enter choice: ");
        fflush(stdout);
        scanf("%15s", choice);

        char buf[BUFFER_SIZE];
        if (strcmp(choice, "1") == 0) {
            printf("Enter message: ");
            fflush(stdout);
            fgets(message, sizeof(message), stdin);
            message[strcspn(message, "\n")] = '\0'; // Remove newline

            snprintf(buf, sizeof(buf), "message:%s:%s", username, message);
            send(conn_fd, buf, strlen(buf), 0);

            int bytes_received = recv(conn_fd, buf, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) {
                ERR_EXIT("recv");
            }
            buf[bytes_received] = '\0'; // Ensure null-termination

            printf("%s\n", LINE);
            if (buf[0] == '0') {
                printf("\033[0;31mServer response: %s\033[0m\n", buf + 1);  // Red
            } else if (buf[0] == '1') {
                printf("\033[0;32mServer response: %s\033[0m\n", buf + 1);  // Green
            }
        } else if (strcmp(choice, "2") == 0) {
            send(conn_fd, "logout", strlen("logout"), 0);

            int bytes_received = recv(conn_fd, buf, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) {
                ERR_EXIT("recv");
            }
            buf[bytes_received] = '\0'; // Ensure null-termination

            printf("%s\n", LINE);
            if (buf[0] == '0') {
                printf("\033[0;31mServer response: %s\033[0m\n", buf + 1);  // Red
            } else if (buf[0] == '1') {
                printf("\033[0;32mServer response: %s\033[0m\n", buf + 1);  // Green
            }
            break;
        } else {
            printf("%s\n", LINE);
            printf("\033[0;31mUnknown choice. Please try again.\033[0m\n");
        }
        printf("%s\n", LINE);
    }
}

