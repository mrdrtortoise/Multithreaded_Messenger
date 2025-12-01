#include <stdio.h>
#include <stdlib.h>
#include "tcp.h"

// use a reader writer lock to allow only one writer in a critical section at a time
// but multiple readers can read the linked list at the same time

int main()
{
    int sd;
    check(sd = tcp_socket_open(SERVER_PORT, NULL));
    Listen(sd, SERVER_BACKLOG);

    start_accepting_clients(sd);
}