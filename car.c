#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include "network_utils.h"
#include "common.h"
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#define MILLISECOND 1000

char *status_names[] = {
    "Opening", "Open", "Closing", "Closed", "Between"};

// Store the car details:
typedef struct
{
    char name[100];
    char lowest_floor[4];
    char highest_floor[4];
    int delay;
} car_information;

// Global variables:
car_shared_mem *shared_mem;
car_information car_info;
char car_name[100] = "/car";
int shm_fd = -1;
int early_exit_delay = 0;
pthread_mutex_t early_exit_delay_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t delay_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t delay_cond = PTHREAD_COND_INITIALIZER;
int controller_sock_fd;

// Function definitions:
void terminate_shared_memory(int sig_num);
void *go_through_sequence(void *arg);
void *handle_button_press(void *arg);
void *individual_service_mode(void *arg);
char get_call_direction(const char *source, const char *destination);
void *connect_to_controller(void *arg);
void delay();

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        printf("Usage: {name} {lowest floor} {highest floor} {delay}\n");
        exit(1);
    }

    signal(SIGINT, terminate_shared_memory);

    // Ensure the car doesn't crash when write fails.
    signal(SIGPIPE, SIG_IGN);

    strcat(car_name, argv[1]);

    strcpy(car_info.name, argv[1]);
    strcpy(car_info.lowest_floor, argv[2]);
    strcpy(car_info.highest_floor, argv[3]);
    car_info.delay = atoi(argv[4]);

    // Unlink the shared memory in case it exists.
    shm_unlink(car_name);

    shm_fd = shm_open(car_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("shm_open");
        exit(1);
    }

    if (ftruncate(shm_fd, sizeof(car_shared_mem)) == -1)
    {
        perror("ftruncate");
        exit(1);
    }

    shared_mem = mmap(0, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_mem == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    // Initialize the mutex.
    pthread_mutexattr_t mutattr;
    pthread_mutexattr_init(&mutattr);
    pthread_mutexattr_setpshared(&mutattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shared_mem->mutex, &mutattr);
    pthread_mutexattr_destroy(&mutattr);

    // Initialize the condition variable.
    pthread_condattr_t condattr;
    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&shared_mem->cond, &condattr);
    pthread_condattr_destroy(&condattr);

    // Initialize the shared memory.
    strcpy(shared_mem->current_floor, argv[2]);
    strcpy(shared_mem->destination_floor, argv[2]);
    strcpy(shared_mem->status, status_names[3]); // Initially "Closed"
    shared_mem->open_button = 0;
    shared_mem->close_button = 0;
    shared_mem->door_obstruction = 0;
    shared_mem->overload = 0;
    shared_mem->emergency_stop = 0;
    shared_mem->individual_service_mode = 0;
    shared_mem->emergency_mode = 0;

    // Create handle button press thread
    pthread_t button_thread;
    if (pthread_create(&button_thread, NULL, handle_button_press, NULL) != 0)
    {
        perror("pthread_create() for button press");
        exit(EXIT_FAILURE);
    }

    // Handle the car state thread
    pthread_t go_through_sequence_thread;
    if (pthread_create(&go_through_sequence_thread, NULL, go_through_sequence, NULL) != 0)
    {
        perror("pthread_create() for sequence thread");
        exit(EXIT_FAILURE);
    }

    pthread_t individual_service_mode_thread;
    if (pthread_create(&individual_service_mode_thread, NULL, individual_service_mode, NULL) != 0)
    {
        perror("pthread_create() for individual service mode thread.");
        exit(EXIT_FAILURE);
    }

    pthread_t connect_to_controller_thread;
    if (pthread_create(&connect_to_controller_thread, NULL, connect_to_controller, NULL) != 0)
    {
        perror("pthread_create() for individual service mode thread.");
        exit(EXIT_FAILURE);
    }
    pthread_join(connect_to_controller_thread, NULL);

    pthread_join(button_thread, NULL);
    pthread_join(individual_service_mode_thread, NULL);
    pthread_join(go_through_sequence_thread, NULL);

    return 0;
}

// Function: handles operations for individual service mode.
void *individual_service_mode(void *arg)
{
    while (1)
    {
        pthread_mutex_lock(&shared_mem->mutex);
        if (shared_mem->individual_service_mode == 1)
        {
            pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);

            if (get_call_direction(car_info.highest_floor, shared_mem->destination_floor) == 'U')
            {
                strcpy(shared_mem->destination_floor, shared_mem->current_floor);
            }
            else if (strcmp(shared_mem->current_floor, shared_mem->destination_floor) != 0 &&
                     strcmp(shared_mem->status, "Closed") == 0)
            {
                strcpy(shared_mem->status, "Between");
                pthread_cond_broadcast(&shared_mem->cond);

                pthread_mutex_unlock(&shared_mem->mutex);
                delay();
                pthread_mutex_lock(&shared_mem->mutex);

                strcpy(shared_mem->current_floor, shared_mem->destination_floor);
                strcpy(shared_mem->status, "Closed");

                pthread_cond_broadcast(&shared_mem->cond);
            }
        }
        pthread_mutex_unlock(&shared_mem->mutex);
    }

    pthread_exit(NULL);
}

// Function: updates the car's status based on the button presses.
void *handle_button_press(void *arg)
{
    while (1)
    {
        pthread_mutex_lock(&shared_mem->mutex);
        pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);

        if (shared_mem->open_button)
        {
            shared_mem->open_button = 0;
            if (shared_mem->individual_service_mode == 1)
            {
                strcpy(shared_mem->status, "Open");
                pthread_cond_broadcast(&shared_mem->cond);
            }
            else if (strcmp(shared_mem->status, "Closing") == 0 || strcmp(shared_mem->status, "Closed") == 0)
            {
                strcpy(shared_mem->status, "Opening");
                pthread_cond_broadcast(&shared_mem->cond);
            }
        }
        else if (shared_mem->close_button)
        {
            shared_mem->close_button = 0;
            if (shared_mem->individual_service_mode == 1)
            {
                strcpy(shared_mem->status, "Closed");
                pthread_cond_broadcast(&shared_mem->cond);
            }
            else if (strcmp(shared_mem->status, "Open") == 0)
            {
                strcpy(shared_mem->status, "Closing");
                pthread_cond_broadcast(&shared_mem->cond);

                pthread_mutex_lock(&delay_mutex);
                early_exit_delay = 1;
                pthread_cond_signal(&delay_cond);
                pthread_mutex_unlock(&delay_mutex);
            }
        }

        pthread_mutex_unlock(&shared_mem->mutex);
    }
    pthread_exit(NULL);
}

// Function: state machine for status. Attempt to get the car's status to closed, and react to changes from external programs.
void *go_through_sequence(void *arg)
{
    (void)arg;

    while (1)
    {
        pthread_mutex_lock(&shared_mem->mutex);
        pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);
        if (shared_mem->individual_service_mode == 0)
        {
            if (strcmp(shared_mem->status, "Opening") == 0)
            {
                pthread_mutex_unlock(&shared_mem->mutex);
                delay();
                pthread_mutex_lock(&shared_mem->mutex);
                strcpy(shared_mem->status, "Open");
                pthread_cond_broadcast(&shared_mem->cond);
            }

            if (strcmp(shared_mem->status, "Open") == 0)
            {
                pthread_mutex_unlock(&shared_mem->mutex);
                delay();
                pthread_mutex_lock(&shared_mem->mutex);
                strcpy(shared_mem->status, "Closing");
                pthread_cond_broadcast(&shared_mem->cond);
            }

            if (strcmp(shared_mem->status, "Closing") == 0)
            {
                pthread_mutex_unlock(&shared_mem->mutex);
                delay();
                pthread_mutex_lock(&shared_mem->mutex);
                strcpy(shared_mem->status, "Closed");
                pthread_cond_broadcast(&shared_mem->cond);
            }
        }

        pthread_mutex_unlock(&shared_mem->mutex);
    }

    pthread_exit(NULL);
}

// Function: delay mechanism that utilizes pthread_cond_timedwait for absolute delays.
void delay()
{
    struct timespec ts;
    int rt = 0;

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += car_info.delay / 1000;
    ts.tv_nsec += (car_info.delay % 1000) * 1000000;

    // Adjust for nanosecond overflow
    if (ts.tv_nsec >= 1000000000)
    {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock(&delay_mutex);
    while (1)
    {
        rt = pthread_cond_timedwait(&delay_cond, &delay_mutex, &ts);
        pthread_mutex_lock(&early_exit_delay_mutex);
        if (early_exit_delay)
        {
            early_exit_delay = 0;
            pthread_mutex_unlock(&early_exit_delay_mutex);
            pthread_mutex_unlock(&delay_mutex);
            return; // Exit early
        }
        pthread_mutex_unlock(&early_exit_delay_mutex);

        // If the timeout occurred, break the loop
        if (rt == ETIMEDOUT)
        {
            break;
        }
    }
    pthread_mutex_unlock(&delay_mutex);
}

// Send messages in the form:
// STATUS {status} {current floor} {destination floor}

// Function: periodically send status messages to the controller.
void *send_status_messages(void *arg)
{
    // Send messages in the form:
    // STATUS {status} {current floor} {destination floor}

    char status_message[256];
    while (1)
    {
        pthread_mutex_lock(&shared_mem->mutex);
        pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);

        sprintf(status_message, "STATUS %s %s %s", shared_mem->status, shared_mem->current_floor, shared_mem->destination_floor);

        pthread_mutex_unlock(&shared_mem->mutex);
        send_message(controller_sock_fd, status_message);
    }

    // This message should be sent when:
    // - Everytime the shared memory changes (check condition variable)
    // - If delay (ms) has passed since the last message.
    pthread_exit(NULL);
}

// If the destination floor is different from the current floor and the doors are closed, the car will:
// - Change its status to Between
// - Wait (delay) ms
// - Change its current floor to be 1 closer to the destination floor, and its status to Closed

// Function: Function thread to move the cars current floor to the destination floor.
// Arguments: unused void pointer
// Returns: void
void *normal_operation(void *arg)
{
    while (1)
    {
        pthread_mutex_lock(&shared_mem->mutex);
        pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);
        if (strcmp(shared_mem->destination_floor, shared_mem->current_floor) != 0)
        {
            if (strcmp(shared_mem->status, "Closed") == 0)
            {
                strcpy(shared_mem->status, "Between");
                pthread_cond_broadcast(&shared_mem->cond);

                delay();

                // - Change its current floor to be 1 closer to the destination floor, and its status to Closed
            }
        }
        pthread_mutex_unlock(&shared_mem->mutex);
    }

    // If the current floor and destination floor are equal, call "reached_destination_floor" function.
    pthread_exit(NULL);
}

// Function: Function thread to connect to the controller and receive dispatch floor messages.
// Arguments: void pointer (unused)
// Returns: void
void *connect_to_controller(void *arg)
{
    controller_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (controller_sock_fd == -1)
    {
        perror("socket()");
        pthread_exit(NULL);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    const char *ipaddress = "127.0.0.1";

    if (inet_pton(AF_INET, ipaddress, &addr.sin_addr) != 1)
    {
        fprintf(stderr, "inet_pton(%s)\n", ipaddress);
        close(controller_sock_fd);
        pthread_exit(NULL);
    }

    if (connect(controller_sock_fd, (const struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        close(controller_sock_fd);
        pthread_exit(NULL);
    }

    char car_initialisation_message[256];
    sprintf(car_initialisation_message, "CAR %s %s %s", car_info.name, car_info.lowest_floor, car_info.highest_floor);
    send_message(controller_sock_fd, car_initialisation_message);

    char status_message[256];
    pthread_mutex_lock(&shared_mem->mutex);
    sprintf(status_message, "STATUS %s %s %s", shared_mem->status, shared_mem->current_floor, shared_mem->destination_floor);
    pthread_mutex_unlock(&shared_mem->mutex);
    send_message(controller_sock_fd, status_message);

    while (1)
    {
        char *message_from_controller = receive_msg(controller_sock_fd);

        if (strncmp(message_from_controller, "FLOOR", 5) == 0)
        {
            char dispatch_floor[4];
            sscanf(message_from_controller, "FLOOR %s", dispatch_floor); // New floor call.

            pthread_mutex_lock(&shared_mem->mutex);
            if (strcmp(shared_mem->current_floor, dispatch_floor) == 0) // If the car is already on that floor.
            {
                strcpy(shared_mem->status, "Opening");
                pthread_cond_broadcast(&shared_mem->cond);
            }
            else // Set the new destination floor.
            {
                if (strcmp(shared_mem->status, "Between") != 0) // If a new destination arrives while the car is in the Between status, that destination will not replace the car's current destination until the car reaches the next floor.
                {
                    strcpy(shared_mem->destination_floor, dispatch_floor);
                    pthread_cond_broadcast(&shared_mem->cond);
                }
            }
            pthread_mutex_unlock(&shared_mem->mutex);
        }
        else
        {
            free(message_from_controller);
            break;
        }

        free(message_from_controller);
    }

    shutdown(controller_sock_fd, SHUT_RDWR); // Disable both reading and writing
    close(controller_sock_fd);

    pthread_exit(NULL);
}

// Cleans up the shared memory upon program termination.
// Returns: void.
void terminate_shared_memory(int sig_num)
{
    signal(SIGINT, terminate_shared_memory);

    munmap(shared_mem, sizeof(car_shared_mem));

    close(shm_fd);

    shm_unlink(car_name);

    exit(0);
}
