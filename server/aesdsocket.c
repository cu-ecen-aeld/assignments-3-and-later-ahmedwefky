#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/queue.h>
#include <time.h>

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#if USE_AESD_CHAR_DEVICE == 1
#define AESD_DATA_FILE "/dev/aesdchar"
#else
#define AESD_DATA_FILE "/var/tmp/aesdsocketdata"
#endif

/* Thread data structure */
typedef struct thread_data_s
{
    pthread_t thread_id;
    int client_fd;
    struct sockaddr_in client_addr;
    int thread_complete;
    SLIST_ENTRY(thread_data_s) entries;
} thread_data_t;

/* Global variable to indicate if a signal was caught */
volatile sig_atomic_t caught_signal = 0;

/* Mutex for file synchronization */
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Linked list head */
SLIST_HEAD(thread_list, thread_data_s) head;

/* Signal handler function */
void signal_handler(int signo)
{
    /* Check if the signal is SIGINT or SIGTERM */
    if (signo == SIGINT || signo == SIGTERM)
    {
        /* Set the flag to indicate signal was caught */
        caught_signal = 1;
    }
}

/* Timestamp thread function */
#if USE_AESD_CHAR_DEVICE == 0
void *timestamp_thread(void *arg)
{
    (void)arg;
    time_t now;
    struct tm tm_info;
    char time_str[128];
    FILE *fp;
    int i;
    
    while (!caught_signal)
    {
        /* Wait 10 seconds, checking for signal every 100ms */
        for (i = 0; i < 100; i++)
        {
            if (caught_signal)
            {
                break;
            }
            
            usleep(100000);
        }

        if (caught_signal)
        {
            break;
        }

        now = time(NULL);
        localtime_r(&now, &tm_info);
        strftime(time_str, sizeof(time_str), "timestamp:%a, %d %b %Y %T %z\n", &tm_info);

        pthread_mutex_lock(&file_mutex);
        fp = fopen(AESD_DATA_FILE, "a");
        if (fp != NULL)
        {
            if (fputs(time_str, fp) == EOF)
            {
                syslog(LOG_ERR, "write timestamp failed");
            }
            fclose(fp);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}
#endif

/* Connection handling thread function */
void *connection_handler(void *arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    int client_fd = data->client_fd;
    char ip_str[INET_ADDRSTRLEN];
    char recv_buf[1024];
    char *buf = NULL;
    size_t buf_len = 0;
    ssize_t bytes_received;
    char *new_buf;
    char *newline_ptr;
    size_t packet_length;
    FILE *fp;
    FILE *read_fp;
    char send_buf[1024];
    size_t bytes_read;

    inet_ntop(AF_INET, &data->client_addr.sin_addr, ip_str, sizeof(ip_str));
    syslog(LOG_INFO, "Accepted connection from %s", ip_str);

    while ((bytes_received = recv(client_fd, recv_buf, sizeof(recv_buf), 0)) > 0)
    {
        new_buf = realloc(buf, buf_len + bytes_received);
        if (new_buf == NULL)
        {
            syslog(LOG_ERR, "realloc failed");
            if (buf) free(buf);
            buf = NULL;
            buf_len = 0;
            break;
        }
        buf = new_buf;
        memcpy(buf + buf_len, recv_buf, bytes_received);
        buf_len += bytes_received;

        while ((newline_ptr = memchr(buf, '\n', buf_len)) != NULL)
        {
            packet_length = newline_ptr - buf + 1;

            pthread_mutex_lock(&file_mutex);
            fp = fopen(AESD_DATA_FILE, "a");
            if (fp != NULL)
            {
                if (fwrite(buf, 1, packet_length, fp) != packet_length)
                {
                    syslog(LOG_ERR, "fwrite failed");
                }
                fclose(fp);
            }
            pthread_mutex_unlock(&file_mutex);

            read_fp = fopen(AESD_DATA_FILE, "r");
            if (read_fp != NULL)
            {
                while ((bytes_read = fread(send_buf, 1, sizeof(send_buf), read_fp)) > 0)
                {
                    if (send(client_fd, send_buf, bytes_read, 0) == -1)
                    {
                        syslog(LOG_ERR, "send failed");
                        break;
                    }
                }
                fclose(read_fp);
            }

            memmove(buf, newline_ptr + 1, buf_len - packet_length);
            buf_len -= packet_length;
        }
    }

    if (buf)
    {
        free(buf);
    }
    syslog(LOG_INFO, "Closed connection from %s", ip_str);
    close(client_fd);
    data->thread_complete = 1;
    return NULL;
}

int main(int argc, char *argv[])
{
    int server_fd;
    struct sockaddr_in server_addr;    
    struct sigaction sa;
#if USE_AESD_CHAR_DEVICE == 0
    pthread_t timer_thread;
#endif
    thread_data_t *entry = NULL;
    thread_data_t *cur;
    thread_data_t *prev;    
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;

    /* Open the system log */
    openlog("aesdsocket", LOG_PID, LOG_USER);
    
    /* Initialize signal action structure to zero */
    memset(&sa, 0, sizeof(sa));

    /* Set the handler function */
    sa.sa_handler = signal_handler;

    /* Register signal handler for SIGINT */
    if (sigaction(SIGINT, &sa, NULL) != 0)
    {
        perror("sigaction");
        return -1;
    }
    
    /* Register signal handler for SIGTERM */
    if (sigaction(SIGTERM, &sa, NULL) != 0)
    {
        perror("sigaction");
        return -1;
    }

    /* Create a stream socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        perror("socket");
        return -1;
    }

    /* Set SO_REUSEADDR socket option to reuse address to allow restarting server immediately */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    /* Clear the server address structure */
    memset(&server_addr, 0, sizeof(server_addr));
    /* Set address family to AF_INET (IPv4) */
    server_addr.sin_family = AF_INET;
    /* Accept connections from any IP address */
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    /* Set port number to 9000, converted to network byte order */
    server_addr.sin_port = htons(9000);

    /* Bind the socket to the specified address and port 9000 */
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("bind");
        close(server_fd);
        return -1;
    }

    /* Start listening for connections with a backlog of 1 to allow 1 client at a time */
    if (listen(server_fd, 1) == -1)
    {
        perror("listen");
        close(server_fd);
        return -1;
    }

    /* Check for daemon mode argument */
    if (argc > 1 && strcmp(argv[1], "-d") == 0)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        if (pid > 0)
        {
            exit(EXIT_SUCCESS);
        }

        /* Create a new session to daemonize the server or detach it from the terminal that started it */
        if (setsid() < 0)
        {
            perror("setsid");
            exit(EXIT_FAILURE);
        }

        /* Change working directory to root to avoid blocking file system unmounts */
        if (chdir("/") < 0)
        {
            perror("chdir");
            exit(EXIT_FAILURE);
        }

        /* Close standard file descriptors and redirect them to /dev/null */
        close(STDIN_FILENO);       /* Close FD 0 */
        close(STDOUT_FILENO);      /* Close FD 1 */
        close(STDERR_FILENO);      /* Close FD 2 */
        
        /* Open /dev/null. Since 0 is free, it gets FD 0. */
        if (open("/dev/null", O_RDWR) == -1)
        {
            syslog(LOG_ERR, "open /dev/null failed");
            exit(EXIT_FAILURE);
        }
        
        /* Duplicates FD 0. Since 1 is free, it gets FD 1. */
        if (dup(0) == -1)
        {
            syslog(LOG_ERR, "dup failed");
            exit(EXIT_FAILURE);
        }

        /* Duplicates FD 0. Since 2 is free, it gets FD 2. */
        if (dup(0) == -1)
        {
            syslog(LOG_ERR, "dup failed");
            exit(EXIT_FAILURE);
        }
        /* Now stdin, stdout, and stderr point to /dev/null */
        /* This ensures that future sockets and files get unique descriptors starting from 3 */
    }

    printf("Server listening on port 9000\n");

    /* Initialize list head */
    SLIST_INIT(&head);

    /* Start timestamp thread */
#if USE_AESD_CHAR_DEVICE == 0
    if (pthread_create(&timer_thread, NULL, timestamp_thread, NULL) != 0)
    {
        syslog(LOG_ERR, "Failed to create timestamp thread");
        close(server_fd);
        return -1;
    }
#endif

    /* Loop until a signal is caught */
    while (!caught_signal)
    {
        client_addr_len = sizeof(client_addr);
        
        /* Accept a new connection */
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd == -1)
        {
            if (errno != EINTR)
            {
                perror("accept error");
            }
            continue;
        }
        
        /* Create thread data */
        thread_data_t *new_thread = malloc(sizeof(thread_data_t));
        if (new_thread == NULL)
        {
            syslog(LOG_ERR, "malloc failed");
            close(client_fd);
            continue;
        }
        new_thread->client_fd = client_fd;
        new_thread->client_addr = client_addr;
        new_thread->thread_complete = 0;

        /* Insert into list */
        SLIST_INSERT_HEAD(&head, new_thread, entries);

        /* Create thread */
        if (pthread_create(&new_thread->thread_id, NULL, connection_handler, (void *)new_thread) != 0)
        {
            syslog(LOG_ERR, "pthread_create failed");
            close(client_fd);
            SLIST_REMOVE(&head, new_thread, thread_data_s, entries);
            free(new_thread);
            continue;
        }
        
        /* Cleanup completed threads */
        cur = SLIST_FIRST(&head);
        prev = NULL;
        while (cur != NULL)
        {
            if (cur->thread_complete)
            {
                pthread_join(cur->thread_id, NULL);
                if (prev == NULL)
                {
                    SLIST_REMOVE_HEAD(&head, entries);
                    free(cur);
                    cur = SLIST_FIRST(&head);
                }
                else
                {
                    SLIST_REMOVE(&head, cur, thread_data_s, entries);
                    free(cur);
                    cur = SLIST_NEXT(prev, entries);
                }
            }
            else
            {
                prev = cur;
                cur = SLIST_NEXT(cur, entries);
            }
        }
    }

    /* Check if loop exited due to a signal */
    if (caught_signal)
    {
        syslog(LOG_INFO, "Caught signal, exiting");
        
        /* Wait for the timestamp thread to exit */
#if USE_AESD_CHAR_DEVICE == 0
        pthread_join(timer_thread, NULL);
#endif

        /* Request exit from all threads */
        SLIST_FOREACH(entry, &head, entries)
        {
            shutdown(entry->client_fd, SHUT_RDWR);
        }

        /* Join all threads */
        while (!SLIST_EMPTY(&head))
        {
            entry = SLIST_FIRST(&head);
            pthread_join(entry->thread_id, NULL);
            SLIST_REMOVE_HEAD(&head, entries);
            free(entry);
        }

#if USE_AESD_CHAR_DEVICE == 0
        remove(AESD_DATA_FILE);
#endif
    }

    closelog();
    close(server_fd);
    pthread_mutex_destroy(&file_mutex);
    return EXIT_SUCCESS;
}