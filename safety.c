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

void custom_print(const char *str);
int string_compare(const char *str1, const char *str2);
int check_data_consistency(car_shared_mem *shared_mem);
int is_valid_floor(const char *floor);

int main(int argc, char **argv)
{

    if (argc != 2)
    {
        custom_print("Usage: {car name}\n");
        return 1;
    }

    car_shared_mem *ptr;
    {
        char car_name[100];

        if (snprintf(car_name, sizeof(car_name), "/car%s", argv[1]) < 0)
        {
            return 1;
        }

        int shm_fd = shm_open(car_name, O_RDWR, 0666);

        if (shm_fd == -1)
        {
            custom_print("Unable to access car ");
            custom_print(argv[1]);
            custom_print(".\n");
            return 1;
        }

        ptr = mmap(0, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

        if (ptr == MAP_FAILED)
        {
            return 1;
        }
    }

    // This loop is an exception to MISRA C, document why later.
    while (1)
    {
        pthread_mutex_lock(&ptr->mutex);

        // Wait for the condition variable to be signaled
        pthread_cond_wait(&ptr->cond, &ptr->mutex);

        if ((ptr->door_obstruction == 1) && (string_compare(ptr->status, "Closing") == 1))
        {
            strcpy(ptr->status, "Opening");
        }

        if ((ptr->emergency_stop == 1) && (ptr->emergency_mode == 0))
        {
            custom_print("The emergency stop button has been pressed!\n");
            ptr->emergency_mode = 1;
        }

        if (ptr->overload == 1 && ptr->emergency_mode == 0)
        {
            custom_print("The overload sensor has been tripped!\n");
            ptr->emergency_mode = 1;
        }

        if (check_data_consistency(ptr) == 0)
        {
            // Emergency mode is not 1:
            // current_floor or destination_floor is not valid
            // or
            // Status is not one of the 5 valid statuses
            // or
            // Any of the uint8_t fields contain something other than 0 or 1.
            // or
            // Door obstruction is 1 and status is not either Opening or Closing
            // Print a message to inform the operator
            // Put car into emergency mode
            custom_print("Data consistency error!\n");
            ptr->emergency_mode = 1;
        }

        pthread_mutex_unlock(&ptr->mutex);
    }

    return 0;
}

void custom_print(const char *str)
{
    if (str != NULL)
    {
        size_t length = strlen(str);

        ssize_t bytes_written = write(1, str, length);
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

    for (int i = 0; i < len1; i++)
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
    if (strlen(floor) > 3)
        return 0;
    if (isalpha(floor[0]) && floor[0] != 'B')
        return 0;
    for (int i = 1; i < strlen(floor); i++)
    {
        if (!isdigit(floor[i]))
            return 0;
    }
    return 1;
}

int check_data_consistency(car_shared_mem *shared_mem)
{
    char *status_names[] = {
        "Opening", "Open", "Closing", "Closed", "Between"};

    if (shared_mem->emergency_mode == 0)
    {
        if (!is_valid_floor(shared_mem->current_floor) || !is_valid_floor(shared_mem->destination_floor))
        {
            return 0;
        }

        for (int i = 0; i < 5; i++)
        {
            int is_valid_status = 0;

            if (string_compare(shared_mem->status, status_names[i]) == 1)
            {
                is_valid_status = 1;
            }

            return is_valid_status;
        }

        if (shared_mem->open_button > 1 || shared_mem->close_button > 1 ||
            shared_mem->door_obstruction > 1 || shared_mem->overload > 1 ||
            shared_mem->emergency_stop > 1 || shared_mem->individual_service_mode > 1 ||
            shared_mem->emergency_mode > 1)
        {
            return 0;
        }

        if (shared_mem->door_obstruction == 1)
        {
            if (!(string_compare(shared_mem->status, "Opening") == 1) || !(string_compare(shared_mem->status, "Closing") == 1))
            {
                return 0;
            }
        }
    }

    return 1;
}
