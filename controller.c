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
#include <pthread.h>

// Create a new socket for every car.
// Create a new thread for every car.

// Upon connecting to the controlle, the car will send a prefixed message:
// CAR {name} {lowest floor} {highest floor}

// Upon initialisation, or value changes, or every delay ms passed,
// the controller will receive a status message:
// STATUS {status} {current floor} {destination floor}
// The controller needs to keep track of this.

// The controller will receive messages from the call pad in the format:
// CALL {source floor} {destination floor}

typedef struct
{
    int carfd;
    char name[100];
    char current_floor[4];
    char destination_floor[4];
    char status[8];
    char lowest_floor[4];
    char highest_floor[5];
} car_information;

typedef struct
{
    char direction[2];
    char source_floor[4];
    char destination_floor[4];
} call_requests;

void recv_looped(int fd, void *buf, size_t sz);
char *receive_msg(int fd);
void *handle_car(void *arg);

int main()
{
    int listensockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listensockfd == -1)
    {
        perror("socket()");
        exit(1);
    }

    int opt_enable = 1;
    if (setsockopt(listensockfd, SOL_SOCKET, SO_REUSEADDR, &opt_enable, sizeof(opt_enable)) == -1)
    {
        perror("setsockopt()");
        exit(1);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listensockfd, (const struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        perror("bind()");
        exit(1);
    }

    if (listen(listensockfd, 10) == -1)
    {
        perror("listen()");
        exit(1);
    }

    for (;;)
    {
        struct sockaddr_in clientaddr;
        socklen_t clientaddr_len = sizeof(clientaddr);
        int clientfd = accept(listensockfd, (struct sockaddr *)&clientaddr, &clientaddr_len);
        if (clientfd == -1)
        {
            perror("accept()");
            exit(1);
        }

        char *msg = receive_msg(clientfd);

        if (strncmp(msg, "CAR", 3) == 0)
        {
            int *car_clientfd = malloc(sizeof(int));
            *car_clientfd = clientfd;

            pthread_t car_thread;
            if (pthread_create(&car_thread, NULL, handle_car, car_clientfd) != 0)
            {
                perror("pthread_create()");
                exit(1);
            }

            // Detach the thread so that its resources are automatically freed when it terminates
            if (pthread_detach(car_thread) != 0)
            {
                perror("pthread_detach()");
                exit(1);
            }
        }
        else if (strncmp(msg, "CALL", 4) == 0)
        {
            // Add the call request to the queue of struct_call request
            // The CALL thread will handle this list.
        }

        printf("Received this msg from client: %s\n", msg);
        free(msg);

        if (shutdown(clientfd, SHUT_RDWR) == -1)
        {
            perror("shutdown()");
            exit(1);
        }
        if (close(clientfd) == -1)
        {
            perror("close()");
            exit(1);
        }
    }
}

void *handle_car(void *arg)
{
    while (1)
    {
        // Receive new status.
        // Update the global linked list with this new status.
    }

    // Terminate thread once connection to that socketfd has been closed.

    pthread_exit(NULL); // Terminate the thread
}

void handle_call()
{
    // Receive call
    // Add destination, source, and direction to the linked list.
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
            exit(1);
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

// Open a single thread for handling all calls.

// If the message received is CAR
// Create a new thread that communicates with the socketfd
// Have thread running indefinitely receiving the status updates
// Terminate thread once connection to that socketfd has been closed.

// If the message received is CALL
// Add the call request to the list of struct_call request
// The CALL thread will handle this list.