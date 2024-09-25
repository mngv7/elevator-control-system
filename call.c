#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

void send_looped(int fd, const void *buf, size_t sz)
{
    const char *ptr = buf;
    size_t remain = sz;

    while (remain > 0)
    {
        ssize_t sent = write(fd, ptr, remain);
        if (sent == -1)
        {
            perror("write()");
            exit(1);
        }
        ptr += sent;
        remain -= sent;
    }
}

void send_controller_message(int fd, const char *buf)
{
    uint32_t len = htonl(strlen(buf));
    send_looped(fd, &len, sizeof(len));
    send_looped(fd, buf, strlen(buf));
}

int main(int argc, char **argv)
{
    // Check the number of command line arguments.
    if (argc != 3)
    {
        printf("Usage: {source floor} {destination floor}\n");
        exit(1);
    }

    // Create a socket.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd == -1)
    {
        perror("socket()");
        exit(1);
    }

    // Socket address setup
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

    // Establish connection
    if (connect(sockfd, (const struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        printf("Unable to connect to elevator system.\n");
        exit(EXIT_FAILURE);
    }

    // Read the command line arguments and store in corresponding strings.
    char current_floor[4];
    char destination_floor[4];

    for (int i = 0; i < strlen(argv[1]); i++)
    {
        if (strlen(argv[1]) > 3)
        {
            printf("Invalid floor(s) specified.\n");
            exit(EXIT_FAILURE);
        }
        current_floor[i] = argv[1][i];
    }

    for (int i = 0; i < strlen(argv[2]); i++)
    {
        if (strlen(argv[2]) > 3)
        {
            printf("Invalid floor(s) specified.\n");
            exit(EXIT_FAILURE);
        }
        destination_floor[i] = argv[2][i];
    }

    if (!strcmp(current_floor, destination_floor))
    {
        printf("You are already on that floor!\n");
        exit(EXIT_FAILURE);
    }

    printf("Current floor: %s\n", current_floor);
    printf("Destination floor: %s\n", destination_floor);

    // Send the message.
    char buf[1024];
    fgets(buf, 1023, stdin);
    send_controller_message(sockfd, buf);
    printf("Sent this msg to client: %s\n", buf);

    // Shut down read and write on the socket.
    if (shutdown(sockfd, SHUT_RDWR) == -1)
    {
        perror("shutdown()");
        exit(1);
    }

    // Close the socket.
    if (close(sockfd) == -1)
    {
        perror("close()");
        exit(1);
    }

    return EXIT_SUCCESS;
}