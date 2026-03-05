#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/tcp.h>
#include <stdint.h>

#define CLIENT_ID_MAX_LEN 10
#define FRAME_HEADER_SIZE 4
#define MESSAGE_BUFFER_SIZE 1600

typedef enum {
    READ_HEADER = 0,
    READ_BODY
} read_state_t;

ssize_t send_complete(int sock, const void *buf, size_t len) {
    size_t sent = 0;
    const char *p = buf;
    while (sent < len) {
        ssize_t r = send(sock, p + sent, len - sent, 0);
        if (r <= 0) 
            return -1;
        sent += r;
    }
    return sent;
}

int establish_connection(const char *ip, int port, const char *id) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        // IP invalid
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sock);
        exit(EXIT_FAILURE);
    }

    char id_msg[CLIENT_ID_MAX_LEN + 2];
    snprintf(id_msg, sizeof(id_msg), "%s\n", id);

    uint32_t len = strlen(id_msg);
    uint32_t net_len = htonl(len);

    if (send_complete(sock, &net_len, FRAME_HEADER_SIZE) < 0 ||
        send_complete(sock, id_msg, len) < 0) {
        perror("send id");
        close(sock);
        exit(EXIT_FAILURE);
    }

    return sock;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        exit(EXIT_FAILURE);
    }

    char id[CLIENT_ID_MAX_LEN + 1];
    strncpy(id, argv[1], CLIENT_ID_MAX_LEN);
    id[CLIENT_ID_MAX_LEN] = '\0';

    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    int port = atoi(argv[3]);
    int sock = establish_connection(argv[2], port, id);

    // Dezactiveaza Nagle
    int flag = 1;
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        perror("setsockopt TCP_NODELAY");
        close(sock);
        exit(EXIT_FAILURE);
    }

    read_state_t state      = READ_HEADER;
    uint32_t     to_read    = FRAME_HEADER_SIZE;
    uint32_t     buf_offset = 0;
    uint8_t      buf[MESSAGE_BUFFER_SIZE];

    fd_set read_fds;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);

        int max_fd = sock > STDIN_FILENO ? sock : STDIN_FILENO;

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char line[256];

            if (!fgets(line, sizeof(line), stdin)) {
                break;
            }

            size_t len = strlen(line);
            if (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                line[len - 1] = '\n';
                line[len]     = '\0';
            }

            uint32_t msg_len = strlen(line);
            uint32_t net_len = htonl(msg_len);

            if (send_complete(sock, &net_len, FRAME_HEADER_SIZE) < 0 ||
                send_complete(sock, line,      msg_len)   < 0) {
                perror("send");
                break;
            }

            if (strncmp(line, "exit", 4) == 0) {
                break;
            }
        }

        if (FD_ISSET(sock, &read_fds)) {
            ssize_t r = recv(
                sock,
                buf + buf_offset,
                to_read - buf_offset,
                0
            );
            if (r <= 0) {
                printf("Server closed connection.\n");
                break;
            }
            buf_offset += (uint32_t)r;
            if (state == READ_HEADER) {
                if (buf_offset < FRAME_HEADER_SIZE) {
                    continue;
                }

                uint32_t net_len;
                memcpy(&net_len, buf, FRAME_HEADER_SIZE);
                uint32_t len = ntohl(net_len);

                if (len == 0 || len > MESSAGE_BUFFER_SIZE) {
                    break;
                }

                to_read    = len;
                buf_offset = 0;
                state      = READ_BODY;
                continue;
            }

            if (state == READ_BODY) {
                if (buf_offset < to_read) {
                    continue;
                }

                char *msg = malloc(to_read + 1);
                if (!msg) {
                    perror("malloc");
                    break;
                }

                memcpy(msg, buf, to_read);
                msg[to_read] = '\0';
                char *line = msg;
                char *next;
                while ((next = strchr(line, '\n')) != NULL) {
                    *next = '\0';
                    if (strlen(line) > 0) {
                        printf("%s\n", line);
                    }
                    line = next + 1;
                }
                if (*line) {
                    printf("%s\n", line);
                }

                free(msg);
                state      = READ_HEADER;
                to_read    = FRAME_HEADER_SIZE;
                buf_offset = 0;
            }
        }
    }

    close(sock);
    return 0;
}
