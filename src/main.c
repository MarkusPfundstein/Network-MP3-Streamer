#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/select.h>

static int g_go_on;

typedef void (*request_callback)(char *, int, int);

typedef struct request_action_table_s {
    char* cmd;
    request_callback callback;
} request_action_table_t;

#define ERROR_FILE_NOT_FOUND 404
#define ERROR_SIZE 4

static void 
get_sound_file_callback(char *video_file, int len, int socket)
{
    FILE *fp;
    char buffer[4096];
    size_t bytes_read;
    int bytes_written;

    fprintf(stderr, "get_sound_file callback: %d\n", len);
    fprintf(stderr, "%s", video_file);

    fp = fopen("/src/quiiStream/dre.mp3", "r");
    if (!fp) {
        perror("fopen");
        return;
    }
    bytes_read = 0;
    do {
        bytes_read = fread(buffer, 1, sizeof(buffer), fp);
        if (bytes_read <= 0) {
            if (bytes_read != 0) {
                perror("fread");
            }
            break;
        } else {
            bytes_written = write(socket, buffer, bytes_read);
            if (bytes_written <= 0) {
                if (bytes_written != 0) {
                    perror("write");
                }
                break;
            }
        }
    } while (1);
    fclose(fp);
}

void
parse_request(char *buffer, int len, int socket)
{
    int cmd_len;
    static const request_action_table_t action_table[] = {
        { "get_sound_file", &get_sound_file_callback },
    };

    int i;
    for (i = 0; i < sizeof(action_table) / sizeof(request_action_table_t); ++i) {
        cmd_len = strlen(action_table[i].cmd);
        if (cmd_len > len) {
            continue;
        }
        if (strncmp(buffer, action_table[i].cmd, cmd_len) == 0) {
            if (len - cmd_len > 1) {
                action_table[i].callback(buffer + cmd_len + 1, 
                                         len - cmd_len - 1, 
                                         socket);
            }
        }
    }
}

int
child_main(int socket, int argc, char **argv)
{
    int n;
    char buffer[4096];

    fprintf(stderr, "read request from socket: %d\n", socket);
    n = read(socket, buffer, sizeof(buffer));
    if (n <= 0) {
        if (n == 0) {
            fprintf(stderr, "socket %d closed connection\n", socket);
        } else {
            perror("read");
        }
    } else {
        parse_request(buffer, n, socket);
    }

   fprintf(stderr, "done with socket: %d\n", socket);
   return 0;
}

void
handle_sigint(int sig)
{
    g_go_on = 0;
}

int
main(int argc, char** argv)
{
    int sockfd;
    int newfd;
    int portno;
    int pid;
    int ready;
    fd_set accept_set;
    socklen_t socklen;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;

    signal(SIGCHLD, SIG_IGN);
    signal(SIGINT, handle_sigint);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    portno = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        return 1;
    }

    listen(sockfd, 5);
    socklen = sizeof(cli_addr);
    g_go_on = 1;
    while (g_go_on) {
        FD_SET(sockfd, &accept_set);
        ready = select(sockfd + 1, &accept_set, NULL, NULL, NULL);
        if (ready < 0) {
            perror("select");
            g_go_on = 0;
        } else if (ready > 0) {
            if (FD_ISSET(sockfd, &accept_set)) {
                newfd = accept(sockfd, (struct sockaddr *)&cli_addr, &socklen);
                if (newfd < 0) {
                    perror("accept");
                } else {
                    pid = fork();
                    if (pid < 0) {
                        perror("fork");
                    } else if (pid == 0) {
                        close(sockfd);
                        child_main(newfd, argc, argv);
                        close(newfd);
                        exit(0);
                    } else {
                        close(newfd);
                    }
                }
            }
        }
    }
    fprintf(stderr, "shutdown\n");
    close(sockfd);
    return 0;
}
