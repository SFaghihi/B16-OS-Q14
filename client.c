//
//  client.c
//  q14
//
//  Created by Soroush Faghihi on 2/13/18.
//  Copyright © 2018 soroush. All rights reserved.
//
#include "util.h"
#include <fcntl.h>

int pending_flag = 0;

struct cl_arg_t {
    int socket;
    int keep_running;
    pthread_mutex_t *writing_mutex;
};
void *client_process_stdin(struct cl_arg_t *arg)
{
    int sckt = arg->socket;
    conn_data_info_t data_inf;
    char *buff = NULL;
    size_t line_len = 0;
    ssize_t read_len;
    while (arg->keep_running && (read_len = getline(&buff, &line_len, stdin)) > 0) {
        data_inf.data_len = read_len;
        data_inf.data_flag = CONN_STDIN;
        if (socket_write_safe(sckt, &data_inf, buff, arg->writing_mutex)) {
            free(buff); close(sckt); exit(-4);
        }
        if (pending_flag) {
            data_inf.data_flag = CONN_CMD_SIG;
            data_inf.data_len = pending_flag;
            pthread_mutex_lock(arg->writing_mutex);
            if (sock_write(sckt, &data_inf, sizeof(conn_data_info_t))) {
                free(buff); close(sckt); exit(-4);
            }
        }
    }
    if (buff)
        free(buff);
    if (!arg->keep_running) {
        return NULL;
    } else if (feof(stdin) || !ferror(stdin)) {
        printf("Received EOF from keyboard\n");
        data_inf.data_len = 0;
        data_inf.data_flag = CONN_STDIN_EOF;
        if (sock_write(sckt, &data_inf, sizeof(data_inf)) < 0) {
            close(sckt); exit(-4);
        }
    } else {
        perror("Keyboard receiving error ");
        fprintf(stderr, "Will send EOF anyway\n");
        data_inf.data_len = 0;
        data_inf.data_flag = CONN_STDIN_EOF;
        if (sock_write(sckt, &data_inf, sizeof(data_inf)) < 0) {
            close(sckt); exit(-4);
        }
        //err(-3, "stdin read error with code: %d, ", errno);
    }
    return NULL;
}

int client_process_output(int sckt, int errfd)
{
    /* Initialization */
    int err_stream_open = errfd >= 0, out_stream_open = 1, cmd_finished = 0;
    conn_data_info_t data_inf;
    ssize_t read_len;
    
    // Read data_info
    while ((read_len = socket_read_info(sckt, &data_inf)) > 0)
    {
        switch (data_inf.data_flag)
        {
            case CONN_STDOUT:
                if (out_stream_open)
                    socket_read_data(sckt, data_inf.data_len, STDOUT_FILENO);
                break;
            
            case CONN_STDERR:
                if (err_stream_open)
                    socket_read_data(sckt, data_inf.data_len, errfd);
                break;
                
            case CONN_STDOUT_EOF:
                out_stream_open = 0;
                break;
                
            case CONN_STDERR_EOF:
                if (err_stream_open)
                    close(errfd);
                err_stream_open = 0;
            
            case CONN_CMD_CLOSE:
                cmd_finished = 1;
                printf("CMD returned with %d\n", (int)data_inf.data_len);
                break;
                
            default:
                fprintf(stderr, "Received Data type of unsupported type!!!:\n");
                socket_read_data(sckt, data_inf.data_len, STDERR_FILENO);
                break;
        }
        if (cmd_finished)
            break;
    }
    /*if (0 && read_len == 0) {
        err(1, "Connection Closed! (EOF Received!)");
    } else */if (read_len <= 0) {
        return 1;
    }
    return 0;
}

void handle_sig(int sig)
{
    pending_flag = sig;
}

int client_routine(const char *addr, const char *port, const char *cmd, const char *stderr_fn)
{
    signal(SIGPIPE, SIG_IGN);
    
    int errfd = -1;
    conn_data_info_t request;
    request.data_len = strlen(cmd);
    request.data_flag = CONN_CMD;
    if (stderr_fn) {
        if (strcmp(stderr_fn, "-") == 0) {
            errfd = STDERR_FILENO;
        } else {
            errfd = open(stderr_fn, O_WRONLY | O_CREAT | O_TRUNC);
            if (errfd < 0)
                err(-2, "stderr file could not be opened error with code: %d, ", errno);
        }
        request.data_flag = CONN_CMD_STDERR;
    }
    int sckt = getsockclient(addr, port);
    if (sckt < 0)
        err(-1, "Socket creation error with code: %d, ", errno);
    
    // Send the cmd request
    if (sock_write(sckt, (void *)&request, sizeof(request)) < 0) {
        close(sckt); exit(-4);
    }
    if (sock_write(sckt, (void *)cmd, request.data_len) < 0) {
        close(sckt); exit(-4);
    }
    
    // Start Stdin processing
    pthread_t stdin_proc;
    pthread_mutex_t writing_mutex = PTHREAD_MUTEX_INITIALIZER;
    struct cl_arg_t stdin_arg = {sckt, 1, &writing_mutex};
    pthread_create(&stdin_proc, NULL, (void *(*)(void *))&client_process_stdin, &stdin_arg);
    
    // Start read processing
    int retval = client_process_output(sckt, errfd);
    
    // Exit procedure
    if (pthread_mutex_trylock(&writing_mutex)) {
        stdin_arg.keep_running = 0;
        pthread_join(stdin_proc, NULL);
    } else {
        pthread_cancel(stdin_proc);
        pthread_mutex_unlock(&writing_mutex);
    }
    
    if (retval)
        err(2, "Connection Read Error with code: %d, ", errno);
    
    return 0;
}

