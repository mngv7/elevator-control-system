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
} car_information;

int shm_fd = -1;
car_shared_mem *ptr; // Pointer to shared memory

void terminate_shared_memory(int sig_num);
void *connnect_to_controller(void *arg);

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

    car_information car_info;

    strcpy(car_info.name, argv[1]);
    strcpy(car_info.lowest_floor, argv[2]);
    strcpy(car_info.highest_floor, argv[3]);
    car_info.delay = atoi(argv[4]);

    // Unlink the shared memory incase it exists.
    shm_unlink("/carA");

    shm_fd = shm_open(argv[1], O_CREAT | O_RDWR, 0666);
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

    ptr = mmap(0, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (ptr == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    // Initialised the mutex.
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&ptr->mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    // Initialise the condition variable.
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&ptr->cond, &cond_attr);
    pthread_condattr_destroy(&cond_attr);

    strcpy(ptr->current_floor, argv[2]);
    strcpy(ptr->destination_floor, argv[2]);
    strcpy(ptr->status, status_names[3]);
    ptr->open_button = 0;
    ptr->close_button = 0;
    ptr->door_obstruction = 0;
    ptr->overload = 0;
    ptr->emergency_stop = 0;
    ptr->individual_service_mode = 0;
    ptr->emergency_mode = 0;

    pthread_t controller_thread;
    if (pthread_create(&controller_thread, NULL, connnect_to_controller, &car_info) != 0)
    {
        perror("pthread_create()");
        exit(EXIT_FAILURE);
    }

    while (1)
        ;
    return 0;
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
        usleep(10000);
        if (connect(sockfd, (const struct sockaddr *)&addr, sizeof(addr)) == 0)
        {
            break;
        }
    }
    char car_initialisation_message[256];

    sscanf(car_initialisation_message, "CAR %s %s %s", car_info->name, car_info->lowest_floor, car_info->highest_floor);
    send_message(sockfd, car_initialisation_message);
    pthread_exit(NULL);
}

void *send_status_messages(void *arg)
{
    // Send messages in the form:
    // STATUS {status} {current floor} {destination floor}

    // This message should be sent when:
    // - Immediately after the car initialisation message
    // - Everytime the shared memory changes (check condition variable)
    // - If delay (ms) has passed since the last message.
    pthread_exit(NULL);
}

void terminate_shared_memory(int sig_num)
{
    signal(SIGINT, terminate_shared_memory);
    // Unmap the shared memory
    munmap(ptr, sizeof(car_shared_mem));

    // Close the file descriptor
    close(shm_fd);

    shm_unlink("/carA");
    exit(0);
}