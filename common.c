#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char get_call_direction(const char *source, const char *destination)
{
    int source_int = (source[0] == 'B') ? -atoi(source + 1) : atoi(source);
    int destination_int = (destination[0] == 'B') ? -atoi(destination + 1) : atoi(destination);

    if (source_int < destination_int)
        return 'U'; // Up
    if (source_int > destination_int)
        return 'D'; // Down
    return 'S';     // Same
}