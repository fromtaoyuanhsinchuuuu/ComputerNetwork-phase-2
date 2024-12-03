#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h> // 用於 EAGAIN 和 EWOULDBLOCK

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
pthread_mutex_t users_lock = PTHREAD_MUTEX_INITIALIZER; // 用於同步在線用戶資料的鎖

int conn_fd = -1;

void show_menu();
void send_register(int conn_fd);
void send_login(int conn_fd);
void logged_in_interface(int conn_fd, char* username);
void update_online_users(const char* data);
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

void send_register(int conn_fd) {
    char name[MAXNAME + 1]; // +1 for null terminator
    printf("Enter the user name (max %d characters): ", MAXNAME);
    fflush(stdout);
    scanf("%15s", name);

    // char buf[BUFFER_SIZE];
    snprintf(buf, sizeof(buf), "register:%s", name);
    send(conn_fd, buf, BUFFER_SIZE, 0);

    if (strncmp(buf, RESPONSE, strlen(RESPONSE))){
        memset(buf, 0, BUFFER_SIZE);
        bytes_received = recv(conn_fd, buf, BUFFER_SIZE - 1, 0);
    }
    if (bytes_received <= 0) {
        ERR_EXIT("recv");
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

    // char buf[BUFFER_SIZE];
    snprintf(buf, sizeof(buf), "login:%s", name);
    send(conn_fd, buf, strlen(buf), 0);

    printf("into send_login\n");
    if (strncmp(buf, RESPONSE, strlen(RESPONSE))){
        memset(buf, 0, BUFFER_SIZE);
        bytes_received = recv(conn_fd, buf, BUFFER_SIZE - 1, 0);
    }
    if (bytes_received <= 0) {
        ERR_EXIT("recv");
    }
    printf("bute_receive:%d\n", bytes_received);
    buf[bytes_received] = '\0'; // Ensure null-termination
    printf("asjdlkajds\n");
    printf("buf %s\n", buf);
    fflush(stdout);
    printf("%d %c\n",strncmp(buf, RESPONSE, strlen(RESPONSE)),  buf[strlen(RESPONSE)]);
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
            pthread_mutex_lock(&users_lock);
            printf("Online users:\n");
            for (int i = 0; i < MAX_ONLINE_USERS; i++) {
                if (online_users[i].status) {
                    printf("- %s (%s)\n", online_users[i].username, online_users[i].ip_port);
                }
            }
            pthread_mutex_unlock(&users_lock);
        } else if (strcmp(choice, "2") == 0) {
            // 發送訊息
            printf("Enter message: ");
            fflush(stdout);
            scanf(" %[^\n]", message);

            snprintf(buf, sizeof(buf), "message:%s:%s", username, message);
            send(conn_fd, buf, strlen(buf), 0);

            memset(buf, 0, BUFFER_SIZE);
            bytes_received = recv(conn_fd, buf, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) {
                ERR_EXIT("recv");
            }
            buf[bytes_received] = '\0'; // Ensure null-termination

            if (!strncmp(buf, RESPONSE, strlen(RESPONSE)) && buf[strlen(RESPONSE)] == '1') {
                printf("\033[0;32m%s\033[0m\n", buf + strlen(RESPONSE) + 1);
            } else {
                printf("\033[0;31m%s\033[0m\n", buf + strlen(RESPONSE) + 1);
            }
        } else if (strcmp(choice, "3") == 0) {
            send(conn_fd, "logout", strlen("logout"), 0);
            break;
        } else {
            printf("\033[0;31mUnknown choice. Please try again.\033[0m\n");
        }
    }
}

void *listen_for_server(void *arg) {
    while (1) {
        // memset(buf, 0, BUFFER_SIZE);
        printf("208!\n");
        bytes_received = recv(conn_fd, buf, BUFFER_SIZE - 1, 0);
        printf("211\n");
        if (bytes_received <= 0) {
            perror("recv");
            break;
        }

        buf[bytes_received] = '\0'; // 確保以 NULL 結尾

        printf("lsited_for_server_buf:%s\n", buf);
        if (!strncmp(buf, "USER_LIST:", strlen("USER_LIST:"))) {
            pthread_mutex_lock(&users_lock);
            handle_user_list(buf);
            pthread_mutex_unlock(&users_lock);
        }
    }
    return NULL;
}

void handle_user_list(const char *message) {

    // 清空在線用戶列表
    printf("into user list!\n");
    memset(online_users, 0, sizeof(online_users));

    // 跳過消息前綴 "USER_LIST:"
    const char *user_list = message + strlen("USER_LIST:");

    // 解析用戶信息
    char *token = strtok((char *)user_list, ",");
    int index = 0;
    while (token != NULL && index < MAX_ONLINE_USERS) {
        int temp_status = 0; // 用於暫存狀態
        sscanf(token, "%[^|]|%[^:]:%d",
        online_users[index].username,
        online_users[index].ip_port,
        &temp_status);
        online_users[index].status = (temp_status != 0);  // 將整數轉為布林值
        online_users[index].status = true; // 標記為在線
        token = strtok(NULL, ",");
        index++;
    }


    // 顯示在線用戶列表
    printf("Updated online user list:\n");
    for (int i = 0; i < MAX_ONLINE_USERS; i++) {
        if (online_users[i].status) {
            printf("- %s (%s)\n", online_users[i].username, online_users[i].ip_port);
        }
    }
}