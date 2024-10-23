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

// Function definitions:
void terminate_shared_memory(int sig_num);
void *opening_to_closed_sequence(void *arg);
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
    strcpy(shared_mem->status, status_names[3]);
    shared_mem->open_button = 0;
    shared_mem->close_button = 0;
    shared_mem->door_obstruction = 0;
    shared_mem->overload = 0;
    shared_mem->emergency_stop = 0;
    shared_mem->individual_service_mode = 0;
    shared_mem->emergency_mode = 0;

    // Create handle button press thread here
    pthread_t button_thread;
    if (pthread_create(&button_thread, NULL, handle_button_press, NULL) != 0)
    {
        perror("pthread_create() for button press");
        exit(EXIT_FAILURE);
    }

    while (1)
        ;
    return 0;
}

void *handle_button_press(void *arg)
{
    (void)arg; // Unused argument.

    while (1)
    {
        pthread_mutex_lock(&shared_mem->mutex);
        pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);

        if (shared_mem->open_button == 1)
        {
            shared_mem->open_button = 0; // Reset the button state
            printf("--- Open button received!\n");

            if (strcmp(shared_mem->status, "Open") == 0)
            {
                // Delay if currently open
                delay();
                strcpy(shared_mem->status, "Closing");
            }

            // Handle Closing and Closed statuses
            if (strcmp(shared_mem->status, "Closing") == 0 || strcmp(shared_mem->status, "Closed") == 0)
            {
                printf("~~~ Door is closed or closing!\n");
                pthread_t sequence_thread; // Create a new thread for the sequence
                if (pthread_create(&sequence_thread, NULL, opening_to_closed_sequence, NULL) != 0)
                {
                    perror("pthread_create() for opening to closed sequence");
                }
                else
                {
                    pthread_detach(sequence_thread); // Detach the thread so it can clean up itself
                }
            }
        }
        else if (shared_mem->close_button == 1)
        {
            shared_mem->close_button = 0; // Reset the button state
            printf("--- Close button received!\n");

            if (strcmp(shared_mem->status, "Open") == 0)
            {
                printf("~~~ Open to closing!\n");
                strcpy(shared_mem->status, "Closing");
            }
        }

        pthread_mutex_unlock(&shared_mem->mutex);
    }
    pthread_exit(NULL);
}

void *opening_to_closed_sequence(void *arg)
{
    (void)arg; // Unused argument.

    pthread_mutex_lock(&shared_mem->mutex);
    strcpy(shared_mem->status, "Opening");
    pthread_mutex_unlock(&shared_mem->mutex);

    delay();

    pthread_mutex_lock(&shared_mem->mutex);
    strcpy(shared_mem->status, "Open");
    pthread_mutex_unlock(&shared_mem->mutex);

    delay();

    pthread_mutex_lock(&shared_mem->mutex);
    strcpy(shared_mem->status, "Closing");
    pthread_mutex_unlock(&shared_mem->mutex);

    delay();

    pthread_mutex_lock(&shared_mem->mutex);
    strcpy(shared_mem->status, "Closed");
    pthread_mutex_unlock(&shared_mem->mutex);

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
    } while (rt == 0);
    pthread_mutex_unlock(&shared_mem->mutex);

    // Get the end time
    clock_gettime(CLOCK_REALTIME, &end_time);

    // Calculate the actual delay duration in milliseconds
    long actual_delay_ms = (end_time.tv_sec - start_time.tv_sec) * 1000 +
                           (end_time.tv_nsec - start_time.tv_nsec) / 1000000;

    // Print the debugging information
    printf(">>> Requested delay: %d ms, Actual delay: %ld ms\n", time_in_ms, actual_delay_ms);
}

void terminate_shared_memory(int sig_num)
{
    signal(SIGINT, terminate_shared_memory);

    munmap(shared_mem, sizeof(car_shared_mem));

    close(shm_fd);

    shm_unlink(car_name);

    exit(0);
}
