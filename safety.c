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

void custom_print(const char *str);
int string_compare(const char *str1, const char *str2);

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

int main(int argc, char **argv)
{
    car_shared_mem *ptr;

    if (argc != 2)
    {
        custom_print("Usage: {car name}\n");
        return 1;
    }

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

    while (1)
    {
        // Lock the shared memory structure
        pthread_mutex_lock(&ptr->mutex);

        // Wait for the condition variable to be signaled
        pthread_cond_wait(&ptr->cond, &ptr->mutex);

        // Update shared memory based on conditions
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

        // Unlock the shared memory structure
        pthread_mutex_unlock(&ptr->mutex);
    }

    return 0;
}

void custom_print(const char *str)
{
    if (str != NULL)
    {
        size_t length = strlen(str); // Get the length directly

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
