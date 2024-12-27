// config.c
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// 创建监听端口
int create_listen_port(int *listen_fd, struct sockaddr_in *servaddr, int port, int max_connection) {
    if ((*listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        return -1;

    int opt = 1;
    if (setsockopt(*listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
        return -1;

    memset(servaddr, 0, sizeof(*servaddr));
    servaddr->sin_family = AF_INET;
    servaddr->sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr->sin_port = htons(port);

    if (bind(*listen_fd, (struct sockaddr*)servaddr, sizeof(*servaddr)) == -1)
        return -1;

    if (listen(*listen_fd, max_connection) == -1)
        return -1;

    return 1;
}

// 连接到指定端口
int connect_to_port(int *conn_fd, struct sockaddr_in *servaddr, char *ip, int port) {
    if ((*conn_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        return -1;

    memset(servaddr, 0, sizeof(*servaddr));
    servaddr->sin_family = AF_INET;
    servaddr->sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &servaddr->sin_addr) <= 0)
        return -1;

    if (connect(*conn_fd, (struct sockaddr*)servaddr, sizeof(*servaddr)) == -1)
        return -1;

    return 1;
}

// 格式化缓冲区
void format_buffer(char* buf, const char* signal, const char* from, const char* to, const char* mes) {
    memset(buf, 0, BUFFER_SIZE);
    strncpy(buf, signal, SIGNAL_SIZE);
    strncpy(buf + SIGNAL_SIZE, from, FROM_SIZE);
    strncpy(buf + SIGNAL_SIZE + FROM_SIZE, to, TO_SIZE);
    strncpy(buf + SIGNAL_SIZE + FROM_SIZE + TO_SIZE, mes, MES_SIZE);
}

// 分割缓冲区
void slice_buffer(const char* buf, char* signal, char* from, char* to, char* mes) {
    memset(signal, 0, SIGNAL_SIZE);
    memset(from, 0, FROM_SIZE);
    memset(to, 0, TO_SIZE);
    memset(mes, 0, MES_SIZE);
    strncpy(signal, buf, SIGNAL_SIZE);
    strncpy(from, buf + SIGNAL_SIZE, FROM_SIZE);
    strncpy(to, buf + SIGNAL_SIZE + FROM_SIZE, TO_SIZE);
    strncpy(mes, buf + SIGNAL_SIZE + FROM_SIZE + TO_SIZE, MES_SIZE);
}

// 获取时间戳
char* timestamp() {
    static char buffer[26];
    time_t timer;
    struct tm* tm_info;
    time(&timer);
    tm_info = localtime(&timer);
    strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    return buffer;
}

// 初始化 SSL 服务器
SSL_CTX* initialize_ssl_server(const char* cert_file, const char* key_file) {
    SSL_CTX *ctx;
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    ctx = SSL_CTX_new(SSLv23_server_method());
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }
    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }
    if (!SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr, "Private key does not match the public certificate\n");
        return NULL;
    }
    return ctx;
}

// 初始化 SSL 客户端
SSL_CTX* initialize_ssl_client() {
    SSL_CTX *ctx;
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    ctx = SSL_CTX_new(SSLv23_client_method());
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }
    return ctx;
}

// 清理 SSL
void cleanup_ssl() {
    ERR_free_strings();
    EVP_cleanup();
}

// 发送完整数据
int send_full(int sockfd, const void *buf, size_t len) {
    size_t total_sent = 0;
    const uint8_t *data = (const uint8_t *)buf;
    
    while (total_sent < len) {
        ssize_t sent = send(sockfd, data + total_sent, len - total_sent, 0);
        if (sent <= 0) {
            perror("Failed to send data");
            return -1;
        }
        total_sent += sent;
    }
    return total_sent;
}

// 接收完整数据
int recv_full(int sockfd, void *buf, size_t len) {
    size_t total_received = 0;
    uint8_t *data = (uint8_t *)buf;
    
    while (total_received < len) {
        ssize_t received = recv(sockfd, data + total_received, len - total_received, 0);
        if (received <= 0) {
            return -1;
        }
        total_received += received;
    }
    return total_received;
}
