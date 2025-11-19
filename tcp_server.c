#include <stdio.h>
#include <stdlib.h>
#include "tcp.h"

int main(){
    int sd;
    check(sd = tcp_socket_open(SERVER_PORT));
    Listen(sd, SERVER_BACKLOG);

    struct sockaddr_in clientAddr;
    socklen_t clientAddrSize = sizeof(clientAddr);

    int clientSocketFD = accept(sd, (struct sockaddr *)&clientAddr, &clientAddrSize);
    if(clientSocketFD >= 0){
        printf("accepted a client with clientSocketFD: %d\n", clientSocketFD);
    }
    else{
        perror("client could not be accepted");
    }
    char buffer[1024];
    
    while(1){
        ssize_t amount_recieved = recv(clientSocketFD, buffer, 1024, 0);
        if(amount_recieved > 0){
            buffer[amount_recieved] = 0;
            printf("%s\n", buffer);
        }

        if(amount_recieved == 0){
            break;
        }

    }

    close(clientSocketFD);
    shutdown(sd, SHUT_RDWR);
}