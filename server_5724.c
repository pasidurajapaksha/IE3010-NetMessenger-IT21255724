#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>

int main ()
{
	struct sockaddr_in server;
	memset(&server, 0, sizeof(server));

	//Socket Creation
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0)
	{
		perror("Socket");
		return 1;
	}

	server.sin_family = AF_INET;
	server.sin_port = htons(11724);
	inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

	//Bind
	if ((bind(sockfd, (struct sockaddr *)&server, sizeof(server))) < 0)
	{
		perror("Bind");
		return 1;
	}

	//Listen
	if ((listen(sockfd, 5)) < 0)
	{
		perror("Listen");
		return 1;
	}

	//Accept
	int connfd = accept(sockfd, NULL, NULL);

	if (connfd < 0)
	{
		perror("Accept");
		return 1;
	}

	ssize_t sent;
	ssize_t receive;

	//char message [] = "Hello\n";
	char buffer [100];

	while(1)
	{
		receive = recv(connfd, buffer, sizeof(buffer) - 1, 0);

		if (receive > 0)
		{
			buffer[receive] = '\0';
			printf("Client: %s", buffer);
			
			sent = send(connfd, buffer, receive, 0);

			if (sent > 0)
			{
				printf("Received Bytes:%zd\n", receive);
				printf("Sent Bytes:%zd\n", sent);
			}
			else if (sent == 0)
			{
				printf("Zero bytes Sent!\n");
			}
			else
			{
				perror("Send");
				break;
			}
		}
		
		else if (receive == 0)
		{
			printf("Client Stopped Sending\n");
			break;
		}
		
		else
		{
			perror("Receive");
			break;
		}
}
	close (sockfd);
	close (connfd);
	return 0;
}
