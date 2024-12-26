#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

#define PORT 8784
#define BUFFER_SIZE 4096

void error_exit(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

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

int main() {
    const char *video_file = "test.mp4";
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // 初始化 FFmpeg
    avcodec_register_all();

    // 打開視頻文件
    AVFormatContext *format_ctx = NULL;
    if (avformat_open_input(&format_ctx, video_file, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open video file: %s\n", video_file);
        return -1;
    }

    // 查找流信息
    if (avformat_find_stream_info(format_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information.\n");
        avformat_close_input(&format_ctx);
        return -1;
    }

    // 查找視頻流
    int video_stream_index = -1;
    for (int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    if (video_stream_index == -1) {
        fprintf(stderr, "Could not find a video stream.\n");
        avformat_close_input(&format_ctx);
        return -1;
    }

    AVCodecParameters *codec_params = format_ctx->streams[video_stream_index]->codecpar;

    // 創建 Socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        error_exit("Socket creation failed");

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        error_exit("Bind failed");

    if (listen(server_fd, 1) == -1)
        error_exit("Listen failed");

    printf("Waiting for client to connect...\n");

    if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len)) == -1)
        error_exit("Accept failed");

    printf("Client connected. Waiting for confirmation...\n");

    // 等待確認
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    if (recv(client_fd, buffer, BUFFER_SIZE, 0) <= 0) {
        fprintf(stderr, "Failed to receive confirmation.\n");
        close(client_fd);
        close(server_fd);
        avformat_close_input(&format_ctx);
        return -1;
    }

    if (buffer[0] != 'Y') {
        printf("Client did not confirm, closing connection.\n");
        close(client_fd);
        close(server_fd);
        avformat_close_input(&format_ctx);
        return 0;
    }

    printf("Confirmation received. Starting streaming...\n");


    printf("SPS/PPS size: %d\n", codec_params->extradata_size);
    if (codec_params->extradata_size <= 0) {
        fprintf(stderr, "Invalid SPS/PPS data.\n");
        return -1;
    }

    printf("extradata (size=%d): ", codec_params->extradata_size);
    for (int i = 0; i < codec_params->extradata_size; i++) {
        printf("%02X ", codec_params->extradata[i]);
    }
    printf("\n");

    printf("Width: %d, Height: %d, Pix Format: %d\n",
       codec_params->width, codec_params->height, codec_params->format);


    // 發送 SPS/PPS
    uint8_t *sps = codec_params->extradata;
    int sps_size = codec_params->extradata_size;

    if (send(client_fd, &sps_size, sizeof(sps_size), 0) == -1)
        error_exit("Failed to send SPS size");
    if (send(client_fd, sps, sps_size, 0) == -1)
        error_exit("Failed to send SPS data");

    printf("Sent SPS/PPS data of size %d bytes.\n", sps_size);

    // 傳輸視頻幀
    AVPacket packet;

    int frame_count = 0;
    while (av_read_frame(format_ctx, &packet) >= 0) {
        if (packet.stream_index == video_stream_index) {
            int frame_size = packet.size;
            // printf("frame size: %d\n", frame_size);
            int bytes_sent = 0;
            send(client_fd, &frame_size, sizeof(frame_size), 0);


            bytes_sent = send_full(client_fd, packet.data, packet.size);
    
            printf("Sent frame of size %d bytes.\n", frame_size);
            printf("bytes_sent: %d\n", bytes_sent);
            usleep(1000);  // 1ms delay
        }
        av_packet_unref(&packet);
        frame_count++;
    }
    printf("frame_count: %d\n", frame_count);

    printf("Streaming finished.\n");

    // 清理資源
    avformat_close_input(&format_ctx);
    close(client_fd);
    close(server_fd);

    return 0;
}
