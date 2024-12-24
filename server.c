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
#include "config.h"


//--- FUNCTION ---//
//worker
void *worker_thread(void *arg);

// not logged in
void handle_no_login(int conn_fd, struct sockaddr_in cliaddr);
int  register_user(int conn_fd, char* name);
int  login_user(int conn_fd, char* name, struct sockaddr_in cliaddr);

// logged in
void handle_user(int conn_fd, char* name);
char *get_current_timestamp();


//--- USER INFO ---//
typedef struct {
    // 註冊後不變資料
    char name[MAX_NAME];               // 使用者名稱
    int  id;                           // 使用者ID

    // 每次登入後紀錄，依照情況更改
    bool status;                       // 是否online
    int  socket_fd;                    // Socket file descriptor
    char ip[INET_ADDRSTRLEN];          // IP位址
    int  client_port;                  // Client 連接埠號
    int  receiver_port;                // Receiver 連接埠號
} User;

User users[MAX_USERS];
int user_count = 0;
pthread_mutex_t users_lock = PTHREAD_MUTEX_INITIALIZER;          // Access users的lock


//--- THREAD POOL ---//
int task_queue[QUEUE_SIZE];
int queue_front = 0, queue_rear = 0, queue_count = 0;
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;          // Access queue的lock
pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;       // Worker等待有Task

pthread_t workers[MAX_ONLINE];
bool stop_flag = false;


//--- MAIN FUNCTION ---//
int main() {
    for (int i = 0; i < MAX_ONLINE; i++)
        pthread_create(&workers[i], NULL, worker_thread, NULL);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
        ERR_EXIT("socket");

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SERVER_PORT);

    if (bind(listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0)
        ERR_EXIT("bind");
    if (listen(listen_fd, 10) < 0)
        ERR_EXIT("listen");

    printf("Server is running on port %d\n", SERVER_PORT);
    printf("%s\n", LINE);

    while (true) {
        // 宣告Client的變數
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);

        // 等待連線
        int conn_fd = accept(listen_fd, (struct sockaddr*)&cliaddr, &clilen);
        if (conn_fd < 0) ERR_EXIT("accept");
        printf("New connection from %s:%d [%s]\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), get_current_timestamp());

        // 新增task
        pthread_mutex_lock(&queue_lock);
        if (queue_count >= QUEUE_SIZE) {
            // task已滿，回覆連線失敗
            send(conn_fd, QUEUE_FULL, strlen(QUEUE_FULL), 0);
        } else {
            task_queue[queue_rear] = conn_fd;
            queue_rear = (queue_rear + 1) % QUEUE_SIZE;
            queue_count++;
            pthread_cond_signal(&queue_not_empty);
        }
        pthread_mutex_unlock(&queue_lock);
    }

    stop_flag = true;
    close(listen_fd);
    return 0;
}


// Worker
void *worker_thread(void *arg) {
    (void)arg;
    while (!stop_flag) {
        // 取得task
        pthread_mutex_lock(&queue_lock);
        while (queue_count == 0)
            pthread_cond_wait(&queue_not_empty, &queue_lock);
        int conn_fd = task_queue[queue_front];
        queue_front = (queue_front + 1) % QUEUE_SIZE;
        queue_count--;
        pthread_mutex_unlock(&queue_lock);

        // 取得client資訊
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        if (getpeername(conn_fd, (struct sockaddr*)&cliaddr, &clilen) != 0) {
            printf("[Error] Error in getting client info\n");
            continue;
        }
        if (send(conn_fd, ACCEPT_TASK, strlen(ACCEPT_TASK), 0) < 0) {
            printf("[Error] Error in accepting task\n");
            continue;
        }

        handle_no_login(conn_fd, cliaddr);
        close(conn_fd);
    }
    return NULL;
}


// No Logged In
void handle_no_login(int conn_fd, struct sockaddr_in cliaddr) {
    char buf[BUFFER_SIZE];
    while (true) {
        // 1. Register  |  2. Login  |  3. Exit
        memset(buf, 0, BUFFER_SIZE);
        if (recv(conn_fd, buf, BUFFER_SIZE, 0) < 0) {
            printf("[Disconnected] conn_fd: %d\n", conn_fd);
            break;
        }

        if (strncmp(buf, REGISTER, strlen(REGISTER)) == 0) {
            char *name = buf + strlen(REGISTER);
            pthread_mutex_lock(&users_lock);
            int r = register_user(conn_fd, name);
            pthread_mutex_unlock(&users_lock);
            if (r == 1)  continue;
            if (r == 0)  continue;
            if (r == -1) break;
        } else if (strncmp(buf, LOGIN, strlen(LOGIN)) == 0) {
            char *name = buf + strlen(LOGIN);
            pthread_mutex_lock(&users_lock);
            int r = login_user(conn_fd, name, cliaddr);
            pthread_mutex_unlock(&users_lock);
            if (r == 1)  handle_user(conn_fd, name);
            if (r == 0)  continue;
            if (r == -1) break;
        } else if (strncmp(buf, EXIT, strlen(EXIT)) == 0) {
            printf("[Exit] conn_fd: %d\n", conn_fd);
            break;
        } else {
            printf("[Error] Unknow command: %s\n", buf);
            send(conn_fd, UNKNOW, BUFFER_SIZE, 0);
        }
    }
    return;
}

int register_user(int conn_fd, char* name) {
    if (user_count >= MAX_USERS) {
        if (send(conn_fd, USER_FULL, BUFFER_SIZE, 0) < 0) return -1;
        return 0;
    } else if (strlen(name) > MAX_NAME) {
        if (send(conn_fd, NAME_EXCEED, BUFFER_SIZE, 0) < 0) return -1;
        return 0;
    } else {
        for (int i = 0; i < user_count; i++) {
            if (strcmp(users[i].name, name) == 0) {
                if (send(conn_fd, NAME_REGISTERED, BUFFER_SIZE, 0) < 0) return -1;
                return 0;
            }
        }
        // 填資料
        strncpy(users[user_count].name, name, MAX_NAME);
        users[user_count].id = user_count;
        users[user_count].status = false;
        user_count++;

        if (send(conn_fd, REGISTER_SUCCESS, BUFFER_SIZE, 0) < 0) return -1;
        printf("[Register] NO.%d %s\n", user_count-1, name);

        return 1;
    }
}

int login_user(int conn_fd, char* name, struct sockaddr_in cliaddr) {
    // 取得IP及Port
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cliaddr.sin_addr, client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(cliaddr.sin_port);

    char buf[BUFFER_SIZE];
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].name, name) == 0) {
            if (users[i].status == true) {
                if (send(conn_fd, LOGGED_IN, BUFFER_SIZE, 0) < 0)
                    return -1;  // 斷線
                return 0;
            }
            users[i].status = true;
            users[i].socket_fd = conn_fd;
            strncpy(users[i].ip, client_ip, INET_ADDRSTRLEN);
            users[i].client_port = client_port;

            if (send(conn_fd, ASK_RCVR_PORT, BUFFER_SIZE, 0) < 0) {
                users[i].status = false;
                return -1;  // 斷線
            }
            memset(buf, 0, BUFFER_SIZE);
            if (recv(conn_fd, buf, BUFFER_SIZE, 0) < 0) {
                users[i].status = false;
                return -1;  // 斷線
            }

            users[i].receiver_port = atoi(buf);

            if (send(conn_fd, LOGIN_SUCCESS, BUFFER_SIZE, 0) < 0) {
                users[i].status = false;
                return -1;
            }

            printf("[Login] %s\n", name);
            return 1;
        }
    }

    if (send(conn_fd, NO_REGISTER, BUFFER_SIZE, 0) < 0)
        return -1;

    return 0;
}


// Logged In
void handle_user(int conn_fd, char* username) {
    char buf[BUFFER_SIZE];
    // char response[BUFFER_SIZE];

    while (true) {
        memset(buf, 0, BUFFER_SIZE);
        if (recv(conn_fd, buf, BUFFER_SIZE, 0) < 0) {
            printf("User %s disconnected.\n", username);
            break;
        }

        if (strcmp(buf, SHOW_LIST) == 0) {       // Show online users list
            pthread_mutex_lock(&users_lock);
            char user_info[1024];
            for (int i = 0; i < user_count; i++) {
                sprintf(user_info, "%2d: ", i);
                if (strcmp(username, users[i].name) == 0)
                    sprintf(user_info + strlen(user_info), "YOU ");
                else if (users[i].status)
                    sprintf(user_info + strlen(user_info), " *  ");
                else
                    sprintf(user_info + strlen(user_info), "    ");
                sprintf(user_info + strlen(user_info), "%s", users[i].name);
                if (i != user_count)
                    user_info[strlen(user_info)] = '\n';
            }
            pthread_mutex_unlock(&users_lock);

            if (send(conn_fd, user_info, BUFFER_SIZE, 0) < 0)
                break;
        } else if (strncmp(buf, SEND_MES, strlen(SEND_MES)) == 0) {            // Relay message
            int target_id = atoi(buf + strlen(SEND_MES));
            if (send(conn_fd, ASK_MES, BUFFER_SIZE, 0) < 0)
                break;
            char message[MAX_MES];
            memset(buf, 0, BUFFER_SIZE);
            if (recv(conn_fd, message, BUFFER_SIZE, 0) < 0)
                break;
            printf("Message from %s to %d: %s\n", username, target_id, message);

            // 與目標receiver建立連線
            pthread_mutex_lock(&users_lock);
            int tmp_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (tmp_fd < 0) ERR_EXIT("socket");
            struct sockaddr_in targetaddr;
            memset(&targetaddr, 0, sizeof(targetaddr));
            targetaddr.sin_family = AF_INET;
            targetaddr.sin_port = htons(users[target_id].receiver_port);
            if (inet_pton(AF_INET, users[target_id].ip, &targetaddr.sin_addr) <= 0)
                ERR_EXIT("Invalid IP");
            pthread_mutex_unlock(&users_lock);
            if (connect(tmp_fd, (struct sockaddr*)&targetaddr, sizeof(targetaddr)) < 0)
                if (send(conn_fd, MES_FAIL, BUFFER_SIZE, 0) < 0) break;

            // 傳訊息
            char to_receiver[BUFFER_SIZE];
            sprintf(to_receiver, "%s<%s>: %s", IS_MES, username, message);
            if (send(tmp_fd, to_receiver, BUFFER_SIZE, 0) < 0) {
                if (send(conn_fd, MES_FAIL, BUFFER_SIZE, 0) < 0) break;
            } else {
                if (send(conn_fd, MES_SUCCESS, BUFFER_SIZE, 0) < 0) break;
            }
            close(tmp_fd);
        } else if (strncmp(buf, ASK_USER_INFO, strlen(ASK_USER_INFO)) == 0) {       // Direct Message or File
            int target_id = atoi(buf + strlen(ASK_USER_INFO));
            pthread_mutex_lock(&users_lock);
            if (users[target_id].status == false) {
                if (send(conn_fd, OFFLINE, BUFFER_SIZE, 0) < 0) break;
            }
            else {
                char to_client[BUFFER_SIZE];
                sprintf(to_client, "%s %d", users[target_id].ip, users[target_id].receiver_port);
                if (send(conn_fd, to_client, BUFFER_SIZE, 0) < 0) break;
            }
            pthread_mutex_unlock(&users_lock);
        } else if (strcmp(buf, LOGOUT) == 0) {                     // Logout
            printf("[Logout] %s\n", username);
            send(conn_fd, LOGOUT_SUCCESS, BUFFER_SIZE, 0);
            break;
        } else {
            printf("[Error] Unknow command: %s\n", buf);
            if (send(conn_fd, UNKNOW, BUFFER_SIZE, 0) < 0)
                break;
        }
    }

    // Status改成Offline
    pthread_mutex_lock(&users_lock);
    for (int i=0 ; i<user_count ; i++)
        if (strcmp(username, users[i].name) == 0)
            users[i].status = false;
    pthread_mutex_unlock(&users_lock);

    return;
}

char* get_current_timestamp() {
    static char timestamp[20];
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", t);
    return timestamp;
}
