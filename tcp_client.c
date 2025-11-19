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
    while(1){
        if (fgets(client_request, BUFFER_SIZE, stdin) == NULL)
        {
            perror("fgets failed");
            exit(1);
        }
        /// Remove newclient_request (enter)
        client_request[strlen(client_request) - 1] = '\0';

        if(strcmp(client_request, "exit") == 0){
            break;
        }

        ssize_t amount_sent = send(sd, client_request, 1024, 0);
    }

    close(sd);

    return 0;
}