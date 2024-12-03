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
#include <fcntl.h> // 用於設置非阻塞模式
#include <sys/select.h> // 用於 select()

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
pthread_mutex_t buf_lock = PTHREAD_MUTEX_INITIALIZER;   // 用於保護 buf 的鎖

int conn_fd = -1;

// 將套接字設置為非阻塞模式
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl get");
        exit(EXIT_FAILURE);
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl set");
        exit(EXIT_FAILURE);
    }
}

void show_menu();
void send_register(int conn_fd);
void send_login(int conn_fd);
void logged_in_interface(int conn_fd, char *username);
void handle_user_list(const char *message);
void *listen_for_server(void *arg);
int recv_with_timeout(int fd, char *buffer, size_t length, int timeout_sec);

int main() {
    conn_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn_fd < 0) ERR_EXIT("socket");

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &servaddr.sin_addr) <= 0) ERR_EXIT("Invalid IP");

    if (connect(conn_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) ERR_EXIT("connect");

    // 設置 conn_fd 為非阻塞模式
    set_nonblocking(conn_fd);

    printf("Connected to server at port %d\n", PORT);

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
            // 显示在线用户
            pthread_mutex_lock(&users_lock);
            printf("Online users:\n");
            for (int i = 0; i < MAX_ONLINE_USERS; i++) {
                if (online_users[i].status) {
                    printf("- %s (%s)\n", online_users[i].username, online_users[i].ip_port);
                }
            }
            pthread_mutex_unlock(&users_lock);
        } else if (strcmp(choice, "2") == 0) {
            // 发送消息
            printf("Enter message: ");
            fflush(stdout);
            scanf(" %[^\n]", message);

            pthread_mutex_lock(&buf_lock); // 加锁保护 buf
            snprintf(buf, BUFFER_SIZE, "message:%s:%s", username, message);
            send(conn_fd, buf, strlen(buf), 0);
            pthread_mutex_unlock(&buf_lock);

            pthread_mutex_lock(&buf_lock); // 加锁保护 buf
            memset(buf, 0, BUFFER_SIZE);  // 清空缓冲区
            bytes_received = recv_with_timeout(conn_fd, buf, BUFFER_SIZE - 1, 5); // 超时设置为5秒

            if (bytes_received <= 0) {
                if (bytes_received == 0) {
                    printf("\033[0;31mServer response timed out.\033[0m\n");
                } else {
                    perror("recv");
                }
                pthread_mutex_unlock(&buf_lock);
                continue;
            }

            buf[bytes_received] = '\0'; // 确保以 NULL 结尾

            // 检查响应并处理
            if (!strncmp(buf, RESPONSE, strlen(RESPONSE)) && buf[strlen(RESPONSE)] == '1') {
                printf("\033[0;32m%s\033[0m\n", buf + strlen(RESPONSE) + 1);
            } else {
                printf("\033[0;31m%s\033[0m\n", buf + strlen(RESPONSE) + 1);
            }
            pthread_mutex_unlock(&buf_lock); // 解锁
        } else if (strcmp(choice, "3") == 0) {
            // 发送登出请求
            pthread_mutex_lock(&buf_lock);
            send(conn_fd, "logout", strlen("logout"), 0);
            pthread_mutex_unlock(&buf_lock);
            break;
        } else {
            printf("\033[0;31mUnknown choice. Please try again.\033[0m\n");
        }
    }
}


void send_register(int conn_fd) {
    char name[MAXNAME + 1];
    printf("Enter the user name (max %d characters): ", MAXNAME);
    fflush(stdout);
    scanf("%15s", name);

    // 发送注册请求
    pthread_mutex_lock(&buf_lock); // 加锁保护 buf
    snprintf(buf, BUFFER_SIZE, "register:%s", name);
    send(conn_fd, buf, strlen(buf), 0);
    printf("193 send!\n");q1
    pthread_mutex_unlock(&buf_lock);

    // 等待服务器响应
    pthread_mutex_lock(&buf_lock); // 加锁保护 buf
    memset(buf, 0, BUFFER_SIZE);  // 清空缓冲区
    bytes_received = recv_with_timeout(conn_fd, buf, BUFFER_SIZE - 1, 5); // 超时设置为5秒

    if (bytes_received <= 0) {
        if (bytes_received == 0) {
            printf("\033[0;31mServer response timed out.\033[0m\n");
        } else {
            perror("recv");
        }
        pthread_mutex_unlock(&buf_lock);
        return;
    }

    buf[bytes_received] = '\0'; // 确保以 NULL 结尾

    // 检查响应并处理
    if (!strncmp(buf, RESPONSE, strlen(RESPONSE)) && buf[strlen(RESPONSE)] == '1') {
        printf("\033[0;32m%s\033[0m\n", buf + strlen(RESPONSE) + 1);
    } else {
        printf("\033[0;31m%s\033[0m\n", buf + strlen(RESPONSE) + 1);
    }
    pthread_mutex_unlock(&buf_lock); // 解锁
}

void send_login(int conn_fd) {
    char name[MAXNAME + 1];
    printf("Enter the user name (max %d characters): ", MAXNAME);
    fflush(stdout);
    scanf("%15s", name);

    // 发送登录请求
    pthread_mutex_lock(&buf_lock); // 加锁保护 buf
    snprintf(buf, BUFFER_SIZE, "login:%s", name);
    send(conn_fd, buf, strlen(buf), 0);
    pthread_mutex_unlock(&buf_lock);

    // 等待服务器响应
    pthread_mutex_lock(&buf_lock); // 加锁保护 buf
    memset(buf, 0, BUFFER_SIZE);  // 清空缓冲区
    bytes_received = recv_with_timeout(conn_fd, buf, BUFFER_SIZE - 1, 5); // 超时设置为5秒

    if (bytes_received <= 0) {
        if (bytes_received == 0) {
            printf("\033[0;31mServer response timed out.\033[0m\n");
        } else {
            perror("recv");
        }
        pthread_mutex_unlock(&buf_lock);
        return;
    }

    buf[bytes_received] = '\0'; // 确保以 NULL 结尾

    // 检查响应并处理
    if (!strncmp(buf, RESPONSE, strlen(RESPONSE)) && buf[strlen(RESPONSE)] == '1') {
        printf("\033[0;32m%s\033[0m\n", buf + strlen(RESPONSE) + 1);
        logged_in_interface(conn_fd, name);
    } else {
        printf("\033[0;31m%s\033[0m\n", buf + strlen(RESPONSE) + 1);
    }
    pthread_mutex_unlock(&buf_lock); // 解锁
}




void *listen_for_server(void *arg) {
    while (1) {
        pthread_mutex_lock(&buf_lock); // 加鎖
        memset(buf, 0, BUFFER_SIZE);

        // 使用 recv_with_timeout() 接收數據
        bytes_received = recv_with_timeout(conn_fd, buf, BUFFER_SIZE - 1, 5); // 超時設定為 5 秒
        // printf("296!\n");
        if (bytes_received < 0) {
            perror("recv");
            pthread_mutex_unlock(&buf_lock);
            break;
        } else if (bytes_received == 0) {
            pthread_mutex_unlock(&buf_lock);
            sleep(0.5);  // 模擬讓其他 thread 有機會運行
            // continue; // 超時，繼續等待c
        }

        buf[bytes_received] = '\0'; // 確保以 NULL 結尾
        // printf("Received: %s\n", buf);

        if (!strncmp(buf, "USER_LIST:", strlen("USER_LIST:"))) {
            handle_user_list(buf);
        }

        pthread_mutex_unlock(&buf_lock); // 解鎖
    }
    return NULL;
}

// 利用 select() 確保非阻塞接收數據
int recv_with_timeout(int fd, char *buffer, size_t length, int timeout_sec) {
    fd_set read_fds;
    struct timeval timeout;

    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;

    int ret = select(fd + 1, &read_fds, NULL, NULL, &timeout);
    if (ret < 0) {
        perror("select");
        return -1;
    } else if (ret == 0) {
        // printf("recv() timed out.\n");
        return 0; // 超時
    }

    // 檢查文件描述符是否可讀
    if (FD_ISSET(fd, &read_fds)) {
        return recv(fd, buffer, length, 0);
    }

    return 0;
}

void handle_user_list(const char *message) {
    pthread_mutex_lock(&users_lock);
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

    pthread_mutex_unlock(&users_lock);
}
