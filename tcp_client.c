#include <stdio.h>
#include "tcp.h"

#define CLIENT_PORT 0

int main(){
    int sd = tcp_socket_open(CLIENT_PORT);
    assert(sd > -1);

    struct sockaddr_in server_addr;
    set_sockdet_addr(&server_addr, "127.0.0.1", SERVER_PORT);

    int result = connect(sd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if(result == 0){
        printf("connection was successfull\n");
    }

    char client_request[BUFFER_SIZE], server_response[BUFFER_SIZE];
    pthread_t sendThread, recThread;

    pthread_create(&sendThread, NULL, streamUserInput, (void *)&sd);
    pthread_create(&recThread, NULL, streamServerOutput, (void *)&sd);

    pthread_join(sendThread, NULL);
    pthread_join(recThread, NULL);

    close(sd);

    return 0;
}