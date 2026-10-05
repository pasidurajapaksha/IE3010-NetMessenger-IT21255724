#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>

#include "config_5724.h"
#include "file_io_5724.h"
#include <time.h>

#define COMMAND_SIZE 1024
#define RESPONSE_SIZE 2048
#define OUTPUT_SIZE FILE_QUEUE_SIZE

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1)
    {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Format user/room lists on two display lines; wire responses stay one line. */
static void display_response(const char *response)
{
    if (strncmp(response, "OK USERS ", 9) == 0 ||
        strncmp(response, "OK ROOMS ", 9) == 0)
    {
        printf("Server: OK\n");
        printf("%s\n", response + 3);
    }
    else if (strncmp(response, "MSG ", 4) == 0)
    {
        printf("%s\n", response);
    }
    else
    {
        printf("Server: %s\n", response);
    }
}

static char local_username[32] = "unregistered";
static int local_registered;
static unsigned char *download_data;
static size_t download_size, download_received;
static char download_sender[32], download_name[128];
static time_t download_activity;

static void finish_download(void)
{
    if (file_save("downloads", local_username, download_sender,
                  download_name, download_data, download_size) == -1)
        perror("save received file (existing files are not overwritten)");
    else
        printf("\nReceived file: downloads/%s/%s/%s (%zu bytes)\n",
               local_username, download_sender, download_name, download_size);
    free(download_data);
    download_data = NULL;
    download_activity = 0;
    printf("> ");
    fflush(stdout);
}

/* /send is a local convenience command, not a new wire protocol command.
   It calculates the length and emits SENDFILE plus unmodified file bytes. */
static int queue_input(const char *command, char *output, size_t *used)
{
    if (strncmp(command, "/send ", 6) != 0)
    {
        if (!strcmp(command, "/send") || !strcmp(command, "SENDFILE") ||
            !strncmp(command, "SENDFILE ", 9))
        {
            fprintf(stderr, "Use /send <target> <local-file-path>\n");
            return 0;
        }
        size_t n = strlen(command);
        if (n + 1 > OUTPUT_SIZE - *used)
        {
            fprintf(stderr, "Outgoing queue full; command not sent.\n");
            return 0;
        }
        memcpy(output + *used, command, n);
        *used += n;
        output[(*used)++] = '\n';
        return 0;
    }
    if (!local_registered)
    {
        fprintf(stderr, "Register successfully before sending files.\n");
        return 0;
    }

    char target[32], path[512], extra;
    if (sscanf(command + 6, "%31s %511s %c", target, path, &extra) != 2 ||
        !file_component_valid(target))
    {
        fprintf(stderr, "Usage: /send <target> <path-without-spaces>\n");
        return 0;
    }
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    if (!file_component_valid(name))
    {
        fprintf(stderr, "Unsafe filename. Use letters, digits, dot, _ or -.\n");
        return 0;
    }

    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd == -1) { perror("open upload"); return 0; }
    struct stat st;
    if (fstat(fd, &st) == -1 || !S_ISREG(st.st_mode) ||
        st.st_size < 0 || (unsigned long long)st.st_size > MAX_FILE_SIZE)
    {
        fprintf(stderr, "Upload must be a regular file no larger than 1 MiB.\n");
        close(fd);
        return 0;
    }
    size_t size = (size_t)st.st_size;
    unsigned char *data = malloc(size ? size : 1);
    if (!data) { close(fd); return -1; }
    size_t done = 0;
    while (done < size)
    {
        ssize_t n = read(fd, data + done, size - done);
        if (n == -1 && errno == EINTR) continue;
        if (n <= 0)
        {
            fprintf(stderr, "Unable to read complete upload.\n");
            free(data); close(fd); return 0;
        }
        done += (size_t)n;
    }
    close(fd);

    char header[256];
    int n = snprintf(header, sizeof(header), "SENDFILE %s %s %zu\n",
                     target, name, size);
    if (n < 0 || (size_t)n >= sizeof(header) ||
        (size_t)n + size > OUTPUT_SIZE - *used)
    {
        fprintf(stderr, "Outgoing queue full; file not sent.\n");
        free(data); return 0;
    }
    memcpy(output + *used, header, (size_t)n);
    *used += (size_t)n;
    memcpy(output + *used, data, size);
    *used += size;
    free(data);
    printf("Queued file: %s -> %s (%zu bytes)\n", name, target, size);
    return 0;
}

/* Return 1 to finish, 0 to continue, or -1 on error.
   Incomplete lines are preserved between receive operations. */
static int receive_messages(
    int socket_fd,
    char *response,
    size_t *response_length
)
{
    char buffer[8192];

    ssize_t received = recv(
        socket_fd,
        buffer,
        sizeof(buffer),
        0
    );

    if (received == -1)
    {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 0;
        }

        perror("recv");
        return -1;
    }

    if (received == 0)
    {
        if (*response_length > 0 || download_data)
        {
            fprintf(stderr, "\nServer closed during a response.\n");
            return -1;
        }

        printf("\nServer closed the connection.\n");
        return 1;
    }

    for (ssize_t i = 0; i < received; i++)
    {
        if (download_data)
        {
            size_t take = download_size - download_received;
            size_t available = (size_t)(received - i);
            if (take > available) take = available;
            memcpy(download_data + download_received, buffer + i, take);
            download_received += take;
            download_activity = time(NULL);
            i += (ssize_t)take - 1;
            if (download_received == download_size) finish_download();
            continue;
        }

        char current = buffer[i];

        if (current == '\n')
        {
            response[*response_length] = '\0';

            if (!strncmp(response, "OK REGISTERED ", 14))
            {
                char name[32], tag[32], extra;
                if (sscanf(response + 14, "%31s %31s %c", name, tag, &extra) == 2 &&
                    !strcmp(tag, NODE_ID) && file_component_valid(name))
                {
                    memcpy(local_username, name, strlen(name) + 1);
                    local_registered = 1;
                }
            }

            /* Recipient format: MSG FILE sender filename size, then raw bytes. */
            if (!strncmp(response, "MSG FILE ", 9))
            {
                char size_text[32], extra;
                if (!local_registered ||
                    sscanf(response + 9, "%31s %127s %31s %c",
                           download_sender, download_name, size_text, &extra) != 3 ||
                    !file_component_valid(download_sender) ||
                    !file_component_valid(download_name) ||
                    file_size_parse(size_text, &download_size) != 0)
                {
                    fprintf(stderr, "Invalid incoming file header.\n");
                    return -1;
                }
                download_data = malloc(download_size ? download_size : 1);
                if (!download_data) return -1;
                download_received = 0;
                download_activity = time(NULL);
                *response_length = 0;
                if (!download_size) finish_download();
                continue;
            }

            printf("\n");
            display_response(response);
            *response_length = 0;

            if (strcmp(response, "OK BYE " NODE_ID) == 0)
            {
                return 1;
            }

            printf("> ");
            fflush(stdout);
        }
        else if (current == '\0')
        {
            fprintf(stderr, "\nInvalid NUL byte in response.\n");
            return -1;
        }
        else if (*response_length < RESPONSE_SIZE - 1)
        {
            response[(*response_length)++] = current;
        }
        else
        {
            fprintf(stderr, "\nServer response is too long.\n");
            return -1;
        }
    }

    return 0;
}

/* Send pending commands without blocking on a slow server. */
static int flush_output(
    int socket_fd,
    char *output,
    size_t *output_length
)
{
    ssize_t sent = send(
        socket_fd,
        output,
        *output_length,
        MSG_NOSIGNAL
    );

    if (sent == -1)
    {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 0;
        }

        perror("send");
        return -1;
    }

    if (sent == 0)
    {
        return -1;
    }

    *output_length -= (size_t)sent;

    memmove(
        output,
        output + (size_t)sent,
        *output_length
    );

    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <server-ipv4-address>\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct sockaddr_in address = {0};

    address.sin_family = AF_INET;
    address.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, argv[1], &address.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid server IPv4 address\n");
        return EXIT_FAILURE;
    }

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd == -1)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    if (socket_fd >= FD_SETSIZE)
    {
        fprintf(stderr, "Socket descriptor exceeds select limit.\n");
        close(socket_fd);
        return EXIT_FAILURE;
    }

    /* Establish the connection before switching to nonblocking I/O. */
    if (connect(socket_fd, (struct sockaddr *)&address,
                sizeof(address)) == -1)
    {
        perror("connect");
        close(socket_fd);
        return EXIT_FAILURE;
    }

    if (set_nonblocking(socket_fd) == -1)
    {
        perror("fcntl");
        close(socket_fd);
        return EXIT_FAILURE;
    }

    printf("NetMessenger client | %s\n", REGISTRATION_NUMBER);
    printf("Connected to %s:%d\n", argv[1], SERVER_PORT);
    printf("Commands: REGISTER <name>, LIST, BCAST <message>,\n");
    printf("          PMSG <username> <message>, JOIN <room>, LEAVE <room>,\n");
    printf("          ROOMS, RMSG <room> <message>, QUIT\n");
    printf("Files: /send <user-or-room> <local-path> (max 1 MiB, no spaces in path)\n");
    printf("Room example: JOIN study, then RMSG study Hello everyone\n");
    printf("Join/leave notifications appear automatically as MSG JOIN/LEAVE.\n");
    printf("Press Ctrl+D on an empty line to disconnect locally.\n");
    printf("> ");
    fflush(stdout);

    char command[COMMAND_SIZE];
    size_t command_length = 0;
    int discard_input = 0;

    char response[RESPONSE_SIZE];
    size_t response_length = 0;

    static char output[OUTPUT_SIZE];
    size_t output_length = 0;

    int exit_status = EXIT_SUCCESS;

    for (;;)
    {
        fd_set read_fds;
        fd_set write_fds;

        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(socket_fd, &read_fds);

        if (output_length > 0)
        {
            FD_SET(socket_fd, &write_fds);
        }

        int highest_fd = socket_fd > STDIN_FILENO
                         ? socket_fd : STDIN_FILENO;

        struct timeval timeout = {1, 0};
        int ready = select(
            highest_fd + 1,
            &read_fds,
            &write_fds,
            NULL,
            &timeout
        );

        if (ready == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("select");
            exit_status = EXIT_FAILURE;
            break;
        }

        if (download_data && time(NULL) - download_activity >= 30)
        {
            fprintf(stderr, "Incoming file timed out; incomplete data discarded.\n");
            exit_status = EXIT_FAILURE;
            break;
        }

        /* Incoming messages are processed independently of keyboard input. */
        if (FD_ISSET(socket_fd, &read_fds))
        {
            int result = receive_messages(
                socket_fd,
                response,
                &response_length
            );

            if (result != 0)
            {
                if (result == -1)
                {
                    exit_status = EXIT_FAILURE;
                }

                break;
            }
        }

        if (FD_ISSET(socket_fd, &write_fds) && output_length > 0)
        {
            if (flush_output(socket_fd, output, &output_length) == -1)
            {
                exit_status = EXIT_FAILURE;
                break;
            }
        }

        /* read() avoids hidden stdio input buffering with select(). */
        if (FD_ISSET(STDIN_FILENO, &read_fds))
        {
            char current;

            ssize_t received = read(
                STDIN_FILENO,
                &current,
                1
            );

            if (received == -1)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                perror("read stdin");
                exit_status = EXIT_FAILURE;
                break;
            }

            if (received == 0)
            {
                /* EOF closes locally. Use QUIT for an acknowledged exit. */
                printf("\n");
                break;
            }

            if (current == '\n')
            {
                if (discard_input)
                {
                    fprintf(stderr,
                            "Invalid or oversized command; not sent.\n");
                }
                else
                {
                    command[command_length] = '\0';
                    if (queue_input(command, output, &output_length) == -1)
                    {
                        exit_status = EXIT_FAILURE;
                        break;
                    }
                }

                command_length = 0;
                discard_input = 0;

                printf("> ");
                fflush(stdout);
            }
            else if (!discard_input)
            {
                if (current == '\0' ||
                    command_length >= sizeof(command) - 1)
                {
                    discard_input = 1;
                }
                else
                {
                    command[command_length++] = current;
                }
            }
        }
    }

    if (close(socket_fd) == -1)
    {
        perror("close");
        exit_status = EXIT_FAILURE;
    }

    free(download_data);
    printf("Client stopped.\n");
    return exit_status;
}
