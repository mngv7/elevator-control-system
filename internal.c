#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>    // For O_* constants
#include <sys/mman.h> // For mmap(), shm_open()
#include <sys/stat.h> // For mode constants
#include <unistd.h>   // For ftruncate(), close()

int main(int argc, char **argv)
{
    // Check the number of command line arguments.
    if (argc != 3)
    {
        printf("Usage: {car name} {operation}\n");
        exit(1);
    }

    int shm_fd = shm_open(argv[1], O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        printf("Unable to access %s\n", argv[1]);
        exit(1);
    }
}
