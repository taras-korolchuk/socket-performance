#include "server.h"

void log_event(const char* event) {
    FILE *log_file = fopen(LOG_FILE, "a");
    if (log_file == NULL) {
        perror("Unable to open log file");
        return;
    }
    time_t now;
    time(&now);
    fprintf(log_file, "%s%s\n", ctime(&now), event);
    fclose(log_file);
}

void print_usage(char* prog_name) {
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("Options:\n");
    printf("  --type [unix | inet]                                                      Socket type (default: unix)\n");
    printf("  --mode [blocking-sync | nonblocking | blocking-async | nonblocking-async] Mode (default: blocking)\n");
    printf("  --port PORT                                                               Server port (default: 8080)\n");
    printf("  --help                                                                    Show this help message\n");
}

int main(int argc, char* argv[]) {
    config_t config;
    config.socket_type = UNIX_SOCKET;
    config.mode = BLOCKING_SYNC;
    config.port = 8080;

    static struct option long_options[] = {
            {"type", required_argument, 0, 0},
            {"mode", required_argument, 0, 0},
            {"port", required_argument, 0, 0},
            {"help", no_argument,       0, 0},
            {0, 0,                      0, 0}
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
        } else if (strcmp(long_options[option_index].name, "port") == 0) {
            config.port = atoi(optarg);
        } else if (strcmp(long_options[option_index].name, "help") == 0) {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        }
    }

    int server_fd, client_fd;
    struct sockaddr_un unix_address;
    struct sockaddr_in inet_address;

    if (config.socket_type == UNIX_SOCKET) {
        unlink(UNIX_SOCKET_PATH);
        if ((server_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        memset(&unix_address, 0, sizeof(unix_address));
        unix_address.sun_family = AF_UNIX;
        strncpy(unix_address.sun_path, UNIX_SOCKET_PATH, sizeof(unix_address.sun_path) - 1);

        if (bind(server_fd, (struct sockaddr *) &unix_address, sizeof(unix_address)) == -1) {
            perror("bind");
            exit(EXIT_FAILURE);
        }
    } else {
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        inet_address.sin_family = AF_INET;
        inet_address.sin_addr.s_addr = INADDR_ANY;
        inet_address.sin_port = htons(config.port);

        if (bind(server_fd, (struct sockaddr *) &inet_address, sizeof(inet_address)) == -1) {
            perror("bind");
            exit(EXIT_FAILURE);
        }
    }

    if (config.mode == NONBLOCKING_SYNC || config.mode == NONBLOCKING_ASYNC) {
        int flags = fcntl(server_fd, F_GETFL, 0);
        fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    }

    if (listen(server_fd, 10) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server is listening...\n");
    log_event("Server started listening");

    if (config.mode == BLOCKING_ASYNC || config.mode == NONBLOCKING_ASYNC) {
        int epoll_fd = epoll_create1(0);
        if (epoll_fd == -1) {
            perror("epoll_create1");
            exit(EXIT_FAILURE);
        }

        struct epoll_event event, events[10];
        event.events = EPOLLIN;
        event.data.fd = server_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
            perror("epoll_ctl");
            exit(EXIT_FAILURE);
        }

        while (1) {
            int n = epoll_wait(epoll_fd, events, 10, -1);
            if (n == -1) {
                perror("epoll_wait");
                exit(EXIT_FAILURE);
            }

            for (int i = 0; i < n; i++) {
                if (events[i].data.fd == server_fd) {
                    client_fd = accept(server_fd, NULL, NULL);
                    if (client_fd == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("accept");
                    }

                    log_event("Client connected");
                    printf("Client connected\n");

                    event.events = EPOLLIN | EPOLLET;
                    event.data.fd = client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                        perror("epoll_ctl");
                        exit(EXIT_FAILURE);
                    }
                } else {
                    char buffer[BUFFER_SIZE];
                    ssize_t bytes_read;
                    size_t packets_received = 0;
                    while ((bytes_read = recv(events[i].data.fd, buffer, BUFFER_SIZE, 0)) > 0) {
                        ++packets_received;
                    }
                    if (bytes_read == 0) {
                        printf("Packets received: %zu\n", packets_received);
                        log_event("Client disconnected");
                        printf("Client disconnected\n");
                        close(events[i].data.fd);
                    } else if (bytes_read == -1 && errno != EAGAIN) {
                        perror("recv");
                        close(events[i].data.fd);
                    }
                }
            }
        }
    } else {
        struct pollfd pfd;
        pfd.fd = server_fd;
        pfd.events = POLLIN;

        while (1) {
            int ret = poll(&pfd, 1, -1);
            if (ret > 0) {
                if (pfd.revents & POLLIN) {
                    client_fd = accept(server_fd, NULL, NULL);
                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            continue;
                        } else {
                            perror("accept");
                            exit(EXIT_FAILURE);
                        }
                    }

                    log_event("Client connected");
                    printf("Client connected\n");

                    if (config.mode == NONBLOCKING_SYNC) {
                        int flags = fcntl(client_fd, F_GETFL, 0);
                        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
                    }

                    struct pollfd client_pfd;
                    client_pfd.fd = client_fd;
                    client_pfd.events = POLLIN;
                    size_t packets_received = 0;

                    while (1) {
                        int client_ret = poll(&client_pfd, 1, -1);
                        if (client_ret > 0) {
                            if (client_pfd.revents & POLLIN) {
                                char buffer[BUFFER_SIZE];
                                ssize_t bytes_read = recv(client_fd, buffer, BUFFER_SIZE, 0);
                                if (bytes_read > 0) {
                                    ++packets_received;
                                } else if (bytes_read == 0) {
                                    printf("Packets received: %zu\n", packets_received);
                                    log_event("Client disconnected");
                                    printf("Client disconnected\n");
                                    close(client_fd);
                                    break;
                                } else if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                                    perror("recv");
                                    close(client_fd);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}