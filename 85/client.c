// client.c
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>
#include <arpa/inet.h>
#include <gtk/gtk.h>

// OpenSSL Headers
#include <openssl/ssl.h>
#include <openssl/err.h>

// SDL2 Headers
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

//--- THREADS ---//
void *client_thread(void *arg);
void *relay_thread(void *arg);
void file_thread();                 // using main thread to run GTK
void *direct_thread(void *arg);
bool stop_flag = false;
pthread_cond_t is_logged_in = PTHREAD_COND_INITIALIZER;       // relay & file thread 在登入後才開始動作
pthread_mutex_t relay_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

//--- FUNCTION@ ---//
// client_thread
void show_main_menu();
void show_logged_in_menu();
int handle_no_logged_ssl(SSL *ssl);
int send_register_ssl(SSL *ssl);
int send_login_ssl(SSL *ssl);
int handle_logged_ssl(SSL *ssl);
int show_online_ssl(SSL *ssl);
int send_relay_ssl(SSL *ssl);
int send_direct_ssl(SSL *ssl);
int send_file_ssl(SSL *ssl);
int recv_streaming_ssl(SSL *ssl);   // 添加新的函數聲明

// file_thread
int file_questioner(char *from, char *filename);
bool accept_file = false;

//--- USER INFO ---//
typedef struct {
    char name[MAX_NAME];               // 使用者名稱
    bool status;                       // 是否 Login
    SSL *ssl_socket;                   // SSL Socket for communicate
    SSL *relay_ssl;                    // SSL Socket for Relay message
    SSL *file_ssl;                     // SSL Socket for file transmission
    int  receiver_fd;                  // Direct message 連接埠號
    int  receiver_port;                // Direct message 連接埠號
} User;

User user;

SSL_CTX *ctx;

//--- SHOW MENU ---//
void show_main_menu() {
    printf("Please choose an option(1, 2, 3):\n");
    printf("1. Register\n");
    printf("2. Login\n");
    printf("3. Exit\n");
}

void show_logged_in_menu() {
    printf("Please choose an option(1, 2, 3, 4, 5):\n");
    printf("1. Show online users\n");
    printf("2. Relay send message\n");
    printf("3. Direct send message\n");
    printf("4. Send file\n");
    printf("5. Stream video\n");
    printf("6. Log out\n");
}
//--- GTK FUNCTION ---//
// delete_event 信號處理函數
gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    printf("delete_event triggered\n");
    gtk_main_quit(); // 退出主事件迴圈
    return FALSE;    // 返回 FALSE允許進一步處理（會觸發 destroy 信號）
}

// destroy 信號處理函數
void on_destroy(GtkWidget *widget, gpointer data) {
    if (accept_file)
        printf("you select "GREEN"yes\n"NONE);
    else
        printf("you select "RED"no\n"NONE);
    gtk_main_quit(); // 確保主事件迴圈結束
}

// "Yes" 按鈕的回調函數
void on_yes_button_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *window = GTK_WIDGET(data); // 傳遞的視窗指標
    accept_file = 1; // 使用者按下 "Yes"
    if (GTK_IS_WIDGET(window)) {
        gtk_widget_destroy(window); // 銷毀視窗
    }
    gtk_main_quit(); // 結束主事件迴圈
}

// "No" 按鈕的回調函數
void on_no_button_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *window = GTK_WIDGET(data); // 傳遞的視窗指標
    accept_file = 0; // 使用者按下 "No"
    if (GTK_IS_WIDGET(window)) {
        gtk_widget_destroy(window); // 銷毀視窗
    }
    gtk_main_quit(); // 結束主事件迴圈
}

// 主函數實作
int file_questioner(char *from, char *filename) {
    GtkWidget *window;
    GtkWidget *vbox;
    GtkWidget *label;
    GtkWidget *button_yes;
    GtkWidget *button_no;
    GtkWidget *button_box;

    // 初始化 GTK
    gtk_init(NULL, NULL);

    // 創建主視窗
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Confirmation");
    gtk_window_set_default_size(GTK_WINDOW(window), 300, 150);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);

    // 連接信號：delete_event 和 destroy
    g_signal_connect(G_OBJECT(window), "delete_event", G_CALLBACK(on_delete_event), NULL);
    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(on_destroy), NULL);

    // 設定垂直布局容器
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // 添加標籤顯示訊息
    char message[1024];
    sprintf(message, "%s sent a file (%s) to you.\nWould you accept it?", from, filename);
    label = gtk_label_new(message);
    gtk_box_pack_start(GTK_BOX(vbox), label, TRUE, TRUE, 0);

    // 創建按鈕容器
    button_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    // 創建 "Yes" 按鈕
    button_yes = gtk_button_new_with_label("Yes");
    g_signal_connect(button_yes, "clicked", G_CALLBACK(on_yes_button_clicked), window);
    gtk_container_add(GTK_CONTAINER(button_box), button_yes);

    // 創建 "No" 按鈕
    button_no = gtk_button_new_with_label("No");
    g_signal_connect(button_no, "clicked", G_CALLBACK(on_no_button_clicked), window);
    gtk_container_add(GTK_CONTAINER(button_box), button_no);

    // 顯示所有元件
    gtk_widget_show_all(window);

    // 啟動主事件迴圈
    gtk_main();

    // 返回用戶的選擇
    return accept_file;
}

//--- MAIN FUNCTION ---//
int main() {
    // 初始化 SSL 客戶端上下文
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
    ctx = initialize_ssl_client();
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    // 取得 Receiver Port
    printf("Enter receiver port for direct message: ");
    scanf("%d", &(user.receiver_port));
    
    // 建立Receiver socket
    struct sockaddr_in rcvraddr;
    if (create_listen_port (&(user.receiver_fd), &rcvraddr, user.receiver_port, 1) == -1) {
        ERR_EXIT("create_listen_port");
    }

    // 建立Receiver socket (未使用 SSL)
    // 注意：Receiver socket 主要在客戶端接收來自其他客戶端的直接消息，不涉及 SSL
    // 這裡假設客戶端不需要建立接收 socket，因為直接消息是由其他客戶端發起的
    // 如果需要，您可以自行添加接收 socket 的實作

    // 連線至server
    struct sockaddr_in servaddr;
    int sock_fd;
    if (connect_to_port(&sock_fd, &servaddr, SERVER_IP, SERVER_PORT) == -1) {
        ERR_EXIT("connect_to_port");
    }

    // 為連接建立 SSL 結構
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock_fd);
    if (SSL_connect(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    // 接收伺服器回應
    char buf[BUFFER_SIZE];
    memset(buf, 0, BUFFER_SIZE);
    int bytes = SSL_read(ssl, buf, BUFFER_SIZE - 1);
    if (bytes <= 0) {
        printf("Error in SSL_read\n");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    buf[bytes] = '\0'; // 確保字串結尾

    if (strcmp(buf, QUEUE_FULL) == 0) {
        printf("Server response: queue full\n");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(sock_fd);
        return 0;
    } else if (strcmp(buf, ACCEPT_TASK) == 0) {
        printf("Server response: accept task\n");
        printf("Start creating threads...\n");
        pthread_t client_thd, relay_thd, direct_thd;
        pthread_create(&relay_thd, NULL, relay_thread, NULL);
        pthread_create(&direct_thd, NULL, direct_thread, NULL);
        pthread_create(&client_thd, NULL, client_thread, (void*)ssl);
        file_thread();
        pthread_join(client_thd, NULL);
        pthread_join(relay_thd, NULL);
        pthread_join(direct_thd, NULL);
    }

    // 清理
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    cleanup_ssl();
    close(sock_fd);
    return 0;
}

//--- Client Thread ---//
void *client_thread(void *arg) {
    SSL *ssl = (SSL*)arg;
    user.status = false;
    if (handle_no_logged_ssl(ssl) != 1)
        printf("Error in handle_no_logged_ssl\n");
    stop_flag = true;
    SSL_shutdown(ssl);
    shutdown(user.receiver_fd, SHUT_RDWR);
    close(user.receiver_fd);
    // 不要關閉 SSL，因為可能在其他地方使用
    pthread_cond_broadcast(&is_logged_in);
    printf("client thread leave\n");
    return NULL;
}

// No Logged In via SSL
int handle_no_logged_ssl(SSL *ssl) {
    while (true) {
        show_main_menu();
        int choice;
        printf("Enter your choice: ");
        scanf("%d", &choice);

        if (choice == 1) {
            send_register_ssl(ssl);
        } else if (choice == 2) {
            if (send_login_ssl(ssl) == 1)
                handle_logged_ssl(ssl);
        } else if (choice == 3) {
            if (SSL_write(ssl, EXIT, strlen(EXIT)) <= 0) {
                printf("Error in SSL_write\n");
                break;
            }
            printf("Exiting...\n");
            break;
        } else {
            printf("Error! Please enter a number between 1~3\n");
        }
    }
    return 1;
}

// Register via SSL
int send_register_ssl(SSL *ssl) {
    char name[MAX_NAME];
    printf("Enter your username (max %d characters): ", MAX_NAME-1);
    scanf("%s", name);

    char buf[BUFFER_SIZE];
    sprintf(buf, "%s%s", REGISTER, name);
    if (SSL_write(ssl, buf, strlen(buf)) <= 0) {
        printf("Error in SSL_write\n");
        return 0;
    }

    memset(buf, 0, sizeof(buf));
    int bytes = SSL_read(ssl, buf, sizeof(buf) - 1);
    if (bytes <= 0) {
        printf("Error in SSL_read\n");
        return 0;
    }
    buf[bytes] = '\0';

    if (strcmp(buf, REGISTER_SUCCESS) == 0) {
        printf(GREEN"Register Success!\n"NONE);
        return 1;
    }
    else {
        printf("Server response: "RED"%s\n"NONE, buf);
        return 0;
    }
}

// Login via SSL
int send_login_ssl(SSL *ssl) {
    char name[MAX_NAME];
    printf("Enter your username: ");
    scanf("%s", name);

    char buf[BUFFER_SIZE];
    sprintf(buf, "%s%s", LOGIN, name);
    if (SSL_write(ssl, buf, strlen(buf)) <= 0) {
        printf("Error in SSL_write\n");
        return 0;
    }

    // Relay Socket
    memset(buf, 0, sizeof(buf));
    int bytes = SSL_read(ssl, buf, BUFFER_SIZE - 1);
    if (bytes <= 0) {
        printf("Error in SSL_read\n");
        return 0;
    }
    buf[bytes] = '\0';
    if (strcmp(buf, RELAY_SOCKET) != 0) {
        printf("Server response: User has "RED"%s\n"NONE, buf);
        return 0;
    }

    // 建立 Relay Socket
    struct sockaddr_in relayaddr;
    int relay_fd;
    if (connect_to_port(&relay_fd, &relayaddr, SERVER_IP, SIDE_PORT) != 1) {
        printf("Error in connecting to Relay Socket\n");
        return 0;
    }

    SSL *relay_ssl = SSL_new(ctx);
    SSL_set_fd(relay_ssl, relay_fd);
    if (SSL_connect(relay_ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(relay_ssl);
        close(relay_fd);
        return 0;
    }
    user.relay_ssl = relay_ssl;

    // File Socket
    memset(buf, 0, sizeof(buf));
    bytes = SSL_read(ssl, buf, BUFFER_SIZE - 1);
    if (bytes <= 0) {
        printf("Error in SSL_read\n");
        SSL_shutdown(relay_ssl);
        SSL_free(relay_ssl);
        close(relay_fd);
        return 0;
    }
    buf[bytes] = '\0';
    if (strcmp(buf, FILE_SOCKET) != 0) {
        printf("the code shouldn't went to here.\n");
        SSL_shutdown(relay_ssl);
        SSL_free(relay_ssl);
        close(relay_fd);
        return 0;
    }

    int file_fd;
    if (connect_to_port(&file_fd, &relayaddr, SERVER_IP, SIDE_PORT) != 1) {
        printf("Error in connecting to File Socket\n");
        SSL_shutdown(relay_ssl);
        SSL_free(relay_ssl);
        close(relay_fd);
        return 0;
    }

    SSL *file_ssl = SSL_new(ctx);
    SSL_set_fd(file_ssl, file_fd);
    if (SSL_connect(file_ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(file_ssl);
        close(file_fd);
        SSL_shutdown(relay_ssl);
        SSL_free(relay_ssl);
        close(relay_fd);
        return 0;
    }
    user.file_ssl = file_ssl;

    // 傳 receiver port
    memset(buf, 0, sizeof(buf));
    bytes = SSL_read(ssl, buf, BUFFER_SIZE - 1);
    if (strcmp(buf, ASK_RCVR_PORT) != 0) {
        printf("the code shouldn't went to here.\n");
    }

    memset(buf, 0, sizeof(buf));
    sprintf(buf, "%d", user.receiver_port);
    SSL_write(ssl, buf, strlen(buf));

    memset(buf, 0, sizeof(buf));
    bytes = SSL_read(ssl, buf, BUFFER_SIZE - 1);

    if (strcmp(buf, LOGIN_SUCCESS) != 0) {
        printf("the code shouldn't went to here.\n");
    }

    strcpy(user.name, name);
    user.status = true;
    pthread_cond_broadcast(&is_logged_in);
    printf(GREEN"Login Success!\n"NONE);
    return 1;
}

// Logged In via SSL
int handle_logged_ssl(SSL *ssl) {
    char buf[BUFFER_SIZE];
    while (true) {
        printf("\n%s\n", LINE);
        printf("Command list:\n");
        printf("1) show list\n");
        printf("2) broadcast\n");
        printf("3) chat\n");
        printf("4) file transfer\n");
        printf("5) stream video\n");
        printf("6) logout\n");
        printf("%s\n", LINE);

        int choice;
        printf("Enter your choice: ");
        scanf("%d", &choice);

        if (choice == 1) {
            show_online_ssl(ssl);
        } else if (choice == 2) {
            send_relay_ssl(ssl);
        } else if (choice == 3) {
            send_direct_ssl(ssl);
        } else if (choice == 4) {
            send_file_ssl(ssl);
        } else if (choice == 5) {
            recv_streaming_ssl(ssl);
        } else if (choice == 6) {
            if (SSL_write(ssl, LOGOUT, strlen(LOGOUT)) <= 0)
                break;
            printf(GREEN"Logged out successfully.\n"NONE);
            break;
        } else {
            printf(RED"Unknown choice. Please try again.\n"NONE);
        }
    }
    return 1;
}

// Show Online Users via SSL
int show_online_ssl(SSL *ssl) {
    char buf[BUFFER_SIZE];
    if (SSL_write(ssl, SHOW_LIST, strlen(SHOW_LIST)) <= 0) {
        printf("Error in SSL_write\n");
        return 0;
    }
    memset(buf, 0, sizeof(buf));
    int bytes = SSL_read(ssl, buf, BUFFER_SIZE - 1);
    if (bytes <= 0) {
        printf("Error in SSL_read\n");
        return 0;
    }
    buf[bytes] = '\0';
    printf("Online users:\n%s\n", buf);
    return 1;
}

// Relay Send Message via SSL
int send_relay_ssl(SSL *ssl) {
    int target_id;
    char buf[BUFFER_SIZE];
    printf("Who you want to send to?\n");
    printf("Please enter his/her ID: ");
    scanf("%d", &target_id);
    sprintf(buf, "%s%d", RELAY_MES, target_id);
    if (SSL_write(ssl, buf, strlen(buf)) <= 0) {
        printf("Error in SSL_write\n");
        return 0;
    }

    memset(buf, 0, sizeof(buf));
    int bytes = SSL_read(ssl, buf, BUFFER_SIZE - 1);
    if (bytes <= 0) {
        printf("Error in SSL_read\n");
        return 0;
    }
    buf[bytes] = '\0';

    if (strcmp(buf, ASK_MES) != 0) {
        printf("Unexpected server response.\n");
        return 0;
    }

    char message[MAX_MES];
    printf("Enter your message: ");
    scanf(" %[^\n]", message);
    if (SSL_write(ssl, message, strlen(message)) <= 0) {
        printf("Error in SSL_write\n");
        return 0;
    }

    memset(buf, 0, sizeof(buf));
    bytes = SSL_read(ssl, buf, BUFFER_SIZE - 1);
    if (bytes <= 0) {
        printf("Error in SSL_read\n");
        return 0;
    }
    buf[bytes] = '\0';
    if (strcmp(buf, MES_SUCCESS) != 0) {
        printf("Server response: "RED"%s\n"NONE, buf);
        return 0;
    }

    printf(GREEN"Relay Send Message Success!\n"NONE);
    return 1;
}

// Direct Send Message via SSL
int send_direct_ssl(SSL *ssl) {
    int target_id;
    char buf[BUFFER_SIZE];
    printf("Who you want to send to?\n");
    printf("Please enter his/her ID: ");
    scanf("%d", &target_id);
    sprintf(buf, "%s%d", DIRECT_MES, target_id);
    if (SSL_write(ssl, buf, strlen(buf)) <= 0) {
        printf("Error in SSL_write\n");
        return 0;
    }

    memset(buf, 0, sizeof(buf));
    int bytes = SSL_read(ssl, buf, BUFFER_SIZE - 1);
    if (bytes <= 0) {
        printf("Error in SSL_read\n");
        return 0;
    }
    buf[bytes] = '\0';

    if (strcmp(buf, OFFLINE) == 0) {
        printf(RED"User offline or doesn't exist. Can't send message.\n"NONE);
        return 0;
    }

    char ip[INET_ADDRSTRLEN];
    int port;
    sscanf(buf, "%s %d", ip, &port);
    printf("Target IP: %s\nTarget Port: %d\n", ip, port);

    char message[MAX_MES];
    printf("Enter your message: ");
    scanf(" %[^\n]", message);

    int tmp_fd;
    struct sockaddr_in targetaddr;
    if (connect_to_port(&tmp_fd, &targetaddr, ip, port) != 1) {
        printf(RED"Error in connecting to Receiver\n"NONE);
        return 0;
    }

    // 傳訊息
    char to_receiver[BUFFER_SIZE];
    format_buffer(to_receiver, IS_MES, user.name, "", message);
    send(tmp_fd, to_receiver, BUFFER_SIZE, 0);

    close(tmp_fd);
    return 1;
}

// Send File via SSL
int send_file_ssl(SSL *ssl) {
    int target_id;
    char buf[BUFFER_SIZE];
    printf("Who you want to send to?\n");
    printf("Please enter his/her ID: ");
    scanf("%d", &target_id);
    sprintf(buf, "%s%d", FILE_TRANSFER, target_id);
    if (SSL_write(ssl, buf, strlen(buf)) <= 0) {
        printf("Error in SSL_write\n");
        return 0;
    }

    memset(buf, 0, sizeof(buf));
    int bytes = SSL_read(ssl, buf, BUFFER_SIZE - 1);
    if (bytes <= 0) {
        printf("Error in SSL_read\n");
        return 0;
    }
    buf[bytes] = '\0';

    if (strcmp(buf, ASK_FILE_NAME) != 0) {
        printf("Unexpected server response.\n");
        return 0;
    }

    char filename[MAX_MES];
    printf("Enter your filename: ");
    scanf("%s", filename);
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        printf(RED"Error! "NONE"Can't open file %s\n", filename);
        return 0;
    }
    if (SSL_write(ssl, filename, strlen(filename)) <= 0) {
        printf("Error in SSL_write\n");
        fclose(fp);
        return 0;
    }

    memset(buf, 0, sizeof(buf));
    bytes = SSL_read(ssl, buf, BUFFER_SIZE - 1);
    if (bytes <= 0) {
        printf("Error in SSL_read\n");
        fclose(fp);
        return 0;
    }
    buf[bytes] = '\0';

    if (strcmp(buf, OFFLINE) == 0) {
        printf(RED"User offline or doesn't exist. Can't send file\n"NONE);
        fclose(fp);
        return 0;
    }

    if (strcmp(buf, FILE_FAIL) == 0) {
        printf(RED"Error in asking target\n"NONE);
        fclose(fp);
        return 0;
    }

    if (strcmp(buf, REJECT_FILE) == 0) {
        printf(RED"User rejected file\n"NONE);
        fclose(fp);
        return 0;
    }

    // 開始傳送檔案
    char content[BUFFER_SIZE];
    memset(content, 0, BUFFER_SIZE);
    while ((bytes = fread(content, 1, BUFFER_SIZE, fp)) > 0) {
        if (SSL_write(ssl, content, bytes) <= 0) {
            printf(RED"Error in sending file\n"NONE);
            break;
        }
        memset(buf, 0, sizeof(buf));
        bytes = SSL_read(ssl, buf, BUFFER_SIZE - 1);
        if (bytes <= 0) {
            printf(RED"Error in SSL_read\n"NONE);
            break;
        }
        buf[bytes] = '\0';
        if (strcmp(buf, ACK_FILE) != 0) {
            printf(RED"Error in receiving ACK\n"NONE);
            break;
        }
        memset(content, 0, BUFFER_SIZE);
    }
    // 傳送結束訊息
    SSL_write(ssl, END_OF_FILE, strlen(END_OF_FILE));
    fclose(fp);
    return 1;
}

// Relay Thread
void *relay_thread(void *arg) {
    while (!stop_flag) {
        pthread_mutex_lock(&relay_lock);
        while (!stop_flag && user.status == false)
            pthread_cond_wait(&is_logged_in, &relay_lock);
        pthread_mutex_unlock(&relay_lock);

        while (user.status) {
            char buf[BUFFER_SIZE], signal[SIGNAL_SIZE], from[MAX_NAME], to[MAX_NAME], mes[MAX_MES];
            int bytes = SSL_read(user.relay_ssl, buf, BUFFER_SIZE - 1);
            if (bytes <= 0)
                break;
            buf[bytes] = '\0';
            slice_buffer(buf, signal, from, to, mes);
            if (strcmp(signal, IS_MES) != 0)
                printf("Unexpected signal in relay_thread\n");
            printf("<%s>: %s\n", from, mes);
            printf(GREEN"Sent by Relay Message\n"NONE);
        }
    }
    printf("Relay thread leave\n");
    return NULL;
}

void *direct_thread(void *arg) {
    while (!stop_flag) {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        int conn_fd = accept(user.receiver_fd, (struct sockaddr*)&cliaddr, &clilen);
        if (conn_fd < 0) {
            perror("accept in direct_thread");
            continue;
        }

        printf("Accepted direct connection from %s:%d\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));

        // 讀取數據
        char buf[BUFFER_SIZE], signal[SIGNAL_SIZE], from[MAX_NAME], to[MAX_NAME], mes[MAX_MES];
        int bytes = recv(conn_fd, buf, BUFFER_SIZE, 0);
        if (bytes <= 0) {
            printf("recv failed or connection closed\n");
            close(conn_fd);
            continue;
        }
        buf[bytes] = '\0';
        slice_buffer(buf, signal, from, to, mes);
        if (strcmp(signal, IS_MES) != 0) {
            printf("Unexpected signal in direct_thread: %s\n", signal);
        }
        printf("<%s>: %s\n", from, mes);
        printf(GREEN"Sent by Direct Message\n"NONE);

        close(conn_fd);
    }

    printf("Direct thread leave\n");
    return NULL;
}

// File Thread
void file_thread() {
    while (!stop_flag) {
        pthread_mutex_lock(&file_lock);
        while (!stop_flag && user.status == false)
            pthread_cond_wait(&is_logged_in, &file_lock);
        pthread_mutex_unlock(&file_lock);

        while (user.status) {
            char buf[BUFFER_SIZE], signal[SIGNAL_SIZE], from[MAX_NAME], to[MAX_NAME], mes[MAX_MES];
            int bytes = SSL_read(user.file_ssl, buf, BUFFER_SIZE - 1);
            if (bytes <= 0)
                break;
            buf[bytes] = '\0';
            slice_buffer(buf, signal, from, to, mes);
            if (strcmp(signal, IS_FILE) != 0)
                printf("Unexpected signal in file_thread\n");

            int r = file_questioner(from, mes);
            if (r == 1) {
                if (SSL_write(user.file_ssl, ACCEPT_FILE, strlen(ACCEPT_FILE)) <= 0) {
                    printf("Error in SSL_write\n");
                    continue;
                }
                FILE *fp = fopen(mes, "w");
                if (fp == NULL) {
                    printf("Error opening file for writing.\n");
                    continue;
                }
                while (true) {
                    memset(buf, 0, sizeof(buf));
                    bytes = SSL_read(user.file_ssl, buf, BUFFER_SIZE - 1);
                    if (bytes <= 0) {
                        printf("Error in SSL_read\n");
                        break;
                    }
                    buf[bytes] = '\0';
                    if (strcmp(buf, END_OF_FILE) == 0)
                        break;
                    fwrite(buf, 1, bytes, fp);
                    if (SSL_write(user.file_ssl, ACK_FILE, strlen(ACK_FILE)) <= 0) {
                        printf("Error in SSL_write\n");
                        break;
                    }
                }
                fclose(fp);
            } else {
                if (SSL_write(user.file_ssl, REJECT_FILE, strlen(REJECT_FILE)) <= 0) {
                    printf("Error in SSL_write\n");
                }
            }
        }
    }
    printf("File thread leave\n");
}

// 接收視頻流
int recv_streaming_ssl(SSL *ssl) {
    char buf[BUFFER_SIZE];
    
    // 发送STREAM_CMD和文件名以请求服务器开始流媒体
    char filename[BUFFER_SIZE];
    printf("请输入要发送的文件名: ");
    scanf("%s", filename);
    filename[strcspn(filename, "\n")] = '\0';
    
    char stream_cmd[BUFFER_SIZE];
    snprintf(stream_cmd, sizeof(stream_cmd), "%s %s", STREAM_CMD, filename);
    if (SSL_write(ssl, stream_cmd, strlen(stream_cmd)) <= 0) {
        printf("发送STREAM_CMD失败\n");
        return -1;
    }
    
    // 建立到視頻流服務器的連接
    int stream_fd;
    struct sockaddr_in stream_addr;
    if (connect_to_port(&stream_fd, &stream_addr, SERVER_IP, STREAM_PORT) < 0) {
        printf("Failed to connect to streaming server\n");
        return -1;
    }

    printf("Connected to streaming server on port %d\n", STREAM_PORT);
    
    // 初始化 FFmpeg
    av_register_all();

    // 接收 SPS/PPS
    int sps_size;
    if (recv(stream_fd, &sps_size, sizeof(sps_size), 0) <= 0)
        error_exit("Failed to receive SPS size");

    uint8_t *sps = (uint8_t *)malloc(sps_size);
    if (recv(stream_fd, sps, sps_size, 0) <= 0)
        error_exit("Failed to receive SPS data");

    printf("Received SPS/PPS of size %d bytes.\n", sps_size);

    // 初始化解碼器
    AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec)
        error_exit("Codec not found");

    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx)
        error_exit("Could not allocate codec context");

    codec_ctx->extradata = sps;
    codec_ctx->extradata_size = sps_size;

    printf("SPS/PPS size: %d\n", codec_ctx->extradata_size);
    if (codec_ctx->extradata_size <= 0) {
        fprintf(stderr, "Invalid SPS/PPS data.\n");
        return -1;
    }

    for (int i = 0; i < codec_ctx->extradata_size; i++) {
        printf("%02X ", codec_ctx->extradata[i]);
    }
    printf("\n");

    if (avcodec_open2(codec_ctx, codec, NULL) < 0)
        error_exit("Could not open codec");

    // 檢查是否需要解碼幀來初始化
    if (codec_ctx->width == 0 || codec_ctx->height == 0) {
        printf("Decoder needs more data to initialize. Decoding first frame...\n");

        // 接收幀大小
        int frame_size;
        if (recv(stream_fd, &frame_size, sizeof(frame_size), 0) <= 0) {
            perror("Failed to receive frame size");
        }

        // 接收幀數據
        uint8_t *frame_data = (uint8_t *)malloc(frame_size);
        int bytes_recv = recv_full(stream_fd, frame_data, frame_size);

        printf("Received frame data: size=%d, bytes=%d\n", frame_size, bytes_recv);

        AVPacket packet;
        av_init_packet(&packet);
        packet.data = frame_data;
        packet.size = frame_size;

        if (avcodec_send_packet(codec_ctx, &packet) == 0) {
            AVFrame *frame = av_frame_alloc();
            if (avcodec_receive_frame(codec_ctx, frame) == 0) {
                printf("After decoding: width=%d, height=%d, pix_fmt=%d\n",
                    codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt);
            }
            av_frame_free(&frame);
        }

        free(frame_data);
    }

    printf("Decoder initialized: width=%d, height=%d, pix_fmt=%d\n",
       codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt);

    // 初始化 SDL2
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Window *window = SDL_CreateWindow("Video Stream",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          1280, 720,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Texture *texture = NULL;

    // 接收視頻幀並解碼
    AVPacket packet;
    AVFrame *frame = av_frame_alloc();

    struct SwsContext *sws_ctx = sws_getContext(codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
                            codec_ctx->width, codec_ctx->height, AV_PIX_FMT_YUV420P,
                            SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_ctx) {
        fprintf(stderr, "Failed to initialize sws_getContext.\n");
        return -1;
    }

    if (!frame || !sws_ctx)
        error_exit("Could not initialize FFmpeg structures");

    uint8_t *y_plane = (uint8_t *)malloc(codec_ctx->width * codec_ctx->height);
    uint8_t *u_plane = (uint8_t *)malloc(codec_ctx->width * codec_ctx->height / 4);
    uint8_t *v_plane = (uint8_t *)malloc(codec_ctx->width * codec_ctx->height / 4);
    int y_pitch = codec_ctx->width;
    int uv_pitch = codec_ctx->width / 2;

    uint8_t *data[3] = {y_plane, u_plane, v_plane};
    int linesize[3] = {y_pitch, uv_pitch, uv_pitch};

    int frame_initialized = 0;
    int frame_count = 0;

    int consecutive_errors = 0;
    while (1) {
        // 接收幀大小
        int frame_size = 0;
        if (recv_full(stream_fd, &frame_size, sizeof(frame_size)) <= 0) {
            perror("Failed to receive frame size!");
            break;  
        }

        uint8_t *frame_data = (uint8_t *)malloc(frame_size);
        int bytes_recv = recv_full(stream_fd, frame_data, frame_size);
        if (bytes_recv <= 0) {
            free(frame_data);
            break;
        }

        printf("Received frame data: size=%d, bytes=%d\n", frame_size, bytes_recv);
        assert(bytes_recv == frame_size);
        consecutive_errors = 0;  // 重置錯誤計數

        // 解碼
        av_init_packet(&packet);
        packet.data = frame_data;
        packet.size = frame_size;

        if (avcodec_send_packet(codec_ctx, &packet) < 0) {
            fprintf(stderr, "Error sending packet to decoder.\n");
            free(frame_data);
            continue;
        }

        if (avcodec_receive_frame(codec_ctx, frame) == 0) {
            // 延遲初始化 Texture
            if (!frame_initialized) {
                printf("Initializing video context: width=%d, height=%d, pix_fmt=%d\n",
                    codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt);

                texture = SDL_CreateTexture(renderer,
                                            SDL_PIXELFORMAT_YV12,
                                            SDL_TEXTUREACCESS_STREAMING,
                                            codec_ctx->width,
                                            codec_ctx->height);
                if (!texture) {
                    fprintf(stderr, "SDL_CreateTexture Error: %s\n", SDL_GetError());
                    return -1;
                }

                frame_initialized = 1;
            }

            // 將解碼幀轉換為 YUV420P 格式
            uint8_t *data[3] = {y_plane, u_plane, v_plane};
            int linesize[3] = {y_pitch, uv_pitch, uv_pitch};

            sws_scale(sws_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0,
                    codec_ctx->height, data, linesize);

            // 顯示幀
            SDL_UpdateYUVTexture(texture, NULL, y_plane, y_pitch,
                                u_plane, uv_pitch, v_plane, uv_pitch);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
        }

        free(frame_data);
        frame_count++;
        av_packet_unref(&packet);

        printf("Frame count: %d\n", frame_count);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                goto cleanup;
            }
        }
    }

cleanup:
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (frame) av_frame_free(&frame);
    if (sws_ctx) sws_freeContext(sws_ctx);
    free(y_plane);
    free(u_plane);
    free(v_plane);
    avcodec_free_context(&codec_ctx);
    SDL_Quit();
    close(stream_fd);
    printf("Video playback ended\n");
    return 0;
}


