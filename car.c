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
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#define MILLISECOND 1000

typedef struct
{
    pthread_mutex_t mutex;           // Locked while accessing struct contents
    pthread_cond_t cond;             // Signalled when the contents change
    char current_floor[4];           // C string in the range B99-B1 and 1-999
    char destination_floor[4];       // Same format as above
    char status[8];                  // C string indicating the elevator's status
    uint8_t open_button;             // 1 if open doors button is pressed, else 0
    uint8_t close_button;            // 1 if close doors button is pressed, else 0
    uint8_t door_obstruction;        // 1 if obstruction detected, else 0
    uint8_t overload;                // 1 if overload detected
    uint8_t emergency_stop;          // 1 if stop button has been pressed, else 0
    uint8_t individual_service_mode; // 1 if in individual service mode, else 0
    uint8_t emergency_mode;          // 1 if in emergency mode, else 0
} car_shared_mem;

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

pthread_cond_t status_change_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t status_change_mutex = PTHREAD_MUTEX_INITIALIZER;

// Global variables:
car_shared_mem *shared_mem;
car_information car_info;
char car_name[100] = "/car";
int shm_fd = -1;
int early_exit_delay;
pthread_mutex_t early_exit_delay_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function definitions:
void terminate_shared_memory(int sig_num);
void *go_through_sequence(void *arg);
void *handle_button_press(void *arg);
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

    pthread_join(button_thread, NULL);
    pthread_join(go_through_sequence_thread, NULL);

    return 0;
}

void *handle_button_press(void *arg)
{
    (void)arg;

    while (1)
    {
        pthread_mutex_lock(&shared_mem->mutex);
        pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);

        if (shared_mem->open_button == 1)
        {
            shared_mem->open_button = 0;

            if (strcmp(shared_mem->status, "Open") == 0)
            {
                // Handle another delay
            }

            if (strcmp(shared_mem->status, "Closing") == 0 || strcmp(shared_mem->status, "Closed") == 0)
            {
                strcpy(shared_mem->status, "Opening");

                pthread_mutex_unlock(&shared_mem->mutex);
                pthread_mutex_lock(&status_change_mutex);
                pthread_cond_signal(&status_change_cond);
                pthread_mutex_unlock(&status_change_mutex);
                continue;
            }
        }
        else if (shared_mem->close_button == 1)
        {
            shared_mem->close_button = 0;

            if (strcmp(shared_mem->status, "Open") == 0)
            {
                strcpy(shared_mem->status, "Closing");

                pthread_mutex_lock(&early_exit_delay_mutex);
                early_exit_delay = 1;
                pthread_mutex_unlock(&early_exit_delay_mutex);

                pthread_mutex_unlock(&shared_mem->mutex);
                pthread_mutex_lock(&status_change_mutex);
                pthread_cond_signal(&status_change_cond);
                pthread_mutex_unlock(&status_change_mutex);
                continue;
            }
        }

        pthread_mutex_unlock(&shared_mem->mutex);
    }
    pthread_exit(NULL);
}


void *go_through_sequence(void *arg)
{
    (void)arg;

    while (1)
    {
        pthread_mutex_lock(&status_change_mutex);
        pthread_cond_wait(&status_change_cond, &status_change_mutex);

        pthread_mutex_lock(&shared_mem->mutex);

        if (strcmp(shared_mem->status, "Opening") == 0)
        {
            pthread_mutex_unlock(&shared_mem->mutex);
            delay();
            pthread_mutex_lock(&shared_mem->mutex);
            strcpy(shared_mem->status, "Open");
        }

        if (strcmp(shared_mem->status, "Open") == 0)
        {
            pthread_mutex_unlock(&shared_mem->mutex);
            delay();
            pthread_mutex_lock(&shared_mem->mutex);
            strcpy(shared_mem->status, "Closing");
        }

        if (strcmp(shared_mem->status, "Closing") == 0)
        {
            pthread_mutex_unlock(&shared_mem->mutex);
            delay();
            pthread_mutex_lock(&shared_mem->mutex);
            strcpy(shared_mem->status, "Closed");
        }

        pthread_mutex_unlock(&shared_mem->mutex);
        pthread_mutex_unlock(&status_change_mutex);
    }

    pthread_exit(NULL);
}

void delay()
{
    const int time_in_ms = car_info.delay;
    struct timespec ts, start_time, end_time;
    int rt = 0;

    // Get the start time
    clock_gettime(CLOCK_REALTIME, &start_time);

    // Set the target time for delay
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += time_in_ms / 1000;
    ts.tv_nsec += (time_in_ms % 1000) * 1000000;

    // Adjust for nanosecond overflow
    if (ts.tv_nsec >= 1000000000)
    {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock(&shared_mem->mutex);
    do
    {
        rt = pthread_cond_timedwait(&shared_mem->cond, &shared_mem->mutex, &ts);

        pthread_mutex_lock(&early_exit_delay_mutex);
        if (early_exit_delay)
        {
            early_exit_delay = 0;
            pthread_mutex_unlock(&early_exit_delay_mutex);
            break;
        }
        pthread_mutex_unlock(&early_exit_delay_mutex); 

    } while (rt == 0);
    pthread_mutex_unlock(&shared_mem->mutex);

    // Get the end time
    clock_gettime(CLOCK_REALTIME, &end_time);

    // Calculate the elapsed time
    long seconds = end_time.tv_sec - start_time.tv_sec;
    long nanoseconds = end_time.tv_nsec - start_time.tv_nsec;
    if (nanoseconds < 0)
    {
        seconds -= 1;
        nanoseconds += 1000000000;
    }
}


void *send_status_messages(void *arg)
{
    // Send messages in the form:
    // STATUS {status} {current floor} {destination floor}

    // This message should be sent when:
    // - Immediately after the car initialisation message (complete).
    // - Everytime the shared memory changes (check condition variable)
    // - If delay (ms) has passed since the last message.
    pthread_exit(NULL);
}

void *normal_operation(void *arg)
{
    // If the destination floor is different from the current floor and the doors are closed, the car will:
    // - Change its status to Between
    // - Wait (delay) ms
    // - Change its current floor to be 1 closer to the destination floor, and its status to Closed

    // If the current floor and destination floor are equal, call "reached_destination_floor" function.
    pthread_exit(NULL);
}

void *indiviudal_service_mode(void *arg)
{
    if (shared_mem->individual_service_mode == 1)
    {
        // If a change in destination floor is detected:
        //  - Only if the car is closed:
        //      - Set the status to "Between".
        //      - Wait delay (ms)
        //      - Set the current floor to the destination floor
        //      - Set the status to "Closed".
    }
    pthread_exit(NULL);
}

void *connnect_to_controller(void *arg)
{
    car_information *car_info = (car_information *)arg;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        perror("socket()");
        exit(1);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    const char *ipaddress = "127.0.0.1";

    if (inet_pton(AF_INET, ipaddress, &addr.sin_addr) != 1)
    {
        fprintf(stderr, "inet_pton(%s)\n", ipaddress);
        exit(1);
    }
    while (1)
    {
        usleep(car_info->delay * 1000);
        if (connect(sockfd, (const struct sockaddr *)&addr, sizeof(addr)) == 0)
        {
            break;
        }
    }
    char car_initialisation_message[256];

    sprintf(car_initialisation_message, "CAR %s %s %s", car_info->name, car_info->lowest_floor, car_info->highest_floor);
    send_message(sockfd, car_initialisation_message);

    char status_initialisation_message[256];

    pthread_mutex_lock(&shared_mem->mutex);
    sprintf(status_initialisation_message, "STATUS %s %s %s", shared_mem->status, shared_mem->current_floor, shared_mem->destination_floor);
    pthread_mutex_unlock(&shared_mem->mutex);

    send_message(sockfd, status_initialisation_message);

    while (1)
    {
        char *message_from_controller = receive_msg(sockfd);

        if (strncmp(message_from_controller, "FLOOR", 5) == 0)
        {
            char dispatch_floor[4];
            sscanf(message_from_controller, "FLOOR %s", dispatch_floor); // New floor call.

            pthread_mutex_lock(&shared_mem->mutex);
            if (strcmp(shared_mem->current_floor, dispatch_floor) == 0) // If the car is already on that floor.
            {
                // reached_destination_floor(shared_mem, car_info->delay); // Open and close the doors.
            }
            else // Set the new destination floor.
            {
                if (strcmp(shared_mem->status, "Between") != 0) // If a new destination arrives while the car is in the Between status, that destination will not replace the car's current destination until the car reaches the next floor.
                {
                    strcpy(shared_mem->destination_floor, dispatch_floor);
                }
            }
            pthread_mutex_unlock(&shared_mem->mutex);
        }
        else
        {
            break;
        }

        free(message_from_controller);
    }

    pthread_exit(NULL);
}

void terminate_shared_memory(int sig_num)
{
    signal(SIGINT, terminate_shared_memory);

    munmap(shared_mem, sizeof(car_shared_mem));

    close(shm_fd);

    shm_unlink(car_name);

    exit(0);
}
