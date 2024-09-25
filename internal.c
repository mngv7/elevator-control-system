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
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>

car_shared_mem *ptr;

char *status_names[] = {
    "Opening", "Open", "Closing", "Closed", "Between"};

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    char current_floor[4];
    char destination_floor[4];
    char status[8];
    uint8_t open_button;
    uint8_t close_button;
    uint8_t door_obstruction;
    uint8_t overload;
    uint8_t emergency_stop;
    uint8_t individual_service_mode;
    uint8_t emergency_mode;
} car_shared_mem;

void handle_open(void)
{
    pthread_mutex_lock(&ptr->mutex);
    ptr->open_button = 1;
    pthread_cond_signal(&ptr->cond);

    pthread_mutex_unlock(&ptr->mutex);
}

void handle_close(void)
{
    pthread_mutex_lock(&ptr->mutex);
    ptr->close_button = 1;
    pthread_cond_signal(&ptr->cond);

    pthread_mutex_unlock(&ptr->mutex);
}

void handle_up(void)
{
    pthread_mutex_lock(&ptr->mutex);

    if (!ptr->individual_service_mode)
    {
        printf("Operation only allowed in service mode.");
        exit(1);
    }

    if (strcmp(ptr->status, status_names[3]))
    {
        printf("Operation not allowed while doors are open.");
        exit(1);
    }

    if (strcmp(ptr->status, status_names[4]))
    {
        printf("Operation not allowed while elevator is moving.");
        exit(1);
    }

    // TODO: Set the destination floor to +1 the current floor

    pthread_mutex_unlock(&ptr->mutex);
}

void handle_stop(void)
{
    pthread_mutex_lock(&ptr->mutex);
    ptr->emergency_stop = 1;
    pthread_cond_signal(&ptr->cond);
    pthread_mutex_unlock(&ptr->mutex);
}

void handle_service_on(void)
{
}

void handle_service_off(void)
{
}

void handle_down(void)
{
}

int main(int argc, char **argv)
{
    // Check the number of command line arguments.
    if (argc != 3)
    {
        printf("Usage: {car name} {operation}\n");
        exit(1);
    }

    int shm_fd = shm_open(argv[1], O_RDWR, 0666);

    if (shm_fd == -1)
    {
        printf("Unable to access %s\n", argv[1]);
        exit(1);
    }

    ptr = mmap(0, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (ptr == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    char *operation;
    strcpy(operation, argv[2]);

    if (!strcmp(operation, "open"))
    {
        handle_open();
    }
    else if (!strcmp(operation, "close"))
    {
        handle_close();
    }
    else if (!strcmp(operation, "stop"))
    {
        handle_stop();
    }
    else if (!strcmp(operation, "service_on"))
    {
        handle_stop();
    }
    else if (!strcmp(operation, "service_off"))
    {
        handle_stop();
    }
    else if (!strcmp(operation, "up"))
    {
        handle_up();
    }
    else if (!strcmp(operation, "down"))
    {
        handle_stop();
    }
    else
    {
        printf("Invalid operation.\n");
        exit(1);
    }

    return 0;
}