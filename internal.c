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
#include "common.h"

car_shared_mem *shared_mem;

int is_floor_change_allowed();
void update_shared_mem(uint8_t *ptr_to_update, int new_val);
void handle_floor_change(int direction);

int main(int argc, char **argv)
{
    // Check for the correct number of command-line arguments
    if (argc != 3)
    {
        printf("Usage: {car name} {operation}\n");
        exit(EXIT_FAILURE);
    }

    // Create the shared memory name by appending the car name
    char car_name[100] = "/car";
    strcat(car_name, argv[1]);

    // Open the shared memory segment for the specified car
    int shm_fd = shm_open(car_name, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        printf("Unable to access car %s.\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    // Map the shared memory segment into the process's address space
    shared_mem = mmap(0, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_mem == MAP_FAILED)
    {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    // Determine operation based on command-line argument
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
        // Enable service mode
        pthread_mutex_lock(&shared_mem->mutex);
        shared_mem->emergency_mode = 0;
        pthread_mutex_unlock(&shared_mem->mutex);

        update_shared_mem(&shared_mem->individual_service_mode, 1);
    }
    else if (!strcmp(argv[2], "service_off"))
    {
        // Disable service mode
        update_shared_mem(&shared_mem->individual_service_mode, 0);
    }
    else if (!strcmp(argv[2], "up"))
    {
        // Attempt to move up if allowed
        if (is_floor_change_allowed() == 1)
        {
            handle_floor_change(1);
        }
    }
    else if (!strcmp(argv[2], "down"))
    {
        // Attempt to move down if allowed
        if (is_floor_change_allowed() == 1)
        {
            handle_floor_change(-1);
        }
    }
    else
    {
        // Handle invalid operation
        printf("Invalid operation.\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

// Function that checks the shared memory to determine whether the elevator can be moved.
// Returns:
// - int: 1 if the elevator is allowed to be moved, else 0.
int is_floor_change_allowed(void)
{
    pthread_mutex_lock(&shared_mem->mutex);

    if (shared_mem->individual_service_mode == 0)
    {
        printf("Operation only allowed in service mode.\n");
    }
    else if (strcmp(shared_mem->status, "Between") == 0)
    {
        printf("Operation not allowed while elevator is moving.\n");
    }
    else if (strcmp(shared_mem->status, "Closed") != 0)
    {
        printf("Operation not allowed while doors are open.\n");
    }
    else
    {
        pthread_mutex_unlock(&shared_mem->mutex);
        return 1; // Operation allowed, exit function early
    }

    pthread_mutex_unlock(&shared_mem->mutex);
    return 0;
}

// Function to update the shared memory segment and broadcast changes.
// Arguments:
// - ptr_to_update: A pointer to the shared memory member that's going to be changed.
// - new_value: the value that the struct member should be updated to.
void update_shared_mem(uint8_t *ptr_to_update, int new_value)
{
    pthread_mutex_lock(&shared_mem->mutex);
    *ptr_to_update = new_value;
    pthread_cond_broadcast(&shared_mem->cond);
    pthread_mutex_unlock(&shared_mem->mutex);
}

// Function to handle floor changes based on direction.
// Arguments:
// - direction: 1 if going up, -1 if going down.
void handle_floor_change(int direction)
{
    pthread_mutex_lock(&shared_mem->mutex); // Lock shared state

    int current_floor = atoi(shared_mem->current_floor); // Convert current floor to integer

    if (shared_mem->current_floor[0] == 'B') // Handle basement floors
    {
        current_floor = atoi(shared_mem->current_floor + 1); // Skip 'B'
        if (direction == 1)                                  // Going up
        {
            if (strcmp(shared_mem->current_floor, "B1") == 0)
            {
                strcpy(shared_mem->destination_floor, "1"); // Destination to first floor
            }
            else
            {
                current_floor--; // Move to previous basement floor
            }
        }
        else // Going down
        {
            current_floor++; // Move to next basement floor
        }
        snprintf(shared_mem->destination_floor, 4, "B%02d", current_floor); // Set destination
    }
    else // Handle regular floors
    {
        if (direction == 1) // Going up
        {
            current_floor++; // Increment floor
        }
        else // Going down
        {
            current_floor--; // Decrement floor
        }

        // Set destination based on current floor limits
        if (current_floor <= 0)
        {
            snprintf(shared_mem->destination_floor, 4, "B1"); // Set to basement if below ground
        }
        else
        {
            if (current_floor > 999) // Cap at maximum floor
            {
                current_floor = 999;
            }
            snprintf(shared_mem->destination_floor, 4, "%d", current_floor); // Set destination
        }
    }

    pthread_cond_broadcast(&shared_mem->cond);
    pthread_mutex_unlock(&shared_mem->mutex);
    exit(EXIT_SUCCESS);
}