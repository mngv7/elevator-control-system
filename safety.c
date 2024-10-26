/**
 * safety.c - Elevator Safety Critical Component
 * 
 * This component has been developed with strict adherence to safety-critical software development practices. Key considerations include:
 * 
 * 1. Error Handling: Comprehensive error handling is implemented to manage all potential failure scenarios, ensuring the system can handle unexpected conditions gracefully.
 * 2. Data Consistency: Rigorous checks are in place to ensure data consistency, preventing invalid states and reducing the risk of undefined behavior.
 * 3. Thread Safety: Synchronization primitives (mutexes and condition variables) are used to protect shared resources and ensure thread safety, preventing race conditions.
 * 4. Resource Management: Proper management of resources such as shared memory and mutexes is ensured, avoiding memory leaks and resource contention.
 * 5. Compliance with MISRA C: Efforts have been made to adhere to MISRA C guidelines wherever possible, with documented justifications for any necessary deviations.
 * 6. Documentation: All deviations from safety-critical best practices are thoroughly documented and justified to provide clear reasoning for their necessity.
 * 7. Rule 21.13: Any value passed to a function in ctype.h shall be representable as an unsigned char or be the value EOF. The code ensures explicit casting of input values for isdigit and isalpha.
 * 
 * Deviations from MISRA C Guidelines:
 * 1) Infinite Loop: Necessary for continuous operation in real-time systems.
 * 2) Use of stdio.h: Used for custom print functions to handle error messages.
 * 3) Use of pthreads: Required for synchronization in a multi-threaded environment.
 * 4) snprintf: Used for safe string handling, despite being part of stdio.h.
 * 5) strncpy: Necessary for string manipulation, ensuring null-termination.
 * 
 * Justifications:
 * - Infinite loops are essential for real-time systems that need continuous operation.
 * - Standard I/O functions are used in a controlled manner for error reporting.
 * - Pthreads provide necessary synchronization primitives for concurrent operations.
 * - snprintf and strncpy are used with sufficient bounds checking to avoid buffer overflows.
 */

#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdio.h> // DEVIATION - 2

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
void custom_print(const char *string_to_print);
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
        if (snprintf(car_name, sizeof(car_name), "/car%s", argv[1]) < 0) // DEVIATION - 4
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
    // Justification - 1
    while (1) // DEVIATION - 1
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
            strncpy(shared_mem->status, "Opening", STATUS_LENGTH - 1U); // DEVIATION - 5
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

void custom_perror(const char *msg)
{
    if (msg != NULL)
    {
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

    if (isalpha((unsigned char)floor[0]) && floor[0] != 'B')
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

    assert(shared_mem != NULL); // Check for NULL pointer

    if (shared_mem->emergency_mode != EMERGENCY_MODE_ON)
    {
        // Create temporary buffers to ensure null-termination
        char current_floor_buffer[sizeof(shared_mem->current_floor)];         // Size: 4
        char destination_floor_buffer[sizeof(shared_mem->destination_floor)]; // Size: 4

        // Safely copy the current and destination floors to ensure null-termination
        strncpy(current_floor_buffer, shared_mem->current_floor, sizeof(current_floor_buffer) - 1); // DEVIATION - 5
        current_floor_buffer[sizeof(current_floor_buffer) - 1] = '\0';                              // Null-terminate

        strncpy(destination_floor_buffer, shared_mem->destination_floor, sizeof(destination_floor_buffer) - 1); // DEVIATION - 5
        destination_floor_buffer[sizeof(destination_floor_buffer) - 1] = '\0';                                  // Null-terminate

        // Validate floors
        if (!is_valid_floor(current_floor_buffer) || !is_valid_floor(destination_floor_buffer))
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