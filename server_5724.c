#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#include "config_5724.h"

/* Send the entire response, even when send() writes only part of it.
   This helper is intended for the blocking socket used at this stage. */
static int send_response(int client_fd, const char *response)
{
    size_t length = strlen(response);
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent = send(
            client_fd,
            response + total_sent,
            length - total_sent,
            MSG_NOSIGNAL
        );

        if (sent == -1)
        {
            /* Retry if a signal interrupted the operation. */
            if (errno == EINTR)
            {
                continue;
            }

            perror("send");
            return -1;
        }

        if (sent == 0)
        {
            fprintf(stderr, "Unable to send the complete response.\n");
            return -1;
        }

        total_sent += (size_t)sent;
    }

    return 0;
}

int main(void)
{
    /* Create an IPv4 TCP socket. */
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_fd == -1)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    printf("NetMessenger server | %s\n", REGISTRATION_NUMBER);
    printf("TCP socket created successfully.\n");
    printf("Socket descriptor: %d\n", listen_fd);

    /* Allow address reuse when restarting the server. */
    int reuse_address = 1;

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_address, sizeof(reuse_address)) == -1)
    {
        perror("setsockopt");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    /* Configure the local IPv4 address and personalised port. */
    struct sockaddr_in server_address = {0};

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_BIND_IP,
                  &server_address.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid server binding address\n");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (bind(listen_fd, (struct sockaddr *)&server_address,
             sizeof(server_address)) == -1)
    {
        perror("bind");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    /* The backlog controls pending connections, not active clients. */
    if (listen(listen_fd, 8) == -1)
    {
        perror("listen");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    printf("Listening on %s:%d\n", SERVER_BIND_IP, SERVER_PORT);
    printf("Waiting for a client connection...\n");
    fflush(stdout);

    struct sockaddr_in client_address = {0};
    socklen_t client_address_length;
    int client_fd;

    /* Accept one client, retrying if interrupted by a signal. */
    for (;;)
    {
        client_address_length = sizeof(client_address);

        client_fd = accept(
            listen_fd,
            (struct sockaddr *)&client_address,
            &client_address_length
        );

        if (client_fd != -1)
        {
            break;
        }

        if (errno != EINTR)
        {
            perror("accept");
            close(listen_fd);
            return EXIT_FAILURE;
        }
    }

    char client_ip[INET_ADDRSTRLEN];

    if (inet_ntop(AF_INET, &client_address.sin_addr,
                  client_ip, sizeof(client_ip)) == NULL)
    {
        perror("inet_ntop");
        close(client_fd);
        close(listen_fd);
        return EXIT_FAILURE;
    }

    printf("Client connected from %s:%u\n",
           client_ip,
           (unsigned int)ntohs(client_address.sin_port));

    printf("Client socket descriptor: %d\n", client_fd);

    /* Preserve unfinished commands across recv() calls. */
    char receive_buffer[512];
    char command[1024];
    size_t command_length = 0;
    int stop_receiving = 0;

    /* Registration state belongs to this single connection. */
    int registered = 0;
    char username[32] = {0};

    while (!stop_receiving)
    {
        ssize_t received = recv(
            client_fd,
            receive_buffer,
            sizeof(receive_buffer),
            0
        );

        if (received == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("recv");
            break;
        }

        /* Zero means the peer has closed its sending side. */
        if (received == 0)
        {
            printf("Client disconnected.\n");

            if (command_length > 0)
            {
                printf("Discarding an incomplete command.\n");
            }

            break;
        }

        /* A received buffer may contain part of a line or many lines. */
        for (ssize_t i = 0; i < received; i++)
        {
            char current = receive_buffer[i];

            if (current == '\n')
            {
                command[command_length] = '\0';
                printf("Complete command: %s\n", command);

                char response[160];

                if (strncmp(command, "REGISTER ", 9) == 0)
                {
                    const char *requested_name = command + 9;
                    size_t name_length = strlen(requested_name);

                    /* Reserve one byte for the terminating NUL. */
                    int valid_name = name_length > 0 &&
                                     name_length < sizeof(username);

                    /* Allow ASCII letters, digits, underscores and hyphens. */
                    for (size_t j = 0; j < name_length; j++)
                    {
                        char c = requested_name[j];

                        if (!((c >= 'a' && c <= 'z') ||
                              (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') ||
                              c == '_' || c == '-'))
                        {
                            valid_name = 0;
                            break;
                        }
                    }

                    if (registered)
                    {
                        snprintf(response, sizeof(response),
                                 "ERR 005 ALREADY_REGISTERED %s\n",
                                 NODE_ID);
                    }
                    else if (!valid_name)
                    {
                        snprintf(response, sizeof(response),
                                 "ERR 006 INVALID_USERNAME %s\n",
                                 NODE_ID);
                    }
                    else
                    {
                        /* Copy the validated username, including its NUL. */
                        memcpy(username, requested_name, name_length + 1);
                        registered = 1;

                        snprintf(response, sizeof(response),
                                 "OK REGISTERED %s %s\n",
                                 username, NODE_ID);
                    }
                }
                else if (strcmp(command, "REGISTER") == 0)
                {
                    snprintf(response, sizeof(response),
                             "ERR 006 INVALID_USERNAME %s\n",
                             NODE_ID);
                }
                else if (!registered)
                {
                    snprintf(response, sizeof(response),
                             "ERR 007 REGISTER_REQUIRED %s\n",
                             NODE_ID);
                }
                else
                {
                    /* Other commands will be implemented in later stages. */
                    snprintf(response, sizeof(response),
                             "ERR 008 UNKNOWN_COMMAND %s\n",
                             NODE_ID);
                }

                command_length = 0;

                if (send_response(client_fd, response) == -1)
                {
                    stop_receiving = 1;
                    break;
                }
            }
            else if (current == '\0')
            {
                /* NUL is invalid in a text command.
                   Binary file data will need a separate parser state. */
                fprintf(stderr, "Invalid NUL byte in command.\n");

                (void)send_response(
                    client_fd,
                    "ERR 009 INVALID_COMMAND " NODE_ID "\n"
                );

                stop_receiving = 1;
                break;
            }
            else if (command_length < sizeof(command) - 1)
            {
                command[command_length++] = current;
            }
            else
            {
                /* Close after the error to avoid parsing a truncated line. */
                fprintf(stderr, "Command exceeds the buffer limit.\n");

                (void)send_response(
                    client_fd,
                    "ERR 010 COMMAND_TOO_LONG " NODE_ID "\n"
                );

                stop_receiving = 1;
                break;
            }
        }
    }

    int exit_status = EXIT_SUCCESS;

    /* Release both descriptors, even if the first close reports an error. */
    if (close(client_fd) == -1)
    {
        perror("close client");
        exit_status = EXIT_FAILURE;
    }

    if (close(listen_fd) == -1)
    {
        perror("close listener");
        exit_status = EXIT_FAILURE;
    }

    printf("Server stopped.\n");

    return exit_status;
}
