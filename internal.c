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

int main(int argc, char **argv)
{
    // Check the number of command line arguments.
    if (argc != 3)
    {
        printf("Usage: {car name} {operation}\n");
        exit(1);
    }
}
