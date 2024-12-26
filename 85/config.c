// config.c
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>

// SSL Initialization
SSL_CTX* initialize_ssl_server(const char* cert_file, const char* key_file) {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    // 設定憑證和私鑰
    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) <= 0 ) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    return ctx;
}

SSL_CTX* initialize_ssl_client() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    const SSL_METHOD *method = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    return ctx;
}

void cleanup_ssl() {
    EVP_cleanup();
}

void format_buffer(char* buf, const char* signal, const char* from, const char* to, const char* mes) {
    memset(buf, 0, TOTAL_SIZE);
    snprintf(buf, SIGNAL_SIZE, "%s", signal);
    snprintf(buf + SIGNAL_SIZE, FROM_SIZE, "%s", from);
    snprintf(buf + SIGNAL_SIZE + FROM_SIZE, TO_SIZE, "%s", to);
    snprintf(buf + SIGNAL_SIZE + FROM_SIZE + TO_SIZE, MES_SIZE, "%s", mes);
}

void slice_buffer(const char* buf, char* signal, char* from, char* to, char* mes) {
    strncpy(signal, buf, SIGNAL_SIZE);
    signal[SIGNAL_SIZE - 1] = '\0';
    strncpy(from, buf + SIGNAL_SIZE, FROM_SIZE);
    from[FROM_SIZE - 1] = '\0';
    strncpy(to, buf + SIGNAL_SIZE + FROM_SIZE, TO_SIZE);
    to[TO_SIZE - 1] = '\0';
    strncpy(mes, buf + SIGNAL_SIZE + FROM_SIZE + TO_SIZE, MES_SIZE);
    mes[MES_SIZE - 1] = '\0';
}

char* timestamp() {
    static char _timestamp[20];
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(_timestamp, sizeof(_timestamp), "%H:%M:%S", t);
    return _timestamp;
}

int create_listen_port(int *listen_fd, struct sockaddr_in *servaddr, int port, int max_connection) {
    *listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*listen_fd < 0) {
        printf("[Error] Socket error in create_listen_port\n");
        return -1;
    }

    memset(servaddr, 0, sizeof(*servaddr));
    servaddr->sin_family = AF_INET;
    servaddr->sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr->sin_port = htons(port);

    if (bind(*listen_fd, (struct sockaddr*)servaddr, sizeof(*servaddr)) < 0) {
        printf("[Error] Bind error in create_listen_port\n");
        return -1;
    }
    if (listen(*listen_fd, max_connection) < 0) {
        printf("[Error] Listen error in create_listen_port\n");
        return -1;
    }
    return 1;
}

int connect_to_port(int *conn_fd, struct sockaddr_in *servaddr, char *ip, int port) {
    *conn_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*conn_fd < 0) {
        printf("[Error] Socket error in connect_to_port\n");
        return -1;
    }

    memset(servaddr, 0, sizeof(*servaddr));
    servaddr->sin_family = AF_INET;
    servaddr->sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &servaddr->sin_addr) <= 0) {
        printf("[Error] Socket error in connect_to_port\n");
        return -1;
    }
    if (connect(*conn_fd, (struct sockaddr*)servaddr, sizeof(*servaddr)) < 0) {
        printf("[Error] Socket error in connect_to_port\n");
        return -1;
    }

    return 1;
}
