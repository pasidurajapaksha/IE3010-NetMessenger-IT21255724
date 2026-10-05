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

#define COMMAND_SIZE 1024
#define RESPONSE_SIZE 2048
#define OUTPUT_SIZE 4096

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1)
    {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Format LIST on two display lines, preserving the wire protocol. */
static void display_response(const char *response)
{
    if (strncmp(response, "OK USERS ", 9) == 0)
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

/* Return 1 to finish, 0 to continue, or -1 on error.
   Incomplete lines are preserved between receive operations. */
static int receive_messages(
    int socket_fd,
    char *response,
    size_t *response_length
)
{
    char buffer[512];

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
        if (*response_length > 0)
        {
            fprintf(stderr, "\nServer closed during a response.\n");
            return -1;
        }

        printf("\nServer closed the connection.\n");
        return 1;
    }

    for (ssize_t i = 0; i < received; i++)
    {
        char current = buffer[i];

        if (current == '\n')
        {
            response[*response_length] = '\0';

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
    printf("Commands: REGISTER <name>, LIST, BCAST <message>, QUIT\n");
    printf("Press Ctrl+D on an empty line to disconnect locally.\n");
    printf("> ");
    fflush(stdout);

    char command[COMMAND_SIZE];
    size_t command_length = 0;
    int discard_input = 0;

    char response[RESPONSE_SIZE];
    size_t response_length = 0;

    char output[OUTPUT_SIZE];
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

        int ready = select(
            highest_fd + 1,
            &read_fds,
            &write_fds,
            NULL,
            NULL
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
                else if (command_length + 1 >
                         sizeof(output) - output_length)
                {
                    fprintf(stderr,
                            "Outgoing queue full; command not sent.\n");
                }
                else
                {
                    memcpy(
                        output + output_length,
                        command,
                        command_length
                    );

                    output_length += command_length;
                    output[output_length++] = '\n';
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

    printf("Client stopped.\n");
    return exit_status;
}
