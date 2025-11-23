#include "tcp.h"

char *commandTypes[COMMAND_NR] = {"conn", "say", "sayto", "mute", "unmute", "rename", "disconn", "kick"};

pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;
clientNode_t *head = NULL;

typedef struct client
{
    // client IP and Socket info
    int clientSocketFD;
    int serverSocketFD;
    struct sockaddr_in clientAddress;
    // stores error if an error occurs
    int error;
    // stores whether accepted or not
    bool is_accepted;

    // A standard client has the following attributes:
    char client_name[MAX_CLIENT_NAME];
    char *muted_clients[MAX_CONN_CLIENTS];
} client_t;

typedef struct clientNode
{
    client_t *client;
    struct clientNode *next;
} clientNode_t;

void check(int retval)
{
    if (retval < 0)
    {
        fprintf(stderr, "there was an error\n");
        exit(1);
    }
}

void Listen(int serverSocketFD, int backlog)
{
    int result = listen(serverSocketFD, backlog);
    if (result == 0)
    {
        printf("Server started listening on port: %d\n", SERVER_PORT);
    }
    else
    {
        perror("Listen Failed\n");
    }
}

int set_sockdet_addr(struct sockaddr_in *addr, const char *ip, int port)
{
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);

    if (ip == NULL)
    {
        addr->sin_addr.s_addr = INADDR_ANY;
    }
    else
    {
        if (inet_pton(AF_INET, ip, &addr->sin_addr) <= 0)
        {
            return -1;
        }
    }

    return 0;
}

int tcp_socket_open(int port)
{

    int sd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in this_addr;
    check(set_sockdet_addr(&this_addr, NULL, port));

    int result = bind(sd, (struct sockaddr *)&this_addr, sizeof(this_addr));
    if (result == 0)
    {
        printf("Successfully bound sockaddr to socket: %d\n", sd);
    }
    else
    {
        perror("Binding Failed\n");
    }

    return sd;
}

void start_accepting_clients(int serverSocketFD)
{
    while (1)
    {
        client_t *newClient = accept_new_client(serverSocketFD);

        if (newClient->is_accepted == true)
        {
            spawnClientHandlerThread(newClient);
        }
        else
        {
            free(newClient);
        }
    }
}

client_t *accept_new_client(int serverSocketFD)
{
    struct sockaddr_in clientAddr;
    int clientAddrSize = sizeof(clientAddr);
    int clientSocketFD = accept(serverSocketFD, (struct sockaddr *)&clientAddr, &clientAddrSize);

    client_t *newClient = malloc(sizeof(client_t));
    newClient->clientAddress = clientAddr;
    strncpy(newClient->client_name, "n/a", MAX_CLIENT_NAME);
    newClient->serverSocketFD = serverSocketFD;
    for (int i = 0; i < MAX_CONN_CLIENTS; i++)
    {
        newClient->muted_clients[i] = NULL;
    }
    if (clientSocketFD > 0)
    {
        newClient->clientSocketFD = clientSocketFD;
        newClient->is_accepted = true;
    }
    else
    {
        perror("client could not be connected\n");
        newClient->error = clientSocketFD;
        newClient->is_accepted = false;
    }
    return newClient;
}

void spawnClientHandlerThread(client_t *newClient)
{
    pthread_t thread;
    pthread_create(&thread, NULL, clientHandler, (void *)newClient);
}

void *clientHandler(void *newClient)
{
    printf("running clientHandler for newClient\n");
    client_t *client = (client_t *)newClient;
    char clientRequest[BUFFER_SIZE], arg[BUFFER_SIZE], serverResponse[BUFFER_SIZE];
    char *responseFromCommandLaunch = NULL;
    int readResult, parseResult;
    ssize_t amount_sent;
    while (1)
    {
        readResult = recv(client->clientSocketFD, clientRequest, BUFFER_SIZE, 0);

        if (readResult > 0)
        {
            clientRequest[readResult] = '\0';
            printf("Server has recieved the a client request from client[%d]: %s\n", client->clientSocketFD, clientRequest);

            parseResult = parseClientRequest(arg, clientRequest);
            if (parseResult > -1)
            {
                // MUST FREE RESPONSE ONCE DONE USING (MALLOC) -> TODO
                responseFromCommandLaunch = launchCommand(parseResult, arg, client);
                if (strcmp(client->client_name, "n/a") == 0)
                {
                    printf("Client name is n/a. Invalid. Must set name\n");
                    strcpy(serverResponse, "\n>>please set a name before entering chatroom\n\n");
                    amount_sent = send(client->clientSocketFD, serverResponse, strlen(serverResponse), 0);
                }
                else if (responseFromCommandLaunch != NULL)
                {
                    /*if (strcmp(responseFromCommandLaunch, "Disconnecting...\n") == 0)
                    {
                        free(responseFromCommandLaunch);
                        continue;
                        ;
                    }*/
                    printf("sending response from command launch\n");
                    amount_sent = send(client->clientSocketFD, responseFromCommandLaunch, strlen(responseFromCommandLaunch), 0);
                    free(responseFromCommandLaunch);
                }
            }
            else
            {
                strcpy(serverResponse, "\n>>The input was invalid. Command might not exist.\n>>Check syntax (comm$ arg)\n>>except exit comm ('exit')\n\n");
                amount_sent = send(client->clientSocketFD, serverResponse, strlen(serverResponse), 0);
            }
        }
        else
        {
            printf("connection with client [%d] was broken...\n", client->clientSocketFD);
            printf("Terminating connection with this client\n");
            break;
        }
    }
    /*if (!freeClientFromLL(client))
    {
        // the problem with this is that we dont know if the client was connected before and then disconnected
        // and that is why freeClientFromLL() fails
        // or is it because it hasnt been added to the linked list but the memory for the client has been allocated.
        // ig you would have to make a different exit based on if the client types exit or disconn.
        // or you just delete the exit command altogether.
        // the client has to connect to the server before being able to disconnect
        // or does disconnect mean stay in the client handler but just remove from linked list. exit means terminate.
        // i think the latter is correct.
        // then dont free the client.
        // only free the node in the list
        close(client->clientSocketFD);
        free(client);
    }*/
    freeClientFromLL(client);
    close(client->clientSocketFD);
    free(client);
}

int parseClientRequest(char parsedClientRequest[], char clientRequest[])
{
    int n = strlen(clientRequest), j = 0, i = 0, c = 0;
    char command[n];
    bool foundSeperatorToken = false;
    for (i = 0; clientRequest[i] != '\0' && i < n; i++)
    {
        if (clientRequest[i] == '$')
        {
            foundSeperatorToken = true;
            if (clientRequest[i + 1] == ' ')
            {
                i += 2; // skip past that token and the space that follows
            }
            else
            {
                i++;
            }
        }
        if (!foundSeperatorToken)
        {
            command[c++] = clientRequest[i];
            printf("command[%d]: %c\n", i, command[i]);
        }
        else
        {
            parsedClientRequest[j++] = clientRequest[i];
        }
    }
    command[c] = '\0';
    parsedClientRequest[j] = '\0';
    if (!foundSeperatorToken)
    {
        printf("$ char was not found in request\n");
        return -1;
    }
    for (int i = 0; i < COMMAND_NR; i++)
    {
        if (strcmp(command, commandTypes[i]) == 0)
        {
            return i;
        }
    }
    printf("Error: command entered by client not found\n");
    return -1;
}

char *launchCommand(int commandIdx, char *arg, client_t *client)
{
    char *serverResponse;
    if (commandIdx == 0)
    {
        serverResponse = launchConn(arg, client);
        return serverResponse;
    }
    else if (commandIdx == 6)
    {
        serverResponse = launchDisconn(client);
        return serverResponse;
    }
    return NULL;
}

char *launchConn(char *arg, client_t *newClient)
{
    // check if client is already connectec
    pthread_rwlock_rdlock(&rwlock);
    clientNode_t *curr = head;
    char *buffer = malloc(BUFFER_SIZE);
    while (curr != NULL)
    {
        if ((curr->client->clientAddress.sin_port == newClient->clientAddress.sin_port) &&
            (curr->client->clientAddress.sin_addr.s_addr == newClient->clientAddress.sin_addr.s_addr))
        {
            printf("client already exits\n");
            snprintf(buffer, BUFFER_SIZE, "\n>>You are already conneced to the server with username: %s\n\n", curr->client->client_name);
            pthread_rwlock_unlock(&rwlock);
            return buffer;
        }
        curr = curr->next;
    }
    pthread_rwlock_unlock(&rwlock);
    pthread_rwlock_wrlock(&rwlock);
    strncpy(newClient->client_name, arg, MAX_CLIENT_NAME - 1);
    newClient->client_name[MAX_CLIENT_NAME - 1] = '\0';
    clientNode_t *newNode = malloc(sizeof(clientNode_t));
    newNode->client = newClient;
    newNode->next = head;
    head = newNode;
    printf("added to the server successfully\n");
    snprintf(buffer, BUFFER_SIZE, "\n>>Successfully connected you to the server with username: %s\n\n", newNode->client->client_name);
    pthread_rwlock_unlock(&rwlock);
    return buffer;
}

char *launchDisconn(client_t *newClient)
{
    /// this function should be replaced mostly by just freeClientFromLL().
    // check for return value to see what to send to client
    //  check if this client is connected already
    char *buffer = malloc(BUFFER_SIZE);
    if (freeClientFromLL(newClient))
    {
        strcpy(buffer, "Disconnecting...\n");
        return buffer;
    }
    else
    {
        strcpy(buffer, "Could not Disconnect. You are not connected\n");
        return buffer;
    }
}

bool freeClientFromLL(client_t *rmClient)
{
    pthread_rwlock_wrlock(&rwlock);
    clientNode_t *curr = head;
    clientNode_t *prev = NULL;
    bool isConnected = false;
    while (curr != NULL)
    {
        if ((curr->client->clientAddress.sin_port == rmClient->clientAddress.sin_port) &&
            (curr->client->clientAddress.sin_addr.s_addr == rmClient->clientAddress.sin_addr.s_addr))
        {
            printf("client to be freed has been found in the linked list\n");
            isConnected = true;
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    if (isConnected)
    {
        if (prev == NULL)
        {
            head = curr->next;
        }
        else
        {
            prev->next = curr->next;
        }
        // free the clientNode and the client inside
        free(curr);
    }
    else
    {
        printf("client to be freed has not been found in linked list\n");
    }
    pthread_rwlock_unlock(&rwlock);
    return isConnected;
}

void *streamUserInput(void *clientSocketFD)
{
    printf("started user input stream\n");
    int *sd = (int *)clientSocketFD;
    char client_request[BUFFER_SIZE];
    while (1)
    {
        if (fgets(client_request, BUFFER_SIZE, stdin) == NULL)
        {
            perror("fgets failed");
            exit(1);
        }
        /// Remove newclient_request (enter)
        client_request[strlen(client_request) - 1] = '\0';

        if (strcmp(client_request, "exit") == 0)
        {
            exit(0);
        }

        ssize_t amount_sent = send(*sd, client_request, strlen(client_request), 0);
    }
}

void *streamServerOutput(void *clientSocketFD)
{
    printf("started server output stream\n");
    int *sd = (int *)clientSocketFD;
    char serverResponse[BUFFER_SIZE];
    while (1)
    {
        int readResult = recv(*sd, serverResponse, BUFFER_SIZE, 0);
        if (readResult > 0)
        {
            serverResponse[readResult] = '\0';
            printf("%s", serverResponse);
        }
        else if (readResult == 0)
        {
            printf("Connection Closed by Peer\n");
        }
        else
        {
            perror("recv failed");
            break;
        }
    }
    close(*sd);
}