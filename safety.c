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

void *wait_for_signal_thread(void *ptr);
void *update_shared_mem_thread(void *ptr);
void custom_print(const char *str);

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
    pthread_t consumer;
    pthread_t producer;

    car_shared_mem *ptr;

    if (argc != 2)
    {
        printf("Usage: {car name}\n");
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
        printf("Unable to access car %s\n", argv[1]);
        return 1;
    }

    ptr = mmap(0, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (ptr == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    pthread_create(&consumer, NULL, wait_for_signal_thread, ptr);
    pthread_create(&producer, NULL, update_shared_mem_thread, ptr);

    if (pthread_join(consumer, NULL) != 0)
    {
        printf("pthread join fail.\n");
        return 1;
    }

    if (pthread_join(producer, NULL) != 0)
    {
        printf("pthread join fail.\n");
        return 1;
    }

    return 0;
}

void *wait_for_signal_thread(void *ptr)
{
    car_shared_mem *shared_mem = (car_shared_mem *)ptr;

    while (1)
    {
        pthread_mutex_lock(&shared_mem->mutex);

        // Wait for the condition signal
        pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);

        pthread_mutex_unlock(&shared_mem->mutex);

        usleep(100000); // Sleep for 100ms
    }

    return NULL;
}

void *update_shared_mem_thread(void *ptr)
{
    car_shared_mem *shared_mem = (car_shared_mem *)ptr;

    while (1)
    {
        pthread_mutex_lock(&shared_mem->mutex);

        if (shared_mem->door_obstruction == 1 && strcmp(shared_mem->status, "Closing") == 0)
        {
            strcpy(shared_mem->status, "Opening");
        }

        if ((shared_mem->emergency_stop == 1) && (shared_mem->emergency_mode == 0))
        {
            custom_print("The emergency stop button has been pressed!\n");
        }

        pthread_mutex_unlock(&shared_mem->mutex);

        // Signal the condition variable to wake up the other thread
        pthread_cond_signal(&shared_mem->cond);

        usleep(100000); // Sleep for 100ms
    }

    return NULL;
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
