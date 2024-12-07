#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <poll.h>

#define BUFFER_SIZE 1024
#define UNIX_SOCKET_PATH "/tmp/socket_perf_test"
#define LOG_FILE "server_log.txt"

typedef enum {
    UNIX_SOCKET, INET_SOCKET
} socket_type_t;

typedef enum {
    BLOCKING_SYNC, NONBLOCKING_SYNC, BLOCKING_ASYNC, NONBLOCKING_ASYNC
} socket_mode_t;

typedef struct {
    socket_type_t socket_type;
    socket_mode_t mode;
    int port;
} config_t;

void log_event(const char* event);

void print_usage(char* prog_name);