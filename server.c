#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define SERVER
#include "config.h"

typedef struct {
    char name[MAXNAME];
    char ip[INET_ADDRSTRLEN];
    int port;
    int socket_fd;  // 新增 socket 描述符
    bool status;
} User;

User users[MAX_USERS];
int user_count = 0;

int task_queue[QUEUE_SIZE];
int queue_front = 0, queue_rear = 0, queue_count = 0;

pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t queue_not_full = PTHREAD_COND_INITIALIZER;

pthread_mutex_t users_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_t workers[THREAD_POOL_SIZE];

void *worker_thread(void *arg);
void enqueue_task(int conn_fd);
int dequeue_task();
void handle_client(int conn_fd, struct sockaddr_in cliaddr);
void register_user(int conn_fd, char* name);
bool login_user(int conn_fd, char* name, char* ip, int port);
void broadcast_user_list(void);
char *get_current_timestamp();
void handle_user(int conn_fd, char* name); // 切換到用戶功能


int main() {
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) ERR_EXIT("socket");

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) ERR_EXIT("bind");
    if (listen(listen_fd, 10) < 0) ERR_EXIT("listen");

    printf("Server is running on port %d\n", PORT);
    printf("%s\n", LINE);

    while (1) {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        int conn_fd = accept(listen_fd, (struct sockaddr*)&cliaddr, &clilen);
        if (conn_fd < 0) ERR_EXIT("accept");

        printf("New connection from %s:%d [%s]\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), get_current_timestamp());
        enqueue_task(conn_fd);
    }

    close(listen_fd);
    return 0;
}

void *worker_thread(void *arg) {
    (void)arg;
    while (1) {
        int conn_fd = dequeue_task();
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);

        getpeername(conn_fd, (struct sockaddr*)&cliaddr, &clilen);
        handle_client(conn_fd, cliaddr);
        close(conn_fd);
    }
    return NULL;
}

void enqueue_task(int conn_fd) {
    pthread_mutex_lock(&queue_lock);
    while (queue_count == QUEUE_SIZE) {
        pthread_cond_wait(&queue_not_full, &queue_lock);
    }
    task_queue[queue_rear] = conn_fd;
    queue_rear = (queue_rear + 1) % QUEUE_SIZE;
    queue_count++;
    pthread_cond_signal(&queue_not_empty);
    pthread_mutex_unlock(&queue_lock);
}

int dequeue_task() {
    pthread_mutex_lock(&queue_lock);
    while (queue_count == 0) {
        pthread_cond_wait(&queue_not_empty, &queue_lock);
    }
    int conn_fd = task_queue[queue_front];
    queue_front = (queue_front + 1) % QUEUE_SIZE;
    queue_count--;
    pthread_cond_signal(&queue_not_full);
    pthread_mutex_unlock(&queue_lock);
    return conn_fd;
}

void handle_client(int conn_fd, struct sockaddr_in cliaddr) {
    char buf[BUFFER_SIZE];


    while (1) {
        memset(buf, 0, BUFFER_SIZE);
        int len = recv(conn_fd, buf, BUFFER_SIZE, 0);
        if (len <= 0) {
            printf("Client disconnected: %d\n", conn_fd);
            close(conn_fd);
            return;
        }

        printf("buf:%s\n", buf);
        if (strncmp(buf, "register:", 9) == 0) {
            char* name = buf + 9; // 提取用戶名稱
            register_user(conn_fd, name);
        } else if (strncmp(buf, "login:", 6) == 0) {
            // 提取 IP 和 Port
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &cliaddr.sin_addr, client_ip, INET_ADDRSTRLEN);
            int client_port = ntohs(cliaddr.sin_port);

            // 處理登入
            char* name = buf + 6; // 提取用戶名稱
            if (login_user(conn_fd, name, client_ip, client_port)) {
                printf("User logged in successfully: %s\n", name);
                handle_user(conn_fd, name); // 切換到用戶功能
                // break;
            } else {
                printf("Login failed for user: %s\n", name);
                // send(conn_fd, "RESPONSE:0Login failed.", strlen("RESPONSE:0Login failed."), 0);
            }
        } else if (strncmp(buf, "exit", 4) == 0) {
            printf("Client exited: %d\n", conn_fd);
            close(conn_fd);
            return;
        } else {
            send(conn_fd, "RESPONSE:0Unknown command.", strlen("RESPONSE:0Unknown command."), 0);
        }
    }
}

void handle_user(int conn_fd, char* username) {
    char buf[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    while (1) {
        memset(buf, 0, BUFFER_SIZE);
        int len = recv(conn_fd, buf, BUFFER_SIZE, 0);
        if (len <= 0) {
            printf("User %s disconnected.\n", username);
            close(conn_fd);
            return;
        }

        if (strncmp(buf, "1", 1) == 0) { // show user list
            // Show online users
            pthread_mutex_lock(&users_lock);
            snprintf(response, BUFFER_SIZE, "RESPONSE:USER_LIST:");
            for (int i = 0; i < MAX_USERS; i++) {
                if (users[i].status) {
                    char user_info[128];
                    snprintf(user_info, sizeof(user_info), "%s|%s:%d,", users[i].name,
                             users[i].ip, users[i].port);
                    strncat(response, user_info, sizeof(response) - strlen(response) - 1);
                }
            }
            pthread_mutex_unlock(&users_lock);
            send(conn_fd, response, strlen(response), 0);

        } else if (strncmp(buf, "2:", 2) == 0) {
            // Send message
            char* message = buf + 2;
            printf("Message from %s: %s\n", username, message);
            snprintf(response, sizeof(response), "RESPONSE:1Message sent: %s", message);
            send(conn_fd, response, strlen(response), 0);

        } else if (strncmp(buf, "3", 1) == 0) {
            // Logout
            printf("User %s logged out.\n", username);
            pthread_mutex_lock(&users_lock);
            for (int i = 0; i < MAX_USERS; i++) {
                if (strcmp(users[i].name, username) == 0) {
                    users[i].status = false;
                    users[i].socket_fd = -1; // 清空 socket 描述符
                    break;
                }
            }
            pthread_mutex_unlock(&users_lock);
            send(conn_fd, "RESPONSE:1Logged out successfully.", strlen("RESPONSE:1Logged out successfully."), 0);
            close(conn_fd);
            return;

        } else {
            snprintf(response, sizeof(response), "RESPONSE:0Unknown command.");
            send(conn_fd, response, strlen(response), 0);
        }
    }
}

void register_user(int conn_fd, char* name) {
    pthread_mutex_lock(&users_lock);
    if (user_count >= MAX_USERS) {
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, sizeof(error_msg), "RESPONSE:0Registration failed: maximum users reached.");
        send(conn_fd, error_msg, strlen(error_msg), 0);
    } else if (strlen(name) > MAXNAME) {
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, sizeof(error_msg), "RESPONSE:0Registration failed: name too long.");
        send(conn_fd, error_msg, strlen(error_msg), 0);
    } else {
        for (int i = 0; i < user_count; i++) {
            if (strcmp(users[i].name, name) == 0) {
                char error_msg[BUFFER_SIZE];
                snprintf(error_msg, sizeof(error_msg), "RESPONSE:0Registration failed: name already taken.");
                send(conn_fd, error_msg, strlen(error_msg), 0);
                pthread_mutex_unlock(&users_lock);
                return;
            }
        }
        strncpy(users[user_count].name, name, MAXNAME);
        users[user_count].status = false;
        user_count++;
        char success_msg[BUFFER_SIZE];
        snprintf(success_msg, sizeof(success_msg), "RESPONSE:1Registration successful.");
        send(conn_fd, success_msg, strlen(success_msg), 0);
    }
    pthread_mutex_unlock(&users_lock);
}

bool login_user(int conn_fd, char* name, char* ip, int port) {
    pthread_mutex_lock(&users_lock);
    printf("user_count:%d\n", user_count);

    char buf[BUFFER_SIZE];
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].name, name) == 0) {
            users[i].status = true;
            users[i].socket_fd = conn_fd;
            strncpy(users[i].ip, ip, INET_ADDRSTRLEN);
            users[i].port = port;

            strncpy(buf, "RESPONSE:1Login successful.", BUFFER_SIZE);
            int sent_byte = send(conn_fd, buf, BUFFER_SIZE, 0);
            printf("sent bytes:%d\n", sent_byte);

            printf("now login:%s\n", name);
            pthread_mutex_unlock(&users_lock);
            return true;
        }
    }

    printf("269 login failed\n");
    strncpy(buf, "RESPONSE:0Login failed: user not registered.", BUFFER_SIZE);
    send(conn_fd, buf, BUFFER_SIZE, 0);
    pthread_mutex_unlock(&users_lock);
    return false;
}

void logout_user(int conn_fd) {
    pthread_mutex_lock(&users_lock);
    for (int i = 0; i < user_count; i++) {
        if (users[i].socket_fd == conn_fd) {
            users[i].status = false;
            users[i].socket_fd = -1;
            break;
        }
    }
    pthread_mutex_unlock(&users_lock);

}



char* get_current_timestamp() {
    static char timestamp[20];
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", t);
    return timestamp;
}

