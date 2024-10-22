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

int shm_fd = -1;
car_shared_mem *car_shared_memory; // Pointer to shared memory
char car_name[100] = "/car";

void terminate_shared_memory(int sig_num);
void *connnect_to_controller(void *arg);
void reached_destination_floor(car_shared_mem *shared_mem, int delay_ms);
void *handle_button_press(void *arg);
void traverse_car(car_shared_mem *shared_mem);

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

    printf("The name of the shared memory is: '%s'\n", car_name);

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

    /*pthread_t controller_thread;
    if (pthread_create(&controller_thread, NULL, connnect_to_controller, &car_info) != 0)
    {
        perror("pthread_create()");
        exit(EXIT_FAILURE);
    }*/

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
        printf("Waiting for broadcast...\n");
        pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);
        printf("Broadcast received!\n");

        printf("Attempting to lock mutex...\n");
        pthread_mutex_lock(&shared_mem->mutex);
        printf("Mutex locked!\n");

        if (shared_mem->open_button == 1)
        {
            printf("Detected open button pushed\n");
            shared_mem->open_button = 0;
            if (strcmp(shared_mem->status, "Open") == 0)
            {
                // - If the status is Open the car should wait another (delay) ms before switching to Closing.
            }

            if ((strcmp(shared_mem->status, "Closing") == 0) ||
                (strcmp(shared_mem->status, "Closed") == 0))
            {
                // - If the status is Closing or Closed the car should switch to Opening and repeat the steps from there
            }

            // - If the status is Opening or Between the button does nothing
        }

        if (shared_mem->close_button == 1)
        {
            shared_mem->close_button = 0;
            if (strcmp(shared_mem->status, "Open") == 0)
            {
                // - If the status is Open the car should immediately switch to Closing
                strcpy(shared_mem->status, "Closing");
            }
        }
        pthread_mutex_unlock(&shared_mem->mutex);
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

    car_shared_mem *shared_mem = car_info->ptr_to_shared_mem;

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
                reached_destination_floor(shared_mem, car_info->delay); // Open and close the doors.
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
    pthread_exit(NULL);
}

void reached_destination_floor(car_shared_mem *shared_mem, int delay_ms)
{
    char *open_door_sequence[] = {"Opening", "Open", "Closing", "Closed"};

    for (int i = 0; i < 4;)
    {
        pthread_mutex_lock(&shared_mem->mutex);

        strcpy(shared_mem->status, open_door_sequence[i]);
        pthread_mutex_unlock(&shared_mem->mutex);

        usleep(delay_ms * 1000);

        pthread_mutex_lock(&shared_mem->mutex);

        int status_changed = 0;
        for (int j = 0; j < 4; j++)
        {
            if (strcmp(shared_mem->status, open_door_sequence[j]) == 0)
            {
                // If status changed, resume from the logical next step
                if (j != i)
                {
                    i = j; // Move to the new step based on the updated status
                    status_changed = 1;
                }
                break;
            }
        }
        pthread_mutex_unlock(&shared_mem->mutex);

        // If status didn't change, proceed to the next step in the normal sequence
        if (!status_changed)
        {
            i++;
        }
    }
}
void terminate_shared_memory(int sig_num)
{
    signal(SIGINT, terminate_shared_memory);

    munmap(car_shared_memory, sizeof(car_shared_mem));

    close(shm_fd);

    shm_unlink(car_name);

    exit(0);
}