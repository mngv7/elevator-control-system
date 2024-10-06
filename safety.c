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

int main(int argc, char **argv)
{
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

    if (close(shm_fd) == -1)
    {
        printf("Error closing shared memory file descriptor: %s\n", strerror(errno));
        return 1;
    }

    ptr = mmap(0, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (ptr == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    return 0;
}