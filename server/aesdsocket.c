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

/* Global variable to indicate if a signal was caught */
volatile sig_atomic_t caught_signal = 0;

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

int main(int argc, char *argv[])
{
    int server_fd;
    int client_fd;
    struct sockaddr_in server_addr;    
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    struct sigaction sa;
    char ip_str[INET_ADDRSTRLEN];
    FILE *fp;
    FILE *read_fp;
    char send_buf[1024];
    size_t bytes_read;
    char *buf = NULL;
    size_t buf_len = 0;
    char recv_buf[1024];    
    ssize_t bytes_received;
    char *new_buf;
    char *newline_ptr;
    size_t packet_length;

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

    /* Start listening for connections with a backlog of 1 to allow 1 client at a time */
    if (listen(server_fd, 1) == -1)
    {
        perror("listen");
        close(server_fd);
        return -1;
    }

    printf("Server listening on port 9000\n");

    /* Loop until a signal is caught */
    while (!caught_signal)
    {
        /* Accept a new connection */
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd == -1)
        {
            perror("accept error");
            continue;
        }
        
        /* Convert client IP address to string */
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        
        /* Log accepted connection */
        syslog(LOG_INFO, "Accepted connection from %s", ip_str);

        /* Loop to receive data from client */
        while ((bytes_received = recv(client_fd, recv_buf, sizeof(recv_buf), 0)) > 0)
        {
            /* Reallocate buffer to accommodate new data */
            new_buf = realloc(buf, buf_len + bytes_received);
            
            /* Check if reallocation failed */
            if (new_buf == NULL)
            {
                syslog(LOG_ERR, "realloc failed");
                if (buf)
                {
                    free(buf);
                    buf = NULL;
                }
                buf_len = 0;
                break;
            }
            buf = new_buf;
            
            /* Copy received data to dynamic buffer */
            memcpy(buf + buf_len, recv_buf, bytes_received);
            
            /* Update buffer length */
            buf_len += bytes_received;            
            
            /* Check for a complete packet in the buffer */
            if ((newline_ptr = memchr(buf, '\n', buf_len)) != NULL)
            {
                /* Calculate length of the packet */
                packet_length = newline_ptr - buf + 1;
                
                /* Open file for appending */
                fp = fopen("/var/tmp/aesdsocketdata", "a");
                
                /* Check if file opened successfully */
                if (fp != NULL)
                {
                    if (fwrite(buf, 1, packet_length, fp) != packet_length)
                    {
                        syslog(LOG_ERR, "fwrite failed");
                    }
                    fclose(fp);
                }

                /* Open file for reading to send back content */
                read_fp = fopen("/var/tmp/aesdsocketdata", "r");
                
                /* Check if file opened successfully */
                if (read_fp != NULL)
                {
                    /* Read file in chunks */
                    while ((bytes_read = fread(send_buf, 1, sizeof(send_buf), read_fp)) > 0)
                    {
                        /* Send chunk to client */
                        if (send(client_fd, send_buf, bytes_read, 0) == -1)
                        {
                            syslog(LOG_ERR, "send failed");
                        }
                    }
                    fclose(read_fp);
                }

                /* Move remaining data to the beginning of the buffer */
                memmove(buf, newline_ptr + 1, buf_len - packet_length);
                /* Update buffer length */
                buf_len -= packet_length;
            }
        }

        if (buf)
        {
            free(buf);
            buf = NULL;
        }
        buf_len = 0;
        
        syslog(LOG_INFO, "Closed connection from %s", ip_str);
        close(client_fd);
    }

    /* Check if loop exited due to a signal */
    if (caught_signal)
    {
        syslog(LOG_INFO, "Caught signal, exiting");
        remove("/var/tmp/aesdsocketdata");
    }

    closelog();
    close(server_fd);
    return EXIT_SUCCESS;
}