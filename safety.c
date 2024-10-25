#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <ctype.h>
#include "common.h"

// Not allowed, will have to remove later.
#include <stdio.h>

// TODO:
// Write a custom function for snprintf.

// Constants for clarity and magic number avoidance
const uint32_t STATUS_LENGTH = 8U;
const uint32_t MAX_CAR_NAME_LENGTH = 100U;
const uint8_t DOOR_OBSTRUCTION_ON = 1U;
const uint8_t EMERGENCY_STOP_ON = 1U;
const uint8_t EMERGENCY_MODE_ON = 1U;
const uint8_t EMERGENCY_MODE_OFF = 0U;
const uint8_t EXIT_FAILURE = 1U;
const uint8_t EXIT_SUCCESS = 0U;

// Function prototypes
void custom_print(const char *str);
int check_data_consistency(const car_shared_mem *shared_mem);
int is_valid_floor(const char *floor);

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        custom_print("Usage: {car name}\n");
        return EXIT_FAILURE;
    }

    car_shared_mem *shared_mem = NULL;
    {
        char car_name[MAX_CAR_NAME_LENGTH];

        // Construct the shared memory name safely
        if (snprintf(car_name, sizeof(car_name), "/car%s", argv[1]) < 0)
        {
            custom_print("Failed to create car name.\n");
            return EXIT_FAILURE;
        }

        int shm_fd = shm_open(car_name, O_RDWR, 0666);
        if (shm_fd == -1)
        {
            custom_print("Unable to access car ");
            custom_print(argv[1]);
            custom_print(".\n");
            return EXIT_FAILURE;
        }

        shared_mem = (car_shared_mem *)mmap(NULL, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (shared_mem == MAP_FAILED)
        {
            custom_print("Memory mapping failed.\n");
            return EXIT_FAILURE;
        }
    }

    // This loop is an exception to MISRA C; document justification as per project requirements
    while (1)
    {
        int ret = pthread_mutex_lock(&shared_mem->mutex);
        if (ret != 0)
        {
            custom_print("Failed to lock mutex.\n");
            break;
        }

        ret = pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);
        if (ret != 0)
        {
            custom_print("Failed to wait on condition variable.\n");
            pthread_mutex_unlock(&shared_mem->mutex);
            break;
        }

        // Check for door obstruction
        if ((shared_mem->door_obstruction == DOOR_OBSTRUCTION_ON) &&
            (strcmp(shared_mem->status, "Closing\n") == 0))
        {
            strncpy(shared_mem->status, "Opening", STATUS_LENGTH - 1U);
            shared_mem->status[STATUS_LENGTH - 1U] = '\0';
        }

        // Check for emergency stop condition
        if ((shared_mem->emergency_stop == EMERGENCY_STOP_ON) &&
            (shared_mem->emergency_mode == EMERGENCY_MODE_OFF))
        {
            custom_print("The emergency stop button has been pressed!\n");
            shared_mem->emergency_mode = EMERGENCY_MODE_ON;
        }

        // Check for overload condition
        if ((shared_mem->overload == 1U) && (shared_mem->emergency_mode == EMERGENCY_MODE_OFF))
        {
            custom_print("The overload sensor has been tripped!\n");
            shared_mem->emergency_mode = EMERGENCY_MODE_ON;
        }

        // Validate data consistency
        if (check_data_consistency(shared_mem) == 0)
        {
            custom_print("Data consistency error!\n");
            shared_mem->emergency_mode = EMERGENCY_MODE_ON;
        }

        ret = pthread_mutex_unlock(&shared_mem->mutex);
        if (ret != 0)
        {
            custom_print("Failed to unlock mutex.\n");
            break;
        }
    }

    return EXIT_SUCCESS;
}

void custom_perror(const char *msg) {
    if (msg != NULL) {
        write(STDERR_FILENO, msg, strlen(msg));
    }
}

void custom_print(const char *str)
{
    if (str != NULL)
    {
        size_t length = strlen(str);
        ssize_t bytes_written = write(STDOUT_FILENO, str, length);
        if (bytes_written < 0)
        {
            custom_perror("write fail\n");
        }
    }
}

int is_valid_floor(const char *floor)
{
    if (strlen(floor) > 3U)
    {
        return 0; // Invalid length
    }

    if (isalpha(floor[0]) && floor[0] != 'B')
    {
        return 0; // Invalid floor designation
    }

    for (size_t i = 1U; i < strlen(floor); i++)
    {
        if (!isdigit((unsigned char)floor[i]))
        {
            return 0; // Invalid character
        }
    }

    return 1; // Valid floor
}

int check_data_consistency(const car_shared_mem *shared_mem)
{
    const char *status_names[] = {
        "Opening", "Open", "Closing", "Closed", "Between"};

    if (shared_mem->emergency_mode != EMERGENCY_MODE_ON)
    {
        if (!is_valid_floor(shared_mem->current_floor) || !is_valid_floor(shared_mem->destination_floor))
        {
            return 0; // Invalid floor data
        }

        int is_valid_status = 0;
        for (size_t i = 0U; i < sizeof(status_names) / sizeof(status_names[0]); i++)
        {
            if (strcmp(shared_mem->status, status_names[i]) == 0)
            {
                is_valid_status = 1; // Valid status found
                break;
            }
        }

        if (is_valid_status == 0)
        {
            return 0; // Invalid status
        }

        // Check if button states are valid
        if (shared_mem->open_button > 1U || shared_mem->close_button > 1U ||
            shared_mem->door_obstruction > 1U || shared_mem->overload > 1U ||
            shared_mem->emergency_stop > 1U || shared_mem->individual_service_mode > 1U ||
            shared_mem->emergency_mode > 1U)
        {
            return 0; // Invalid button state
        }

        // Check door obstruction state
        if (shared_mem->door_obstruction == DOOR_OBSTRUCTION_ON)
        {
            if (!(strcmp(shared_mem->status, "Opening") == 0 ||
                  strcmp(shared_mem->status, "Closing") == 0))
            {
                return 0; // Invalid status for door obstruction
            }
        }
    }

    return 1; // Data is consistent
}
