#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>

int main()
{
	struct sockaddr_in  server;
	memset(&server, 0, sizeof(server));

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0)
	{
		perror("socket");
		return 1;
	}

	server.sin_family = AF_INET;
	server.sin_port = htons(11724);
	inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

	if ((connect(sockfd, (struct sockaddr *)&server, sizeof(server))) < 0)
	{
		perror("Connect");
		return 1;
	}

	while (1)
	{
		printf("Enter a Message: ");
		fflush(stdout);

		char message [100];
		char buffer[100];

		if ((fgets(message, sizeof(message), stdin)) == NULL)
		{
			perror("Input");
			break;
		}

		if(strcmp(message, "quit\n") == 0)
		{
			printf("Connection Closed\n");
			break;
		}

		size_t total_sent = 0;
		size_t length = strlen(message);

		while (total_sent < length)
		{
			ssize_t sent = send (sockfd, &message[total_sent], length - total_sent, 0);
			
			if (sent > 0)
			{
				total_sent = total_sent + (size_t)sent;
				
				printf("Total Accepted Bytes: %zu\n",total_sent);
				printf("Total Bytes Remaining: %zu\n", length - total_sent);
				
				printf("Sent Bytes:%zd\n", sent);
		}
		else if (sent == 0)
		{
			printf("Zero Bytes Sent\n");
			break;
		}
		else
		{
			perror("Send:");
			break;
		}
		}

		ssize_t receive = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
		if (receive > 0)
		{
			printf("Received Bytes:%zd\n",receive);
			buffer[receive] = '\0';
			printf("Server: %s", buffer);
		}
		else if (receive == 0)
		{
			printf("Zero Bytes Received");
			break;
		}
		else
		{
			perror("Receive:");
			break;
		}
	}
	
	close (sockfd);

	return 0;
}
