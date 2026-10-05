#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#include "config_5724.h"

/* Send all command bytes, including the terminating newline. */
static int send_command(int socket_fd, const char *command)
{
    size_t length = strlen(command);
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent = send(
            socket_fd,
            command + total_sent,
            length - total_sent,
            MSG_NOSIGNAL
        );

        if (sent == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("send");
            return -1;
        }

        if (sent == 0)
        {
            fprintf(stderr, "Unable to send the complete command.\n");
            return -1;
        }

        total_sent += (size_t)sent;
    }

    return 0;
}

/* Read exactly one response line.
   Byte-by-byte reading is simple for this initial request/response client.
   Later, we will use buffered receiving for asynchronous messages. */
static int receive_response(int socket_fd)
{
    char response[1024];
    size_t length = 0;

    for (;;)
    {
        char current;
        ssize_t received = recv(socket_fd, &current, 1, 0);

        if (received == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("recv");
            return -1;
        }

        if (received == 0)
        {
            if (length > 0)
            {
                fprintf(stderr, "Server closed during a response.\n");
                return -1;
            }

            printf("Server closed the connection.\n");
            return 0;
        }

        if (current == '\n')
        {
            response[length] = '\0';
            printf("Server: %s\n", response);
            return 1;
        }

        if (current == '\0')
        {
            fprintf(stderr, "Invalid NUL byte in server response.\n");
            return -1;
        }

        if (length >= sizeof(response) - 1)
        {
            fprintf(stderr, "Server response exceeds the buffer limit.\n");
            return -1;
        }

        response[length++] = current;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <server-ipv4-address>\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_address = {0};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, argv[1], &server_address.sin_addr) != 1)
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

    if (connect(socket_fd, (struct sockaddr *)&server_address,
                sizeof(server_address)) == -1)
    {
        perror("connect");
        close(socket_fd);
        return EXIT_FAILURE;
    }

    printf("NetMessenger client | %s\n", REGISTRATION_NUMBER);
    printf("Connected to %s:%d\n", argv[1], SERVER_PORT);
    printf("Enter a command, such as REGISTER pasidu.\n");
    printf("Press Ctrl+D on an empty input line to disconnect.\n");

    /* Allow up to 1023 command characters, a newline, and a NUL. */
    char command[1025];
    int exit_status = EXIT_SUCCESS;

    for (;;)
    {
        printf("> ");
        fflush(stdout);

        if (fgets(command, sizeof(command), stdin) == NULL)
        {
            if (ferror(stdin))
            {
                fprintf(stderr, "Unable to read terminal input.\n");
                exit_status = EXIT_FAILURE;
            }

            break;
        }

        /* Reject incomplete or oversized input rather than sending
           a command without the required newline. */
        if (strchr(command, '\n') == NULL)
        {
            int current;

            while ((current = getchar()) != '\n' && current != EOF)
            {
            }

            fprintf(stderr,
                    "Command too long or missing a newline; not sent.\n");

            if (ferror(stdin))
            {
                exit_status = EXIT_FAILURE;
                break;
            }

            if (feof(stdin))
            {
                break;
            }

            continue;
        }

        if (send_command(socket_fd, command) == -1)
        {
            exit_status = EXIT_FAILURE;
            break;
        }

        int result = receive_response(socket_fd);

        if (result <= 0)
        {
            if (result == -1)
            {
                exit_status = EXIT_FAILURE;
            }

            break;
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
