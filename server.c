//
//  server.c
//  q14
//
//  Created by Soroush Faghihi on 2/13/18.
//  Copyright © 2018 soroush. All rights reserved.
//

#include "util.h"
#include <sys/wait.h>
#include <fcntl.h>
#include <semaphore.h>

/* Global variables */
size_t free_threads[MAX_SIML_CONNS];
ssize_t free_threads_ptr = MAX_SIML_CONNS-1;
pthread_mutex_t free_thread_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t *thread_available = NULL;
int main_socket = -1;

typedef struct thread_info_t {
    pthread_t thread_id;
    sem_t *end_proc;
} thread_info_t;
thread_info_t threads_info[MAX_SIML_CONNS];

typedef struct serv_proc_args {
    int socket;
    int pipe;
    int *is_running;
    data_flag_t type;
    data_flag_t eof_type;
    pthread_mutex_t *mutex;
    sem_t *end_proc;
    pid_t child;
} serv_proc_args;

void server_process_input(serv_proc_args *arg)
{
    int sckt = arg->socket;
    int stdin_pipe = arg->pipe;
    /* Initialization */
    int in_stream_open = 1, cmd_finished = 0;
    conn_data_info_t data_inf;
    ssize_t read_len;
    
    // Read data_info
    while ((read_len = socket_read_info(sckt, &data_inf)) > 0)
    {
        switch (data_inf.data_flag)
        {
            case CONN_STDIN:
                if (in_stream_open)
                    socket_read_data(sckt, data_inf.data_len, stdin_pipe);
                break;
                
            case CONN_STDIN_EOF:
                in_stream_open = 0;
                break;
                
            case CONN_CMD_CLOSE:
                cmd_finished = 1;
                break;
                
            case CONN_CMD_SIG:
                kill(arg->child, (int)data_inf.data_len);
                break;
                
            default:
                fprintf(stderr, "Received Data type of unsupported type!!!:\n");
                socket_read_data(sckt, data_inf.data_len, STDERR_FILENO);
                break;
        }
        if (cmd_finished || !in_stream_open)
            break;
    }
    if (cmd_finished) {
        fprintf(stderr, "Client specifically requested CMD be closed.\n");
        *(arg->is_running) = 0;
        sem_post(arg->end_proc);
        pthread_exit((void *)0);
    } else if (!in_stream_open) {
        fprintf(stderr, "Client closed the stdin stream.\n");
        close(stdin_pipe);
        *(arg->is_running) = 0;
        pthread_exit((void *)0);
    }else if (read_len == 0) {
        fprintf(stderr, "Connection Closed! (EOF Received!)\n");
        *(arg->is_running) = 0;
        sem_post(arg->end_proc);
        //close(arg->pipe);
        //kill(arg->child, SIGHUP);
        pthread_exit((void *)1);
    } else if (read_len < 0) {
        perror("Connection Read Error with code: ");
        *(arg->is_running) = 0;
        sem_post(arg->end_proc);
        //close(arg->pipe);
        //kill(arg->child, SIGHUP);
        pthread_exit((void *)2);
    }
}

void server_process_output(serv_proc_args *arg)
{
    int sckt = arg->socket;
    int outfd = arg->pipe;
    conn_data_info_t data_inf;
    char buff[BUFF_SIZE];
    size_t read_len;
    while ((read_len = read(outfd, buff, BUFF_SIZE)) > 0) {
        data_inf.data_len = read_len;
        data_inf.data_flag = arg->type;
        if (socket_write_safe(sckt, &data_inf, buff, arg->mutex) < 0) {
            *(arg->is_running) = 0;
            sem_post(arg->end_proc);
            pthread_exit((void *)-4);
        }
    }
    if (read_len == 0) {
        data_inf.data_len = 0;
        data_inf.data_flag = arg->eof_type;
        if (socket_write_safe(sckt, &data_inf, NULL, arg->mutex) < 0) {
            *(arg->is_running) = 0;
            sem_post(arg->end_proc);
            pthread_exit((void *)-4);
        }
        pthread_exit((void *)0);
    } else {
        fprintf(stderr, "Output read error with code: %d\n", errno);
        *(arg->is_running) = 0;
        sem_post(arg->end_proc);
        pthread_exit((void *)-3);
    }
}

struct serv_args {
    int socket;
    size_t thread_num;
    struct sockaddr *client_addr;
    sem_t *end_proc;
};

void serv_request(struct serv_args *arg)
{
    int sckt = arg->socket;
    pthread_mutex_t *req_mutex = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(req_mutex, NULL);
    
    // Setup the semaphore
    char sem_name[100]; snprintf(sem_name, 100, "/Q14_Server_%d_%zull", getpid(), arg->thread_num);
    sem_t *end_proc = arg->end_proc;
    
    conn_data_info_t start_req;
    if (socket_read_info(sckt, &start_req) != sizeof(start_req)) {
        fprintf(stderr, "Could not read the request from client. Error code: %d\n", errno);
        goto Thread_Exit;
    }
    if (start_req.data_flag != CONN_CMD && start_req.data_flag != CONN_CMD_STDERR) {
        fprintf(stderr, "Invalid starting request from client. Error code: %d\n", errno);
        goto Thread_Exit;
    }
    // CMD read
    char *cmd = malloc(start_req.data_len + 1);
    socket_read_data_to_var(sckt, start_req.data_len, cmd);
    cmd[start_req.data_len] = '\0';
    
    // Pipe setup
    conn_data_info_t err_inf; err_inf.data_flag = CONN_STDOUT;
    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdin_pipe)) {
        const char *errstr = "Couldn't open stdin pipe!!!\n";
        perror("Couldn't open stdin pipe!!! Error ");
        err_inf.data_len = strlen(errstr);
        socket_write_safe(sckt, &err_inf, errstr, req_mutex);
        free(cmd); close(stdin_pipe[1]); close(stdin_pipe[0]);
        goto Thread_Exit;
    }
    if (pipe(stdout_pipe)) {
        const char *errstr = "Couldn't open stdout pipe!!!\n";
        perror("Couldn't open stdout pipe!!! Error ");
        err_inf.data_len = strlen(errstr);
        socket_write_safe(sckt, &err_inf, errstr, req_mutex);
        free(cmd); close(stdout_pipe[1]); close(stdout_pipe[0]);
        close(stdin_pipe[1]); close(stdin_pipe[0]);
        goto Thread_Exit;
    }
    if (pipe(stderr_pipe)) {
        const char *errstr = "Couldn't open stderr pipe!!!\n";
        perror("Couldn't open stderr pipe!!! Error ");
        err_inf.data_len = strlen(errstr);
        socket_write_safe(sckt, &err_inf, errstr, req_mutex);
        free(cmd); close(stderr_pipe[1]); close(stderr_pipe[0]);
        close(stdin_pipe[1]); close(stdin_pipe[0]);
        close(stdout_pipe[1]); close(stdout_pipe[0]);
        goto Thread_Exit;
    }
    
    // Fork
    pid_t child_pid = fork();
    if (child_pid == 0)
    {
        // Child process: close unused file descriptors and overwrite std(in|err|out)
        close(stdin_pipe[1]); close(stdout_pipe[0]); close(stderr_pipe[0]);
        dup2(stdin_pipe[0], STDIN_FILENO); dup2(stdout_pipe[1], STDOUT_FILENO); dup2(stderr_pipe[1], STDERR_FILENO);
        
        // Set this process as the leader of the process group
        pid_t pgrp = setsid();
        if (pgrp < 0) {
            fprintf(stderr, "CMD: %s\n", cmd);
            perror("We Couldn't become Session leader. Error ");
            exit(-1);
        }
        if (write(stdout_pipe[1], &pgrp, sizeof(pid_t)) != sizeof(pid_t)) {
            fprintf(stderr, "CMD: %s\n", cmd);
            perror("We couldn't communicate the process group ID. Error ");
            exit(-1);
        }
        
        // Start execing the cmd
        execl(cmd, cmd, NULL);
        
        // Should never happen!!!
        fprintf(stderr, "CMD: %s\n", cmd);
        perror("We didn't execve!!! Error ");
        exit(-1);
    }
    
    // Parent process: close unused file descriptors
    close(stdin_pipe[0]); close(stdout_pipe[1]); close(stderr_pipe[1]);
    
    // Read the process group
    pid_t pgrp;
    if (read(stdout_pipe[0], &pgrp, sizeof(pid_t)) != sizeof(pid_t)) {
        const char *errstr = "Couldn't read the process group ID!!!\n";
        perror("We couldn't read the process group ID. Error ");
        err_inf.data_len = strlen(errstr);
        socket_write_safe(sckt, &err_inf, errstr, req_mutex);
        free(cmd);
        close(stdin_pipe[1]); close(stdout_pipe[0]); close(stderr_pipe[0]);
        goto Thread_Exit;
    }
    
    /* Setup socket -> pipline threads */
    // STDIN
    pthread_t stdin_proc; int in_is_running = 1;
    serv_proc_args stdin_arg; stdin_arg.mutex = req_mutex; stdin_arg.is_running = &in_is_running; stdin_arg.child = child_pid;
    stdin_arg.socket = sckt; stdin_arg.pipe = stdin_pipe[1]; stdin_arg.end_proc = end_proc;
    pthread_create(&stdin_proc, NULL, (void *(*)(void *))&server_process_input, &stdin_arg);
    
    // STDOUT
    pthread_t stdout_proc; int out_is_running = 1;
    serv_proc_args stdout_arg; stdout_arg.mutex = req_mutex; stdout_arg.is_running = &out_is_running;
    stdout_arg.socket = sckt; stdout_arg.pipe = stdout_pipe[0]; stdout_arg.end_proc = end_proc;
    stdout_arg.type = CONN_STDOUT; stdout_arg.eof_type = CONN_STDOUT_EOF;
    pthread_create(&stdout_proc, NULL, (void *(*)(void *))&server_process_output, &stdout_arg);
    
    // STDERR
    pthread_t stderr_proc = 0; int err_is_running = 0;
    serv_proc_args stderr_arg; stderr_arg.mutex = req_mutex; stderr_arg.is_running = &err_is_running;
    if (start_req.data_flag == CONN_CMD_STDERR) {
        err_is_running = 1;
        stderr_arg.socket = sckt; stderr_arg.pipe = stderr_pipe[0]; stderr_arg.end_proc = end_proc;
        stderr_arg.type = CONN_STDERR; stderr_arg.eof_type = CONN_STDERR_EOF;
        pthread_create(&stderr_proc, NULL, (void *(*)(void *))&server_process_output, &stderr_arg);
    }
    
    // Wait for proc end
    sem_wait(end_proc);
    close(stdin_pipe[1]); close(stdout_pipe[0]); close(stderr_pipe[0]);
    // time to end gracefully
    usleep(500000);
    
    void *thread_ret = 0;
    if (!in_is_running) {
        pthread_join(stdin_proc, &thread_ret);
        if (thread_ret)
            fprintf(stderr, "There was an error in the thread stdin_proc!\n");
    } else {
        pthread_mutex_lock(req_mutex);
        pthread_cancel(stdin_proc);
        pthread_mutex_unlock(req_mutex);
    }
    if (!out_is_running) {
        pthread_join(stdout_proc, &thread_ret);
        if (thread_ret)
            fprintf(stderr, "There was an error in the thread stdout_proc!\n");
    } else {
        pthread_mutex_lock(req_mutex);
        pthread_cancel(stdout_proc);
        pthread_mutex_unlock(req_mutex);
    }
    
    if (start_req.data_flag == CONN_CMD_STDERR) {
        if (!err_is_running) {
            pthread_join(stderr_proc, &thread_ret);
            if (thread_ret)
                fprintf(stderr, "There was an error in the thread stderr_proc!\n");
        } else {
            pthread_mutex_lock(req_mutex);
            pthread_cancel(stderr_proc);
            pthread_mutex_unlock(req_mutex);
        }
    }
    
    int status;
    pid_t wait_stat = 0;
    for (int i = 0; i < 10; i++) {
        if ((wait_stat = waitpid(child_pid, &status, WUNTRACED | WNOHANG)))
            break;
        usleep(1000);
    }
    /*
    pthread_mutex_lock(req_mutex);
    if (start_req.data_flag == CONN_CMD_STDERR)
        pthread_cancel(stderr_proc);
    pthread_cancel(stdin_proc); pthread_cancel(stdout_proc);
    pthread_mutex_unlock(req_mutex);
    */
    // Send exit status value
    if (!wait_stat) {
        killpg(pgrp, SIGKILL);
        const char *stat = "CMD Didn't stop on its own, so it was killed!\n";
        conn_data_info_t close_req;
        close_req.data_len = sizeof(stat);
        close_req.data_flag = CONN_STDOUT;
        socket_write_safe(sckt, &close_req, stat, req_mutex);
        close_req.data_len = -1;
        close_req.data_flag = CONN_CMD_CLOSE;
        sock_write(sckt, &close_req, sizeof(close_req));
    } else if (WIFEXITED(status)) {
        conn_data_info_t close_req;
        close_req.data_len = WEXITSTATUS(status);
        close_req.data_flag = CONN_CMD_CLOSE;
        sock_write(sckt, &close_req, sizeof(close_req));
    } else if (WIFSIGNALED(status)) {
        const char *stat = "CMD has been stopped due to signal\n";
        conn_data_info_t close_req;
        close_req.data_len = sizeof(stat);
        close_req.data_flag = CONN_STDOUT;
        socket_write_safe(sckt, &close_req, stat, req_mutex);
        close_req.data_len = WTERMSIG(status);
        close_req.data_flag = CONN_CMD_CLOSE;
        sock_write(sckt, &close_req, sizeof(close_req));
    } else if (WIFSTOPPED(status)) {
        killpg(pgrp, SIGKILL);
        const char *stat = "CMD has stopped and so it was killed it!\n";
        conn_data_info_t close_req;
        close_req.data_len = sizeof(stat);
        close_req.data_flag = CONN_STDOUT;
        socket_write_safe(sckt, &close_req, stat, req_mutex);
        close_req.data_len = -1;
        close_req.data_flag = CONN_CMD_CLOSE;
        sock_write(sckt, &close_req, sizeof(close_req));
    }
    
    free(cmd);
    // Thread closing cleanup
Thread_Exit:;
    size_t thread_num = arg->thread_num;
    close(sckt);
    pthread_mutex_destroy(req_mutex);
    sem_close(end_proc);
    sem_unlink(sem_name);
    free(arg->client_addr); free(req_mutex);
    free(arg);
    /* Thread closing intenal cleanup */
    pthread_mutex_lock(&free_thread_mutex);
    free_threads_ptr += 1;
    free_threads[free_threads_ptr] = thread_num;
    sem_post(thread_available);
    pthread_mutex_unlock(&free_thread_mutex);
}

void server_stop(int sig)
{
    if (main_socket > 0)
        close(main_socket);
}

int server_routine(const char *addr, const char *port)
{
    // Signal handling
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, &server_stop);
    signal(SIGTERM, &server_stop);
    signal(SIGHUP, &server_stop);
    
    struct sockaddr *addr_temp = NULL;
    socklen_t addr_temp_sz = 0;
    int sckt = getsockserver(addr, port, &addr_temp, &addr_temp_sz);
    main_socket = sckt;
    printf("Port is now %d\n", ((struct sockaddr_in *)addr_temp)->sin_port);
    if (sckt < 0)
        err(-1, "Socket creation error with code: %d\n", errno);
    
    if (listen(sckt, MAX_BACKLOG))
        err(-2, "Socket listening error with code: %d\n", errno);

    //memset(threads_info, 0, sizeof(thread_info_t) * MAX_SIML_CONNS);
    for (size_t i = 0; i < MAX_SIML_CONNS; i++) {
        threads_info[i].thread_id = 0;
        threads_info[i].end_proc = SEM_FAILED;
    }
    for (size_t i = 0; i < MAX_SIML_CONNS; i++)
        free_threads[i] = i;
    char sem_name[100]; snprintf(sem_name, 100, "/q14_server_threads_sem_%d", getpid());
    int res = sem_unlink(sem_name);
    if ((res < 0 && errno == ENOENT) || res == 0)
        thread_available = sem_open(sem_name, O_CREAT | O_EXCL, 700, MAX_SIML_CONNS);
    
    if (thread_available == SEM_FAILED) {
        close(sckt);
        perror("Couldn't Open Semaphore for thread_available. Error ");
        sem_unlink(sem_name);
        return -1;
    }
        
        
    
    while (1)
    {
        // Accept the incoming connection
        socklen_t addr_size = addr_temp_sz;
        struct sockaddr *addr_acc = malloc(addr_temp_sz);
        int new_sckt = accept(sckt, addr_acc, &addr_size);
        if (new_sckt < 0)
            break;
        
        // Wait for free threads!
        sem_wait(thread_available);
        //while (free_threads_ptr < 0)
        //    sleep(1);
        
        struct serv_args *arg = malloc(sizeof(struct serv_args));
        arg->socket = new_sckt; arg->client_addr = addr_acc;
        
        pthread_mutex_lock(&free_thread_mutex);
        
        // Semaphore for the thread
        arg->thread_num = free_threads[free_threads_ptr];
        pthread_t *thread_place = &(threads_info[arg->thread_num].thread_id) ;// threads + arg->thread_num;
        if (*thread_place) {
            pthread_join(*thread_place, NULL);
            //free(*thread_place);
        }
        char sem_name[100]; snprintf(sem_name, 100, "/q14_server_sem_%d_%d", getpid(), (int)arg->thread_num);
        int res = sem_unlink(sem_name);
        fprintf(stderr, sem_name);
        fprintf(stderr, "HEllp %d\n", errno);
        if ((res < 0 && errno != EACCES) || res == 0) {
            threads_info[arg->thread_num].end_proc = sem_open(sem_name, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
            perror("HEllp ");
        }
        
        if (threads_info[arg->thread_num].end_proc == SEM_FAILED) {
            *thread_place = 0;
            fprintf(stderr, "Couldn't Open Semaphore for thread No.: %zu.", arg->thread_num);
            perror("Error ");
            sem_unlink(sem_name);
            
            free_threads_ptr -= 1;
            pthread_mutex_unlock(&free_thread_mutex);
            continue;
        }
        
        printf("Creating Thread with No.: %zul\n", arg->thread_num);
        pthread_create(thread_place , NULL, (void *(*)(void *))&serv_request, (void *)arg);
        
        free_threads_ptr -= 1;
        pthread_mutex_unlock(&free_thread_mutex);
        
        //usleep(1000000);
    }
    
    // Cleanup
    sem_close(thread_available);
    //snprintf(sem_name, 100, "/q14_server_threads_sem_%d", getpid());
    sem_unlink(sem_name);
    
    /* Wait for all threads to finish */
    for (size_t i = 0; i < MAX_SIML_CONNS; i++) {
        if (sem_post(threads_info[i].end_proc)) {
            if (threads_info[i].thread_id)
                pthread_join(threads_info[i].thread_id, NULL);
        } else
            if (threads_info[i].thread_id)
                pthread_cancel(threads_info[i].thread_id);
        sem_close(threads_info[i].end_proc);
        snprintf(sem_name, 100, "/Q14_Server_%d_%zull", getpid(), i);
        sem_unlink(sem_name);
    }
    
    return 0;
}
