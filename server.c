#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#define TOPIC_NAME_MAX_LEN 100
#define PAYLOAD_MAX_SIZE 1500
#define CLIENT_ID_MAX_LEN 10
#define MESSAGE_BUF_SIZE 1600
#define FRAME_HEADER_SIZE 4
#define COMMAND_MAX_LEN 300

typedef struct sub_node {
    char topic_pattern[256];
    struct sub_node *next;
} sub_node_t;

typedef struct saved_client {
    char id[CLIENT_ID_MAX_LEN + 1];
    sub_node_t *subs;
    struct saved_client *next;
} saved_client_t;

typedef enum {
    READ_HEADER = 0,
    READ_BODY
} read_state_t;

typedef struct client {
    int fd;
    char id[CLIENT_ID_MAX_LEN + 1];
    struct sockaddr_in addr;
    sub_node_t *subs;
    struct client *next;

    // framing fields:
    read_state_t  state;
    uint32_t to_read;
    uint8_t buf[MESSAGE_BUF_SIZE];
    uint32_t buf_offset;
} client_t;

// variabile globale
int tcp_listen_fd = -1, udp_fd = -1;
client_t *clients = NULL;
saved_client_t *saved_clients = NULL;

int  init_tcp_listener(int port);
int  init_udp_socket(int port);
void event_loop(void);
void accept_tcp_connection(void);
void process_udp_packet(void);
int  process_client_message(client_t *cli);
bool match_topic(const char *pattern, const char *topic);
double power10(int exp);
void save_client_subs(const char *id, sub_node_t *subs);
sub_node_t* retrieve_saved_subs(const char *id);
sub_node_t* clone_subscriptions(sub_node_t *subs);
void release_subscriptions(sub_node_t *subs);
ssize_t send_complete(int sock, const void *buf, size_t len);
ssize_t readn(int sock, void *buf, size_t len);
ssize_t receive_complete(int sock, void *buf, size_t len);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    if (port <= 0) {
        exit(EXIT_FAILURE);
    }

    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    signal(SIGPIPE, SIG_IGN);

    tcp_listen_fd = init_tcp_listener(port);
    udp_fd = init_udp_socket(port);
    event_loop();

    // cleanup
    for (client_t *c = clients; c; ) {
        client_t *next_c = c->next;
        if (c->fd >= 0) close(c->fd);
        release_subscriptions(c->subs);
        free(c);
        c = next_c;
    }
    clients = NULL;

    for (saved_client_t *sc = saved_clients; sc; ) {
        saved_client_t *next_sc = sc->next;
        release_subscriptions(sc->subs);
        free(sc);
        sc = next_sc;
    }
    saved_clients = NULL;
    close(tcp_listen_fd);
    close(udp_fd);
    return 0;
}

int init_tcp_listener(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket tcp"); exit(EXIT_FAILURE); }
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        perror("setsockopt TCP_NODELAY");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv = {0};
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        perror("bind tcp"); 
        exit(EXIT_FAILURE);
    }
    if (listen(sock, SOMAXCONN) < 0) {
        perror("listen"); 
        exit(EXIT_FAILURE);
    }
    return sock;
}

int init_udp_socket(int port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { 
        perror("socket udp");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in serv = {0};
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        perror("bind udp"); exit(EXIT_FAILURE);
    }
    return sock;
}

void event_loop() {
    // Bucle principale pentru select si gestionarea evenimentelor
    fd_set read_fds;
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(tcp_listen_fd, &read_fds);
        FD_SET(udp_fd, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        int max_fd = tcp_listen_fd > udp_fd ? tcp_listen_fd : udp_fd;
        max_fd = max_fd > STDIN_FILENO ? max_fd : STDIN_FILENO;
        for (client_t *c = clients; c; c = c->next) {
            if (c->fd >= 0) {
                FD_SET(c->fd, &read_fds);
                if (c->fd > max_fd) 
                    max_fd = c->fd;
            }
        }

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            perror("select"); exit(EXIT_FAILURE);
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char buf[16];
            if (fgets(buf, sizeof(buf), stdin) &&
                strncmp(buf, "exit", 4) == 0) {
                for (client_t *c = clients; c; c = c->next)
                    if (c->fd >= 0) 
                        close(c->fd);
                break;
            }
        }
        if (FD_ISSET(tcp_listen_fd, &read_fds)) accept_tcp_connection();
        if (FD_ISSET(udp_fd, &read_fds)) process_udp_packet();

        client_t **p = &clients;
        while (*p) {
            client_t *c = *p;
            if (c->fd >= 0 && FD_ISSET(c->fd, &read_fds)) {
                if (process_client_message(c) < 0) {
                    printf("Client %s disconnected.\n", c->id);
                    save_client_subs(c->id, c->subs);
                    close(c->fd);
                    release_subscriptions(c->subs);
                    client_t *to_free = c;
                    *p = c->next;
                    free(to_free);
                    continue;
                }
            }
            p = &((*p)->next);
        }
    }
}

void accept_tcp_connection() {
    // Accepta conexiuni TCP noi
    struct sockaddr_in cli_addr;
    socklen_t len = sizeof(cli_addr);
    int new_fd = accept(tcp_listen_fd, (struct sockaddr*)&cli_addr, &len);
    if (new_fd < 0) return;

    int flag = 1;
    if (setsockopt(new_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        perror("setsockopt TCP_NODELAY on new_fd");
        close(new_fd);
        return;
    }

    uint32_t net_len;
    if (receive_complete(new_fd, &net_len, FRAME_HEADER_SIZE) != FRAME_HEADER_SIZE) {
        close(new_fd);
        return;
    }

    uint32_t id_len = ntohl(net_len);
    if (id_len == 0 || id_len > CLIENT_ID_MAX_LEN + 1) {
        close(new_fd);
        return;
    }
    char id[CLIENT_ID_MAX_LEN+2] = {0};
    if (receive_complete(new_fd, id, id_len) != (ssize_t)id_len) {
        close(new_fd); return;
    }
    id[id_len-1] = '\0';

    for (client_t *c = clients; c; c = c->next) {
        if (strcmp(c->id, id) == 0 && c->fd >= 0) {
            printf("Client %s already connected.\n", id);
            close(new_fd);
            return;
        }
    }

    client_t *c = calloc(1, sizeof(*c));
    if (!c) { perror("calloc"); close(new_fd); return; }
    c->fd = new_fd;
    strncpy(c->id, id, CLIENT_ID_MAX_LEN);
    c->addr = cli_addr;
    c->subs = retrieve_saved_subs(id);
    c->next = clients;
    c->state = READ_HEADER;
    c->to_read = FRAME_HEADER_SIZE;
    c->buf_offset = 0;
    clients = c;

    printf("New client %s connected from %s:%d.\n",
           id, inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
}

void process_udp_packet(void) {
    // Preia pachetul UDP
    unsigned char buffer[TOPIC_NAME_MAX_LEN + PAYLOAD_MAX_SIZE + 10];
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    ssize_t recv_len = recvfrom(
        udp_fd,
        buffer,
        sizeof(buffer) - 1,
        0,
        (struct sockaddr*)&cli_addr,
        &cli_len
    );
    if (recv_len < 0) {
        return;
    }
    buffer[recv_len] = '\0';

    // Verifica dimensiunea minima
    if (recv_len < 2) {
        return;
    }

    // Extrage topic-ul (pana la '\0')
    size_t topic_len = 0;
    while (topic_len < (size_t)recv_len && buffer[topic_len] != '\0') {
        topic_len++;
    }
    if (topic_len >= (size_t)recv_len) {
        return;
    }

    char topic[TOPIC_NAME_MAX_LEN + 1];
    strncpy(topic, (char*)buffer, topic_len);
    topic[topic_len] = '\0';

    // Salt fix catre payload (offset = 50)
    size_t offset = 50;
    if (offset >= (size_t)recv_len) {
        return;
    }

    uint8_t type = buffer[offset++];
    if (offset >= (size_t)recv_len) {
        return;
    }

    // Decodează valoarea in functie de tip
    char val_str[256] = {0};
    switch (type) {
        case 0: {
            // INT
            uint8_t sign;
            uint32_t net_val;
            sign = buffer[offset++];
            memcpy(&net_val, buffer + offset, sizeof(net_val));
            offset += sizeof(net_val);

            int val = ntohl(net_val);
            if (sign) {
                val = -val;
            }
            snprintf(val_str, sizeof(val_str), "%d", val);
            break;
        }

        case 1: {
            // SHORT_REAL
            uint16_t net_val;
            memcpy(&net_val, buffer + offset, sizeof(net_val));
            offset += sizeof(net_val);

            float fval = ntohs(net_val) / 100.0f;
            snprintf(val_str, sizeof(val_str), "%.2f", fval);
            break;
        }

        case 2: {
            // FLOAT
            uint8_t sign;
            uint32_t net_val;
            uint8_t power;

            sign = buffer[offset++];
            memcpy(&net_val, buffer + offset, sizeof(net_val));
            offset += sizeof(net_val);
            power = buffer[offset++];

            double f = (double)ntohl(net_val) / power10(power);
            snprintf(
                val_str,
                sizeof(val_str),
                "%s%.*f",
                sign ? "-" : "",
                power,
                f
            );
            break;
        }

        case 3: {
            // STRING
            size_t str_len = recv_len - offset;
            if (str_len >= sizeof(val_str)) {
                str_len = sizeof(val_str) - 1;
            }
            memcpy(val_str, buffer + offset, str_len);
            val_str[str_len] = '\0';
            break;
        }

        default:
            // Tip necunoscut
            return;
    }

    // Construieste mesajul final
    char msg[MESSAGE_BUF_SIZE];
    const char *type_str = (type == 0 ? "INT" :
                            type == 1 ? "SHORT_REAL" :
                            type == 2 ? "FLOAT" :
                                        "STRING");
    int msg_len = snprintf(
        msg,
        sizeof(msg),
        "%s:%d - %s - %s - %s\n",
        inet_ntoa(cli_addr.sin_addr), // adresa UDP
        ntohs(cli_addr.sin_port),     // portul UDP
        topic,                        // topic
        type_str,                     // tipul payload-ului
        val_str                       // valoarea decodată
    );

    uint32_t net_len2 = htonl(msg_len);

    // Trimite fiecărui client abonat
    for (client_t *c = clients; c; c = c->next) {
        if (c->fd < 0) {
            continue;
        }
        for (sub_node_t *s = c->subs; s; s = s->next) {
            if (match_topic(s->topic_pattern, topic)) {
                if (send_complete(c->fd, &net_len2, FRAME_HEADER_SIZE) < 0 ||
                    send_complete(c->fd, msg, msg_len) < 0) {
                    perror("send_complete()");
                }
                break;
            }
        }
    }
}


int process_client_message(client_t *cli) {
    // Primeste date de la client
    ssize_t r = receive_complete(
        cli->fd,
        cli->buf + cli->buf_offset,
        cli->to_read - cli->buf_offset
    );
    if (r <= 0) {
        // Conexiune inchisa sau eroare
        return -1;
    }
    cli->buf_offset += (uint32_t)r;

    // Daca citim lungimea (header)
    if (cli->state == READ_HEADER) {
        if (cli->buf_offset < FRAME_HEADER_SIZE) {
            // header incomplet
            return 0;
        }

        uint32_t net_len;
        memcpy(&net_len, cli->buf, FRAME_HEADER_SIZE);
        uint32_t len = ntohl(net_len);

        // validare dimensiune comanda
        if (len == 0 || len > COMMAND_MAX_LEN) {
            return -1;
        }

        // trece la citirea corpului
        cli->to_read = len;
        cli->buf_offset = 0;
        cli->state = READ_BODY;
        return 0;
    }

    // Daca citim corpul mesajului
    if (cli->state == READ_BODY) {
        if (cli->buf_offset < cli->to_read) {
            // corp incomplet
            return 0;
        }

        // copiere mesaj intr-un buffer terminat cu '\0'
        char message[COMMAND_MAX_LEN + 1];
        memcpy(message, cli->buf, cli->to_read);
        message[cli->to_read] = '\0';

        // eliminare newline
        char *nl = strpbrk(message, "\r\n");
        if (nl) {
            *nl = '\0';
        }

        // Comanda subscribe
        if (strncmp(message, "subscribe ", 10) == 0) {
            sub_node_t *sub = malloc(sizeof(*sub));
            strncpy(
                sub->topic_pattern,
                message + 10,
                sizeof(sub->topic_pattern) - 1
            );
            sub->topic_pattern[sizeof(sub->topic_pattern)-1] = '\0';

            // Adauga la inceputul listei
            sub->next = cli->subs;
            cli->subs = sub;

            // Confirma abonarea
            char resp[256];
            int L = snprintf(
                resp,
                sizeof(resp),
                "Subscribed to topic %s\n",
                message + 10
            );
            uint32_t net_nl = htonl(L);
            send_complete(cli->fd, &net_nl, FRAME_HEADER_SIZE);
            send_complete(cli->fd, resp, L);

        }
        // Comanda unsubscribe
        else if (strncmp(message, "unsubscribe ", 12) == 0) {
            sub_node_t **p = &cli->subs;
            bool found = false;

            // Cauta si sterge patternul
            while (*p) {
                if (strcmp((*p)->topic_pattern, message + 12) == 0) {
                    sub_node_t *tmp = *p;
                    *p = tmp->next;
                    free(tmp);
                    found = true;

                    // Confirma dezabonarea
                    char resp[256];
                    int L = snprintf(
                        resp,
                        sizeof(resp),
                        "Unsubscribed from topic %s\n",
                        message + 12
                    );
                    uint32_t net_nl = htonl(L);
                    send_complete(cli->fd, &net_nl, FRAME_HEADER_SIZE);
                    send_complete(cli->fd, resp, L);
                    break;
                }
                p = &((*p)->next);
            }

            // Daca nu exista abonarea respectiva
            if (!found) {
                char resp[256];
                int L = snprintf(
                    resp,
                    sizeof(resp),
                    "No subscription found for topic %s\n",
                    message + 12
                );
                uint32_t net_nl = htonl(L);
                send_complete(cli->fd, &net_nl, FRAME_HEADER_SIZE);
                send_complete(cli->fd, resp, L);
            }
        }
        // Comanda exit
        else if (strncmp(message, "exit", 4) == 0) {
            return -1;
        }

        // Pregatire pentru urmatorul header
        cli->state      = READ_HEADER;
        cli->to_read    = FRAME_HEADER_SIZE;
        cli->buf_offset = 0;
    }

    return 0;
}


ssize_t send_complete(int sock, const void *buf, size_t len) {
    size_t sent = 0; const char *p = buf;
    while (sent < len) {
        ssize_t r = send(sock, p + sent, len - sent, 0);
        if (r <= 0) 
            return -1;
        sent += r;
    }
    return sent;
}

ssize_t readn(int sock, void *buf, size_t len) {
    size_t tot = 0; char *p = buf;
    while (tot < len) {
        ssize_t r = recv(sock, p + tot, len - tot, 0);
        if (r <= 0) 
            return -1;
        tot += r;
    }
    return tot;
}

ssize_t receive_complete(int sock, void *buf, size_t len) {
    size_t tot = 0; char *p = buf;
    while (tot < len) {
        ssize_t r = recv(sock, p + tot, len - tot, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0)   
            return 0;
        tot += r;
    }
    return tot;
}

void save_client_subs(const char *id, sub_node_t *subs) {
    // Salveaza lista de abonari la reconectare
    if (!subs) return;
    for (saved_client_t *sc = saved_clients; sc; sc = sc->next) {
        if (strcmp(sc->id, id) == 0) {
            release_subscriptions(sc->subs);
            sc->subs = clone_subscriptions(subs);
            return;
        }
    }
    saved_client_t *sc = calloc(1, sizeof(*sc));
    strncpy(sc->id, id, CLIENT_ID_MAX_LEN);
    sc->subs = clone_subscriptions(subs);
    sc->next = saved_clients;
    saved_clients = sc;
}

sub_node_t* retrieve_saved_subs(const char *id) {
    // Preia abonari salvate la o reconectare anterioara
    for (saved_client_t *sc = saved_clients; sc; sc = sc->next)
        if (strcmp(sc->id, id) == 0)
            return clone_subscriptions(sc->subs);
    return NULL;
}

sub_node_t* clone_subscriptions(sub_node_t *subs) {
    // Copiaza lista de abonari
    if (!subs) {
        return NULL;
    }

    sub_node_t *head = NULL;
    sub_node_t **tail = &head;
    for (; subs; subs = subs->next) {
        sub_node_t *n = malloc(sizeof(*n));
        if (!n) {
            release_subscriptions(head);
            return NULL;
        }
        strncpy(
            n->topic_pattern,
            subs->topic_pattern,
            sizeof(n->topic_pattern) - 1
        );
        n->topic_pattern[sizeof(n->topic_pattern) - 1] = '\0';
        n->next = NULL;
        *tail = n;
        tail = &n->next;
    }

    return head;
}


void release_subscriptions(sub_node_t *subs) {
    // Elibereaza memoria pentru abonari
    while (subs) { 
        sub_node_t *n = subs->next; 
        free(subs); 
        subs = n; 
    }
}

static bool matchSegments(char **ps, int pcount, char **ts, int tcount,
                          int i, int j) {
    if (i == pcount && j == tcount) {
        return true;
    }
    if (i == pcount) {
        return false;
    }
    if (strcmp(ps[i], "*") == 0) {
        for (int k = j; k <= tcount; k++) {
            if (matchSegments(ps, pcount, ts, tcount, i + 1, k)) {
                return true;
            }
        }
        return false;
    }
    if (j == tcount) {
        return false;
    }
    if (strcmp(ps[i], "+") == 0) {
        return matchSegments(ps, pcount, ts, tcount, i + 1, j + 1);
    }
    if (strcmp(ps[i], ts[j]) == 0) {
        return matchSegments(ps, pcount, ts, tcount, i + 1, j + 1);
    }
    return false;
}

bool match_topic(const char *pattern, const char *topic) {
    char pat_copy[256], top_copy[256];
    strncpy(pat_copy, pattern, sizeof(pat_copy) - 1);
    pat_copy[255] = '\0';
    strncpy(top_copy, topic, sizeof(top_copy) - 1);
    top_copy[255] = '\0';
    char *pat_segs[128], *top_segs[128];
    int pcount = 0, tcount = 0;
    char *tok = strtok(pat_copy, "/");
    while (tok && pcount < 128) {
        pat_segs[pcount++] = tok;
        tok = strtok(NULL, "/");
    }
    tok = strtok(top_copy, "/");
    while (tok && tcount < 128) {
        top_segs[tcount++] = tok;
        tok = strtok(NULL, "/");
    }
    return matchSegments(pat_segs, pcount, top_segs, tcount, 0, 0);
}

double power10(int exp) {
    double r = 1.0;
    for (int i = 0; i < exp; i++) 
        r *= 10.0;
    return r;
}