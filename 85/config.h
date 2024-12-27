// config.h
#ifndef CONFIG_H
#define CONFIG_H

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <netinet/in.h>

// 連線設定
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9530
#define SIDE_PORT 9531
#define STREAM_PORT 9532

// 函数声明
int create_listen_port(int *listen_fd, struct sockaddr_in *servaddr, int port, int max_connection);
int connect_to_port(int *conn_fd, struct sockaddr_in *servaddr, char *ip, int port);

#define BUFFER_SIZE 1024
#define MAX_NAME 16
#define MAX_MES (BUFFER_SIZE - SIGNAL_SIZE - 2 * MAX_NAME)

#define MAX_ONLINE 10                  // 最多同時上限人數
#define MAX_USERS 20                   // 最多註冊人數
#define QUEUE_SIZE 20                  // 最多等待連線人數

#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)

// 添加 error_exit 函数定义
#define error_exit(msg) do { \
    fprintf(stderr, "[Error] %s\n", msg); \
    exit(1); \
} while(0)

// Configuration for message format
#define SIGNAL_SIZE 32
#define FROM_SIZE MAX_NAME
#define TO_SIZE MAX_NAME
#define MES_SIZE (BUFFER_SIZE - SIGNAL_SIZE - 2 * MAX_NAME)
#define TOTAL_SIZE BUFFER_SIZE

// 函数声明
void format_buffer(char* buf, const char* signal, const char* from, const char* to, const char* mes);
void slice_buffer(const char* buf, char* signal, char* from, char* to, char* mes);
char* timestamp();

// SSL 函数声明
SSL_CTX* initialize_ssl_server(const char* cert_file, const char* key_file);
SSL_CTX* initialize_ssl_client();
void cleanup_ssl();

// 数据传输函数声明
int send_full(int sockfd, const void *buf, size_t len);
int recv_full(int sockfd, void *buf, size_t len);

// 訊息定義
#define LINE "================================"

#define QUEUE_FULL "queue full"

#define ACCEPT_TASK "accept task"

#define REGISTER "register:"
    #define USER_FULL "user_full"
    #define NAME_EXCEED "name_exceed"
    #define NAME_REGISTERED "name_registered"
    #define REGISTER_SUCCESS "register_success"
#define LOGIN "login:"
    #define NO_REGISTER "no_register"
    #define LOGGED_IN "logged_in"
    #define RELAY_SOCKET "relay_socket"
    #define FILE_SOCKET "file_socket"
    #define ASK_RCVR_PORT "ask_rcvr_port"
    #define LOGIN_SUCCESS "login_success"
#define EXIT "exit"
#define UNKNOWN "unknown"

#define SHOW_LIST "show_list"
#define RELAY_MES "relay_mes"
    #define ASK_MES "ask_mes"
    #define IS_MES "is_mes "
    #define OFFLINE "offline"
    #define MES_FAIL "mes_fail"
    #define MES_SUCCESS "mes_success"
#define DIRECT_MES "direct_mes"
    #define OFFLINE "offline"
#define FILE_TRANSFER "file_transfer"
    #define ASK_FILE_NAME "ask_file_name"
    #define OFFLINE "offline"
    #define IS_FILE "is_file"
    #define FILE_FAIL "file_fail"
    #define ACCEPT_FILE "accept_file"
    #define REJECT_FILE "reject_file"
    #define ACK_FILE "ack_file"
    #define END_OF_FILE "end_of_file"
#define LOGOUT "logout"
    #define LOGOUT_SUCCESS "logout_success"

// 顏色
#define NONE "\033[m"
#define RED "\033[0;32;31m"
#define LIGHT_RED "\033[1;31m"
#define GREEN "\033[0;32;32m"
#define LIGHT_GREEN "\033[1;32m"
#define BLUE "\033[0;32;34m"
#define LIGHT_BLUE "\033[1;34m"
#define DARY_GRAY "\033[1;30m"
#define CYAN "\033[0;36m"
#define LIGHT_CYAN "\033[1;36m"
#define PURPLE "\033[0;35m"
#define LIGHT_PURPLE "\033[1;35m"
#define BROWN "\033[0;33m"
#define YELLOW "\033[1;33m"
#define LIGHT_GRAY "\033[0;37m"
#define WHITE "\033[1;37m"

// 添加 streaming 相關定義
#define STREAM_CMD "STREAM"
#define STREAM_SOCKET "Please connect to streaming socket"

#endif
