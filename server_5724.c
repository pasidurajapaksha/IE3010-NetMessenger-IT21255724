#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include "config_5724.h"

int main(void)
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_fd == -1)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    printf("NetMessenger server | %s\n", REGISTRATION_NUMBER);
    printf("TCP socket created successfully.\n");
    printf("Socket descriptor: %d\n", listen_fd);

    int reuse_address = 1;

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_address, sizeof(reuse_address)) == -1)
    {
        perror("setsockopt");
        close(listen_fd);
        return EXIT_FAILURE;
    }

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

    if (listen(listen_fd, 8) == -1)
    {
        perror("listen");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    printf("Listening on %s:%d\n", SERVER_BIND_IP, SERVER_PORT);
    printf("Waiting for a client connection...\n");

    struct sockaddr_in client_address = {0};
    socklen_t client_address_length = sizeof(client_address);

    int client_fd = accept(
        listen_fd,
        (struct sockaddr *)&client_address,
        &client_address_length
    );

    if (client_fd == -1)
    {
        perror("accept");
        close(listen_fd);
        return EXIT_FAILURE;
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
    char receive_buffer[512];
    char command[1024];
    size_t command_length = 0;
    int stop_receiving = 0;

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

        if (received == 0)
        {
            printf("Client disconnected.\n");

            if (command_length > 0)
            {
                printf("Discarding an incomplete command.\n");
            }

            break;
        }

        for (ssize_t i = 0; i < received; i++)
        {
            char current = receive_buffer[i];

            if (current == '\n')
            {
                command[command_length] = '\0';
                printf("Complete command: %s\n", command);
                command_length = 0;
            }
            else if (current == '\0')
            {
                fprintf(stderr, "Invalid NUL byte in command.\n");
                stop_receiving = 1;
                break;
            }
            else if (command_length < sizeof(command) - 1)
            {
                command[command_length] = current;
                command_length++;
            }
            else
            {
                fprintf(stderr, "Command exceeds the buffer limit.\n");
                stop_receiving = 1;
                break;
            }
        }
    }
    if (close(client_fd) == -1)
    {
        perror("close client");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    printf("Client connection closed.\n");

    if (close(listen_fd) == -1)
    {
        perror("close listener");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
