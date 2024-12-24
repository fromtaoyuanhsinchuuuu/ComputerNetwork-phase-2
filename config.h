#ifndef CONFIG_H
#define CONFIG_H

// 共用定義
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8011
#define BUFFER_SIZE 1024
#define MAX_NAME 15
#define MAX_MES 512
#define MAX_ONLINE 10                  // 最多同時上限人數
#define MAX_USERS 20                   // 最多註冊人數
#define QUEUE_SIZE 20                  // 最多等待連線人數
#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)

// 訊息定義
#define LINE "================================"

#define QUEUE_FULL "queue full"
#define WAIT_WORKER "wait worker"

#define ACCEPT_TASK "accept task"

#define UNKNOW "unknow"

#define REGISTER "register: "
    #define USER_FULL "user_full"
    #define NAME_EXCEED "name_exceed"
    #define NAME_REGISTERED "name_registered"
    #define REGISTER_SUCCESS "register_success"
#define LOGIN "login: "
    #define ASK_RCVR_PORT "ask_rcvr_port"
    #define LOGGED_IN "logged_in"
    #define LOGIN_SUCCESS "login_success"
    #define NO_REGISTER "no_register"
#define EXIT "exit"


#define SHOW_LIST "show_list"
#define SEND_MES "send_mes"
    #define ASK_MES "ask_mes"
    #define IS_MES "is_mes "
    #define MES_SUCCESS "mes_success"
    #define MES_FAIL "mes_fail"
#define ASK_USER_INFO "ask_user_info"
    #define OFFLINE "offline"
#define LOGOUT "logout"
    #define LOGOUT_SUCCESS "logout_success"

#endif
