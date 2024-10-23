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

car_shared_mem *shared_mem;

void check_up_down_allowed()
{
    pthread_mutex_lock(&shared_mem->mutex);

    if (!shared_mem->individual_service_mode)
    {
        printf("Operation only allowed in service mode.\n");
    }
    else if (strcmp(shared_mem->status, status_names[4]) == 0)
    {
        printf("Operation not allowed while elevator is moving.\n");
    }
    else if (strcmp(shared_mem->status, status_names[3]) != 0)
    {
        printf("Operation not allowed while doors are open.\n");
    }
    else
    {
        pthread_mutex_unlock(&shared_mem->mutex);
        return; // Operation allowed, exit function early
    }

    pthread_mutex_unlock(&shared_mem->mutex);
    exit(1); // Exit if any condition fails
}

void update_shared_mem(uint8_t *ptr_to_update, int new_val)
{
    pthread_mutex_lock(&shared_mem->mutex);
    *ptr_to_update = new_val;
    pthread_cond_signal(&shared_mem->cond);
    pthread_mutex_unlock(&shared_mem->mutex);
}

void handle_floor_change(int direction)
{
    pthread_mutex_lock(&shared_mem->mutex);

    int current_floor = atoi(shared_mem->current_floor);

    if (shared_mem->current_floor[0] == 'B') // Handling basement floors
    {
        current_floor = atoi(shared_mem->current_floor + 1); // Skip the first character
        if (direction == 1)
        {
            if (strcmp(shared_mem->current_floor, "B1") == 0)
            {
                strcpy(shared_mem->destination_floor, "1");
            }
            else
            {
                current_floor--;
            }
        }
        else
        {
            current_floor++;
        }
        snprintf(shared_mem->destination_floor, 4, "B%02d", current_floor);
    }
    else
    {
        if (direction == 1) // Going up
        {
            current_floor++;
        }
        else
        {
            current_floor--;
        }

        if (current_floor <= 0)
        {
            snprintf(shared_mem->destination_floor, 4, "B1");
        }
        else
        {
            if (current_floor > 999)
            {
                current_floor = 999;
            }
            snprintf(shared_mem->destination_floor, 4, "%d", current_floor);
        }
    }

    pthread_cond_signal(&shared_mem->cond);
    pthread_mutex_unlock(&shared_mem->mutex);
    exit(0);
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        printf("Usage: {car name} {operation}\n");
        exit(1);
    }

    char car_name[100] = "/car";

    strcat(car_name, argv[1]);

    int shm_fd = shm_open(car_name, O_RDWR, 0666);

    if (shm_fd == -1)
    {
        printf("Unable to access car %s.\n", argv[1]);
        exit(1);
    }

    shared_mem = mmap(0, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (shared_mem == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    if (!strcmp(argv[2], "open"))
    {
        update_shared_mem(&shared_mem->open_button, 1);
    }
    else if (!strcmp(argv[2], "close"))
    {
        update_shared_mem(&shared_mem->close_button, 1);
    }
    else if (!strcmp(argv[2], "stop"))
    {
        update_shared_mem(&shared_mem->emergency_stop, 1);
    }
    else if (!strcmp(argv[2], "service_on"))
    {
        update_shared_mem(&shared_mem->individual_service_mode, 1);
        shared_mem->emergency_mode = 0;
    }
    else if (!strcmp(argv[2], "service_off"))
    {
        update_shared_mem(&shared_mem->individual_service_mode, 0);
    }
    else if (!strcmp(argv[2], "up"))
    {
        check_up_down_allowed();
        handle_floor_change(1);
    }
    else if (!strcmp(argv[2], "down"))
    {
        check_up_down_allowed();
        handle_floor_change(-1);
    }
    else
    {
        printf("Invalid operation.\n");
        exit(1);
    }

    return 0;
}