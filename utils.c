#include "tcp.h"

char *commandTypes[COMMAND_NR] = {"conn", "say", "sayto", "mute", "unmute", "rename", "disconn", "kick"};

pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;
clientNode_t *head = NULL;
volatile sig_atomic_t running = 1;

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
    char *muted_clients[MAX_CONN_CLIENTS+1];
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
    for (int i = 0; i < MAX_CONN_CLIENTS+1; i++)
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
    printf("\n\nrunning clientHandler for newClient...\n\n");
    client_t *client = (client_t *)newClient;
    // arg is the argument of a normal command
    // arg2 is for sayTo when specifying the message that is to be sent to the client specified in arg
    char clientRequest[BUFFER_SIZE], arg[BUFFER_SIZE], recClientName[MAX_CLIENT_NAME], serverResponse[BUFFER_SIZE];
    char *responseFromCommandLaunch = NULL;
    int readResult, parseResult, argc;
    ssize_t amount_sent;
    while (1)
    {
        printf("Server Linked List\n");
        printServerLL();
        printf("list of muted clients for: %s\n", client->client_name);
        printClientMuted(client);
        readResult = recv(client->clientSocketFD, clientRequest, BUFFER_SIZE, 0);

        if (readResult > 0)
        {
            clientRequest[readResult] = '\0';
            printf("Server has recieved the a client request from client[%d]: %s\n", client->clientSocketFD, clientRequest);

            parseResult = parseClientRequest(&argc, arg, recClientName, clientRequest);
            if (parseResult > -1)
            {
                // MUST FREE RESPONSE ONCE DONE USING (MALLOC) -> TODO
                printf("launching command\n");
                responseFromCommandLaunch = launchCommand(parseResult, arg, recClientName, client);
                /*if (inServerList(client, false, false, NULL) && parseResult != 0 && client->client_name == NULL)
                {
                    printf("Client name is n/a. Invalid. Must set name\n");
                    strcpy(serverResponse, "\nSERVER >> please connect to the server first (using conn$ <username>)\n\n");
                    amount_sent = send(client->clientSocketFD, serverResponse, strlen(serverResponse), 0);
                }*/
                if (responseFromCommandLaunch != NULL)
                {
                    printf("sending response from command launch\n");
                    amount_sent = send(client->clientSocketFD, responseFromCommandLaunch, strlen(responseFromCommandLaunch), 0);
                    free(responseFromCommandLaunch);
                }
                else
                {
                    strcpy(serverResponse, "\nSERVER >> This Command Does Not Exist.\n\n");
                    amount_sent = send(client->clientSocketFD, serverResponse, strlen(serverResponse), 0);
                }
            }
            else if (parseResult == -2)
            {
                // the command entered had correct syntax but the recipient does not exist
                snprintf(serverResponse, BUFFER_SIZE, "\nSERVER >> The recipient (%s) does not exist.\n\n", recClientName);
                send(client->clientSocketFD, serverResponse, strlen(serverResponse), 0);
            }

            else
            {
                strcpy(serverResponse, "\nSERVER >> The input was invalid. Command might not exist.\nSERVER >> Check syntax (comm$ arg)\nSERVER >> except exit comm ('exit')\n\n");
                send(client->clientSocketFD, serverResponse, strlen(serverResponse), 0);
            }
        }
        else
        {
            printf("connection with client [%d] was broken...\n", client->clientSocketFD);
            printf("Terminating connection with this client\n");
            break;
        }
    }
    printf("\n\nattempting to free node containing disconnected client...\n");
    freeClientNodeFromLL(client);
    printf("closing clientSocketFD: %d\n", client->clientSocketFD);
    close(client->clientSocketFD);
    printf("freeing memory of clients muted list...\n\n");
    freeListOfMutedClients(client);
    printf("freeing memory of client...\n\n");
    free(client);
}

int parseClientRequest(int *argc, char arg[], char sendToClient[], char clientRequest[])
{
    /// using tokens
    char command[MAX_COMMAND_LEN];
    char *args[MAX_WORDCOUNT]; // conn$ arg1 arg2 NULL
    char *token = strtok(clientRequest, " ");
    *argc = 0;
    while (token != NULL && *argc < MAX_WORDCOUNT)
    {
        args[(*argc)++] = token;
        token = strtok(NULL, " ");
    }
    args[(*argc)] = NULL;

    // extracting the commandType
    snprintf(command, MAX_COMMAND_LEN, "%s", args[0]);
    int commandStrLen = strlen(command);
    if (command[commandStrLen - 1] == '$')
    {
        command[commandStrLen - 1] = '\0';
    }
    else
    {
        printf("\nfirst element in args[] did not have a '$' char at the end\n\n");
        return -1;
    }
    printf("\ncommand is now: %s\n\n", command);

    // extracting arg (first element after $ for normal commands and second element after $ for sayTo)
    // extracting recipient (if available)
    if((*argc) > 1){
        if (strcmp(command, "sayto") != 0)
        {
            strcpy(arg, args[1]);
            for (int i = 2; args[i] != NULL && i < MAX_WORDCOUNT; i++)
            {
                strcat(arg, " ");
                strcat(arg, args[i]);
            }
            printf("setting arg to: %s\n", arg);
            sendToClient = NULL;
        }
        else
        {
            if (inServerList(NULL, false, true, args[1]))
            {
                printf("setting arg to: %s\nsetting client recipient to: %s\n", args[2], args[1]);
                strcpy(sendToClient, args[1]);
                strcpy(arg, args[2]);
                for (int i = 3; args[i] != NULL && i < MAX_WORDCOUNT; i++)
                {
                    strcat(arg, " ");
                    strcat(arg, args[i]);
                }
            }
            else
            {
                strcpy(sendToClient, args[1]);
                printf("the recipient does not exist\n");
                return -2;
            }
        }
    }
    else{
        arg = NULL;
        sendToClient = NULL;
    }
    // finding which index in commandTypes the command is
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

char *launchCommand(int commandIdx, char *arg, char *client_r, client_t *client)
{
    char *serverResponse;
    if (inServerList(client, false, false, NULL) == true || (commandIdx == 0))
    {
        if (commandIdx == 0)
        {
            serverResponse = launchConn(arg, client);
            return serverResponse;
        }
        else if (commandIdx == 1)
        {
            serverResponse = launchSay(arg, client, NULL, false);
            return serverResponse;
        }
        else if (commandIdx == 2)
        {
            serverResponse = launchSay(arg, client, client_r, true);
            return serverResponse;
        }
        else if(commandIdx == 3){
            serverResponse = muteClient(client, arg, false); //*HAVE TO MAKE FUNCTION TO FREE LIST OF MUTED CLIENTS WHEN SERVER OR CLIENT EXITS*
            return serverResponse;
        }
        else if (commandIdx == 6)
        {
            serverResponse = launchDisconn(client);
            return serverResponse;
        }
        else
        {
            return NULL;
        }
    }
    else
    {
        char *buffer = malloc(BUFFER_SIZE);
        strcpy(buffer, "\nSERVER >> Connect To The Server Using 'conn$ <username>' Before Using Other Commands\n\n");
        return buffer;
    }
}

char *launchConn(char *arg, client_t *newClient)
{
    printf("entered launchConn\n");
    // check if client is already connectec
    char *buffer = malloc(BUFFER_SIZE);
    if (arg != NULL)
    {
        printf("arg is not NULL\n");
        // trying to get rdlock
        pthread_rwlock_rdlock(&rwlock);
        clientNode_t *curr = head;
        while (curr != NULL)
        {
            printf("looping inside launchConn\n");
            if ((curr->client->clientAddress.sin_port == newClient->clientAddress.sin_port) &&
                (curr->client->clientAddress.sin_addr.s_addr == newClient->clientAddress.sin_addr.s_addr))
            {
                printf("client already exits\n");
                snprintf(buffer, BUFFER_SIZE, "\nSERVER >> You are already conneced to the server with username: %s\n\n", curr->client->client_name);
                pthread_rwlock_unlock(&rwlock);
                return buffer;
            }
            else if (inServerList(newClient, true, true, arg) == true)
            {
                printf("client witht this name already exits\n");
                snprintf(buffer, BUFFER_SIZE, "\nSERVER >> This Username (%s) Is Already Taken By Someone Else. Please Choose Another One \n\n", curr->client->client_name);
                pthread_rwlock_unlock(&rwlock);
                return buffer;
            }
            curr = curr->next;
        }
        pthread_rwlock_unlock(&rwlock);
        pthread_rwlock_wrlock(&rwlock);
        printf("adding a new node to the serverList\n");
        strncpy(newClient->client_name, arg, MAX_CLIENT_NAME - 1);
        newClient->client_name[MAX_CLIENT_NAME - 1] = '\0';
        clientNode_t *newNode = malloc(sizeof(clientNode_t));
        newNode->client = newClient;
        newNode->next = head;
        head = newNode;
        printf("added to the server successfully\n");
        snprintf(buffer, BUFFER_SIZE, "\nSERVER >> Successfully connected you to the server with username: %s\n\n", newNode->client->client_name);
        pthread_rwlock_unlock(&rwlock);
    }
    else
    {
        printf("arg is NULL\n");
        strcpy(buffer, "Missing name to connect to chatroom with (conn$ <username>)\n");
    }
    return buffer;
}

char *launchDisconn(client_t *newClient)
{
    /// this function should be replaced mostly by just freeClientFromLL().
    // check for return value to see what to send to client
    //  check if this client is connected already
    char *buffer = malloc(BUFFER_SIZE);
    if (freeClientNodeFromLL(newClient))
    {
        strcpy(buffer, "\nDisconnecting...\n\n");
        return buffer;
    }
    else
    {
        strcpy(buffer, "Could not Disconnect. You are not connected\n");
        return buffer;
    }
}

char *launchSay(char *arg, client_t *client_s, char *client_r, bool sayTo)
{

    char msgBroadcast[1042]; // MAX_BUFFER_SIZE + MAX_CLIENT_NAME + 2
    char DMmessage[MAX_SERVER_RESPONSE]; // msgBroadcast + 1042
    int i, j;
    for (j = 0; client_s->client_name[j] != '\0'; j++)
    {
        msgBroadcast[j] = client_s->client_name[j];
    }
    msgBroadcast[j++] = ':';
    msgBroadcast[j++] = ' ';
    for (i = 0; arg[i] != '\0'; i++)
    {
        msgBroadcast[j++] = arg[i];
    }
    msgBroadcast[j++] = '\n';
    msgBroadcast[j++] = '\n';
    msgBroadcast[j++] = '\0';
    pthread_rwlock_rdlock(&rwlock);
    clientNode_t *curr = head;
    while (curr != NULL)
    {
        if (curr->client->clientSocketFD != client_s->clientSocketFD)
        {
            if(!sayTo && !isMuted(client_s, curr->client->client_name, true)){
                printf("\n%s is being sent a message from %s\n", curr->client->client_name, client_s->client_name);
                snprintf(DMmessage, 64, "\033[3;31m<Group Message message from ~%s>\033[32m\n", client_s->client_name);
                strcat(DMmessage, msgBroadcast);
                strcat(DMmessage, "\033[0m\n");
                send(curr->client->clientSocketFD, DMmessage, strlen(DMmessage), 0);
            }
            else{
                if((strcmp(curr->client->client_name, client_r)) == 0 && (!isMuted(client_s, client_r, true))){
                    printf("\n%s is sending an exclusive message to %s\n\n", client_s->client_name, client_r);
                    snprintf(DMmessage, 64, "\033[3;31m<dm message from ~%s>\033[32m\n", client_s->client_name);
                    strcat(DMmessage, msgBroadcast);
                    strcat(DMmessage, "\033[0m\n");
                    send(curr->client->clientSocketFD, DMmessage, strlen(DMmessage), 0);
                }
            }
        }
        curr = curr->next;
    }
    pthread_rwlock_unlock(&rwlock);
    char *serverResponse = malloc(BUFFER_SIZE);
    if(sayTo){
        snprintf(serverResponse, BUFFER_SIZE, "\nSERVER >> broadcasted message to %s...\n\n", client_r);
    }
    else{
        strcpy(serverResponse, "\nSERVER >> broadcasted message to connected clients...\n\n");
    }
    return serverResponse;
}

char *muteClient(client_t *client, char *nameOfClientToBeMuted, bool hasLock){
    int i = 0;
    if(!hasLock) pthread_rwlock_rdlock(&rwlock);
    char *buffer = malloc(BUFFER_SIZE);
    bool alreadyPresent = false;
    if(inServerList(NULL, true, true, nameOfClientToBeMuted)){
        while(client->muted_clients[i] != NULL && i < MAX_CONN_CLIENTS){
            if(strcmp(client->muted_clients[i], nameOfClientToBeMuted) == 0){
                strcpy(buffer, "the person you tried to mute is already muted\n");
                alreadyPresent = true;
                break;
            }
            i++;
        }
        if(i == MAX_CONN_CLIENTS){
            snprintf(buffer, BUFFER_SIZE, "You Have Reached The Maximum Number Of Muted Clients (%d)\n", MAX_CONN_CLIENTS);
        }
        else if(!alreadyPresent){
            printf("the index of muted_clients where the newly muted user will be stored is: %d\n", i);
            client->muted_clients[i] = strdup(nameOfClientToBeMuted);
            snprintf(buffer, BUFFER_SIZE, "you have muted: %s\nentry at location %d of your muted clients list\n", nameOfClientToBeMuted, i);
        } 
    }
    else{
        strcpy(buffer, "\nSERVER >> The Client That You Want To Mute Does Not Exist\n\n");
    }
    if(!hasLock) pthread_rwlock_unlock(&rwlock);
    return buffer;
}

bool freeClientNodeFromLL(client_t *rmClient)
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
        // free
        free(curr);
    }
    else
    {
        printf("client to be freed has not been found in linked list\n");
    }
    pthread_rwlock_unlock(&rwlock);
    return isConnected;
}

void freeListOfMutedClients(client_t *client){
    for(int i = 0; i < MAX_CONN_CLIENTS; i++){
        free(client->muted_clients[i]);
        client->muted_clients[i] = NULL;
    }
}

void *streamUserInput(void *clientSocketFD)
{
    printf("started user input stream\n");
    int *sd = (int *)clientSocketFD;
    char client_request[BUFFER_SIZE];
    while (running)
    {
        printf("\033[3m >\033[0m");
        if (fgets(client_request, BUFFER_SIZE, stdin) == NULL)
        {
            perror("fgets failed");
            exit(1);
        }
        /// Remove newclient_request (enter)
        client_request[strlen(client_request) - 1] = '\0';

        if (strcmp(client_request, "exit") == 0)
        {
            running = 0;
            exit(0);
        }
        if (running)
        {
            ssize_t amount_sent = send(*sd, client_request, strlen(client_request), 0);
            if (amount_sent < 0)
            {
                perror("send failed");
                running = 0;
                break;
            }
        }
    }
    return NULL;
}

void *streamServerOutput(void *clientSocketFD)
{
    printf("started server output stream\n");
    int *sd = (int *)clientSocketFD;
    char serverResponse[MAX_SERVER_RESPONSE];
    while (running)
    {
        int readResult = recv(*sd, serverResponse, MAX_SERVER_RESPONSE, 0);
        if (readResult > 0)
        {
            serverResponse[readResult] = '\0';
            printf("%s", serverResponse);
        }
        else if (readResult == 0)
        {
            printf("Connection Closed by Peer\n");
            close(*sd);
            running = 0;
            printf("running set to: %d\n", running);
            break;
        }
        else
        {
            perror("recv failed");
            close(*sd);
            running = 0;
            break;
        }
    }
    return NULL;
}

bool isMuted(client_t *client, char *name, bool hasLock){
    if(!hasLock) pthread_rwlock_rdlock(&rwlock);
    for(int i = 0; i < MAX_CONN_CLIENTS; i++){
        printf("checking if muted client[i] is equal to name");
        if(client->muted_clients[i] != NULL){
            if(strcmp(client->muted_clients[i], name) == 0){
                if(!hasLock) pthread_rwlock_unlock(&rwlock);
                return true;
            }
        }
    }
    if(!hasLock) pthread_rwlock_unlock(&rwlock);
    return false;
}

void printServerLL()
{
    pthread_rwlock_rdlock(&rwlock);
    clientNode_t *curr = head;
    char IPaddr[INET_ADDRSTRLEN];
    int i = 0;
    while (curr != NULL)
    {
        inet_ntop(AF_INET, &curr->client->clientAddress.sin_addr, IPaddr, INET_ADDRSTRLEN);
        printf("--> |");
        printf("IP: %s, Port: %u, Name: %s", IPaddr, ntohs(curr->client->clientAddress.sin_port), curr->client->client_name);
        printf("|\n");
        curr = curr->next;
    }
    printf("--> NULL\n");
    pthread_rwlock_unlock(&rwlock);
}

void printClientMuted(client_t *client){
    int i = 0;
    for(i = 0; i < MAX_CONN_CLIENTS; i++){
        printf("--> |");
        if(client->muted_clients[i] != NULL){
            printf("%s |", client->muted_clients[i]);
        }
        else{
            printf("NULL |");
        }
        if(i%5 == 0){
            printf("\n");
        }
    }
    printf("\n");
}

bool inServerList(client_t *client, bool hasLock, bool byClientName, char *name)
{
    if (!hasLock)
    {
        pthread_rwlock_rdlock(&rwlock);
    }
    clientNode_t *curr = head;
    while (curr != NULL)
    {
        if (byClientName && (strcmp(curr->client->client_name, name) == 0))
        {
            printf("client with the same name was found.\n");
            if (!hasLock)
                pthread_rwlock_unlock(&rwlock);
            return true;
        }
        if (!byClientName && (curr->client->clientSocketFD == client->clientSocketFD))
        {
            if (!hasLock)
                pthread_rwlock_unlock(&rwlock);
            return true;
        }
        curr = curr->next;
    }
    if (!hasLock)
    {
        pthread_rwlock_unlock(&rwlock);
    }
    printf("client with the same name was not found.\n");
    return false;
}