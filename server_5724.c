#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>

int main()
{
	struct sockaddr_in server;

	memset(&server, 0, sizeof(server));

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	char buffer [100];
	ssize_t sent;

	if (sockfd < 0)
	{
		perror("socket");
		return 1;
	}

	server.sin_family = AF_INET;
	server.sin_port = htons(8080);
	inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

	if ((bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0))
	{
		perror("bind");
		return 1;
	}
	
	if (listen(sockfd,5) < 0)
	{
		perror("listen");
		return 1;
	}

	int connfd = accept(sockfd, NULL, NULL);

	if (connfd < 0)
	{
		perror("accept");
		return 1;
	}

	while(1)
	{
		ssize_t received = recv(connfd, buffer, sizeof(buffer) - 1, 0);
		
		if (received > 0)
		{
			buffer[received] = '\0';
			
			printf("Received %zd bytes from client\n", received);
			printf("Message: %s\n", buffer);
			
			sent = send(connfd, buffer, received,0);
			
			if(sent < 0)
			{
				perror("send");
				break;
			}
			else
			{
				printf("%zd bytes sent\n", sent);
			}
		}
		
		else if (received == 0)
		{
			printf("Client has stopped Sending\n");
			break;
		}
		
		else
		{
			perror("recv");
			break;
		}
	}

	close(connfd);
	close(sockfd);

	return 0;
}
