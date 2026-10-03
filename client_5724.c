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

	char message [100];
	char buffer[100];
	ssize_t sent;
	ssize_t receive;

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0)
	{
		perror("socket");
		return 1;
	}

	server.sin_family = AF_INET;
	server.sin_port = htons(8080);
	inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

	if ((connect(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0))
	{
		perror("connect");
		return 1;
	}

	while(1)
	{
		printf("Enter a Message:");
		fflush(stdout);
		
		if (fgets(message, sizeof(message), stdin) == NULL)
		{
			printf("could not read input");
			close (sockfd);
			break;
		}

		if (strcmp(message, "quit\n") ==0)
		{
			printf("Closing the Connection\n");
			break;
		}
		
		sent = send(sockfd, message, strlen(message), 0);
		
		if (sent < 0)
		{
			perror("send");
			break;
		}
		
		else
		{
			printf("%zd bytes for sent\n",sent);
		}
		
		receive = recv (sockfd, buffer, 99, 0);
		
		if (receive > 0)
		{
			buffer[receive] = '\0';
			printf ("%zd Bytes Received from server\n", receive); 
			printf("Server: %s\n", buffer);
		}
		
		else if (receive == 0 )
		{
			printf("Server Stopped Sending\n");
		}
		
		else{
			perror("Receive");
			break;
		}
	}

	close(sockfd);

	return 0;
}
	
