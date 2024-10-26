#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <stddef.h> // for size_t

// Function declarations
void recv_looped(int fd, void *buf, size_t sz);
void send_looped(int fd, const void *buf, size_t sz);
void send_message(int fd, const char *buf);
char *receive_msg(int fd);
int establish_connection_client();
int establish_connection_server();

#endif // NETWORK_UTILS_H
