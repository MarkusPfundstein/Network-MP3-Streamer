#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/select.h>

/* send to remote to identify sockets */
const char *IDENT_STREAM_SOCK = "0x23231412";
const char *IDENT_CMD_SOCK  = "0x41235322";

/* comes from remote if its done with playing */
const char *REMOTE_CMD_DONE = "0x32423423";

typedef int (*request_callback)(char *, int, int);

typedef struct request_action_table_s {
    char* cmd;
    request_callback callback;
} request_action_table_t;

#define ERROR_FILE_NOT_FOUND 404
#define ERROR_SIZE 4

static int
make_socket_nonblock(int fd)
{
    int x;
    x = fcntl(fd, F_GETFL, 0);
    if (x < 0) {
        return x;
    }
    return fcntl(fd, F_SETFL, x | O_NONBLOCK);
}

static int 
send_sound_file(char *file, int len, int socket)
{
    FILE *fp;
    char buffer[4096];
    size_t bytes_read;
    int bytes_written;
    int err;

    fprintf(stderr, "send_sound_file : %s\n", file);

    fp = fopen(file, "r");
    if (!fp) {
        perror("fopen");
        return -1;
    }
    err = 0;
    bytes_read = 0;
    do {
        bytes_read = fread(buffer, 1, sizeof(buffer), fp);
        if (bytes_read <= 0) {
            if (bytes_read != 0) {
                perror("fread");
                err = 1;
            }
            break;
        } else {
            bytes_written = write(socket, buffer, bytes_read);
            if (bytes_written <= 0) {
                fprintf(stderr, "lost connection to mp3player\n");
                if (bytes_written != 0) {
                    perror("write");
                    err = 2;
                }
                break;
            }
        }
    } while (1);
    fclose(fp);
    fprintf(stderr, "send sound_file done\n");
    return err;
}

/*
static void
parse_request(char *buffer, int len, int socket)
{
    int cmd_len;
    static const request_action_table_t action_table[] = {
        { "send_audio", &send_sound_file },
    };

    fprintf(stderr, "parse_request %s %d\n", buffer, len);

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
}*/

static void*
cmd_thread_main(void *args)
{
    int socket;
    fd_set read_set;
    int bytes_written;
    int bytes_read;
    int num_ready;
    char buffer[256];
    socket = *(int*)args;
    free(args);

    make_socket_nonblock(STDIN_FILENO);
    make_socket_nonblock(socket);
    
    while (1) {
        FD_SET(STDIN_FILENO, &read_set);
        FD_SET(socket, &read_set);
        num_ready = select(20, &read_set, NULL, NULL, NULL);
        if (num_ready < 0) {
            perror("select");
            break;
        } else if (num_ready > 0) {
            if (FD_ISSET(STDIN_FILENO, &read_set)) {
                memset(buffer, 0, sizeof(buffer));
                bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer));
                if (bytes_read > 0) {
                    fprintf(stderr, "jo\n");
                    bytes_written = write(socket, buffer, strlen(buffer));
                    fprintf(stderr, "wrote %d to socket\n", bytes_written);
                    if (bytes_written <= 0) {
                        fprintf(stderr, "can't write cmd_sock\n");
                        if (bytes_written < 0) {
                            perror("cmd_socket write");
                        }
                        break;
                    }
                }
            }
            if (FD_ISSET(socket, &read_set)) {
                bytes_read = read(socket, buffer, sizeof(buffer));
                if (bytes_read > 0) {
                    if (strncmp(buffer,
                                REMOTE_CMD_DONE,
                                strlen(REMOTE_CMD_DONE)) == 0) {
                        fprintf(stderr, "DONE SIGNAL\n");
                        break;
                    }
                } else {
                    if (bytes_read < 0) {
                        perror("read");
                    }
                    break;
                }
            }
        }
    }
    fprintf(stderr, "leave cmd_thread\n");
    return NULL;
}

int
main(int argc, char** argv)
{
    int stream_fd;
    int cmd_fd;
    int portno;
    int err;
    int *thread_args;
    int bytes_written;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    pthread_t cmd_thread;

    err = 0;

    if (argc < 4) {
        fprintf(stderr, "usage %s hostname port file\n", argv[0]);
        return 1;
    }

    portno = atoi(argv[2]);
    stream_fd = socket(AF_INET, SOCK_STREAM, 0);
    cmd_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (stream_fd < 0 || cmd_fd < 0) {
        perror("socket");
        err = 1;
        goto clean;
    }

    server = gethostbyname(argv[1]);
    if (server == NULL) {
        perror("gethostbyname");
        err = 1;
        goto clean;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char*)server->h_addr,
          (char*)&serv_addr.sin_addr.s_addr,
          server->h_length);
    serv_addr.sin_port = htons(portno);

    if (connect(stream_fd, 
                (struct sockaddr*)&serv_addr, 
                sizeof(serv_addr)) < 0) {
        perror("stream_fd connect");
        goto clean;
    }
    
    bytes_written = write(stream_fd, 
                          IDENT_STREAM_SOCK, 
                          strlen(IDENT_STREAM_SOCK));
    if (bytes_written <= 0) {
        perror("write");
        goto clean;
    }

    if (connect(cmd_fd,
                (struct sockaddr*)&serv_addr,
                sizeof(serv_addr)) < 0) {
        perror("cmd_fd connect");
        goto clean;
    }

    bytes_written = write(cmd_fd, 
                          IDENT_CMD_SOCK, 
                          strlen(IDENT_CMD_SOCK));
    if (bytes_written <= 0) {
        perror("cmd_socket write");
        goto clean;
    }

    thread_args = malloc(sizeof(int));
    *thread_args = cmd_fd;
    if (pthread_create(&cmd_thread, NULL, cmd_thread_main, thread_args) < 0) {
        perror("pthread_create");
        goto clean;
    } 
    
    send_sound_file(argv[3], strlen(argv[3]), stream_fd);

    close(stream_fd);


clean:

    fprintf(stderr, "shutdown, waiting for thread...\n");
    pthread_join(cmd_thread, NULL);
    fprintf(stderr, "shutdown\n");

    close(cmd_fd);
    close(stream_fd);
    return 0;
}
