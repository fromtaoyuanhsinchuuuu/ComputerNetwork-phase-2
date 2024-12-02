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
    bool registered;
} User;

User users[MAX_USERS];
int user_count = 0;

int task_queue[QUEUE_SIZE];
int queue_front = 0, queue_rear = 0, queue_count = 0;
int client_count = 0;  // 當前已連接客戶端數量
pthread_mutex_t client_count_lock = PTHREAD_MUTEX_INITIALIZER;  // 用於同步的鎖

pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t queue_not_full = PTHREAD_COND_INITIALIZER;

pthread_t workers[THREAD_POOL_SIZE];

void *worker_thread(void *arg);
void enqueue_task(int conn_fd);
int dequeue_task();
void handle_client(int conn_fd);
void register_user(int conn_fd, char* name);
bool login_user(int conn_fd, char* name);
void send_message(int conn_fd, char* username, char* message);
char *get_current_timestamp();

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

        printf("New connection from %s [%s]\n", inet_ntoa(cliaddr.sin_addr), get_current_timestamp());
        enqueue_task(conn_fd);
    }

    close(listen_fd);
    return 0;
}

void *worker_thread(void *arg) {
    (void)arg;
    while (1) {
        int conn_fd = dequeue_task();

        // 更新客戶端計數
        pthread_mutex_lock(&client_count_lock);
        client_count++;
        printf("Client connected. Current clients: %d [%s]\n", client_count, get_current_timestamp());
        pthread_mutex_unlock(&client_count_lock);

        handle_client(conn_fd);

        // 客戶端斷開連接時更新計數
        pthread_mutex_lock(&client_count_lock);
        client_count--;
        printf("Client disconnected. Current clients: %d [%s]\n", client_count, get_current_timestamp());
        pthread_mutex_unlock(&client_count_lock);

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

void handle_client(int conn_fd) {
    char buf[BUFFER_SIZE];

    while (1) {
        memset(buf, 0, BUFFER_SIZE);
        int len = recv(conn_fd, buf, BUFFER_SIZE, 0);
        if (len <= 0) break; // Connection closed

        if (strncmp(buf, "relay:", 6) == 0) {
            // Relay Mode: extract target client info and forward message
            char *message = buf + 6;
            printf("Relay message: %s\n", message); // Debug info
            // [Add logic to forward message to the target client here]
        } else if (strncmp(buf, "direct:", 7) == 0) {
            // Direct Mode: direct messages are handled by clients themselves
            printf("Direct mode message setup initiated.\n");
        } else {
            printf("Unknown command received: %s\n", buf);
        }
    }

    close(conn_fd);
}

void register_user(int conn_fd, char* name) {
    if (user_count >= MAX_USERS) {
        send(conn_fd, "0Registration failed: maximum users reached.", BUFFER_SIZE, 0);
        return;
    }
    if (strlen(name) > MAXNAME) {
        send(conn_fd, "0Registration failed: name too long.", BUFFER_SIZE, 0);
        return;
    }
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].registered && strcmp(users[i].name, name) == 0) {
            send(conn_fd, "0Registration failed: name already taken.", BUFFER_SIZE, 0);
            return;
        }
    }
    strncpy(users[user_count].name, name, MAXNAME);
    users[user_count].registered = true;
    user_count++;
    send(conn_fd, "1Registration successful.", BUFFER_SIZE, 0);
    printf("USER '%s' registered successfully! [%s]\n", name, get_current_timestamp());
}

bool login_user(int conn_fd, char* name) {
    for (int i = 0; i < user_count; i++) {
        if (users[i].registered && strcmp(users[i].name, name) == 0) {
            send(conn_fd, "1Login successful.", BUFFER_SIZE, 0);
            printf("%s logged in! [%s]\n", name, get_current_timestamp());
            return true;
        }
    }
    send(conn_fd, "0Login failed: user not registered.", BUFFER_SIZE, 0);
    return false;
}

void send_message(int conn_fd, char* username, char* message) {
    if (strlen(username) + strlen(message) + 10 > BUFFER_SIZE) {
        send(conn_fd, "0Message too long.", BUFFER_SIZE, 0);
        return;
    }
    char formatted_message[BUFFER_SIZE];
    snprintf(formatted_message, sizeof(formatted_message), "<%s>: %s", username, message);
    printf("%s [%s]\n", formatted_message, get_current_timestamp());
    send(conn_fd, "1Message received.", BUFFER_SIZE, 0);
}

char* get_current_timestamp() {
    static char timestamp[20];
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", t);
    return timestamp;
}

