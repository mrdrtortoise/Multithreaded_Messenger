// libraries needed for various functions
// use man page for details
#define _GNU_SOURCE

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
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>

#define BUFFER_SIZE 1024
#define MAX_WORDCOUNT 64
#define SERVER_PORT 12001
#define SERVER_BACKLOG 5
#define MAX_CONN_CLIENTS 20
#define MAX_CLIENT_NAME 16
#define MAX_COMMAND_LEN 10
#define MAX_SERVER_RESPONSE 1106
#define ADMIN_PORT_NUMBER 2001
#define COMMAND_NR 8
#define CONN 0 // done
#define SAY 1 // done
#define SAYTO 2 // done
#define MUTE 3 // done
#define UNMUTE 4 // done
#define RENAME 5 // TODO
#define DISCONN 6 // done
#define KICK 7 // only for admin TODO

struct client;
typedef struct client client_t;

struct clientNode;
typedef struct clientNode clientNode_t;

typedef struct clientRead
{
    int clientSocketFD;
    struct sockaddr_in serverAddress;
} clientRead_t;

extern clientNode_t *head;

extern char *commandTypes[COMMAND_NR];

void check(int retval);

int set_sockdet_addr(struct sockaddr_in *addr, const char *ip, int port);

int tcp_socket_open(int port);

/// Server Functions
// memory management
bool freeClientNodeFromLL(client_t *rmClient, bool haswrlock);
void freeListOfMutedClients(client_t *client, bool haswrlock);
// wrapper funcitons
void Listen(int serverSocketFD, int backlog);

void start_accepting_clients(int serverSocketFD);
client_t *accept_new_client(int serverSocketFD);

void spawnClientHandlerThread(client_t *newClient);
void *clientHandler(void *newClient);

int parseClientRequest(int *argc, char arg1[], char arg2[], char clientRequest[]);

// launching different possible client commands
char *launchCommand(int commandIdx, char *arg, char *arg2, client_t *client);
char *launchConn(char *arg, client_t *client);
char *launchDisconn(client_t *newClient);
char *launchSay(char *arg, client_t *client_s, char *client_r, bool sayTo);
char *muteClient(client_t *client, char *nameOfClientToBeMuted);
char *unmuteClient(client_t *client, char *nameOfClientToBeUnmuted);
char *renameClient(client_t *client, char *newName);
char *kickClient(client_t *client, char *nameOfClientToBeKicked);

/// Client Functions
void *streamUserInput(void *clientSocketFD);
void *streamServerOutput(void *clientInfo);

// server list function
bool isMuted(client_t *client, char *name, bool hasLock);
// finds if client * or clientname is in the serverlist. returns a pointer to the client if found. returns NULL if not found
client_t *inServerList(client_t *client, bool hasLock, bool byClientName, char *name);
void printServerLL();
void printClientMuted(client_t *client);
