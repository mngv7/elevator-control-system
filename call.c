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

// Functions from network_utils.h
int establish_connection();
char *receive_msg(int fd);
void recv_looped(int fd, void *buf, size_t sz);
void send_message(int fd, const char *buf);
void send_looped(int fd, const void *buf, size_t sz);

int is_valid_floor(const char *floor);

int main(int argc, char **argv)
{
    // Check the number of command line arguments.
    if (argc != 3)
    {
        printf("Usage: {source floor} {destination floor}\n");
        return EXIT_FAILURE;
    }

    if (!is_valid_floor(argv[1]) || !is_valid_floor(argv[2]))
    {
        printf("Invalid floor(s) specified.\n");
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], argv[2]) == 0)
    {
        printf("You are already on that floor!\n");
        return EXIT_FAILURE;
    }

    // Establish connection and send the first message.
    int sockfd = establish_connection();

    char sendbuf[BUFFER_SIZE];

    // Format and send message to the controller: CALL {source floor} {destination floor}
    snprintf(sendbuf, sizeof(sendbuf), "CALL %s %s", argv[1], argv[2]);
    send_message(sockfd, sendbuf);

    // Blocking function to wait for the controller's response.
    char *msg_from_controller = receive_msg(sockfd);
    fflush(stdout);

    // Handle the controller's response and print appropriate message.
    if (strncmp(msg_from_controller, "UNAVAILABLE", 11) == 0)
    {
        printf("Sorry, no car is available to take this request.\n");
    }
    else if (strncmp(msg_from_controller, "CAR", 3) == 0) // CAR {car name}
    {
        printf("Car %s is arriving.\n", msg_from_controller + 4);
    }
    else
    {
        // Special case
        printf("Unexpected response: %s\n", msg_from_controller);
    }

    free(msg_from_controller);

    // Close the first connection.
    if (close(sockfd) == -1)
    {
        perror("close()");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

// Function: checks if the argument (floor) is valid.
// Returns: 1 if valid, else 0
int is_valid_floor(const char *floor)
{
    size_t len = strlen(floor);

    // Check for length and valid first character
    if (len == 0 || len > 3 || (isalpha(floor[0]) && floor[0] != 'B'))
    {
        return 0;
    }

    // Ensure all remaining characters are digits
    for (size_t i = 1; i < len; i++)
    {
        if (!isdigit(floor[i]))
        {
            return 0;
        }
    }

    return 1;
}
