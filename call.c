#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include "network_utils.h"

#define BUFFER_SIZE 1024

int establish_connection()
{
    // Create a new socket for each request.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        perror("socket()");
        exit(EXIT_FAILURE);
    }

    int opt_enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_enable, sizeof(opt_enable)) == -1)
    {
        perror("setsockopt()");
        exit(EXIT_FAILURE);
    }

    // Setup socket address for server connection.
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    const char *ipaddress = "127.0.0.1";

    if (inet_pton(AF_INET, ipaddress, &addr.sin_addr) != 1)
    {
        fprintf(stderr, "inet_pton(%s)\n", ipaddress);
        exit(EXIT_FAILURE);
    }

    // Establish the connection.
    if (connect(sockfd, (const struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        printf("Unable to connect to elevator system.\n");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

int is_valid_floor(const char *floor)
{
    if (strlen(floor) > 3)
        return 0;
    if (isalpha(floor[0]) && floor[0] != 'B')
        return 0;
    for (int i = 1; i < strlen(floor); i++)
    {
        if (!isdigit(floor[i]))
            return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    // Check the number of command line arguments.
    if (argc != 3)
    {
        printf("Usage: {source floor} {destination floor}\n");
        exit(EXIT_FAILURE);
    }

    if (!is_valid_floor(argv[1]) || !is_valid_floor(argv[2]))
    {
        printf("Invalid floor(s) specified.\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[1], argv[2]) == 0)
    {
        printf("You are already on that floor!\n");
        exit(EXIT_FAILURE);
    }

    // Establish connection and send the first message.
    int sockfd = establish_connection();
    char sendbuf[BUFFER_SIZE];
    snprintf(sendbuf, sizeof(sendbuf), "CALL %s %s", argv[1], argv[2]);
    send_message(sockfd, sendbuf);

    // Receive the first response.
    char *msg = receive_msg(sockfd);
    fflush(stdout);

    if (strncmp(msg, "UNAVAILABLE", 11) == 0)
    {
        printf("Sorry, no car is available to take this request.\n");
    }
    else if (strncmp(msg, "CAR", 3) == 0)
    {
        printf("Car %s is arriving.\n", msg + 4);
    }
    else
    {
        printf("Unexpected response: %s\n", msg);
    }
    free(msg);

    // Close the first connection.
    if (close(sockfd) == -1)
    {
        perror("close()");
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}
