#include "tcp.h"

typedef struct client{
    //client IP and Socket info
    int clientSocketFD;
    int serverSocketFD;
    struct sockaddr_in clientAddress;
    //stores error if an error occurs
    int error;
    //stores whether accepted or not
    bool is_accepted;

    //A standard client has the following attributes:
    char *client_name;
    char *muted_clients[MAX_CONN_CLIENTS];
} client_t;

typedef struct clientNode{
    client_t *client;
    struct clientNode *next;
} clientNode_t;


void check(int retval){
    if(retval < 0){
        fprintf(stderr, "there was an error\n");
        exit(1);
    }
}


void Listen(int serverSocketFD, int backlog){
    int result = listen(serverSocketFD, backlog);
    if(result == 0){
        printf("Server started listening on port: %d\n", SERVER_PORT);
    }
    else{
        perror("Listen Failed\n");
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

void start_accepting_clients(int serverSocketFD){
    while(1){
        client_t *newClient = accept_new_client(serverSocketFD);

        if(newClient->is_accepted == true){
            spawnClientHandlerThread(newClient);
        }
        else{
            free(newClient);
        }
    }   
}

client_t * accept_new_client(int serverSocketFD){
    struct sockaddr_in clientAddr;
    int clientAddrSize = sizeof(clientAddr);
    int clientSocketFD = accept(serverSocketFD, (struct sockaddr *)&clientAddr, &clientAddrSize);

    client_t * newClient = malloc(sizeof(client_t));
    newClient->clientAddress = clientAddr;
    newClient->client_name = NULL;
    newClient->serverSocketFD = serverSocketFD;
    for(int i = 0; i < MAX_CONN_CLIENTS; i++){
        newClient->muted_clients[i] = NULL;
    }
    if(clientSocketFD > 0){
        newClient->clientSocketFD = clientSocketFD;
        newClient->is_accepted = true;
    }
    else{
        perror("client could not be connected\n");
        newClient->error = clientSocketFD;
        newClient->is_accepted = false;
    }
    return newClient;
}

void spawnClientHandlerThread(client_t *newClient){
    pthread_t thread;
    pthread_create(&thread, NULL, clientHandler, (void *)newClient);
}

void *clientHandler(void *newClient){
    printf("running clientHandler for newClient\n");
    client_t *client = (client_t *)newClient;
    char clientRequest[BUFFER_SIZE], arg[BUFFER_SIZE], serverResponse[BUFFER_SIZE];
    int readResult, parseResult;
    ssize_t amount_sent;
    while(1){
        readResult = recv(client->clientSocketFD, clientRequest, BUFFER_SIZE, 0);

        if(readResult > 0){
            printf("Server has recieved the a client request from client[%d]: %s\n", client->clientSocketFD, clientRequest);

            parseResult = parseClientRequest(arg, clientRequest);
            if(parseResult > -1){
                launchCommand(parseResult, arg, client);
            }
            if(client->client_name == NULL){
                printf("Client name is NULL. Invalid. Must set name\n");
                strcpy(serverResponse, "please set a name before entering chatroom\n");
                amount_sent = send(client->clientSocketFD, serverResponse, BUFFER_SIZE, 0);
            }
        }
        else{
            printf("connection with client [%d] was broken...\n", client->clientSocketFD);
            printf("Terminating connection with this client\n");
            break;
        }
    }
    close(client->clientSocketFD);
    free(client);
}

int parseClientRequest(char parsedClientRequest[], char clientRequest[]){
    int n = strlen(clientRequest), j = 0;
    char command[n];
    bool foundSeperatorToken = false;
    for(int i = 0; clientRequest[i] != '$' && i < n; i++){
        if(i == n-1 && clientRequest[n-1] != '$'){
            printf("there was no '$' token found in the input. Invalid\n");
            return -1;
        }
        if(clientRequest[i] == '$'){
            foundSeperatorToken = true;
            i += 2; //skip past that token and the space that follows
        }
        if(!foundSeperatorToken){
            command[i] = clientRequest[i];
        }
        else{
            parsedClientRequest[j++] = clientRequest[i];
        }
    }
    for(int i = 0; i < COMMAND_NR; i++){
        if(strcmp(command, commandTypes[i]) == 0){
            return i;
        }
    }
    printf("Error: command entered by client not found\n");
    return -1;
}

void launchCommand(int commandIdx, char *arg, client_t *client){
    if(commandIdx == 0){
        launchConn(arg, client);
    }
}

void launchConn(char *arg, client_t *client){
    
}

void *streamUserInput(void *clientSocketFD){
    printf("started user input stream\n");
    int * sd = (int *)clientSocketFD;
    char client_request[BUFFER_SIZE];
    while(1){
        if (fgets(client_request, BUFFER_SIZE, stdin) == NULL)
        {
            perror("fgets failed");
            exit(1);
        }
        /// Remove newclient_request (enter)
        client_request[strlen(client_request) - 1] = '\0';

        if(strcmp(client_request, "exit") == 0){
            exit(0);
        }

        ssize_t amount_sent = send(*sd, client_request, strlen(client_request), 0);
    }
}

void *streamServerOutput(void *clientSocketFD){
    printf("started server output stream\n");
    int *sd = (int *)clientSocketFD;
    char serverResponse[BUFFER_SIZE];
    while(1){
        int readResult = recv(*sd, serverResponse, BUFFER_SIZE, 0);
        if(readResult > 0){
            serverResponse[readResult] = '\0';
            printf("%s", serverResponse);
        }
        else if(readResult == 0){
            printf("Connection Closed by Peer\n");
        }
        else{
            perror("recv failed");
            break;
        }
    }
    close(*sd);
}