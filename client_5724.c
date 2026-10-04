#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "config_5724.h"

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

    if (close(socket_fd) == -1)
    {
        perror("close");
        return EXIT_FAILURE;
    }

    printf("Client socket closed.\n");

    return EXIT_SUCCESS;
}
