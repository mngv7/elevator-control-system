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
#include <pthread.h>
#include "network_utils.h"

typedef struct
{
    int car_fd;
    char name[100];
    char lowest_floor[4];
    char highest_floor[5];
} car_information;

typedef struct
{
    char direction;
    char floor[4];
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

// Mutex to protect access to the linked list
pthread_mutex_t car_list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t call_queue_list_mutex = PTHREAD_MUTEX_INITIALIZER;

// Head of the linked list
CarNode *car_list_head = NULL;
CallNode *call_list_head = NULL;

void *handle_car(void *arg);
void *update_call_queue(void *arg);
void remove_car_from_list(int car_fd);
void add_car_to_list(car_information new_car);
void print_car_list();
char get_call_direction(const char *source, const char *destination);
void add_call_request(call_requests new_call);
char *get_and_pop_first_stop();
int is_car_available(char *source_floor, char *destination_floor, CarNode *car);
CarNode *choose_car(char *source_floor, char *destination_floor);

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
        }
        else if (strncmp(msg, "CALL", 4) == 0)
        {
            char source_floor[4];
            char destination_floor[4];
            sscanf(msg, "CALL %3s %3s", source_floor, destination_floor);

            CarNode *chosen_car = choose_car(source_floor, destination_floor);

            if (chosen_car == NULL)
            {
                send_message(clientfd, "UNAVAILABLE\n");
            }
            else // If cars are available.
            {
                char **call_info = malloc(sizeof(char *) * 2);
                call_info[0] = strdup(source_floor);
                call_info[1] = strdup(destination_floor);

                pthread_t call_thread;
                if (pthread_create(&call_thread, NULL, update_call_queue, call_info) != 0)
                {
                    perror("pthread_create() for handle_call");
                    free(call_info[0]); // Free allocated memory if thread creation fails
                    free(call_info[1]);
                    free(call_info);
                    exit(1);
                }
                char msg_to_client[110]; // Allocate a buffer large enough to hold the formatted message

                // Format the message with the car name
                snprintf(msg_to_client, sizeof(msg_to_client), "CAR %s\n", chosen_car->car_info.name);

                // Send the formatted message
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
        if (is_car_available(source_floor, destination_floor, current) == 1)
        {
            return current; // Return the current node directly
        }

        current = current->next;
    }

    return NULL; // No available car found
}

int is_car_available(char *source_floor, char *destination_floor, CarNode *car)
{
    if (car_list_head == NULL)
    {
        return 0;
    }

    char *highest_floor = car->car_info.highest_floor;
    char *lowest_floor = car->car_info.lowest_floor;

    // If source or destination floor is above highest floor
    if ((get_call_direction(highest_floor, source_floor) == 'U') ||
        (get_call_direction(highest_floor, destination_floor) == 'U'))
    {
        return 0;
    }

    // If source or destination floor is below lowest floor
    if ((get_call_direction(lowest_floor, source_floor) == 'D') ||
        (get_call_direction(lowest_floor, destination_floor) == 'D'))
    {
        return 0;
    }

    return 1;
}

void *handle_car(void *arg)
{
    int car_clientfd = *((int *)arg); // Dereference the pointer correctly
    free(arg);                        // Free the dynamically allocated memory after dereferencing
    char dispatched_floor[4];

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

        char status[8];
        char current_floor[4];
        char destination_floor[4];

        sscanf(msg, "STATUS %7s %3s %3s", status, current_floor, destination_floor);

        while (1)
        {
            if (call_list_head != NULL)
            {
                break;
            }
        }

        strcpy(dispatched_floor, get_and_pop_first_stop());
        char msg_to_car[10];

        snprintf(msg_to_car, sizeof(msg_to_car), "FLOOR %s", dispatched_floor);

        send_message(car_clientfd, msg_to_car);

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

char *get_and_pop_first_stop()
{
    if (call_list_head == NULL)
    {
        return "E"; // Return "E" if the list is empty
    }

    // Store the floor value to return
    char *first_floor = malloc(strlen(call_list_head->call.floor) + 1); // Allocate memory for the floor string
    if (first_floor == NULL)
    {
        return "Memory allocation failed"; // Handle memory allocation failure
    }
    strcpy(first_floor, call_list_head->call.floor); // Copy the floor value to allocated memory

    // Save the current head node
    struct CallNode *temp = call_list_head;

    // Update the head to the next node
    call_list_head = call_list_head->next;

    // Free the old head node
    free(temp);

    return first_floor; // Return the floor value of the deleted head
}

void *update_call_queue(void *arg)
{
    char **call_info = (char **)arg;
    char *source_floor = call_info[0];
    char *destination_floor = call_info[1];

    char travel_direction = get_call_direction(source_floor, destination_floor);

    call_requests source_call;
    source_call.direction = travel_direction;
    strcpy(source_call.floor, source_floor);

    call_requests destination_call;
    destination_call.direction = travel_direction;
    strcpy(destination_call.floor, destination_floor);

    // Add the above structs to the linked list.
    add_call_request(source_call);
    add_call_request(destination_call);

    free(source_floor);
    free(destination_floor);
    free(call_info);

    pthread_exit(NULL);
}

char get_call_direction(const char *source, const char *destination)
{
    int source_int = -1;
    int destination_int = -1;

    // Validate input strings
    if (source == NULL || destination == NULL)
    {
        return 'E'; // Return 'E' for invalid input
    }

    // Convert source floor to integer
    if (source[0] == 'B')
    {
        source_int = -atoi(source + 1); // Negative value for basement floors
    }
    else
    {
        source_int = atoi(source);
    }

    // Convert destination floor to integer
    if (destination[0] == 'B')
    {
        destination_int = -atoi(destination + 1); // Negative value for basement floors
    }
    else
    {
        destination_int = atoi(destination);
    }

    // Determine the direction of the call
    if (source_int < destination_int)
    {
        return 'U'; // Up
    }
    else if (source_int > destination_int)
    {
        return 'D'; // Down
    }
    else
    {
        return 'E'; // Equal (same floor)
    }
}

void add_call_request(call_requests new_call)
{
    pthread_mutex_lock(&call_queue_list_mutex);

    // Check for duplicates before adding
    CallNode *current = call_list_head;
    while (current != NULL)
    {
        if (strcmp(current->call.floor, new_call.floor) == 0 &&
            current->call.direction == new_call.direction)
        {
            // Duplicate found, exit the function
            pthread_mutex_unlock(&call_queue_list_mutex);
            return;
        }
        current = current->next;
    }

    CallNode *new_node = (CallNode *)malloc(sizeof(CallNode));
    if (new_node == NULL)
    {
        perror("malloc() for CallNode");
        pthread_mutex_unlock(&call_queue_list_mutex);
        return;
    }

    new_node->call = new_call;
    new_node->next = NULL;

    // Insert the new node into the correct position based on direction
    if (call_list_head == NULL)
    {
        // List is empty
        call_list_head = new_node;
    }
    else
    {
        // Existing list, insert in sorted order
        CallNode *prev = NULL;
        current = call_list_head;

        while (current != NULL &&
               ((current->call.direction == 'U' && new_call.direction == 'U' &&
                 atoi(new_call.floor) > atoi(current->call.floor)) ||
                (current->call.direction == 'D' && new_call.direction == 'D' &&
                 atoi(new_call.floor) < atoi(current->call.floor))))
        {
            prev = current;
            current = current->next;
        }

        if (prev == NULL)
        {
            // Insert at head
            new_node->next = call_list_head;
            call_list_head = new_node;
        }
        else
        {
            // Insert in the middle or at the end
            new_node->next = current;
            prev->next = new_node;
        }
    }

    pthread_mutex_unlock(&call_queue_list_mutex);
}

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

void remove_car_from_list(int car_fd)
{
    pthread_mutex_lock(&car_list_mutex);

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

    pthread_mutex_unlock(&car_list_mutex);
}

void print_car_list()
{
    pthread_mutex_lock(&car_list_mutex);
    for (CarNode *curr = car_list_head; curr != NULL; curr = curr->next)
    {
        printf("Car Name: %s, Lowest Floor: %s, Highest Floor: %s\n",
               curr->car_info.name,
               curr->car_info.lowest_floor,
               curr->car_info.highest_floor);
    }
    pthread_mutex_unlock(&car_list_mutex);
}

void print_call_list()
{
    pthread_mutex_lock(&call_queue_list_mutex);
    for (CallNode *curr = call_list_head; curr != NULL; curr = curr->next)
    {
        printf("Call Direction: %c, Floor: %s\n", curr->call.direction, curr->call.floor);
    }
    pthread_mutex_unlock(&call_queue_list_mutex);
}