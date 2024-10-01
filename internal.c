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

car_shared_mem *ptr;

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
        printf("Operation only allowed in service mode.\n");
        pthread_mutex_unlock(&ptr->mutex);

        exit(1);
    }

    if (strcmp(ptr->status, status_names[3]) != 0)
    {
        printf("Operation not allowed while doors are open.\n");
        pthread_mutex_unlock(&ptr->mutex);

        exit(1);
    }

    if (strcmp(ptr->status, status_names[4]) == 0)
    {
        printf("Operation not allowed while elevator is moving.\n");
        pthread_mutex_unlock(&ptr->mutex);

        exit(1);
    }
    pthread_mutex_unlock(&ptr->mutex);

    pthread_mutex_lock(&ptr->mutex);
    if (strcmp(ptr->current_floor, "B1") == 0)
    {
        strcpy(ptr->destination_floor, "1");

        pthread_cond_signal(&ptr->cond);
        pthread_mutex_unlock(&ptr->mutex);
        exit(1);
    }
    pthread_mutex_unlock(&ptr->mutex);

    pthread_mutex_lock(&ptr->mutex);

    if (ptr->current_floor[0] == 'B')
    {
        char snum[12];
        int j = 0;

        // Extract number part from current_floor
        for (int i = 1; ptr->current_floor[i] != '\0'; i++)
        {
            snum[j++] = ptr->current_floor[i];
        }

        snum[j] = '\0'; // Null-terminate the string

        int num = atoi(snum);
        num++;

        sprintf(snum, "%d", num);

        char new_destination_floor[4];
        new_destination_floor[4] = '\0';

        new_destination_floor[0] = 'B';

        strncat(new_destination_floor, snum, 3);
        strncpy(ptr->destination_floor, new_destination_floor, 3);

        pthread_cond_signal(&ptr->cond);
        pthread_mutex_unlock(&ptr->mutex);
        exit(EXIT_SUCCESS);
    }
    else
    {
        int current_floor = atoi(ptr->current_floor);

        // Ensure current_floor is within bounds
        if (current_floor >= 999)
        {
            exit(EXIT_FAILURE); // Exit if the current floor is 999 or more
        }

        current_floor++; // Increment the floor

        char temp_buffer[4]; // Temporary buffer to hold the string
        int length = snprintf(temp_buffer, sizeof(temp_buffer), "%d", current_floor);

        // Check if the length is greater than the destination buffer
        if (length >= sizeof(ptr->destination_floor))
        {
            exit(EXIT_FAILURE); // Prevent overflow
        }

        strncpy(ptr->destination_floor, temp_buffer, sizeof(ptr->destination_floor));
        ptr->destination_floor[sizeof(ptr->destination_floor) - 1] = '\0';

        pthread_cond_signal(&ptr->cond);
        pthread_mutex_unlock(&ptr->mutex);

        exit(EXIT_SUCCESS);
    }

    pthread_mutex_unlock(&ptr->mutex);
}

void handle_down(void)
{
    pthread_mutex_lock(&ptr->mutex);

    if (!ptr->individual_service_mode)
    {
        printf("Operation only allowed in service mode.\n");
        pthread_mutex_unlock(&ptr->mutex);

        exit(1);
    }

    if (strcmp(ptr->status, status_names[3]) != 0)
    {
        printf("Operation not allowed while doors are open.\n");
        pthread_mutex_unlock(&ptr->mutex);

        exit(1);
    }

    if (strcmp(ptr->status, status_names[4]) == 0)
    {
        printf("Operation not allowed while elevator is moving.\n");
        pthread_mutex_unlock(&ptr->mutex);

        exit(1);
    }
    pthread_mutex_unlock(&ptr->mutex);

    // TODO: Set the destination floor to +1 the current floor

    pthread_mutex_lock(&ptr->mutex);
    if (strcmp(ptr->current_floor, "1") == 0)
    {
        strcpy(ptr->destination_floor, "B1");

        pthread_cond_signal(&ptr->cond);
        pthread_mutex_unlock(&ptr->mutex);
        exit(1);
    }
    pthread_mutex_unlock(&ptr->mutex);

    pthread_mutex_lock(&ptr->mutex);

    if (ptr->current_floor[0] == 'B')
    {
        char snum[12];
        int j = 0;

        // Extract number part from current_floor
        for (int i = 1; ptr->current_floor[i] != '\0'; i++)
        {
            snum[j++] = ptr->current_floor[i];
        }

        snum[j] = '\0'; // Null-terminate the string

        int num = atoi(snum);

        if (num <= 99)
        {
            exit(EXIT_FAILURE);
        }

        num--;

        sprintf(snum, "%d", num);

        char new_destination_floor[4];
        new_destination_floor[4] = '\0';

        new_destination_floor[0] = 'B';

        strncat(new_destination_floor, snum, 3);
        strncpy(ptr->destination_floor, new_destination_floor, 3);

        pthread_cond_signal(&ptr->cond);
        pthread_mutex_unlock(&ptr->mutex);
        exit(EXIT_SUCCESS);
    }
    else
    {
        int current_floor = atoi(ptr->current_floor);

        current_floor--; // Increment the floor

        char temp_buffer[4]; // Temporary buffer to hold the string
        int length = snprintf(temp_buffer, sizeof(temp_buffer), "%d", current_floor);

        // Check if the length is greater than the destination buffer
        if (length >= sizeof(ptr->destination_floor))
        {
            exit(EXIT_FAILURE); // Prevent overflow
        }

        strncpy(ptr->destination_floor, temp_buffer, sizeof(ptr->destination_floor));
        ptr->destination_floor[sizeof(ptr->destination_floor) - 1] = '\0';

        pthread_cond_signal(&ptr->cond);
        pthread_mutex_unlock(&ptr->mutex);

        exit(EXIT_SUCCESS);
    }
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
    pthread_mutex_lock(&ptr->mutex);
    ptr->individual_service_mode = 1;
    ptr->emergency_mode = 0;
    pthread_cond_signal(&ptr->cond);
    pthread_mutex_unlock(&ptr->mutex);
}

void handle_service_off(void)
{
    pthread_mutex_lock(&ptr->mutex);
    ptr->individual_service_mode = 0;
    pthread_cond_signal(&ptr->cond);
    pthread_mutex_unlock(&ptr->mutex);
}

int main(int argc, char **argv)
{
    // Check the number of command line arguments.
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

    ptr = mmap(0, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (ptr == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    char operation[11];
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
        handle_service_on();
    }
    else if (!strcmp(operation, "service_off"))
    {
        handle_service_off();
    }
    else if (!strcmp(operation, "up"))
    {
        handle_up();
    }
    else if (!strcmp(operation, "down"))
    {
        handle_down();
    }
    else
    {
        printf("Invalid operation.\n");
        exit(1);
    }

    return 0;
}