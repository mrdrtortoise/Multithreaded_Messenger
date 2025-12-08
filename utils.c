#include "tcp.h"

char *commandTypes[COMMAND_NR] = {"conn", "say", "sayto", "mute", "unmute", "rename", "disconn", "kick", "listmembers"};

// for server output stream to message buffer
char messages[MAX_MESSAGES][MAX_LEN];
int msg_count = 0;
int scrollOffset = 0;

pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;
pthread_mutex_t guiLock = PTHREAD_MUTEX_INITIALIZER;
// groupNode_t *top = NULL;
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
    char *muted_clients[MAX_CONN_CLIENTS + 1];
} client_t;

typedef struct clientNode
{
    client_t *client;
    struct clientNode *next;
} clientNode_t;

// groupNode structure will serve as the top nodes of each group. from each of these nodes there will be a "linked list"
// of client nodes that represent all the clients that are part of that group.
// will have to implement funcitons like "listgroups$", "join$ <groupname>"
// sayto should be able to say to any person on the server.
// say will broadcast locally within a group.
typedef struct groupNode
{
    char groupName[MAX_GROUP_NAME];
    struct groupNode *nextGroup;
    struct clientNode *firstClient;
} groupNode_t;

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

int tcp_socket_open(int port, WINDOW *outputWin)
{

    int sd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in this_addr;
    set_sockdet_addr(&this_addr, NULL, port);
    int curr_x, curr_y;
    getyx(outputWin, curr_y, curr_x);
    char buffer[BUFFER_SIZE];

    int result = bind(sd, (struct sockaddr *)&this_addr, sizeof(this_addr));
    if (result == 0)
    {
        if (outputWin != NULL)
        {
            snprintf(buffer, BUFFER_SIZE, "successfully bound to socket with clientSocketFd: %d.", sd);
            printToWindow(outputWin, buffer);
        }
        else
        {
            printf("successfully bound to socket with clientSocketFD: %d\n", sd);
        }
    }
    else
    {
        if (outputWin != NULL)
        {
            snprintf(buffer, BUFFER_SIZE, "binding failed");
            printToWindow(outputWin, buffer);
        }
        else
        {
            perror("binding failed\n");
        }
    }

    return sd;
}

void start_accepting_clients(int serverSocketFD)
{
    // use writer lock to modify shared top pointer
    /*pthread_rwlock_wrlock(&rwlock);
    groupNode_t *mainGroup = malloc(sizeof(groupNode_t));
    strcpy(mainGroup->groupName, "main");
    mainGroup->nextGroup = NULL;
    top = mainGroup;
    pthread_rwlock_unlock(&rwlock);*/
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
    for (int i = 0; i < MAX_CONN_CLIENTS + 1; i++)
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
                if (responseFromCommandLaunch != NULL)
                {
                    printf("sending response from command launch\n");
                    if (parseResult == 0 || parseResult == 5 || parseResult == 8)
                    {
                        amount_sent = send(client->clientSocketFD, responseFromCommandLaunch, strlen(responseFromCommandLaunch), 0);
                    }
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
    freeClientNodeFromLL(client, false);
    printf("closing clientSocketFD: %d\n", client->clientSocketFD);
    close(client->clientSocketFD);
    printf("freeing memory of clients muted list...\n\n");
    freeListOfMutedClients(client, false);
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
    printf("\ncommand = %s\n\n", command);
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
    if ((*argc) > 1)
    {
        if (strcmp(command, "sayto") != 0)
        {
            strcpy(arg, args[1]);
            for (int i = 2; args[i] != NULL && i < MAX_WORDCOUNT; i++)
            {
                printf("adding %s to args of sayto\n", args[i]);
                strcat(arg, " ");
                strcat(arg, args[i]);
            }
            printf("setting arg to: %s\n", arg);
            sendToClient = NULL;
        }
        else
        {
            if (inServerList(NULL, false, true, args[1]) != NULL)
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
    else
    {
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

    if (inServerList(client, true, false, NULL) != NULL || commandIdx == 0)
    {
        if (commandIdx == CONN)
        {
            serverResponse = launchConn(arg, client);
        }
        else if (commandIdx == SAY)
        {
            serverResponse = launchSay(arg, client, NULL, false);
        }
        else if (commandIdx == SAYTO)
        {
            serverResponse = launchSay(arg, client, client_r, true);
        }
        else if (commandIdx == MUTE)
        {
            serverResponse = muteClient(client, arg);
        }
        else if (commandIdx == UNMUTE)
        {
            serverResponse = unmuteClient(client, arg);
        }
        else if (commandIdx == RENAME)
        {
            serverResponse = renameClient(client, arg);
        }
        else if (commandIdx == DISCONN)
        {
            serverResponse = launchDisconn(client);
        }
        else if (commandIdx == KICK)
        {
            serverResponse = kickClient(client, arg);
        }
        else if (commandIdx == LISTMEMBERS)
        {
            printf("launching listmembers()\n");
            serverResponse = listMembers();
        }
        else
        {
            serverResponse = NULL;
        }
    }
    else
    {
        serverResponse = malloc(BUFFER_SIZE);
        strcpy(serverResponse, "\nSERVER >> Connect To The Server Using 'conn$ <username>' Before Using Other Commands\n\n");
    }

    return serverResponse;
}

// CONCURRENCY CHECKED
char *launchConn(char *arg, client_t *newClient)
{
    printf("entered launchConn\n");
    // check if client is already connectec
    char *buffer = malloc(BUFFER_SIZE);
    if (arg != NULL)
    {
        printf("arg is not NULL\n");
        if (pthread_rwlock_trywrlock(&rwlock) == 0)
        {
            printf("writelock is available\n");
        }
        else
        {
            printf("writelock is not available\n");
        }
        // Phase 1: Read lock for validation
        pthread_rwlock_rdlock(&rwlock);
        printf("got read lock in launchconn\n");

        // Check if client with same address is already connected
        // however right now I am only iterating through one branch of the tree. would have to go through all branches. TODO
        clientNode_t *curr = head;
        while (curr != NULL)
        {
            printf("looping inside launchConn\n");
            if ((curr->client->clientAddress.sin_port == newClient->clientAddress.sin_port) &&
                (curr->client->clientAddress.sin_addr.s_addr == newClient->clientAddress.sin_addr.s_addr))
            {
                printf("client already exits\n");
                snprintf(buffer, BUFFER_SIZE, "SERVER >> You are already conneced to the server with username: %s\n", curr->client->client_name);
                pthread_rwlock_unlock(&rwlock);
                return buffer;
            }
            curr = curr->next;
        }

        // Check if username is already taken
        if (inServerList(NULL, true, true, arg) != NULL)
        {
            printf("client with this name already exits\n");
            snprintf(buffer, BUFFER_SIZE, "SERVER >> This Username (%s) Is Already Taken By Someone Else. Please Choose Another One \n", arg);
            pthread_rwlock_unlock(&rwlock);
            return buffer;
        }

        // Phase 2: Brief write lock for the actual connection
        pthread_rwlock_unlock(&rwlock); // Release read lock
        printf("releasing read lock. aquiring the write lock\n");
        if (pthread_rwlock_trywrlock(&rwlock) == 0)
        {
            printf("writelock is available\n");
        }
        else
        {
            printf("writelock is not available\n");
        }
        pthread_rwlock_wrlock(&rwlock); // Acquire write lock

        // Re-validate that username is still available (state might have changed)
        if (inServerList(NULL, true, true, arg) != NULL)
        {
            // Username taken by another client during the brief window
            strcpy(buffer, "SERVER >> Username taken by another client. Please choose a different name.\n");
            pthread_rwlock_unlock(&rwlock);
            return buffer;
        }

        printf("adding a new node to the serverList\n");
        strncpy(newClient->client_name, arg, MAX_CLIENT_NAME - 1);
        newClient->client_name[MAX_CLIENT_NAME - 1] = '\0';
        clientNode_t *newNode = malloc(sizeof(clientNode_t));
        newNode->client = newClient;
        newNode->next = head;
        head = newNode;
        printf("added to the server successfully\n");
        snprintf(buffer, BUFFER_SIZE, "SERVER >> Successfully connected you to the server with username: %s\n", newNode->client->client_name);
        pthread_rwlock_unlock(&rwlock);
    }
    else
    {
        printf("arg is NULL\n");
        strcpy(buffer, "Missing name to connect to chatroom with (conn$ <username>)\n");
    }
    return buffer;
}

// CONCURRENCY CHECKED
char *launchDisconn(client_t *newClient)
{
    /// this function should be replaced mostly by just freeClientFromLL().
    // check for return value to see what to send to client
    //  check if this client is connected already
    if (pthread_rwlock_trywrlock(&rwlock) == 0)
    {
        printf("writelock is available\n");
    }
    else
    {
        printf("writelock is not available\n");
    }
    pthread_rwlock_wrlock(&rwlock);
    char *buffer = malloc(BUFFER_SIZE);
    if (freeClientNodeFromLL(newClient, true))
    {
        strcpy(buffer, "\nDisconnecting...\n\n");
        strcpy(newClient->client_name, "n/a");
        // freeing muted clients
        freeListOfMutedClients(newClient, true);
        printf("unlocking write lock\n");
        pthread_rwlock_unlock(&rwlock);
        if (pthread_rwlock_trywrlock(&rwlock) == 0)
        {
            printf("writelock is available\n");
        }
        else
        {
            printf("writelock is not available\n");
        }
        return buffer;
    }
    else
    {
        strcpy(buffer, "Could not Disconnect. You are not connected\n");
        pthread_rwlock_unlock(&rwlock);
        return buffer;
    }
}

// CONCURRENCY CHECKED
char *launchSay(char *arg, client_t *client_s, char *client_r, bool sayTo)
{
    char msgBroadcast[1042];             // MAX_BUFFER_SIZE + MAX_CLIENT_NAME + 2
    char DMmessage[MAX_SERVER_RESPONSE]; // msgBroadcast + 1042
    int i, j;
    // need lock here becasue trying to read client_s->client_name in the following code
    pthread_rwlock_rdlock(&rwlock);
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
    msgBroadcast[j++] = '\0';

    client_t *recipientClientStruct = NULL; // initialize the pointer that will be used to check if client_s is muted in the recieving clients muted clients list
    // first you have to find the branch of the server the client is in right now (aka which group)

    clientNode_t *curr = head; // retrieve the start of the server's linked list
    while (curr != NULL)
    {
        if (curr->client->clientSocketFD != client_s->clientSocketFD) // dont send message to the sending client
        {
            if (!sayTo && !isMuted(curr->client, client_s->client_name, true))
            { // first check if it is broadcast or dm. Also check if client_s is muted for currClient
                printf("\n%s is being sent a message from %s\n", curr->client->client_name, client_s->client_name);
                snprintf(DMmessage, 64, "<Group Message message from ~%s>\n", client_s->client_name);
                strcat(DMmessage, msgBroadcast);
                send(curr->client->clientSocketFD, DMmessage, strlen(DMmessage), 0);
            }
            else if (sayTo)
            {
                // finding address of client_r
                recipientClientStruct = inServerList(NULL, true, true, client_r);
                if (recipientClientStruct != NULL)
                {
                    if ((strcmp(curr->client->client_name, client_r)) == 0 && (!isMuted(recipientClientStruct, client_s->client_name, true)))
                    {
                        printf("\n%s is sending an exclusive message to %s\n\n", client_s->client_name, client_r);
                        snprintf(DMmessage, 64, "<dm message from ~%s>\n", client_s->client_name);
                        strcat(DMmessage, msgBroadcast);
                        send(curr->client->clientSocketFD, DMmessage, strlen(DMmessage), 0);
                    }
                }
            }
        }
        curr = curr->next;
    }
    pthread_rwlock_unlock(&rwlock);
    char *serverResponse = malloc(BUFFER_SIZE);
    if (sayTo)
    {
        snprintf(serverResponse, BUFFER_SIZE, "\nSERVER >> broadcasted message to %s...\n\n", client_r);
    }
    else
    {
        strcpy(serverResponse, "\nSERVER >> broadcasted message to connected clients...\n\n");
    }
    return serverResponse;
}

// CONCURRENCY CHECKED
char *muteClient(client_t *client, char *nameOfClientToBeMuted)
{
    int i = 0;
    // get rd lock for validation checks
    pthread_rwlock_rdlock(&rwlock);

    char *buffer = malloc(BUFFER_SIZE);
    bool alreadyPresent = false;
    if (inServerList(NULL, true, true, nameOfClientToBeMuted) != NULL)
    {
        while (client->muted_clients[i] != NULL && i < MAX_CONN_CLIENTS)
        {
            if (strcmp(client->muted_clients[i], nameOfClientToBeMuted) == 0)
            {
                strcpy(buffer, "the person you tried to mute is already muted\n");
                alreadyPresent = true;
                break;
            }
            i++;
        }
        if (i == MAX_CONN_CLIENTS)
        {
            snprintf(buffer, BUFFER_SIZE, "You Have Reached The Maximum Number Of Muted Clients (%d)\n", MAX_CONN_CLIENTS);
        }
        else if (!alreadyPresent)
        {
            pthread_rwlock_unlock(&rwlock); // unlock the readlock from before
            pthread_rwlock_wrlock(&rwlock); // get writer lock

            printf("the index of muted_clients where the newly muted user will be stored is: %d\n", i);
            client->muted_clients[i] = strdup(nameOfClientToBeMuted);
            snprintf(buffer, BUFFER_SIZE, "\nyou have muted: %s\nentry at location %d of your muted clients list\n\n", nameOfClientToBeMuted, i);

            pthread_rwlock_unlock(&rwlock); // unlock writer lock
            return buffer;
        }
    }
    else
    {
        strcpy(buffer, "\nSERVER >> The Client That You Want To Mute Does Not Exist\n\n");
    }
    pthread_rwlock_unlock(&rwlock);
    return buffer;
}

// CONCURRENCY CHECKED
char *unmuteClient(client_t *client, char *nameOfClientToBeUnmuted)
{
    int i = 0;
    // get the read lock to find the position in the array to change
    pthread_rwlock_rdlock(&rwlock);
    char *buffer = malloc(BUFFER_SIZE);
    bool found = false;
    // expecting muted clients list to be arranged with no NULL spaces between client names
    while (i < MAX_CONN_CLIENTS)
    {
        if (client->muted_clients[i] == NULL)
        {
            strcpy(buffer, "client to be unmuted does not exist\n");
            pthread_rwlock_unlock(&rwlock);
            return buffer;
        }
        else if (strcmp(client->muted_clients[i], nameOfClientToBeUnmuted) == 0)
        {
            found = true;
            break;
        }
    }
    // release the read lock
    pthread_rwlock_unlock(&rwlock);
    // get the write lock
    pthread_rwlock_wrlock(&rwlock);
    // free memory at mutedClients[i]
    free(client->muted_clients[i]);
    client->muted_clients[i] = NULL;
    // shift muted clients array to the left starting at i
    for (int j = i; client->muted_clients[j + 1] != NULL; j++)
    {
        client->muted_clients[j] = client->muted_clients[j + 1];
        client->muted_clients[j + 1] = NULL;
    }
    strcpy(buffer, "\nSERVER >> client was unmuted\n\n");
    pthread_rwlock_unlock(&rwlock);
    return buffer;
}

// CONCURRENCY CHECKED
char *renameClient(client_t *client, char *newName)
{
    pthread_rwlock_wrlock(&rwlock);
    char *buffer = malloc(BUFFER_SIZE);
    if (inServerList(NULL, true, true, newName) == NULL)
    {
        if (sizeof(newName) <= MAX_CLIENT_NAME)
        {
            strcpy(client->client_name, newName);
            snprintf(buffer, BUFFER_SIZE, "\nSERVER >> changed your name on the server to: %s\n\n", newName);
        }
        else
        {
            snprintf(buffer, BUFFER_SIZE, "\nSERVER >> couldnt change your name to: %s - (too long)\n\n", newName);
        }
    }
    else
    {
        snprintf(buffer, BUFFER_SIZE, "\nSERVER >> couldnt change your name to: %s - (Already Taken)\n\n", newName);
    }
    pthread_rwlock_unlock(&rwlock);
    return buffer;
}

char *kickClient(client_t *client, char *nameOfClientToBeKicked)
{
    char *buffer = malloc(BUFFER_SIZE);
    client_t *clientToBeKicked = inServerList(NULL, false, true, nameOfClientToBeKicked);
    if (ntohs(client->clientAddress.sin_port) == ADMIN_PORT_NUMBER)
    {
        if (clientToBeKicked != NULL)
        {
            launchDisconn(clientToBeKicked);
            snprintf(buffer, BUFFER_SIZE, "\nSERVER >> client: %s has been kicked from the server\n\n", nameOfClientToBeKicked);
        }
        else
        {
            snprintf(buffer, BUFFER_SIZE, "\nSERVER >> client: %s could not be kicked from the server. ERROR: DOES NOT EXIST\n\n", nameOfClientToBeKicked);
        }
    }
    else
    {
        snprintf(buffer, BUFFER_SIZE, "\nSERVER >> client: %s could not be kicked from the server. ERROR: NOT ADMIN\n\n", nameOfClientToBeKicked);
    }
    return buffer;
}

char *listMembers()
{
    pthread_rwlock_rdlock(&rwlock);
    size_t bufferlen;
    clientNode_t *curr = head;
    char *buffer = malloc(BUFFER_SIZE);
    strcpy(buffer, "8#");
    while (curr != NULL)
    {
        bufferlen = strlen(buffer);
        snprintf(buffer + bufferlen, BUFFER_SIZE - bufferlen, "%s\n", curr->client->client_name);
        curr = curr->next;
        printf("current buffer contents: %s\n", buffer);
    }
    printf("unlocking read lock in listmembers()\n");
    pthread_rwlock_unlock(&rwlock);
    return buffer;
}

// CONCURRENCY CHECKED
bool freeClientNodeFromLL(client_t *rmClient, bool haswrlock)
{
    // Phase 1: Read lock to find the client
    if (!haswrlock)
        pthread_rwlock_rdlock(&rwlock);

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

    if (!isConnected)
    {
        printf("client to be freed has not been found in linked list\n");
        if (!haswrlock)
            pthread_rwlock_unlock(&rwlock);
        return false;
    }

    // Phase 2: Brief write lock for the actual removal
    if (!haswrlock)
        pthread_rwlock_unlock(&rwlock); // Release read lock
    if (!haswrlock)
        pthread_rwlock_wrlock(&rwlock); // Acquire write lock

    // Re-validate that the client is still there
    clientNode_t *verify_curr = head;
    clientNode_t *verify_prev = NULL;
    bool still_connected = false;

    while (verify_curr != NULL)
    {
        if ((verify_curr->client->clientAddress.sin_port == rmClient->clientAddress.sin_port) &&
            (verify_curr->client->clientAddress.sin_addr.s_addr == rmClient->clientAddress.sin_addr.s_addr))
        {
            still_connected = true;
            curr = verify_curr;
            prev = verify_prev;
            break;
        }
        verify_prev = verify_curr;
        verify_curr = verify_curr->next;
    }

    if (still_connected)
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

    if (!haswrlock)
        pthread_rwlock_unlock(&rwlock);
    return still_connected;
}

// CONCURRENCY CHECKED
void freeListOfMutedClients(client_t *client, bool haswrlock)
{
    if (!haswrlock)
        pthread_rwlock_wrlock(&rwlock);
    for (int i = 0; i < MAX_CONN_CLIENTS; i++)
    {
        free(client->muted_clients[i]);
        client->muted_clients[i] = NULL;
    }
    if (!haswrlock)
        pthread_rwlock_unlock(&rwlock);
}

void *streamUserInput(void *clientSocketFD)
{
    threadArgs_t *args = (threadArgs_t *)clientSocketFD;
    char buffer[MAX_LEN - 5] = {0};
    int inputLen = 0;
    int x, y;
    getmaxyx(args->inputWindow, y, x);
    strcpy(buffer, "starting input stream");
    printToWindow(args->debugWindow, buffer);
    wrefresh(args->inputWindow);
    char client_request[BUFFER_SIZE];
    while (running)
    {
        renderOutput(args->outputWindow);

        werase(args->inputWindow);
        box(args->inputWindow, 0, 0);
        mvwprintw(args->inputWindow, 1, 1, " Input (F10 to quit) ");
        mvwprintw(args->inputWindow, y / 2, 1, "> %s", buffer);
        wmove(args->inputWindow, y / 2, 3 + inputLen);
        wrefresh(args->inputWindow);

        // get next char
        int ch = wgetch(args->inputWindow);

        if (ch == ERR)
        {
            // timeout expired to allow server updates
            continue;
        }
        else if (ch == KEY_UP)
        {
            pthread_mutex_lock(&guiLock);
            int visible, w;
            getmaxyx(args->outputWindow, visible, w);
            int maxScroll = msg_count - visible;
            if (maxScroll < 0)
                maxScroll = 0;
            if (scrollOffset < maxScroll)
                scrollOffset++;
            pthread_mutex_unlock(&guiLock);
        }
        else if (ch == KEY_DOWN)
        {
            pthread_mutex_lock(&guiLock);
            if (scrollOffset > 0)
            {
                scrollOffset--;
            }
            pthread_mutex_unlock(&guiLock);
        }
        else if (ch == '\n')
        {
            if (inputLen > 0)
            {
                char line[MAX_LEN];
                if (strcmp(line, "exit"))
                {
                    exit(0);
                }
                ssize_t amount_sent = send(args->sd, line, strlen(line), 0);
                if (amount_sent < 0)
                {
                    char error[15];
                    snprintf(error, 15, "Send Failed");
                    running = 0;
                    break;
                }

                // add message to outut message buffer
                snprintf(line, MAX_LEN, "you: %s", buffer);
                addMessage(line);

                // reset input buffer
                int bufferlen = strlen(buffer);
                for (int i = 0; i < bufferlen; i++)
                {
                    buffer[i] = '\0';
                }
                inputLen = 0;
                // jump back to the bottom
                pthread_mutex_lock(&guiLock);
                scrollOffset = 0;
                pthread_mutex_unlock(&guiLock);
            }
        }
        else if (isprint(ch))
        {
            if (inputLen < MAX_LEN - 1)
            {
                buffer[inputLen++] = ch;
                buffer[inputLen] = '\0';
            }
        }

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
            ssize_t amount_sent = send(args->sd, client_request, strlen(client_request), 0);
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
    threadArgs_t *args = (threadArgs_t *)clientSocketFD;
    char buffer[BUFFER_SIZE];
    strcpy(buffer, "starting output stream");
    printToWindow(args->debugWindow, buffer);
    wrefresh(args->inputWindow);
    char serverResponse[MAX_SERVER_RESPONSE];
    while (running)
    {
        int readResult = recv(args->sd, serverResponse, MAX_SERVER_RESPONSE, 0);
        if (readResult > 0)
        {
            strcpy(buffer, "adding message to queue");
            printToWindow(args->debugWindow, buffer);
            serverResponse[readResult] = '\0';
            addMessage(serverResponse);
        }
        else if (readResult == 0)
        {
            strcpy(buffer, "Connection Closed by Peer");
            printToWindow(args->debugWindow, buffer);
            close(args->sd);
            running = 0;
            break;
        }
        else
        {
            strcpy(buffer, "Recv Failed");
            printToWindow(args->debugWindow, buffer);
            close(args->sd);
            running = 0;
            break;
        }
    }
    return NULL;
}

// CONCURRENCY CHECKED
bool isMuted(client_t *client, char *name, bool hasLock)
{
    if (!hasLock)
        pthread_rwlock_rdlock(&rwlock);
    if (name == NULL)
    {
        if (!hasLock)
            pthread_rwlock_unlock(&rwlock);
        return false;
    }
    for (int i = 0; i < MAX_CONN_CLIENTS; i++)
    {
        printf("checking if muted client[%d] is equal to %s\n", i, name);
        if (client->muted_clients[i] != NULL)
        {
            if (strcmp(client->muted_clients[i], name) == 0)
            {
                if (!hasLock)
                    pthread_rwlock_unlock(&rwlock);
                printf("found %s in list of muted clients at location %d\n", name, i);
                return true;
            }
        }
    }
    if (!hasLock)
        pthread_rwlock_unlock(&rwlock);
    return false;
}

// CONCURRENCY CHECKED
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

// CONCURRENCY CHECKED
void printClientMuted(client_t *client)
{
    pthread_rwlock_rdlock(&rwlock);
    int i = 0;
    for (i = 0; i < MAX_CONN_CLIENTS; i++)
    {
        printf("--> |");
        if (client->muted_clients[i] != NULL)
        {
            printf("%s |", client->muted_clients[i]);
        }
        else
        {
            printf("NULL |");
        }
        if (i % 5 == 0)
        {
            printf("\n");
        }
    }
    printf("\n");
    pthread_rwlock_unlock(&rwlock);
}

// CONCURRENCY CHECKED
client_t *inServerList(client_t *client, bool hasLock, bool byClientName, char *name)
{
    if (!hasLock)
        pthread_rwlock_rdlock(&rwlock);

    clientNode_t *curr = head;
    while (curr != NULL)
    {
        if (byClientName && (strcmp(curr->client->client_name, name) == 0))
        {
            printf("client with the same name was found.\n");
            if (!hasLock)
                pthread_rwlock_unlock(&rwlock);
            return curr->client;
        }
        if (!byClientName && (curr->client->clientSocketFD == client->clientSocketFD))
        {
            if (!hasLock)
                pthread_rwlock_unlock(&rwlock);
            return curr->client;
        }
        curr = curr->next;
    }
    if (!hasLock)
        pthread_rwlock_unlock(&rwlock);
    printf("client with the same name was not found.\n");
    return NULL;
}

// CONCURRENCY IMPLEMENTED
void printToWindow(WINDOW *win, char *buffer)
{
    pthread_mutex_lock(&guiLock);
    int max_x, max_y;
    getmaxyx(win, max_y, max_x);

    if (max_x < 3)
        return;

    char writeBuffer[max_x - 2];
    int i = 0, j = 0, curr_x, curr_y;

    while (buffer[i] != '\0' && i < BUFFER_SIZE)
    {
        // Write the character first
        writeBuffer[j++] = buffer[i++];

        // If we filled the line or reached string end
        if (j == max_x - 3 || buffer[i] == '\0')
        {
            writeBuffer[j] = '\0';
            getyx(win, curr_y, curr_x);
            wprintw(win, "%s", writeBuffer);
            wmove(win, curr_y + 1, 0);
            wrefresh(win);

            // reset buffer
            j = 0;
            memset(writeBuffer, 0, sizeof(writeBuffer));
        }
    }
    getyx(win, curr_y, curr_x);
    wmove(win, curr_y + 1, 0);
    if (curr_y > 0.75 * max_y)
    {
        wscrl(win, 5);
        wmove(win, curr_y - 4, 0);
    }
    wrefresh(win);
    pthread_mutex_unlock(&guiLock);
}

// add message to message buffer with concurrency implemented
void addMessage(const char *text)
{
    pthread_mutex_lock(&guiLock);

    if (msg_count < MAX_MESSAGES)
    {
        strncpy(messages[msg_count], text, MAX_LEN - 1);
        messages[msg_count][MAX_LEN - 1] = '\0';
        msg_count++;
    }
    else
    {
        for (int i = 1; i < MAX_MESSAGES; i++)
        {
            strcpy(messages[i - 1], messages[i]);
        }
        strncpy(messages[MAX_MESSAGES - 1], text, MAX_LEN - 1);
        messages[MAX_MESSAGES - 1][MAX_LEN - 1] = '\0';
    }
    pthread_mutex_unlock(&guiLock);
}

void renderOutput(WINDOW *outputWin)
{
    int height, width;
    getmaxyx(outputWin, height, width);

    int visible = height; // since we are inside the outputContext (scrollable)

    werase(outputWin);

    pthread_mutex_lock(&guiLock);

    int maxScroll = msg_count - visible;
    if (maxScroll < 0)
        maxScroll = 0;
    if (scrollOffset > maxScroll)
        scrollOffset = maxScroll;
    if (scrollOffset < 0)
        scrollOffset = 0;

    int start = msg_count - visible - scrollOffset;
    if (start < 0)
        start = 0;

    int row = 0;
    for (int i = start; i < msg_count && row < height - 1; i++, row++)
    {
        printToWindow(outputWin, messages[i]);
    }

    pthread_mutex_unlock(&guiLock);
    wrefresh(outputWin);
}
