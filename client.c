#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <gtk/gtk.h>
#include "config.h"

int c_receiver_port;
int receiver_fd;
char rcvr_buf[BUFFER_SIZE];
bool stop_flag = false;
static int accept_file = 0;

int conn_fd;
char buf[BUFFER_SIZE];

void c_receiver();
int file_questioner(char *from, char *filename);
void *main_thread(void *arg);

void show_main_menu();
void show_logged_in_menu();
void send_register();
void send_login();
void handle_logged_in_interface(const char *username);

int main() {
    printf("Enter receiver port: ");
    scanf("%d", &c_receiver_port);

    // 建立Receiver socket
    receiver_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (receiver_fd < 0)
        ERR_EXIT("socket");

    struct sockaddr_in rcvraddr;
    memset(&rcvraddr, 0, sizeof(rcvraddr));
    rcvraddr.sin_family = AF_INET;
    rcvraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    rcvraddr.sin_port = htons(c_receiver_port);

    if (bind(receiver_fd, (struct sockaddr*)&rcvraddr, sizeof(rcvraddr)) < 0)
        ERR_EXIT("receiver bind");
    if (listen(receiver_fd, 1) < 0)
        ERR_EXIT("receiver listen");

    // 連線至server
    conn_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn_fd < 0) ERR_EXIT("socket");

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &servaddr.sin_addr) <= 0) ERR_EXIT("Invalid IP");

    if (connect(conn_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
        ERR_EXIT("connect");

    char server_response[BUFFER_SIZE];
    recv(conn_fd, server_response, sizeof(server_response), 0);
    if (strcmp(server_response, QUEUE_FULL) == 0) {
        printf("Server response: queue full\n");
        stop_flag = true;
    } else if (strcmp(server_response, ACCEPT_TASK) == 0) {
        printf("Server response: accept task\n");
        pthread_t main_thd;
        pthread_create(&main_thd, NULL, main_thread, NULL);
    }

    c_receiver();
    return 0;
}

void *main_thread(void *arg) {
    while (!stop_flag) {
        show_main_menu();
        int choice;
        printf("Enter your choice: ");
        scanf("%d", &choice);

        switch (choice) {
            case 1:
                send_register();
                break;
            case 2:
                send_login();
                break;
            case 3:
                send(conn_fd, EXIT, BUFFER_SIZE, 0);
                printf("Exiting...\n");
                stop_flag = true;
                break;
            default:
                printf("Unknown choice. Please try again.\n");
        }
    }

    close(conn_fd);
    close(receiver_fd);
    return NULL;
}

void show_main_menu() {
    printf("\nPlease choose one of the following options:\n");
    printf("1. Register\n");
    printf("2. Login\n");
    printf("3. Exit\n");
}

void show_logged_in_menu() {
    printf("\nChoose an action:\n");
    printf("1. Show online users\n");
    printf("2. Relay send message\n");
    printf("3. Direct send message\n");
    printf("4. Send file\n");
    printf("5. Log out\n");
}

void send_register() {
    char name[MAX_NAME];
    printf("Enter your username (max %d characters): ", MAX_NAME-1);
    scanf("%s", name);

    sprintf(buf, "%s%s", REGISTER, name);
    send(conn_fd, buf, BUFFER_SIZE, 0);

    memset(buf, 0, sizeof(buf));
    recv(conn_fd, buf, sizeof(buf), 0);
    printf("Server response: %s\n", buf);
    return;
}

void send_login() {
    char name[MAX_NAME];
    printf("Enter your username: ");
    scanf("%s", name);

    sprintf(buf, "%s%s", LOGIN, name);
    send(conn_fd, buf, BUFFER_SIZE, 0);

    memset(buf, 0, sizeof(buf));
    recv(conn_fd, buf, BUFFER_SIZE, 0);

    if (strcmp(buf, ASK_RCVR_PORT) == 0) {
// memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "%d", c_receiver_port);
        send(conn_fd, buf, BUFFER_SIZE, 0);

        memset(buf, 0, sizeof(buf));
        recv(conn_fd, buf, sizeof(buf), 0);
        if (strcmp(buf, LOGIN_SUCCESS) == 0) {
            printf("Login successful!\n");
            handle_logged_in_interface(name);
        } else {
            printf("Login failed: %s\n", buf);
        }
    } else {
        printf("Login failed: %s\n", buf);
    }
}

void handle_logged_in_interface(const char *username) {
    while (1) {
        show_logged_in_menu();
        int choice;
        printf("Enter your choice: ");
        scanf("%d", &choice);

        if (choice == 1) {
            send(conn_fd, SHOW_LIST, strlen(SHOW_LIST), 0);
            memset(buf, 0, sizeof(buf));
            recv(conn_fd, buf, BUFFER_SIZE, 0);
            printf("Online users:\n%s\n", buf);
        } else if (choice == 2) {
            int target_id;
            printf("Enter who you want to send to: ");
            scanf("%d", &target_id);
            sprintf(buf, "%s%d", SEND_MES, target_id);
            send(conn_fd, buf, BUFFER_SIZE, 0);

            memset(buf, 0, sizeof(buf));
            recv(conn_fd, buf, BUFFER_SIZE, 0);
            
            if (strcmp(buf, ASK_MES) == 0) {
                char message[MAX_MES];
                printf("Enter your message: ");
                scanf(" %[^\n]", message);
                send(conn_fd, message, BUFFER_SIZE, 0);
            } else {
                printf("Error in relay mes\n");
            }

            memset(buf, 0, sizeof(buf));
            recv(conn_fd, buf, BUFFER_SIZE, 0);

            printf("Relay Send Result: %s\n", buf);
        } else if (choice == 3) {
            int target_id;
            printf("Enter who you want to send to: ");
            scanf("%d", &target_id);
            sprintf(buf, "%s%d", ASK_USER_INFO, target_id);
            send(conn_fd, buf, BUFFER_SIZE, 0);

            memset(buf, 0, sizeof(buf));
            recv(conn_fd, buf, BUFFER_SIZE, 0);
            printf("%s\n", buf);

            if (strcmp(buf, OFFLINE) == 0) {
                printf("User Offline. Can't send message\n");
            } else {
                char info[BUFFER_SIZE];
                memset(info, 0, sizeof(info));
                strcpy(info, buf);
                char ip[INET_ADDRSTRLEN];
                int port;
                char *p;
                printf("aaaaaaaa\n");
                p = strtok(info, " ");
                strcpy(ip, p);
                p = strtok(NULL, " ");
                port = atoi(p);

                printf("Target ip: %s\nTarget port: %d\n", ip, port);

                char message[MAX_MES];
                printf("Enter your message: ");
                scanf(" %[^\n]", message);

                int tmp_fd = socket(AF_INET, SOCK_STREAM, 0);
                if (tmp_fd < 0) ERR_EXIT("socket");
                struct sockaddr_in targetaddr;
                memset(&targetaddr, 0, sizeof(targetaddr));
                targetaddr.sin_family = AF_INET;
                targetaddr.sin_port = htons(port);
                if (inet_pton(AF_INET, ip, &targetaddr.sin_addr) <= 0)
                    ERR_EXIT("Invalid IP");
                if (connect(tmp_fd, (struct sockaddr*)&targetaddr, sizeof(targetaddr)) < 0)
                    printf("Connection Fail\n");
                else {
                    // 傳訊息
                    char to_receiver[BUFFER_SIZE];
                    format_buffer(to_receiver, IS_MES, username, NULL, message);
                    if (send(tmp_fd, to_receiver, BUFFER_SIZE, 0) < 0) {
                        printf("Error in Direct send message\n");
                    }
                }
                close(tmp_fd);
            }
        } else if (choice == 4) {
            int target_id;
            printf("Enter who you want to send to: ");
            scanf("%d", &target_id);
            sprintf(buf, "%s%d", ASK_USER_INFO, target_id);
            send(conn_fd, buf, BUFFER_SIZE, 0);

            memset(buf, 0, sizeof(buf));
            recv(conn_fd, buf, BUFFER_SIZE, 0);

            if (strcmp(buf, OFFLINE) == 0) {
                printf("User Offline. Can't send file\n");
            } else {
                char info[BUFFER_SIZE];
                memset(info, 0, sizeof(info));
                strcpy(info, buf);
                char ip[INET_ADDRSTRLEN];
                int port;
                char *p;
                p = strtok(info, " ");
                strcpy(ip, p);
                p = strtok(NULL, " ");
                port = atoi(p);

                printf("Target ip: %s\nTarget port: %d\n", ip, port);

                char filename[MAX_MES];
                printf("Enter your file: ");
                scanf("%s", filename);

                FILE *fp = fopen(filename, "r");
                if (fp == NULL) {
                    printf("Can't open file %s\n", filename);
                }
                else {
                    int tmp_fd = socket(AF_INET, SOCK_STREAM, 0);
                    if (tmp_fd < 0) ERR_EXIT("socket");
                    struct sockaddr_in targetaddr;
                    memset(&targetaddr, 0, sizeof(targetaddr));
                    targetaddr.sin_family = AF_INET;
                    targetaddr.sin_port = htons(port);
                    if (inet_pton(AF_INET, ip, &targetaddr.sin_addr) <= 0)
                        ERR_EXIT("Invalid IP");
                    if (connect(tmp_fd, (struct sockaddr*)&targetaddr, sizeof(targetaddr)) < 0)
                        printf("Connection Fail\n");
                    else {
                        // 傳檔案
                        char to_receiver[BUFFER_SIZE];
                        format_buffer(to_receiver, IS_FILE, username, NULL, filename);
                        if (send(tmp_fd, to_receiver, BUFFER_SIZE, 0) < 0) {
                            printf("Error in Direct send file\n");
                        } else {
                            memset(buf, 0, sizeof(buf));
                            recv(tmp_fd, buf, BUFFER_SIZE, 0);
                            if (strcmp(buf, REJECT_FILE) == 0) {
                                printf("File rejected\n");
                            }
                            else if (strcmp(buf, ACCEPT_FILE) == 0) {
                                char content[BUFFER_SIZE];
                                memset(content, 0, BUFFER_SIZE);
                                size_t bytesRead;
                                while ((bytesRead = fread(content, 1, BUFFER_SIZE, fp)) > 0) {
                                    send(tmp_fd, content, bytesRead, 0);
                                    printf("read size %d\n", (int)bytesRead);
                                    printf("%s\n", content);

                                    memset(buf, 0, sizeof(buf));
                                    recv(tmp_fd, buf, BUFFER_SIZE, 0);

                                    if (strcmp(buf, ACK_FILE) != 0) {
                                        printf("Error in sending file\n");
                                        break;
                                    }
                                    memset(content, 0, BUFFER_SIZE);
                                }
                                send(tmp_fd, END_OF_FILE, BUFFER_SIZE, 0);
                            }
                        }
                    }
                    close(tmp_fd);
                }
                fclose(fp);
            }
        } else if (choice == 5) {
            send(conn_fd, LOGOUT, strlen(LOGOUT), 0);
            printf("Logged out successfully.\n");
            break;
        } else {
            printf("Unknown choice. Please try again.\n");
        }
    }
}

void c_receiver () {
    while (!stop_flag) {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);

        int tmp_fd = accept(receiver_fd, (struct sockaddr*)&cliaddr, &clilen);
        if (tmp_fd < 0) ERR_EXIT("accept");

        if (recv(tmp_fd, rcvr_buf, BUFFER_SIZE, 0) < 0)
            break;

        char signal[SIGNAL_SIZE], from[MAX_NAME], to[MAX_NAME], mes[MAX_MES];
        slice_buffer(rcvr_buf, signal, from, to, mes);
        
        if (strcmp(signal, IS_MES) == 0) {
            printf("<%s> %s\n", from, mes);
        } else if (strcmp(signal, IS_FILE) == 0) {
            int r = file_questioner(from, mes);
            printf("r: %d\n", r);
            if (r == 1) {
                send(tmp_fd, ACCEPT_FILE, BUFFER_SIZE, 0);
                FILE *fp = fopen(mes, "w");
                int bytesReceived;
                while (true) {
                    memset(rcvr_buf, 0, BUFFER_SIZE);
                    bytesReceived = recv(tmp_fd, rcvr_buf, BUFFER_SIZE, 0);
                    if (strcmp(rcvr_buf, END_OF_FILE) == 0)
                        break;
                    printf("receive: %s\n", rcvr_buf);
                    if (bytesReceived != fwrite(rcvr_buf, 1, bytesReceived, fp))
                        printf("Wrong\n");
                    send(tmp_fd, ACK_FILE, BUFFER_SIZE, 0);
                }
                fclose(fp);
            } else {
                send(tmp_fd, REJECT_FILE, BUFFER_SIZE, 0);
            }
        }
        close(tmp_fd);
    }
    return NULL;
}

// delete_event 信號處理函數
gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    printf("delete_event triggered\n");
    gtk_main_quit(); // 退出主事件迴圈
    return FALSE;    // 返回 FALSE，允許進一步處理（會觸發 destroy 信號）
}

// destroy 信號處理函數
void on_destroy(GtkWidget *widget, gpointer data) {
    printf("destroy triggered\n");
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
