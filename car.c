#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h> // Include pthread header for mutex and condition variable
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

#define MILLISECOND 1000

char *status_names[] = {
    "Opening", "Open", "Closing", "Closed", "Between"};

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

typedef struct
{
    char name[100];
    char lowest_floor[4];
    char highest_floor[4];
    int delay;
    car_shared_mem *ptr_to_shared_mem;
} car_information;

typedef struct
{
    car_shared_mem *shared_mem;
    int delay_ms;
} reached_floor_args;

int shm_fd = -1;
car_shared_mem *car_shared_memory; // Pointer to shared memory
char car_name[100] = "/car";

void terminate_shared_memory(int sig_num);
void opening_to_closed_sequence(car_information *car_info, int delay_ms);
void *handle_button_press(void *arg);

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        printf("Usage: {name} {lowest floor} {highest floor} {delay}\n");
        exit(1);
    }

    signal(SIGINT, terminate_shared_memory);

    // Esnure the car doesn't crash when write fails.
    signal(SIGPIPE, SIG_IGN);

    strcat(car_name, argv[1]);

    car_information car_info;

    strcpy(car_info.name, argv[1]);
    strcpy(car_info.lowest_floor, argv[2]);
    strcpy(car_info.highest_floor, argv[3]);
    car_info.delay = atoi(argv[4]);

    // Unlink the shared memory incase it exists.
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

    car_shared_memory = mmap(0, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (car_shared_memory == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    car_info.ptr_to_shared_mem = car_shared_memory;

    // Initialised the mutex.
    pthread_mutexattr_t mutattr;
    pthread_mutexattr_init(&mutattr);
    pthread_mutexattr_setpshared(&mutattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&car_shared_memory->mutex, &mutattr);
    pthread_mutexattr_destroy(&mutattr);

    // Initialise the condition variable.
    pthread_condattr_t condattr;
    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&car_shared_memory->cond, &condattr);
    pthread_condattr_destroy(&condattr);

    // Initialise the shared memory.
    strcpy(car_shared_memory->current_floor, argv[2]);
    strcpy(car_shared_memory->destination_floor, argv[2]);
    strcpy(car_shared_memory->status, status_names[3]);
    car_shared_memory->open_button = 0;
    car_shared_memory->close_button = 0;
    car_shared_memory->door_obstruction = 0;
    car_shared_memory->overload = 0;
    car_shared_memory->emergency_stop = 0;
    car_shared_memory->individual_service_mode = 0;
    car_shared_memory->emergency_mode = 0;

    // Create handle button press thread here
    pthread_t button_thread;
    if (pthread_create(&button_thread, NULL, handle_button_press, &car_info) != 0)
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
    car_information *car_info = (car_information *)arg;
    car_shared_mem *shared_mem = car_info->ptr_to_shared_mem;

    while (1)
    {
        pthread_mutex_lock(&shared_mem->mutex);

        pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);

        if (shared_mem->close_button == 1)
        {
            printf("Closed button pressed!\n");
            shared_mem->close_button = 0;
            if (strcmp(shared_mem->status, "Open") == 0)
            {
                // - If the status is Open the car should immediately switch to Closing
                strcpy(shared_mem->status, "Closing");
            }
        }

        if (shared_mem->open_button == 1)
        {
            shared_mem->open_button = 0;
            if (strcmp(shared_mem->status, "Open") == 0)
            {
                // - If the status is Open the car should wait another (delay) ms before switching to Closing.
                usleep(car_info->delay * MILLISECOND);
                strcpy(shared_mem->status, "Closing");
            }

            if ((strcmp(shared_mem->status, "Closing") == 0) ||
                (strcmp(shared_mem->status, "Closed") == 0))
            {
                pthread_mutex_unlock(&shared_mem->mutex);

                opening_to_closed_sequence(car_info, car_info->delay);

                pthread_mutex_lock(&shared_mem->mutex);
            }

            // - If the status is Opening or Between the button does nothing
        }

        pthread_mutex_unlock(&shared_mem->mutex);
    }
    pthread_exit(NULL);
}

void opening_to_closed_sequence(car_information *car_info, int delay_ms)
{
    car_shared_mem *shared_mem = car_info->ptr_to_shared_mem;

    // Step 1: Change status to "Opening"
    pthread_mutex_lock(&shared_mem->mutex);
    strcpy(shared_mem->status, "Opening");
    pthread_mutex_unlock(&shared_mem->mutex);
    usleep(delay_ms * MILLISECOND);

    // Step 2: Change status to "Open"
    pthread_mutex_lock(&shared_mem->mutex);
    strcpy(shared_mem->status, "Open");
    pthread_mutex_unlock(&shared_mem->mutex);
    usleep(delay_ms * MILLISECOND);

    // Step 3: Change status to "Closing"
    pthread_mutex_lock(&shared_mem->mutex);
    strcpy(shared_mem->status, "Closing");
    pthread_mutex_unlock(&shared_mem->mutex);
    usleep(delay_ms * MILLISECOND);

    // Step 4: Change status to "Closed"
    pthread_mutex_lock(&shared_mem->mutex);
    strcpy(shared_mem->status, "Closed");
    pthread_mutex_unlock(&shared_mem->mutex);

    pthread_exit(NULL);
}

void terminate_shared_memory(int sig_num)
{
    signal(SIGINT, terminate_shared_memory);

    munmap(car_shared_memory, sizeof(car_shared_mem));

    close(shm_fd);

    shm_unlink(car_name);

    exit(0);
}