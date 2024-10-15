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

void recv_looped(int fd, void *buf, size_t sz);
char *receive_msg(int fd);
void *handle_car(void *arg);
void *update_call_queue(void *arg);
void remove_car_from_list(int car_fd);
void add_car_to_list(car_information new_car);
void send_looped(int fd, const void *buf, size_t sz);
void send_message(int fd, const char *buf);
void print_car_list();
char get_call_direction(char *source, char *destination);
void add_call_request(call_requests new_call);
char *get_and_pop_first_stop();
int is_car_available(char *source_floor, char *destination_floor);

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
            sscanf(msg, "CALL %3s %3s", source_floor, destination_floor);

            if (is_car_available(source_floor, destination_floor) == 0) // If no car available.
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
                snprintf(msg_to_client, sizeof(msg_to_client), "CAR %s\n", car_list_head->car_info.name);

                // Send the formatted message
                send_message(clientfd, msg_to_client);
            }
        }

        free(msg);
    }
}

int is_car_available(char *source_floor, char *destination_floor) // B2 3
{
    if (car_list_head == NULL)
    {
        return 0;
    }

    char *highest_floor = car_list_head->car_info.highest_floor; // 4
    char *lowest_floor = car_list_head->car_info.lowest_floor;   // B1

    printf("Highest floor: %s\n", highest_floor);
    printf("Lowest floor: %s\n", lowest_floor);

    printf("Source floor: %s\n", source_floor);
    printf("Destination floor: %s\n", destination_floor);

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

void print_call_list()
{
    pthread_mutex_lock(&call_queue_list_mutex);
    for (CallNode *curr = call_list_head; curr != NULL; curr = curr->next)
    {
        printf("Call Direction: %c, Floor: %s\n", curr->call.direction, curr->call.floor);
    }
    pthread_mutex_unlock(&call_queue_list_mutex);
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

char get_call_direction(char *source, char *destination)
{
    int source_int = -1;
    int destination_int = -1;

    // Convert source floor to integer
    if (source[0] == 'B')
    {
        source_int = atoi(source + 1);
    }
    else
    {
        source_int = atoi(source);
    }

    // Convert destination floor to integer
    if (destination[0] == 'B')
    {
        destination_int = atoi(destination + 1);
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