#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <assert.h>
#include <errno.h>
#include <sys/time.h>

#define PORT 8784
#define BUFFER_SIZE 4096
#define TIMEOUT 5
#define BUFFER_SIZE 4096

int consecutive_errors = 0;
const int MAX_CONSECUTIVE_ERRORS = 5;

typedef long long ll;

void error_exit(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

int recv_full(int sockfd, void *buf, size_t len) {
    size_t total_received = 0;
    uint8_t *data = (uint8_t *)buf;

    struct timeval tv;
    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt");
        return -1;
    }

    while (total_received < len) {
        ssize_t received = recv(sockfd, data + total_received, len - total_received, 0);
        if (received <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 暫時不可用，可以重試或等待
                printf("Timeout occurred. Retrying...\n");
                continue;
            } else {
                perror("Failed to receive data");
                return -1;
            }
        }
        total_received += received;
    }
    return total_received;
}

int main() {
    const char *server_ip = "127.0.0.1";
    int client_fd;
    struct sockaddr_in server_addr;

    // 初始化 Socket
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        error_exit("Socket creation failed");

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0)
        error_exit("Invalid address or address not supported");

    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        error_exit("Connection failed");

    printf("Connected to server. Enter 'Y' to start receiving video: ");
    
    // 等待用戶輸入確認訊息
    char confirm_msg;
    scanf(" %c", &confirm_msg);

    if (confirm_msg != 'Y') {
        printf("Invalid input. Closing connection.\n");
        close(client_fd);
        return 0;
    }

    // 發送確認訊息
    if (send(client_fd, &confirm_msg, sizeof(confirm_msg), 0) == -1)
        error_exit("Failed to send confirmation");

    printf("Confirmation sent. Waiting for streaming...\n");

    // 初始化 FFmpeg
    av_register_all();

    // 接收 SPS/PPS
    int sps_size;
    if (recv(client_fd, &sps_size, sizeof(sps_size), 0) <= 0)
        error_exit("Failed to receive SPS size");

    uint8_t *sps = (uint8_t *)malloc(sps_size);
    if (recv(client_fd, sps, sps_size, 0) <= 0)
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

    // codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;


    if (avcodec_open2(codec_ctx, codec, NULL) < 0)
        error_exit("Could not open codec");

    // 檢查是否需要解碼幀來初始化
    if (codec_ctx->width == 0 || codec_ctx->height == 0) {
        printf("Decoder needs more data to initialize. Decoding first frame...\n");


         // 接收幀大小
        int frame_size;
        if (recv(client_fd, &frame_size, sizeof(frame_size), 0) <= 0) {
            // 處理接收錯誤
            perror("Failed to receive frame size");
        }

        // 接收幀數據
        uint8_t *frame_data = (uint8_t *)malloc(frame_size);
        int bytes_recv = recv_full(client_fd, frame_data, frame_size);

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
    int frame_size = 0;
    AVPacket packet;
    AVFrame *frame = av_frame_alloc();

    // if (codec_ctx->width == 0 || codec_ctx->height == 0 || codec_ctx->pix_fmt == AV_PIX_FMT_NONE) {
    //     fprintf(stderr, "Invalid decoder parameters: width=%d, height=%d, pix_fmt=%d\n",
    //             codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt);
    //     return -1;
    // }

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
    while (1) {

         // 接收幀大小
        int frame_size = 0;
        if (recv_full(client_fd, &frame_size, sizeof(frame_size)) <= 0) {
            // 處理接收錯誤
            perror("Failed to receive frame size!");
            break;  
        }


        int *frame_data = (int *)malloc(frame_size);
        int bytes_recv = recv_full(client_fd, frame_data, frame_size);


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
            // 延遲初始化 Texture 和 SwsContext
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

                sws_ctx = sws_getContext(codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
                                        codec_ctx->width, codec_ctx->height, AV_PIX_FMT_YUV420P,
                                        SWS_BILINEAR, NULL, NULL, NULL);
                if (!sws_ctx) {
                    fprintf(stderr, "Failed to initialize sws_getContext.\n");
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
    }
}

