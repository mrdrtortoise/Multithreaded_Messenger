// libraries needed for various functions
// use man page for details
#include <sys/types.h>  // data types like size_t, socklen_t
#include <sys/socket.h> // socket(), bind(), connect(), listen(), accept()
#include <netinet/in.h> // sockaddr_in, htons(), htonl(), INADDR_ANY
#include <arpa/inet.h>  // inet_pton(), inet_ntop()
#include <unistd.h>     // close()
#include <string.h>     // memset(), memcpy()
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define BUFFER_SIZE 1024
#define SERVER_PORT 12000
#define SERVER_BACKLOG 10

//wrapper funcitons
void Listen(int serverSocketFD, int backlog){
    int result = listen(serverSocketFD, backlog);
    if(result == 0){
        printf("Server started listening on port: %d\n", SERVER_PORT);
    }
    else{
        perror("Listen Failed\n");
    }
}

void check(int retval){
    if(retval < 0){
        fprintf(stderr, "there was an error\n");
        exit(1);
    }
}

int set_sockdet_addr(struct sockaddr_in *addr, const char *ip, int port){
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);

    if(ip == NULL){
        addr->sin_addr.s_addr = INADDR_ANY;
    }
    else{
        if(inet_pton(AF_INET, ip, &addr->sin_addr) <= 0){
            return -1;
        }
    }

    return 0;
}


int tcp_socket_open(int port){

    int sd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in this_addr;
    check(set_sockdet_addr(&this_addr, NULL, port));                                                                                    

    int result = bind(sd, (struct sockaddr *)&this_addr, sizeof(this_addr));
    if(result == 0){
        printf("Successfully bound sockaddr to socket: %d\n", sd);
    }
    else{
        perror("Binding Failed\n");
    }
    
    return sd;
}

