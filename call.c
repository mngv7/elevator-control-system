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
#include <signal.h>
#include "network_utils.h"

#define BUFFER_SIZE 1024

int socket_fd = -1; // Global to allow cleanup in the signal handler

// Functions from network_utils.h
int establish_connection_client();
char *receive_msg(int fd);
void recv_looped(int fd, void *buf, size_t sz);
void send_message(int fd, const char *buf);
void send_looped(int fd, const void *buf, size_t sz);

int is_valid_floor(const char *floor);
void handle_signal(int signal);

int main(int argc, char **argv)
{
    // Register signal handler for SIGINT (Ctrl+C) and SIGTERM
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

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
    socket_fd = establish_connection_client();
    if (socket_fd == -1)
    {
        fprintf(stderr, "Error: Failed to establish connection to the server.\n");
        return EXIT_FAILURE;
    }

    char send_controller_buffer[BUFFER_SIZE];

    // Format and send message to the controller: CALL {source floor} {destination floor}
    snprintf(send_controller_buffer, sizeof(send_controller_buffer), "CALL %s %s", argv[1], argv[2]);
    send_message(socket_fd, send_controller_buffer);

    // Blocking function to wait for the controller's response.
    char *msg_from_controller = receive_msg(socket_fd);
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

    // Close the connection.
    if (close(socket_fd) == -1)
    {
        perror("close()");
        return EXIT_FAILURE;
    }

    socket_fd = -1; // Reset after closing

    return EXIT_SUCCESS;
}

// Signal handler function to close the socket and exit gracefully
void handle_signal(int signal)
{
    if (socket_fd != -1)
    {
        printf("\nReceived signal %d, closing the connection...\n", signal);
        close(socket_fd); // Close the socket
        socket_fd = -1;   // Reset the socket descriptor to avoid double closing
    }

    exit(EXIT_FAILURE); // Exit after cleanup
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
