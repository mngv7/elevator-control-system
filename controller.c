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

// Linked list node structure
typedef struct CarNode
{
    car_information car_info;
    struct CarNode *next;
} CarNode;

// Mutex to protect access to the linked list
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

// Head of the linked list
CarNode *car_list_head = NULL;

void recv_looped(int fd, void *buf, size_t sz);
char *receive_msg(int fd);
void *handle_car(void *arg);
void remove_car_from_list(char *car_name);
void add_car_to_list(car_information new_car);

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

void add_car_to_list(car_information new_car)
{
    pthread_mutex_lock(&list_mutex);

    CarNode *new_node = (CarNode *)malloc(sizeof(CarNode));
    if (new_node == NULL)
    {
        perror("malloc()");
        pthread_mutex_unlock(&list_mutex);
        return;
    }

    new_node->car_info = new_car;
    new_node->next = car_list_head;

    car_list_head = new_node;

    pthread_mutex_unlock(&list_mutex);
}

void remove_car_from_list(char *car_name)
{
    pthread_mutex_lock(&list_mutex);

    CarNode *current = car_list_head;
    CarNode *prev = NULL;

    while (current != NULL)
    {
        if (strcmp(current->car_info.name, car_name) == 0)
        {
            // Remove the car node from the list
            if (prev == NULL)
            {
                // Car is at the head of the list
                car_list_head = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            free(current);
            break;
        }
        prev = current;
        current = current->next;
    }

    pthread_mutex_unlock(&list_mutex);
}

void update_car_values(char *car_name, char *status, char *current_floor, char *destination_floor)
{
    pthread_mutex_lock(&list_mutex);

    CarNode *current = car_list_head;

    while (current != NULL)
    {
        if (strcmp(current->car_info.name, car_name) == 0)
        {
            strncpy(current->car_info.status, status, sizeof(current->car_info.status));
            strncpy(current->car_info.current_floor, current_floor, sizeof(current->car_info.current_floor));
            strncpy(current->car_info.destination_floor, destination_floor, sizeof(current->car_info.destination_floor));
        }
        current = current->next;
    }

    pthread_mutex_unlock(&list_mutex);
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