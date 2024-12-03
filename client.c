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
#include <sys/select.h> // 使用 select 系統呼叫

#define CLINET
#include "config.h"

#define RESPONSE "RESPONSE:"
#define BROADCAST "BROADCAST:"
#define ERROR "ERROR:"

#define MAX_ONLINE_USERS 10

int bytes_received = -1;
char buf[BUFFER_SIZE];

typedef struct {
    char username[MAXNAME];
    bool status; // 是否在線
    char ip_port[INET_ADDRSTRLEN + 6]; // IP:port 格式，例如 "127.0.0.1:8785"
} OnlineUser;

OnlineUser online_users[MAX_ONLINE_USERS];
int conn_fd = -1;

void show_menu();
void send_register(int conn_fd);
void send_login(int conn_fd);
void logged_in_interface(int conn_fd, char* username);
void update_online_users(const char* data);
void handle_user_list(const char *message);
void* listen_for_server(void* arg);

// 設定 recv 的 timeout（以秒為單位）
#define RECV_TIMEOUT_SEC 5

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

    // 啟動執行緒接收伺服器數據
    pthread_t receiver_thread;
    pthread_create(&receiver_thread, NULL, listen_for_server, NULL);

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
    printf("Please choose one of the following options:\n");
    printf("1. Register\n");
    printf("2. Login\n");
    printf("3. Exit\n");
}

// 使用 select 設定超時的 recv 函數
int recv_with_timeout(int socket_fd, char *buffer, size_t length) {
    fd_set read_fds;
    struct timeval timeout;

    // 設定檔案描述符集
    FD_ZERO(&read_fds);
    FD_SET(socket_fd, &read_fds);

    // 設定 timeout
    timeout.tv_sec = RECV_TIMEOUT_SEC;
    timeout.tv_usec = 0;

    // 使用 select 等待資料到達
    int result = select(socket_fd + 1, &read_fds, NULL, NULL, &timeout);
    if (result < 0) {
        perror("select"); // select 錯誤
        return -1;
    } else if (result == 0) {
        printf("recv timeout: no data received within %d seconds.\n", RECV_TIMEOUT_SEC);
        return 0; // 超時
    }

    // 如果有資料可讀，執行 recv
    return recv(socket_fd, buffer, length, 0);
}

void send_register(int conn_fd) {
    char name[MAXNAME + 1];
    printf("Enter the user name (max %d characters): ", MAXNAME);
    fflush(stdout);
    scanf("%15s", name);

    snprintf(buf, sizeof(buf), "register:%s", name);
    send(conn_fd, buf, strlen(buf), 0);

    memset(buf, 0, BUFFER_SIZE);
    bytes_received = recv_with_timeout(conn_fd, buf, BUFFER_SIZE - 1);
    if (bytes_received <= 0) {
        if (bytes_received == 0) {
            printf("No response from server.\n");
        } else {
            perror("recv");
        }
        return;
    }
    buf[bytes_received] = '\0'; // Ensure null-termination

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

    snprintf(buf, sizeof(buf), "login:%s", name);
    send(conn_fd, buf, strlen(buf), 0);

    memset(buf, 0, BUFFER_SIZE);
    bytes_received = recv_with_timeout(conn_fd, buf, BUFFER_SIZE - 1);
    if (bytes_received <= 0) {
        if (bytes_received == 0) {
            printf("No response from server.\n");
        } else {
            perror("recv");
        }
        return;
    }
    buf[bytes_received] = '\0'; // Ensure null-termination

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
    char message[BUFFER_SIZE];

    while (1) {
        printf("Choose an action:\n");
        printf("1. Show online users\n");
        printf("2. Send message\n");
        printf("3. Log out\n");
        printf("Enter choice: ");
        fflush(stdout);
        scanf("%15s", choice);

        if (strcmp(choice, "1") == 0) {
            // 顯示在線用戶
            printf("Online users:\n");
            for (int i = 0; i < MAX_ONLINE_USERS; i++) {
                if (online_users[i].status) {
                    printf("- %s (%s)\n", online_users[i].username, online_users[i].ip_port);
                }
            }
        } else if (strcmp(choice, "2") == 0) {
            // 發送訊息
            printf("Enter message: ");
            fflush(stdout);
            scanf(" %[^\n]", message);

            snprintf(buf, sizeof(buf), "message:%s:%s", username, message);
            send(conn_fd, buf, strlen(buf), 0);

            memset(buf, 0, BUFFER_SIZE);
            bytes_received = recv_with_timeout(conn_fd, buf, BUFFER_SIZE - 1);
            if (bytes_received <= 0) {
                if (bytes_received == 0) {
                    printf("No response from server for your message.\n");
                } else {
                    perror("recv");
                }
                continue;
            }
            buf[bytes_received] = '\0'; // 確保以 NULL 結尾

            if (!strncmp(buf, RESPONSE, strlen(RESPONSE)) && buf[strlen(RESPONSE)] == '1') {
                printf("\033[0;32m%s\033[0m\n", buf + strlen(RESPONSE) + 1);
            } else {
                printf("\033[0;31m%s\033[0m\n", buf + strlen(RESPONSE) + 1);
            }
        } else if (strcmp(choice, "3") == 0) {
            send(conn_fd, "logout", strlen("logout"), 0);
            printf("Logging out...\n");
            break;
        } else {
            printf("\033[0;31mUnknown choice. Please try again.\033[0m\n");
        }
    }
}


void *listen_for_server(void *arg) {
    while (1) {
        memset(buf, 0, BUFFER_SIZE);
        bytes_received = recv_with_timeout(conn_fd, buf, BUFFER_SIZE - 1);
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                printf("No data from server, skipping...\n");
            } else {
                perror("recv");
            }
            continue;
        }

        buf[bytes_received] = '\0'; // 確保以 NULL 結尾
        printf("Received: %s\n", buf);

        if (!strncmp(buf, "USER_LIST:", strlen("USER_LIST:"))) {
            handle_user_list(buf);
        }
    }
    return NULL;
}

void handle_user_list(const char *message) {
    printf("into user list!\n");
    memset(online_users, 0, sizeof(online_users));

    const char *user_list = message + strlen("USER_LIST:");
    char *token = strtok((char *)user_list, ",");
    int index = 0;
    while (token != NULL && index < MAX_ONLINE_USERS) {
        sscanf(token, "%[^|]|%[^:]", online_users[index].username, online_users[index].ip_port);
        online_users[index].status = true;
        token = strtok(NULL, ",");
        index++;
    }

    printf("Updated online user list:\n");
    for (int i = 0; i < MAX_ONLINE_USERS; i++) {
        if (online_users[i].status) {
            printf("- %s (%s)\n", online_users[i].username, online_users[i].ip_port);
        }
    }
}
