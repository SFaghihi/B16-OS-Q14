//
//  util.c
//  q14
//
//  Created by Soroush Faghihi on 2/14/18.
//  Copyright © 2018 soroush. All rights reserved.
//

#include "util.h"

ssize_t socket_write_safe(int socket, conn_data_info_t *data_inf, const void *data, pthread_mutex_t *mutex)
{
    pthread_mutex_lock(mutex);
    ssize_t err_code = sock_write(socket, data_inf, sizeof(conn_data_info_t));
    if (err_code < 0) {
        close(socket);
        pthread_mutex_unlock(mutex);
        return err_code;
    }
    err_code = sock_write(socket, data, data_inf->data_len);
    if (err_code < 0)
        close(socket);
    pthread_mutex_unlock(mutex);
    return err_code;
}

ssize_t sock_write(int socket, const void *data, ssize_t date_len)
{
    ssize_t written_size;
    while (date_len > 0)
    {
        written_size = write(socket, data, date_len);
        if (written_size < 0) {
            perror("Write error to socket, Error: ");
            //fprintf(stderr, "Write error to socket(%d) with error code: %d\n", socket, errno);
            return written_size;
            //close(socket);
            //exit(-4);
        }
        date_len -= written_size;
        data += written_size;
    }
    return date_len;
}

void socket_read_data(int sckt, ssize_t data_len, int fd)
{
    char buff[BUFF_SIZE];
    ssize_t data_to_read, read_len;
    memset(&buff, 0, BUFF_SIZE);
    
    while (data_len > 0)
    {
        data_to_read = (data_len > BUFF_SIZE) ? BUFF_SIZE : data_len;
        read_len = read(sckt, buff, data_to_read);
        if (read_len == 0) {
            err(1, "Connection Closed! (EOF Received!)\n");
        } else if (read_len < 0) {
            err(2, "Connection Read Error with code: %d\n", errno);
        }
        sock_write(fd, buff, read_len);
        data_len -= read_len;
    }
}

ssize_t socket_read_data_to_var(int sckt, ssize_t data_len, void *output)
{
    const size_t met_size = data_len;
    ssize_t data_to_read, read_len;
    while (data_len > 0) {
        data_to_read = (data_len > met_size) ? met_size : data_len;
        read_len = read(sckt, output, data_to_read);
        if (read_len <= 0)
            return 0;
        data_len -= read_len;
        output += read_len;
    }
    return met_size;
}

ssize_t socket_read_info(int sckt, conn_data_info_t *data_inf)
{
    return socket_read_data_to_var(sckt, sizeof(conn_data_info_t), data_inf);
}

int getsockserver(const char *addr, const char *port, struct sockaddr **result, socklen_t *result_size)
{
    struct addrinfo hints, *res, *res0;
    int error;
    int s;
    const char *cause = NULL;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    error = getaddrinfo(addr, port, &hints, &res0);
    if (error) {
        errx(1, "%s", gai_strerror(error));
        /*NOTREACHED*/
    }
    s = -1;
    for (res = res0; res; res = res->ai_next) {
        s = socket(res->ai_family, res->ai_socktype,
                   res->ai_protocol);
        if (s < 0) {
            cause = "socket";
            continue;
        }
        
        ((struct sockaddr_in *)res->ai_addr)->sin_port = strtol(port, NULL, 10);
        printf("Port is %s == %d\n", port, ((struct sockaddr_in *)res->ai_addr)->sin_port);
        if (bind(s, res->ai_addr, res->ai_addrlen) < 0) {
            cause = "bind";
            close(s);
            s = -1;
            continue;
        }
        
        break;  /* okay we got one */
    }
    if (s < 0) {
        err(1, "%s", cause);
        /*NOTREACHED*/
    }
    *result = malloc(res->ai_addrlen);
    *result_size = res->ai_addrlen;
    memcpy(*result, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res0);
    return s;
}

int getsockclient(const char *addr, const char *port)
{
    struct addrinfo hints, *res, *res0;
    int error;
    int s;
    const char *cause = NULL;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    error = getaddrinfo(addr, port, &hints, &res0);
    if (error) {
        errx(1, "%s", gai_strerror(error));
        /*NOTREACHED*/
    }
    s = -1;
    for (res = res0; res; res = res->ai_next) {
        s = socket(res->ai_family, res->ai_socktype,
                   res->ai_protocol);
        if (s < 0) {
            cause = "socket";
            continue;
        }
        ((struct sockaddr_in *)res->ai_addr)->sin_port = strtol(port, NULL, 10);
        if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
            cause = "connect";
            close(s);
            s = -1;
            continue;
        }
        break;  /* okay we got one */
    }
    if (res && s < 0) {
        uint32_t addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
        in_port_t port_n = ((struct sockaddr_in *)res->ai_addr)->sin_port;
        fprintf(stderr, "ip Addr: %d.%d.%d.%d, port: %d\n", (addr>>0)&0xff, (addr>>8)&0xff, (addr>>16)&0xff, (addr>>24)&0xff, port_n);
        err(1, "%s", cause);
        /*NOTREACHED*/
    }
    freeaddrinfo(res0);
    return s;
}
