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
#include <ctype.h>

#define FLOOR_LENGTH 4U
#define STATUS_LENGTH 8U
#define MAX_CAR_NAME_LENGTH 100U
#define SUCCESS 0
#define FAILURE 1

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    char current_floor[FLOOR_LENGTH];
    char destination_floor[FLOOR_LENGTH];
    char status[STATUS_LENGTH];
    uint8_t open_button;
    uint8_t close_button;
    uint8_t door_obstruction;
    uint8_t overload;
    uint8_t emergency_stop;
    uint8_t individual_service_mode;
    uint8_t emergency_mode;
} car_shared_mem;

void custom_print(const char *str);
int string_compare(const char *str1, const char *str2);
int check_data_consistency(const car_shared_mem *shared_mem);
int is_valid_floor(const char *floor);

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        custom_print("Usage: {car name}\n");
        return FAILURE;
    }

    car_shared_mem *ptr = NULL;
    {
        char car_name[MAX_CAR_NAME_LENGTH];

        if (snprintf(car_name, sizeof(car_name), "/car%s", argv[1]) < 0)
        {
            return FAILURE;
        }

        int shm_fd = shm_open(car_name, O_RDWR, 0666);

        if (shm_fd == -1)
        {
            custom_print("Unable to access car ");
            custom_print(argv[1]);
            custom_print(".\n");
            return FAILURE;
        }

        ptr = (car_shared_mem *)mmap(NULL, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

        if (ptr == MAP_FAILED)
        {
            return FAILURE;
        }
    }

    /* This loop is an exception to MISRA C, document justification as per project requirements */
    while (1)
    {
        int ret = pthread_mutex_lock(&ptr->mutex);
        if (ret != SUCCESS)
        {
            custom_print("Failed to lock mutex.\n");
            break;
        }

        ret = pthread_cond_wait(&ptr->cond, &ptr->mutex);
        if (ret != SUCCESS)
        {
            custom_print("Failed to wait on condition variable.\n");
            pthread_mutex_unlock(&ptr->mutex);
            break;
        }

        if ((ptr->door_obstruction == 1U) && (string_compare(ptr->status, "Closing") == 1))
        {
            (void)strncpy(ptr->status, "Opening", STATUS_LENGTH - 1U);
            ptr->status[STATUS_LENGTH - 1U] = '\0';
        }

        if ((ptr->emergency_stop == 1U) && (ptr->emergency_mode == 0U))
        {
            custom_print("The emergency stop button has been pressed!\n");
            ptr->emergency_mode = 1U;
        }

        if (ptr->overload == 1U && ptr->emergency_mode == 0U)
        {
            custom_print("The overload sensor has been tripped!\n");
            ptr->emergency_mode = 1U;
        }

        if (check_data_consistency(ptr) == 0)
        {
            custom_print("Data consistency error!\n");
            ptr->emergency_mode = 1U;
        }

        ret = pthread_mutex_unlock(&ptr->mutex);
        if (ret != SUCCESS)
        {
            custom_print("Failed to unlock mutex.\n");
            break;
        }
    }

    return SUCCESS;
}

void custom_print(const char *str)
{
    if (str != NULL)
    {
        size_t length = strlen(str);

        ssize_t bytes_written = write(STDOUT_FILENO, str, length);
        if (bytes_written < 0)
        {
            perror("write error");
        }
    }
}

int string_compare(const char *str1, const char *str2)
{
    if (str1 == NULL || str2 == NULL)
    {
        return -1;
    }

    size_t len1 = strlen(str1);
    size_t len2 = strlen(str2);

    if (len1 != len2)
    {
        return 0;
    }

    for (size_t i = 0; i < len1; i++)
    {
        if (str1[i] != str2[i])
        {
            return 0;
        }
    }

    return 1;
}

int is_valid_floor(const char *floor)
{
    if (strlen(floor) > 3U)
    {
        return 0;
    }

    if (isalpha(floor[0]) && floor[0] != 'B')
    {
        return 0;
    }

    for (size_t i = 1U; i < strlen(floor); i++)
    {
        if (!isdigit((unsigned char)floor[i]))
        {
            return 0;
        }
    }

    return 1;
}

int check_data_consistency(const car_shared_mem *shared_mem)
{
    const char *status_names[] = {
        "Opening", "Open", "Closing", "Closed", "Between"};

    if (shared_mem->emergency_mode != 1U)
    {
        if (!is_valid_floor(shared_mem->current_floor) || !is_valid_floor(shared_mem->destination_floor))
        {
            return 0;
        }

        int is_valid_status = 0;

        for (size_t i = 0U; i < 5U; i++)
        {
            if (string_compare(shared_mem->status, status_names[i]) == 1)
            {
                is_valid_status = 1;
            }
        }

        if (is_valid_status == 0)
        {
            return 0;
        }

        if (shared_mem->open_button > 1U || shared_mem->close_button > 1U ||
            shared_mem->door_obstruction > 1U || shared_mem->overload > 1U ||
            shared_mem->emergency_stop > 1U || shared_mem->individual_service_mode > 1U ||
            shared_mem->emergency_mode > 1U)
        {
            return 0;
        }

        if (shared_mem->door_obstruction == 1U)
        {
            if (!(string_compare(shared_mem->status, "Opening") == 1 ||
                  string_compare(shared_mem->status, "Closing") == 1))
            {
                return 0;
            }
        }
    }

    return 1;
}