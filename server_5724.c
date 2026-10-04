#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

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
