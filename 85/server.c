// server.c
#include "config.h"

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

// OpenSSL Headers
#include <openssl/ssl.h>
#include <openssl/err.h>

//--- FUNCTION ---//
// worker
void *worker_thread(void *arg);

// not logged in
void handle_no_login(SSL *ssl);
int register_user_ssl(SSL *ssl, char* name);
int login_user_ssl(SSL *ssl, char* name);

// logged in
int handle_user_ssl(SSL *ssl, char* name);
int show_user_ssl(SSL *ssl, char* name);
int relay_user_ssl(SSL *ssl, char* name, int targetID, char *message);
int direct_user_ssl(SSL *ssl, char* name, int targetID);
int file_user_ssl(SSL *ssl, char* name, int targetID, char *filename);

//--- USER INFO ---//
typedef struct {
    // 註冊後不變的資料
    char name[MAX_NAME];               // 使用者名稱
    int  id;                           // 使用者 ID

    // 每次登入後紀錄，依照情況更改
    bool status;                       // 是否 online
    SSL *ssl_socket;                   // SSL Socket for communicate
    SSL *relay_ssl;                    // SSL Socket for Relay message
    SSL *file_ssl;                     // SSL Socket for file transmission
    char ip[INET_ADDRSTRLEN];          // IP位址
    int  receiver_port;                // Direct message 連接埠號
} User;

User users[MAX_USERS];
int user_count = 0;
pthread_mutex_t users_lock = PTHREAD_MUTEX_INITIALIZER;          // Access users的lock

//--- THREAD POOL ---//
SSL* task_queue_ssl[QUEUE_SIZE];
int queue_front = 0, queue_rear = 0, queue_count = 0;
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;          // Access queue的lock
pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;       // Worker等待有Task

pthread_t workers[MAX_ONLINE];
bool stop_flag = false;

//--- SOCKET ---//
int side_fd;

//--- SSL ---//
SSL_CTX *ssl_ctx;

//--- MAIN FUNCTION ---//
int main() {
    // 初始化 SSL 伺服器上下文
    ssl_ctx = initialize_ssl_server("server.crt", "server.key");

    // 開 main welcome socket
    int listen_fd;
    struct sockaddr_in servaddr;
    if (create_listen_port(&listen_fd, &servaddr, SERVER_PORT, MAX_ONLINE) == -1) {
        ERR_EXIT("create_listen_port");
    }

    // 開 other welcome socket (for client 端的接收)
    struct sockaddr_in sideaddr;
    if (create_listen_port(&side_fd, &sideaddr, SIDE_PORT, MAX_ONLINE) == -1) {
        ERR_EXIT("create_listen_port");
    }

    printf("Server is running on port %d\n", SERVER_PORT);

    // 創建工作執行緒
    for (int i = 0; i < MAX_ONLINE; i++) {
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    }

    while (true) {
        // 宣告Client的變數
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);

        // 等待連線
        int conn_fd = accept(listen_fd, (struct sockaddr*)&cliaddr, &clilen);
        if (conn_fd < 0) {
            printf("[Error] Accept error\n");
            continue;
        }

        printf("New connection from %s:%d [%s]\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), timestamp());

        // 為連接建立 SSL 結構
        SSL *ssl = SSL_new(ssl_ctx);
        SSL_set_fd(ssl, conn_fd);
        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            close(conn_fd);
            continue;
        }

        // 將 SSL 指標傳遞給工作執行緒
        pthread_mutex_lock(&queue_lock);
        if (queue_count >= QUEUE_SIZE) {
            // task已滿，回覆連線失敗
            SSL_write(ssl, QUEUE_FULL, strlen(QUEUE_FULL));
            SSL_shutdown(ssl);
            SSL_free(ssl);
            close(conn_fd);
        } else {
            task_queue_ssl[queue_rear] = ssl;
            queue_rear = (queue_rear + 1) % QUEUE_SIZE;
            queue_count++;
            pthread_cond_signal(&queue_not_empty);
        }
        pthread_mutex_unlock(&queue_lock);
    }

    // 清理
    stop_flag = true;
    pthread_cond_broadcast(&queue_not_empty);
    for (int i = 0; i < MAX_ONLINE; i++) {
        pthread_join(workers[i], NULL);
    }
    cleanup_ssl();
    SSL_CTX_free(ssl_ctx);
    close(listen_fd);
    close(side_fd);
    return 0;
}

// Worker
void *worker_thread(void *arg) {
    while (!stop_flag) {
        // 取得task
        pthread_mutex_lock(&queue_lock);
        while (queue_count == 0 && !stop_flag)
            pthread_cond_wait(&queue_not_empty, &queue_lock);
        if (stop_flag) {
            pthread_mutex_unlock(&queue_lock);
            break;
        }
        SSL *ssl = task_queue_ssl[queue_front];
        queue_front = (queue_front + 1) % QUEUE_SIZE;
        queue_count--;
        pthread_mutex_unlock(&queue_lock);

        // 發送接受任務的回覆
        if (SSL_write(ssl, ACCEPT_TASK, strlen(ACCEPT_TASK)) <= 0) {
            printf("[Error] Error in accepting task\n");
            SSL_shutdown(ssl);
            SSL_free(ssl);
            continue;
        }

        // 處理未登入狀態
        handle_no_login(ssl);

        // 關閉連接
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    return NULL;
}

// Worker 處理未登入狀態
void handle_no_login(SSL *ssl) {
    char buf[BUFFER_SIZE];
    while (true) {
        // 接收資料
        memset(buf, 0, BUFFER_SIZE);
        int bytes = SSL_read(ssl, buf, BUFFER_SIZE);
        if (bytes <= 0) {
            printf("[Error] SSL_read wrong in handle_no_login\n");
            break;
        }
        buf[bytes] = '\0';

        int r; // 功能 function 的 Return 值
        if (strncmp(buf, REGISTER, strlen(REGISTER)) == 0) {
            char *name = buf + strlen(REGISTER);
            pthread_mutex_lock(&users_lock);
            r = register_user_ssl(ssl, name);
            pthread_mutex_unlock(&users_lock);
            if (r == -1) break;
        } else if (strncmp(buf, LOGIN, strlen(LOGIN)) == 0) {
            char *name = buf + strlen(LOGIN);
            pthread_mutex_lock(&users_lock);
            r = login_user_ssl(ssl, name);
            pthread_mutex_unlock(&users_lock);
            if (r == 0)  continue;
            if (r == -1) break;

            // 處理登入後的部份
            r = handle_user_ssl(ssl, name);
            if (r == -1) break;
        } else if (strncmp(buf, EXIT, strlen(EXIT)) == 0) {
            printf("[Exit] Client Exit\n");
            break;
        } else {
            printf("[Error] Unknown command in handle_no_login: %s\n", buf);
            r = SSL_write(ssl, UNKNOWN, strlen(UNKNOWN));
            if (r <= 0) break;
        }
    }
    return;
}

// Register User via SSL
int register_user_ssl(SSL *ssl, char* name) {
    // 檢查名額
    if (user_count >= MAX_USERS) {
        if (SSL_write(ssl, USER_FULL, strlen(USER_FULL)) <= 0) return -1;
        return 0;
    }

    // 檢查姓名長度
    if (strlen(name) >= MAX_NAME) {
        if (SSL_write(ssl, NAME_EXCEED, strlen(NAME_EXCEED)) <= 0) return -1;
        return 0;
    }

    // 檢查是否已註冊
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].name, name) == 0) {
            if (SSL_write(ssl, NAME_REGISTERED, strlen(NAME_REGISTERED)) <= 0) return -1;
            return 0;
        }
    }

    // 填資料
    strncpy(users[user_count].name, name, MAX_NAME);
    users[user_count].id = user_count;
    users[user_count].status = false;
    users[user_count].ssl_socket = NULL;
    users[user_count].relay_ssl = NULL;
    users[user_count].file_ssl = NULL;
    user_count++;

    if (SSL_write(ssl, REGISTER_SUCCESS, strlen(REGISTER_SUCCESS)) <= 0) return -1;
    printf("[Register] %s\n", name);

    return 1;
}

// Login User via SSL
int login_user_ssl(SSL *ssl, char* name) {
    // 取得 login ID
    int login_id = -1;
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].name, name) == 0) {
            login_id = i;
            break;
        }
    }

    // 排除未註冊
    if (login_id == -1) {
        if (SSL_write(ssl, NO_REGISTER, strlen(NO_REGISTER)) <= 0) return -1;
        return 0;
    }

    // 排除已登入
    if (users[login_id].status) {
        if (SSL_write(ssl, LOGGED_IN, strlen(LOGGED_IN)) <= 0) return -1;
        return 0;
    }

    // 更新使用者狀態
    users[login_id].status = true;
    users[login_id].ssl_socket = ssl;

    // 建立 Relay Socket
    if (SSL_write(ssl, RELAY_SOCKET, strlen(RELAY_SOCKET)) <= 0) return -1;

    // 接受 Relay 連接
    int relay_conn_fd = accept(side_fd, NULL, NULL);
    if (relay_conn_fd < 0) {
        printf("[Error] Accept relay socket failed\n");
        if (SSL_write(ssl, FILE_FAIL, strlen(FILE_FAIL)) <= 0) return -1;
        return 0;
    }
    SSL *relay_ssl = SSL_new(ssl_ctx);
    SSL_set_fd(relay_ssl, relay_conn_fd);
    if (SSL_accept(relay_ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(relay_ssl);
        close(relay_conn_fd);
        if (SSL_write(ssl, FILE_FAIL, strlen(FILE_FAIL)) <= 0) return -1;
        return 0;
    }
    users[login_id].relay_ssl = relay_ssl;

    // 建立 File Socket
    if (SSL_write(ssl, FILE_SOCKET, strlen(FILE_SOCKET)) <= 0) return -1;

    int file_conn_fd = accept(side_fd, NULL, NULL);
    if (file_conn_fd < 0) {
        printf("[Error] Accept file socket failed\n");
        if (SSL_write(ssl, FILE_FAIL, strlen(FILE_FAIL)) <= 0) return -1;
        return 0;
    }
    SSL *file_ssl = SSL_new(ssl_ctx);
    SSL_set_fd(file_ssl, file_conn_fd);
    if (SSL_accept(file_ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(file_ssl);
        close(file_conn_fd);
        if (SSL_write(ssl, FILE_FAIL, strlen(FILE_FAIL)) <= 0) return -1;
        return 0;
    }
    users[login_id].file_ssl = file_ssl;

    // 取得 IP
    struct sockaddr_in cliaddr;
    socklen_t clilen = sizeof(cliaddr);
    getpeername(SSL_get_fd(ssl), (struct sockaddr*)&cliaddr, &clilen);
    inet_ntop(AF_INET, &cliaddr.sin_addr, users[login_id].ip, INET_ADDRSTRLEN);

    // 取得 receiver port
    if (SSL_write(ssl, ASK_RCVR_PORT, strlen(ASK_RCVR_PORT)) <= 0) return -1;
    char buf[BUFFER_SIZE];
    memset(buf, 0, BUFFER_SIZE);
    int bytes = SSL_read(ssl, buf, BUFFER_SIZE);
    if (bytes <= 0) return -1;
    buf[bytes] = '\0';
    users[login_id].receiver_port = atoi(buf);

    if (SSL_write(ssl, LOGIN_SUCCESS, strlen(LOGIN_SUCCESS)) <= 0) {
        users[login_id].status = false;
        return -1;
    }

    printf("[Login] %s\n", name);

    return 1;
}

// Handle Logged In User via SSL
int handle_user_ssl(SSL *ssl, char* username) {
    char buf[BUFFER_SIZE];

    while (true) {
        memset(buf, 0, BUFFER_SIZE);
        int bytes = SSL_read(ssl, buf, BUFFER_SIZE);
        if (bytes <= 0) {
            printf("[Error] SSL_read wrong in handle_user for %s\n", username);
            break;
        }
        buf[bytes] = '\0';

        int r; // 功能 function 的 Return 值
        if (strcmp(buf, SHOW_LIST) == 0) {       // Show online users list
            pthread_mutex_lock(&users_lock);
            r = show_user_ssl(ssl, username);
            pthread_mutex_unlock(&users_lock);
            if (r == -1) return -1;

        } else if (strncmp(buf, RELAY_MES, strlen(RELAY_MES)) == 0) {            // Relay message
            int target_id = atoi(buf + strlen(RELAY_MES));

            if (SSL_write(ssl, ASK_MES, strlen(ASK_MES)) <= 0)
                return -1;

            char message[MAX_MES];
            memset(message, 0, sizeof(message));
            bytes = SSL_read(ssl, message, BUFFER_SIZE);
            if (bytes <= 0)
                return -1;
            message[bytes] = '\0';

            printf("Message from %s to ID-%d: %s\n", username, target_id, message);

            pthread_mutex_lock(&users_lock);
            r = relay_user_ssl(ssl, username, target_id, message);
            pthread_mutex_unlock(&users_lock);
            if (r == -1) return -1;
        } else if (strncmp(buf, DIRECT_MES, strlen(DIRECT_MES)) == 0) {       // Direct Message
            int target_id = atoi(buf + strlen(DIRECT_MES));
            pthread_mutex_lock(&users_lock);
            r = direct_user_ssl(ssl, username, target_id);
            pthread_mutex_unlock(&users_lock);
            if (r == -1) return -1;
        } else if (strncmp(buf, FILE_TRANSFER, strlen(FILE_TRANSFER)) == 0) {       // File Transfer
            int target_id = atoi(buf + strlen(FILE_TRANSFER));

            if (SSL_write(ssl, ASK_FILE_NAME, strlen(ASK_FILE_NAME)) <= 0)
                return -1;

            char filename[MAX_MES];
            memset(filename, 0, sizeof(filename));
            bytes = SSL_read(ssl, filename, BUFFER_SIZE);
            if (bytes <= 0)
                return -1;
            filename[bytes] = '\0';

            printf("File from %s to ID-%d: %s\n", username, target_id, filename);

            pthread_mutex_lock(&users_lock);
            r = file_user_ssl(ssl, username, target_id, filename);
            pthread_mutex_unlock(&users_lock);
            if (r == -1) return -1;
        } else if (strcmp(buf, LOGOUT) == 0) {                     // Logout
            printf("[Logout] %s\n", username);
            break;
        } else {
            printf("[Error] Unknown command: %s\n", buf);
            if (SSL_write(ssl, UNKNOWN, strlen(UNKNOWN)) <= 0)
                break;
        }
    }

    // 將使用者狀態設為離線
    pthread_mutex_lock(&users_lock);
    for (int i = 0; i < user_count; i++) {
        if (strcmp(username, users[i].name) == 0) {
            users[i].status = false;
            if (users[i].relay_ssl) {
                SSL_shutdown(users[i].relay_ssl);
                SSL_free(users[i].relay_ssl);
                users[i].relay_ssl = NULL;
            }
            if (users[i].file_ssl) {
                SSL_shutdown(users[i].file_ssl);
                SSL_free(users[i].file_ssl);
                users[i].file_ssl = NULL;
            }
            // users[i].ssl_socket = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&users_lock);

    return 1;
}

// Show Online Users via SSL
int show_user_ssl(SSL *ssl, char* username) {
    char user_info[BUFFER_SIZE];
    memset(user_info, 0, sizeof(user_info));
    for (int i = 0; i < user_count; i++) {
        sprintf(user_info + strlen(user_info), "%2d: ", i);
        if (strcmp(username, users[i].name) == 0)
            sprintf(user_info + strlen(user_info), "YOU ");
        else if (users[i].status)
            sprintf(user_info + strlen(user_info), " *  ");
        else
            sprintf(user_info + strlen(user_info), "    ");
        sprintf(user_info + strlen(user_info), "%s\n", users[i].name);
    }

    if (SSL_write(ssl, user_info, strlen(user_info)) <= 0)
        return -1;

    return 1;
}

// Relay Message via SSL
int relay_user_ssl(SSL *ssl, char* username, int targetID, char *message) {
    // 排除不在線
    if (targetID < 0 || targetID >= user_count || users[targetID].status == false) {
        if (SSL_write(ssl, OFFLINE, strlen(OFFLINE)) <= 0) return -1;
        return 0;
    } 

    // 傳訊息
    char to_receiver[BUFFER_SIZE];
    memset(to_receiver, 0, sizeof(to_receiver));
    format_buffer(to_receiver, IS_MES, username, "", message);

    if (SSL_write(users[targetID].relay_ssl, to_receiver, BUFFER_SIZE) <= 0) {
        if (SSL_write(ssl, MES_FAIL, strlen(MES_FAIL)) <= 0) return -1;
        return 0;
    } else {
        if (SSL_write(ssl, MES_SUCCESS, strlen(MES_SUCCESS)) <= 0) return -1;
        return 1;
    }
}

// Direct Message via SSL
int direct_user_ssl(SSL *ssl, char* username, int targetID) {
    // 排除不在線
    if (targetID < 0 || targetID >= user_count || users[targetID].status == false) {
        if (SSL_write(ssl, OFFLINE, strlen(OFFLINE)) <= 0) return -1;
        return 0;
    }

    // 傳目標的 Ip 及 Port
    char to_client[BUFFER_SIZE];
    memset(to_client, 0, BUFFER_SIZE);
    sprintf(to_client, "%s %d", users[targetID].ip, users[targetID].receiver_port);
    if (SSL_write(ssl, to_client, strlen(to_client)) <= 0)
        return -1;

    return 1;
}

// File Transfer via SSL
int file_user_ssl(SSL *ssl, char* username, int targetID, char *filename) {
    // 排除不在線
    if (targetID < 0 || targetID >= user_count || users[targetID].status == false) {
        if (SSL_write(ssl, OFFLINE, strlen(OFFLINE)) <= 0) return -1;
        return 0;
    }

    // 傳檔案
    char buf[BUFFER_SIZE];
    char to_receiver[BUFFER_SIZE];
    memset(to_receiver, 0, sizeof(to_receiver));
    format_buffer(to_receiver, IS_FILE, username, "", filename);

    if (SSL_write(users[targetID].file_ssl, to_receiver, BUFFER_SIZE) <= 0) {
        if (SSL_write(ssl, FILE_FAIL, strlen(FILE_FAIL)) <= 0) return -1;
        return 0;
    } 

    // 等待對方回應
    memset(buf, 0, BUFFER_SIZE);
    int bytes = SSL_read(users[targetID].file_ssl, buf, BUFFER_SIZE - 1);
    if (bytes <= 0) {
        if (SSL_write(ssl, FILE_FAIL, strlen(FILE_FAIL)) <= 0) return -1;
        return 0;
    }
    buf[bytes] = '\0';

    if (strcmp(buf, REJECT_FILE) == 0) {
        if (SSL_write(ssl, REJECT_FILE, strlen(REJECT_FILE)) <= 0) return -1;
        return 0;
    } else if (strcmp(buf, ACCEPT_FILE) == 0) {
        if (SSL_write(ssl, ACCEPT_FILE, strlen(ACCEPT_FILE)) <= 0) return -1;

        // 假設傳檔案過程不會斷線（不處理）
        while (true) {
            memset(buf, 0, sizeof(buf));
            bytes = SSL_read(ssl, buf, BUFFER_SIZE - 1);
            if (bytes <= 0) {
                printf("[Error] SSL_read during file transfer\n");
                break;
            }
            buf[bytes] = '\0';
            SSL_write(users[targetID].file_ssl, buf, bytes);
            if (strcmp(buf, END_OF_FILE) == 0)
                break;
            memset(buf, 0, sizeof(buf));
            bytes = SSL_read(users[targetID].file_ssl, buf, BUFFER_SIZE - 1);
            if (bytes <= 0) {
                printf("[Error] SSL_read during file transfer\n");
                break;
            }
            buf[bytes] = '\0';
            if (strncmp(buf, ACK_FILE, strlen(ACK_FILE)) != 0)
                printf("[Error] error in transferring file\n");
            SSL_write(ssl, ACK_FILE, strlen(ACK_FILE));
        }
    }
    return 1;
}
