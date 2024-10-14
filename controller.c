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

// Receive a call in the form:
// CALL 1 3
// The from to destination is going up, therefore, store the stops as:
// U1 U3 (see call_requests struct)
// Example:
// CALL 3 1
// D3 D1

// Create a linked-list storing the different stops, the linked list is:
// broken into 3 blocks: Up, Down, Up

// Stops with going up must be stored together in an ascending order.
// Stops going down must be stored together in a descending order.
// If an up stop is below the first entry in the first up block, then add
// it to the second up block.
typedef struct
{
    int car_fd;
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
    char floor[4];
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
void *handle_call(void *arg);
void remove_car_from_list(int car_fd);
void add_car_to_list(car_information new_car);
void update_car_values(int car_clientfd, char *status, char *current_floor, char *destination_floor);
void send_looped(int fd, const void *buf, size_t sz);
void send_message(int fd, const char *buf);
void print_car_list();
char get_call_direction(char *source, char *destination);

int main()
{
    // Create a socket
    int listensockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listensockfd == -1)
    {
        perror("socket()");
        exit(1);
    }

    // Set socket options
    int opt_enable = 1;
    if (setsockopt(listensockfd, SOL_SOCKET, SO_REUSEADDR, &opt_enable, sizeof(opt_enable)) == -1)
    {
        perror("setsockopt()");
        exit(1);
    }

    // Bind the socket to the address
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

    // Listen for incoming connections
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
            car_information new_car;

            int *car_clientfd = malloc(sizeof(int));
            *car_clientfd = clientfd;

            sscanf(msg, "CAR %99s %3s %3s", new_car.name, new_car.lowest_floor, new_car.highest_floor);
            new_car.car_fd = clientfd;

            add_car_to_list(new_car);
            pthread_t car_thread;
            if (pthread_create(&car_thread, NULL, handle_car, car_clientfd) != 0)
            {
                perror("pthread_create()");
                exit(1);
            }

            // Detach the thread so that its resources are automatically freed when it terminates
        }
        else if (strncmp(msg, "CALL", 4) == 0)
        {
            char source_floor[4];
            char destination_floor[4];

            if (car_list_head == NULL)
            {
                send_message(clientfd, "UNAVAILABLE\n");
            }

            sscanf(msg, "CALL %3s %3s", source_floor, destination_floor);
            char **call_info = malloc(sizeof(char *) * 2);
            call_info[0] = strdup(source_floor);      // Duplicate the string for source floor
            call_info[1] = strdup(destination_floor); // Duplicate the string for destination floor

            pthread_t call_thread;
            if (pthread_create(&call_thread, NULL, handle_call, call_info) != 0)
            {
                perror("pthread_create() for handle_call");
                free(call_info[0]); // Free allocated memory if thread creation fails
                free(call_info[1]);
                free(call_info);
                exit(1);
            }
        }

        free(msg);
    }
}

void *handle_call(void *arg)
{
    char **call_info = (char **)arg;
    char *source_floor = call_info[0];
    char *destination_floor = call_info[1];

    free(source_floor);
    free(destination_floor);
    free(call_info);

    pthread_exit(NULL);
}

char get_call_direction(char *source, char *destination)
{
    int source_int = -1;
    int destination_int = -1;

    if (source[0] == 'B')
    {
        source_int = atoi(source + 1);
    }

    if (destination[0] == 'B')
    {
        destination_int = atoi(destination + 1);
    }
    else
    {
        destination_int = atoi(destination);
    }

    if (source_int < destination_int)
    {
        return 'U'; // Up
    }
    else
    {
        return 'D'; // Down
    }
}

void *handle_car(void *arg)
{
    int car_clientfd = *((int *)arg); // Dereference the pointer correctly
    free(arg);                        // Free the dynamically allocated memory after dereferencing

    if (pthread_detach(pthread_self()) != 0)
    {
        perror("pthread_detach()");
        exit(1);
    }

    while (1)
    {
        // The following message is never received.
        char *msg = receive_msg(car_clientfd);
        if (msg == NULL)
        {
            break;
        }

        if (strncmp(msg, "STATUS", 6) == 0)
        {
            char status[8];
            char current_floor[4];
            char destination_floor[4];

            sscanf(msg, "STATUS %7s %3s %3s", status, current_floor, destination_floor);
            update_car_values(car_clientfd, status, current_floor, destination_floor);
        }
        else // If some other unexpected message was sent, terminate the car.
        {
            break;
        }

        free(msg);
    }

    if (shutdown(car_clientfd, SHUT_RDWR) == -1)
    {
        perror("shutdown()");
    }

    if (close(car_clientfd) == -1)
    {
        perror("close()");
    }

    remove_car_from_list(car_clientfd);
    pthread_exit(NULL); // Terminate the thread
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

void remove_car_from_list(int car_fd)
{
    pthread_mutex_lock(&list_mutex);

    CarNode *current = car_list_head;
    CarNode *prev = NULL;

    while (current != NULL)
    {
        if (current->car_info.car_fd == car_fd)
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

void update_car_values(int car_fd, char *status, char *current_floor, char *destination_floor)
{
    pthread_mutex_lock(&list_mutex);
    for (CarNode *curr = car_list_head; curr; curr = curr->next)
    {
        if (curr->car_info.car_fd == car_fd)
        {
            strncpy(curr->car_info.status, status, sizeof(curr->car_info.status));
            strncpy(curr->car_info.current_floor, current_floor, sizeof(curr->car_info.current_floor));
            strncpy(curr->car_info.destination_floor, destination_floor, sizeof(curr->car_info.destination_floor));
            break;
        }
    }
    pthread_mutex_unlock(&list_mutex);
}

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

void send_message(int fd, const char *buf)
{
    uint32_t len = htonl(strlen(buf));
    send_looped(fd, &len, sizeof(len));
    send_looped(fd, buf, strlen(buf));
}

void print_car_list()
{
    pthread_mutex_lock(&list_mutex);
    for (CarNode *curr = car_list_head; curr != NULL; curr = curr->next)
    {
        printf("Car Name: %s, Lowest Floor: %s, Highest Floor: %s\n",
               curr->car_info.name,
               curr->car_info.lowest_floor,
               curr->car_info.highest_floor);
    }
    pthread_mutex_unlock(&list_mutex);
}