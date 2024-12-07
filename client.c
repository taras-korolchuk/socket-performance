#include "client.h"

void print_usage(char* prog_name) {
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("Options:\n");
    printf("  --type [unix | inet]                                                           Socket type (default: unix)\n");
    printf("  --mode [blocking-sync | nonblocking-sync | blocking-async | nonblocking-async] Mode (default: blocking-sync)\n");
    printf("  --address ADDRESS                                                              Server address (default: 127.0.0.1)\n");
    printf("  --port PORT                                                                    Server port (default: 8080)\n");
    printf("  --workload N                                                                   Number of packets to send (default: 100000)\n");
    printf("  --help                                                                         Show this help message\n");
}

int main(int argc, char* argv[]) {
    config_t config;
    config.socket_type = UNIX_SOCKET;
    config.mode = BLOCKING_SYNC;
    config.address = "127.0.0.1";
    config.port = 8080;
    config.workload = 100000;

    static struct option long_options[] = {
            {"type",     required_argument, 0, 0},
            {"mode",     required_argument, 0, 0},
            {"address",  required_argument, 0, 0},
            {"port",     required_argument, 0, 0},
            {"workload", required_argument, 0, 0},
            {"help",     no_argument,       0, 0},
            {0, 0,                          0, 0}
    };

    int option_index = 0;
    while (1) {
        int c = getopt_long(argc, argv, "", long_options, &option_index);
        if (c == -1) {
            break;
        }

        if (strcmp(long_options[option_index].name, "type") == 0) {
            if (strcmp(optarg, "unix") == 0)
                config.socket_type = UNIX_SOCKET;
            else if (strcmp(optarg, "inet") == 0)
                config.socket_type = INET_SOCKET;
            else {
                fprintf(stderr, "Invalid socket type\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(long_options[option_index].name, "mode") == 0) {
            if (strcmp(optarg, "blocking-sync") == 0)
                config.mode = BLOCKING_SYNC;
            else if (strcmp(optarg, "nonblocking-sync") == 0)
                config.mode = NONBLOCKING_SYNC;
            else if (strcmp(optarg, "blocking-async") == 0)
                config.mode = BLOCKING_ASYNC;
            else if (strcmp(optarg, "nonblocking-async") == 0)
                config.mode = NONBLOCKING_ASYNC;
            else {
                fprintf(stderr, "Invalid mode\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(long_options[option_index].name, "address") == 0) {
            config.address = optarg;
        } else if (strcmp(long_options[option_index].name, "port") == 0) {
            config.port = atoi(optarg);
        } else if (strcmp(long_options[option_index].name, "workload") == 0) {
            config.workload = atoi(optarg);
        } else if (strcmp(long_options[option_index].name, "help") == 0) {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        }
    }

    int sock_fd;
    struct sockaddr_un unix_address;
    struct sockaddr_in inet_address;

    struct timespec conn_start, conn_end;
    clock_gettime(CLOCK_MONOTONIC, &conn_start);

    if (config.socket_type == UNIX_SOCKET) {
        if ((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        memset(&unix_address, 0, sizeof(unix_address));
        unix_address.sun_family = AF_UNIX;
        strncpy(unix_address.sun_path, UNIX_SOCKET_PATH, sizeof(unix_address.sun_path) - 1);

        if (connect(sock_fd, (struct sockaddr *) &unix_address, sizeof(unix_address)) == -1) {
            perror("connect");
            exit(EXIT_FAILURE);
        }
    } else {
        if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        inet_address.sin_family = AF_INET;
        inet_address.sin_port = htons(config.port);
        if (inet_pton(AF_INET, config.address, &inet_address.sin_addr) <= 0) {
            perror("inet_pton");
            exit(EXIT_FAILURE);
        }

        if (connect(sock_fd, (struct sockaddr *) &inet_address, sizeof(inet_address)) == -1) {
            perror("connect");
            exit(EXIT_FAILURE);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &conn_end);
    double conn_time = (conn_end.tv_sec - conn_start.tv_sec) * 1000.0 +
                       (conn_end.tv_nsec - conn_start.tv_nsec) / 1000000.0;

    if (config.mode == NONBLOCKING_SYNC || config.mode == NONBLOCKING_ASYNC) {
        int flags = fcntl(sock_fd, F_GETFL, 0);
        if (fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("fcntl");
            exit(EXIT_FAILURE);
        }
    }

    char buffer[BUFFER_SIZE];
    memset(buffer, 'A', BUFFER_SIZE);

    struct timespec data_start, data_end;
    clock_gettime(CLOCK_MONOTONIC, &data_start);

    int total_packets = 0;
    int total_bytes = 0;

    if (config.mode == BLOCKING_ASYNC || config.mode == NONBLOCKING_ASYNC) {
        int epoll_fd = epoll_create1(0);
        if (epoll_fd == -1) {
            perror("epoll_create1");
            exit(EXIT_FAILURE);
        }

        struct epoll_event event;
        event.events = EPOLLOUT;
        event.data.fd = sock_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &event) == -1) {
            perror("epoll_ctl");
            exit(EXIT_FAILURE);
        }

        while (total_packets < config.workload) {
            struct epoll_event events[1];
            int n = epoll_wait(epoll_fd, events, 1, -1);
            if (n == -1) {
                perror("epoll_wait");
                exit(EXIT_FAILURE);
            }

            if (events[0].events & EPOLLOUT) {
                ssize_t bytes_sent = send(sock_fd, buffer, BUFFER_SIZE, 0);
                if (bytes_sent == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    } else {
                        perror("send");
                        exit(EXIT_FAILURE);
                    }
                } else {
                    total_packets++;
                    total_bytes += bytes_sent;
                }
            }
        }

        close(epoll_fd);
    } else {
        struct pollfd pfd;
        pfd.fd = sock_fd;
        pfd.events = POLLOUT;

        while (total_packets < config.workload) {
            int poll_res = poll(&pfd, 1, -1);
            if (poll_res > 0 && (pfd.revents & POLLOUT)) {
                ssize_t bytes_sent = send(sock_fd, buffer, BUFFER_SIZE, 0);
                if (bytes_sent == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    } else {
                        perror("send");
                        exit(EXIT_FAILURE);
                    }
                } else {
                    total_packets++;
                    total_bytes += bytes_sent;
                }
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &data_end);
    double data_time = (data_end.tv_sec - data_start.tv_sec) * 1000.0 +
                       (data_end.tv_nsec - data_start.tv_nsec) / 1000000.0;

    struct timespec close_start, close_end;
    clock_gettime(CLOCK_MONOTONIC, &close_start);

    close(sock_fd);

    clock_gettime(CLOCK_MONOTONIC, &close_end);
    double close_time = (close_end.tv_sec - close_start.tv_sec) * 1000.0 +
                        (close_end.tv_nsec - close_start.tv_nsec) / 1000000.0;

    printf("Connection time: %.3f ms\n", conn_time);
    printf("Data transfer time: %.3f ms\n", data_time);
    printf("Total packets sent: %d\n", total_packets);
    printf("Total bytes sent: %d\n", total_bytes);
    printf("Throughput: %.2f packets/sec, %.2f MB/sec\n",
           (total_packets / (data_time / 1000.0)),
           (total_bytes / (data_time / 1000.0)) / (1024 * 1024));
    printf("Connection close time: %.3f ms\n", close_time);

    return 0;
}