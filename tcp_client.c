#include <stdio.h>
#include "tcp.h"

#define CLIENT_PORT 0

int main(){
    int sd = tcp_socket_open(CLIENT_PORT);
    int connection_attempts = 0;
    assert(sd > -1);

    struct sockaddr_in server_addr;
    set_sockdet_addr(&server_addr, "127.0.0.1", SERVER_PORT);

    while(connection_attempts < 5){
        int result = connect(sd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if(result >= 0){
            printf("\nconnection was successfull\n");
            connection_attempts = 0;
            char client_request[BUFFER_SIZE], server_response[BUFFER_SIZE];
            pthread_t sendThread, recThread;
        
            pthread_create(&sendThread, NULL, streamUserInput, (void *)&sd);
            pthread_create(&recThread, NULL, streamServerOutput, (void *)&sd);
        
            pthread_join(sendThread, NULL);
            pthread_join(recThread, NULL);
        }
        else{
            if(connection_attempts == 0){
                printf("Could Not Connect To Server...\n");
                printf("Trying again in...\n");
            }
            for (int i = 5; i >= 0; i--) {
                printf("\rAttempt[%d] %d", connection_attempts, i);
                fflush(stdout);
                sleep(1);
            }
            connection_attempts++;
        }
    }
    return 0;
}