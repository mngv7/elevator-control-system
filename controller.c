#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include "network_utils.h"
#include "common.h"

typedef struct
{
    int car_fd;
    char name[100];
    char lowest_floor[4];
    char highest_floor[5];
    char current_floor[4];     // New field for current floor
    char destination_floor[4]; // New field for destination floor
    char status[8];
} car_information;

typedef struct
{
    char direction;
    char floor[4];
    int assigned_car_fd;
} call_requests;

// Linked list node structure
typedef struct CarNode
{
    car_information car_info;
    struct CarNode *next;
} CarNode;

typedef struct CallNode
{
    call_requests call;
    struct CallNode *next;
} CallNode;

typedef struct
{
    char *source_floor;
    char *destination_floor;
    int chosen_car_fd;
} CallInfo;

// Mutex to protect access to the linked list
pthread_mutex_t car_list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t call_list_mutex = PTHREAD_MUTEX_INITIALIZER;

// Head of the linked lists
CarNode *car_list_head = NULL;
CallNode *call_list_head = NULL;

void *handle_car(void *arg);
void *update_call_queue(void *arg);
void remove_car_from_list(int car_fd);
void add_car_to_list(car_information new_car);
void print_car_list();
void print_call_list();
char get_call_direction(const char *source, const char *destination);
void add_call_request(call_requests new_call);
char *get_and_pop_first_stop(int socket_fd);
int is_car_available(char *source_floor, char *destination_floor, CarNode *car);
CarNode *choose_car(char *source_floor, char *destination_floor);

int main()
{
    // Create a socket
    int listensockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listensockfd == -1)
    {
        perror("socket()");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    int opt_enable = 1;
    if (setsockopt(listensockfd, SOL_SOCKET, SO_REUSEADDR, &opt_enable, sizeof(opt_enable)) == -1)
    {
        perror("setsockopt()");
        exit(EXIT_FAILURE);
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
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(listensockfd, 10) == -1)
    {
        perror("listen()");
        exit(EXIT_FAILURE);
    }

    for (;;)
    {
        struct sockaddr_in clientaddr;
        socklen_t clientaddr_len = sizeof(clientaddr);
        int clientfd = accept(listensockfd, (struct sockaddr *)&clientaddr, &clientaddr_len);
        if (clientfd == -1)
        {
            perror("accept()");
            exit(EXIT_FAILURE);
        }

        char *msg = receive_msg(clientfd);

        if (strncmp(msg, "CAR", 3) == 0)
        {
            car_information new_car;
            sscanf(msg, "CAR %99s %3s %3s", new_car.name, new_car.lowest_floor, new_car.highest_floor);
            new_car.car_fd = clientfd;

            add_car_to_list(new_car);

            int *car_clientfd = malloc(sizeof(int));
            *car_clientfd = clientfd;

            pthread_t car_thread;
            if (pthread_create(&car_thread, NULL, handle_car, car_clientfd) != 0)
            {
                perror("pthread_create()");
                exit(EXIT_FAILURE);
            }
        }
        else if (strncmp(msg, "CALL", 4) == 0)
        {
            // printf("Call received: %s\n", msg);
            char source_floor[4], destination_floor[4];
            sscanf(msg, "CALL %3s %3s", source_floor, destination_floor);

            CarNode *chosen_car = choose_car(source_floor, destination_floor);
            if (chosen_car == NULL)
            {
                send_message(clientfd, "UNAVAILABLE\n");
            }
            else
            {
                CallInfo *call_info = malloc(sizeof(CallInfo));
                call_info->source_floor = strdup(source_floor);
                call_info->destination_floor = strdup(destination_floor);
                call_info->chosen_car_fd = chosen_car->car_info.car_fd;

                pthread_t call_thread;
                if (pthread_create(&call_thread, NULL, update_call_queue, call_info) != 0)
                {
                    perror("pthread_create() for handle_call");
                    free(call_info);
                    exit(EXIT_FAILURE);
                }

                char msg_to_client[110];
                snprintf(msg_to_client, sizeof(msg_to_client), "CAR %s\n", chosen_car->car_info.name);
                send_message(clientfd, msg_to_client);
            }
        }

        free(msg);
    }
}

CarNode *choose_car(char *source_floor, char *destination_floor)
{
    CarNode *current = car_list_head;
    while (current != NULL)
    {
        if (is_car_available(source_floor, destination_floor, current))
        {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

int is_car_available(char *source_floor, char *destination_floor, CarNode *car)
{
    if (car_list_head == NULL)
    {
        return 0;
    }

    char *highest_floor = car->car_info.highest_floor;
    char *lowest_floor = car->car_info.lowest_floor;

    // Check if source or destination floor is above highest floor
    if ((get_call_direction(highest_floor, source_floor) == 'U') ||
        (get_call_direction(highest_floor, destination_floor) == 'U'))
    {
        return 0;
    }

    // Check if source or destination floor is below lowest floor
    if ((get_call_direction(lowest_floor, source_floor) == 'D') ||
        (get_call_direction(lowest_floor, destination_floor) == 'D'))
    {
        return 0;
    }

    return 1;
}

int has_call_for_car(int car_clientfd)
{
    CallNode *current = call_list_head;
    while (current != NULL)
    {
        if (current->call.assigned_car_fd == car_clientfd)
        {
            return 1; // Found a call for this car
        }
        current = current->next;
    }
    return 0; // No call for this car
}

void *status_checking_thread(void *arg)
{
    int car_clientfd = *((int *)arg);
    free(arg);

    pthread_mutex_lock(&car_list_mutex);
    CarNode *car_node = car_list_head;
    while (car_node != NULL)
    {
        if (car_node->car_info.car_fd == car_clientfd)
        {
            break;
        }
        car_node = car_node->next;
    }
    pthread_mutex_unlock(&car_list_mutex);

    char status[8];
    char current_floor[4];
    char destination_floor[4];

    while (1)
    {
        char *msg = receive_msg(car_clientfd);

        sscanf(msg, "STATUS %s %s %s", status, current_floor, destination_floor);

        pthread_mutex_lock(&car_list_mutex);
        strcpy(car_node->car_info.current_floor, current_floor);
        strcpy(car_node->car_info.destination_floor, destination_floor);
        strcpy(car_node->car_info.status, status);
        pthread_mutex_unlock(&car_list_mutex);

        // Check for emergency or individual service messages
        if (strcmp(msg, "EMERGENCY") == 0 || strcmp(msg, "INDIVIDUAL SERVICE") == 0)
        {
            break;
        }

        free(msg);
    }

    shutdown(car_clientfd, SHUT_RDWR);
    close(car_clientfd);
    remove_car_from_list(car_clientfd);
    pthread_exit(NULL);
}

void *handle_car(void *arg)
{
    int car_clientfd = *((int *)arg);
    free(arg);

    if (pthread_detach(pthread_self()) != 0)
    {
        perror("pthread_detach()");
        exit(EXIT_FAILURE);
    }

    // Access the car's information
    CarNode *car_node = car_list_head;
    while (car_node != NULL)
    {
        if (car_node->car_info.car_fd == car_clientfd)
        {
            break; // Found the correct car
        }
        car_node = car_node->next;
    }

    char dispatched_floor[4];

    int *status_fd = malloc(sizeof(int));
    *status_fd = car_clientfd;
    pthread_t status_thread;
    if (pthread_create(&status_thread, NULL, status_checking_thread, status_fd) != 0)
    {
        perror("pthread_create() for status_checking_thread");
        exit(EXIT_FAILURE);
    }

    while (1)
    {
        pthread_mutex_lock(&call_list_mutex);

        if ((strcmp(car_node->car_info.current_floor, car_node->car_info.destination_floor) == 0) ||
            (strcmp(car_node->car_info.status, "Opening") == 0))
        {
            char *next_stop = get_and_pop_first_stop(car_clientfd);

            if (strcmp(next_stop, "E") != 0) // If there's a valid next stop
            {
                snprintf(dispatched_floor, sizeof(dispatched_floor), "%s", next_stop);
                char msg_to_car[10];
                snprintf(msg_to_car, sizeof(msg_to_car), "FLOOR %s", dispatched_floor);
                send_message(car_clientfd, msg_to_car); // Dispatch the floor

                free(next_stop); // Free the allocated memory for next_stop
            }
        }

        pthread_mutex_unlock(&call_list_mutex);
        usleep(1000); // Sleep to avoid busy waiting
    }
    pthread_exit(NULL);
}

// Function: Retrieves and removes the first stop assigned to the specified car.
// Argument: socket_fd - the file descriptor for the car requesting the stop.
// Returns: A pointer to the floor string of the stop, or "E" if no stop is found or the list is empty.
char *get_and_pop_first_stop(int socket_fd)
{
    if (call_list_head == NULL)
    {
        return "E"; // Return "E" if the list is empty
    }

    CallNode *current = call_list_head;
    CallNode *previous = NULL;

    while (current != NULL)
    {
        // Check if the current call is assigned to the requested car
        if (current->call.assigned_car_fd == socket_fd)
        {
            char *first_floor = malloc(strlen(current->call.floor) + 1);
            if (first_floor == NULL)
            {
                return "Memory allocation failed"; // Handle memory allocation failure
            }
            strcpy(first_floor, current->call.floor);

            // Remove the node from the linked list
            if (previous == NULL)
            {
                call_list_head = call_list_head->next;
            }
            else
            {
                previous->next = current->next;
            }

            free(current); // Free the memory allocated for the removed node

            return first_floor; // Return the floor value of the deleted node
        }
        previous = current;
        current = current->next;
    }

    return "E"; // Return "E" if no matching FD was found
}

// Function: Updates the call queue with source and destination floor requests.
// Argument: A void pointer that is cast to a CallInfo pointer.
// Returns: void
void *update_call_queue(void *arg)
{
    CallInfo *call_info = (CallInfo *)arg;

    // Create call requests for source and destination
    call_requests source_call = {get_call_direction(call_info->source_floor, call_info->destination_floor), "", call_info->chosen_car_fd};
    strcpy(source_call.floor, call_info->source_floor);
    add_call_request(source_call);

    call_requests destination_call = {source_call.direction, "", call_info->chosen_car_fd};
    strcpy(destination_call.floor, call_info->destination_floor);
    add_call_request(destination_call);

    // Free allocated memory for floor strings and CallInfo structure
    free(call_info->source_floor);
    free(call_info->destination_floor);
    free(call_info);

    pthread_exit(NULL);
}


// Function: takes a call and adds it to the queue ensuring floors of the same direction
// are together, while have D floors in descending order and U floors in ascending.
// Arguments:
// new_call - a struct containing the floor, direction, and assigned car.
// Returns: void.
void add_call_request(call_requests new_call)
{
    CallNode *new_node = malloc(sizeof(CallNode));
    new_node->call = new_call;
    new_node->next = NULL;

    pthread_mutex_lock(&call_list_mutex);

    // If the call list is empty
    if (call_list_head == NULL)
    {
        call_list_head = new_node;
        pthread_mutex_unlock(&call_list_mutex);
        return;
    }

    CallNode *current = call_list_head;
    CallNode *previous = NULL;

    // Handle insertion for Upward Call (U)
    if (new_call.direction == 'U')
    {
        while (current != NULL)
        {
            if (current->call.direction == 'U')
            {
                if (get_call_direction(current->call.floor, new_call.floor) == 'D')
                {
                    // Insert before the current node
                    if (previous == NULL)
                    {
                        // Inserting at the head
                        new_node->next = call_list_head;
                        call_list_head = new_node;
                    }
                    else
                    {
                        previous->next = new_node;
                        new_node->next = current;
                    }
                    pthread_mutex_unlock(&call_list_mutex);
                    return;
                }
            }
            else if (current->call.direction == 'D')
            {
                // Insert before the first downward call
                if (previous == NULL)
                {
                    new_node->next = call_list_head;
                    call_list_head = new_node;
                }
                else
                {
                    previous->next = new_node;
                    new_node->next = current;
                }
                pthread_mutex_unlock(&call_list_mutex);
                return;
            }
            previous = current;
            current = current->next;
        }
    }
    // Handle insertion for Downward Call (D)
    else if (new_call.direction == 'D')
    {
        while (current != NULL)
        {
            if (current->call.direction == 'D')
            {
                if (get_call_direction(current->call.floor, new_call.floor) == 'U')
                {
                    // Insert before the current node
                    if (previous == NULL)
                    {
                        // Inserting at the head
                        new_node->next = call_list_head;
                        call_list_head = new_node;
                    }
                    else
                    {
                        previous->next = new_node;
                        new_node->next = current;
                    }
                    pthread_mutex_unlock(&call_list_mutex);
                    return;
                }
            }
            else if (current->call.direction == 'U')
            {
                // Insert before the first upward call
                if (previous == NULL)
                {
                    new_node->next = call_list_head;
                    call_list_head = new_node;
                }
                else
                {
                    previous->next = new_node;
                    new_node->next = current;
                }
                pthread_mutex_unlock(&call_list_mutex);
                return;
            }
            previous = current;
            current = current->next;
        }
    }

    // If no suitable position was found, append to the end of the list
    if (previous != NULL)
    {
        previous->next = new_node;
    }
    else
    {
        call_list_head = new_node;
    }

    pthread_mutex_unlock(&call_list_mutex);
}

// Function: Adds a new car the to car linked list.
// Arguments: new_car - struct containing important information about the available cars.
// Returns: void.
void add_car_to_list(car_information new_car)
{
    pthread_mutex_lock(&car_list_mutex);

    CarNode *new_node = (CarNode *)malloc(sizeof(CarNode));
    if (new_node == NULL)
    {
        perror("malloc()");
        pthread_mutex_unlock(&car_list_mutex);
        return;
    }

    new_node->car_info = new_car;
    new_node->next = car_list_head;

    car_list_head = new_node;

    pthread_mutex_unlock(&car_list_mutex);
}

// Function: Removes a car from the car linked list.
// Arguments: car_fd - the file descriptor for the car to be removed.
// Returns: void
void remove_car_from_list(int car_fd)
{
    pthread_mutex_lock(&car_list_mutex);

    CarNode *current = car_list_head;
    CarNode *prev = NULL;

    // Loop through the car linked list
    while (current != NULL)
    {
        // If found a matching car.
        if (current->car_info.car_fd == car_fd)
        {
            if (prev == NULL)
            {
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

    pthread_mutex_unlock(&car_list_mutex);
}