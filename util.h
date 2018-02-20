//
//  util.h
//  B16_OS_Tutorial
//
//  Created by Soroush Faghihi on 2/13/18.
//  Copyright © 2018 soroush. All rights reserved.
//

#ifndef util_h
#define util_h

/* Some includes !!! */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <string.h>
#include <err.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>

// Metadata type
typedef enum data_flag_t {
    CONN_CMD,
    CONN_CMD_STDERR,
    CONN_STDIN,
    CONN_STDIN_EOF,
    CONN_STDOUT,
    CONN_STDOUT_EOF,
    CONN_STDERR,
    CONN_STDERR_EOF,
    CONN_CMD_CLOSE,
    CONN_CMD_SIG
} data_flag_t;

/* Structs */
typedef struct conn_data_info_t{
    size_t data_len;
    data_flag_t data_flag;
} conn_data_info_t;

/* Macros */
#define BUFF_SIZE 1024
#define MAX_BACKLOG 10
#define MAX_SIML_CONNS 20

/* Utility Functions */
ssize_t sock_write(int socket, const void *data, ssize_t date_len); // Unsafe socket write!!!
ssize_t socket_write_safe(int socket, conn_data_info_t *data_inf, const void *data, pthread_mutex_t *mutex); // Thread safe version with nice interface
void socket_read_data(int sckt, ssize_t data_len, int fd);
ssize_t socket_read_data_to_var(int sckt, ssize_t data_len, void *output);
ssize_t socket_read_info(int sckt, conn_data_info_t *data_inf);

/* Socket address utility */
int getsockserver(const char *addr, const char *port, struct sockaddr **result, socklen_t *result_size);
int getsockclient(const char *addr, const char *port);

/* Routine Defs */
int client_routine(const char *ip, const char *port, const char *cmd, const char *stderr_fn);
int server_routine(const char *ip, const char *port);

#endif /* util_h */
