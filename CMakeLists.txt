cmake_minimum_required(VERSION 3.29)
project(socket_performance C)

set(CMAKE_C_STANDARD 23)

set(CLIENT_HEADER client.h)
set(CLIENT_SOURCE client.c)

set(SERVER_HEADER server.h)
set(SERVER_SOURCE server.c)

add_executable(client ${CLIENT_HEADER} ${CLIENT_SOURCE})
add_executable(server ${SERVER_HEADER} ${SERVER_SOURCE})