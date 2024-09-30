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

#define BUFFER_SIZE 1024

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
            exit(EXIT_FAILURE);
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

void recv_looped(int fd, void *buf, size_t sz)
{
    char *ptr = buf;
    size_t remain = sz;

    while (remain > 0)
    {
        ssize_t received = read(fd, ptr, remain);
        if (received == -1)
        {
            perror("read()");
            exit(EXIT_FAILURE);
        }
        ptr += received;
        remain -= received;
    }
}

char *receive_msg(int fd)
{
    uint32_t nlen;
    recv_looped(fd, &nlen, sizeof(nlen));
    uint32_t len = ntohl(nlen);

    char *buf = malloc(len + 1);
    buf[len] = '\0';
    recv_looped(fd, buf, len);
    return buf;
}

int main(int argc, char **argv)
{
    // Check the number of command line arguments.
    if (argc != 3)
    {
        printf("Usage: {source floor} {destination floor}\n");
        exit(EXIT_FAILURE);
    }

    // Create a socket.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        perror("socket()");
        exit(EXIT_FAILURE);
    }

    int opt_enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_enable, sizeof(opt_enable)) == -1) {
        perror("setsockopt()");
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

    // Check length before copying
    if (strlen(argv[1]) > 3 || strlen(argv[2]) > 3)
    {
        printf("Invalid floor(s) specified.\n");
        exit(EXIT_FAILURE);
    }

    strncpy(current_floor, argv[1], sizeof(current_floor) - 1);
    current_floor[sizeof(current_floor) - 1] = '\0'; // Null-terminate

    strncpy(destination_floor, argv[2], sizeof(destination_floor) - 1);
    destination_floor[sizeof(destination_floor) - 1] = '\0'; // Null-terminate

    if (strcmp(current_floor, destination_floor) == 0)
    {
        printf("You are already on that floor!\n");
        exit(EXIT_FAILURE);
    }

    // printf("Current floor: %s\n", current_floor);
    // printf("Destination floor: %s\n", destination_floor);

    // Send the message.
    char sendbuf[BUFFER_SIZE];
    snprintf(sendbuf, sizeof(sendbuf), "CALL %s %s", current_floor, destination_floor);
    send_controller_message(sockfd, sendbuf);
    // printf("Sent this msg to client: %s\n", buf);

    char *msg = receive_msg(sockfd);

    if (strcmp(msg, "UNAVAILABLE") == 0)
    {
        printf("Sorry, no car is available to take this request.\n");
    }
    else if (strncmp(msg, "CAR", 3) == 0)
    {
        printf("Car %s is arriving.\n", msg + 4);
    }
    else
    {
        printf("Unexpected response.\n");
    }

    // Shut down read and write on the socket.
    if (shutdown(sockfd, SHUT_RDWR) == -1)
    {
        perror("shutdown()");
        exit(EXIT_FAILURE);
    }

    // Close the socket.
    if (close(sockfd) == -1)
    {
        perror("close()");
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}