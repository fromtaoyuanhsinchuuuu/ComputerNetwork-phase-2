#ifndef CONFIG_H
#define CONFIG_H

// 共用定義
#define BUFFER_SIZE 512
#define PORT 8000
#define MAXNAME 15
#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)
#define LINE "================================"

// Server 專用定義
#ifdef SERVER
#define MAX_USERS 3
#define THREAD_POOL_SIZE 10
#define QUEUE_SIZE 20
#endif

// Client 專用定義
#ifdef CLIENT
#define SERVER_IP "127.0.0.1"
#endif

#endif // CONFIG_H

