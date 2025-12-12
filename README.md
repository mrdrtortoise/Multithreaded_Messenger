# Multithreaded Messenger  

Submission for the Second Assignment by Maximilian Heuberger (CID: 02579633) and Jad Saad (CID: 02614625)
---

---

## Project Structure

``` 
-
├── tcp.h # Shared header: constants, structs, function prototypes
├── utils.c # Server logic, command handling, groups, synchronization
├── tcp_client_n.c # ncurses-based TCP client
└── README.md

```

---
## Features

| # | Command        | Syntax                                   | Example                              | Description |
|---|---------------|-------------------------------------------|--------------------------------------|-------------|
| 1 | `conn`        | `conn$ <username>`                        | `conn$ alice`                        | Connects the client to the server with a unique username. Every client must connect before using other commands. |
| 2 | `say`         | `say$ <message>`                         | `say$ Hello everyone!`               | Broadcasts a message to **all clients in the same group** as the sender. |
| 3 | `sayto`       | `sayto$ <username> <message>`            | `sayto$ bob Hi Bob!`                 | Sends a **direct (private) message** to a specific client, provided both clients are in the same group. |
| 4 | `mute`        | `mute$ <username>`                       | `mute$ charlie`                      | Mutes a client so their messages are no longer displayed by the sender. |
| 5 | `unmute`      | `unmute$ <username>`                     | `unmute$ charlie`                    | Removes a client from the sender’s muted list, allowing their messages to be received again. |
| 6 | `rename`      | `rename$ <new_username>`                 | `rename$ alice2`                     | Changes the client’s username, provided the new name is not already taken. |
| 7 | `disconn`     | `disconn$`                               | `disconn$`                           | Disconnects the client from the server and removes them from their current group. |
| 8 | `kick`        | `kick$ <username>`                       | `kick$ eve`                          | **Admin-only command** that forcibly disconnects another client from the server. |
| 9 | `listmembers` | `listmembers$`                           | `listmembers$`                      | Displays a list of all clients currently in the same group as the sender. |
|10 | `creategroup` | `creategroup$ <group_name>`              | `creategroup$ studyroom`             | Creates a new chat group (chatroom) on the server if it does not already exist. |
|11 | `listgroups`  | `listgroups$`                            | `listgroups$`                       | Lists all existing groups currently available on the server. |
|12 | `joingroup`   | `joingroup$ <group_name>`                | `joingroup$ studyroom`               | Moves the client from their current group into the specified group. |

*The table above shows the 12 instructions that were implemented*

The above table shows all the instructions that are implemented in this project. Insturctions 1-8 were mandatory and instructions 9-12 are extra (all have to do with chatrooms). There is description for each of the instructions to further clarify their function.

The server maintains a tree-like structure of clients. There is linked list of groups, each of which branches a linked list of clients. The groupNode, clientNode, and client structures are key in this project for how data is organized, changed, and addressed.

Another Extension that was implemented is TCP communicaton rather than UDP. This ensures reliablility during package transmission, and also requires a three way handshake. Since this type of communication is a established connection between two sockets, the server must maintain a seperate thread for every client connection. This is a hazard as multiple clients can request the server to change something about the servers stored data on clients. This is where mutexes are essential. I used the pthreads reader-writer lock for mututal exclusion. Multiple threads can read and traverse the shared server datastructure at a time, but only on writer may be changing the server at one moment.


## Client–Server Communication
The Client-Server communication is handled via the TCP protocol (first extension). This ensures reliablility during data exchange by using a 3-way handshake before starting to communicate.

Functionalities of the Client side include:
- Establishing a connection with the server (it has to request to connect first)
- Launching two threads for the User, one for streaming the Server Output, and one for streaming the user input to the server.
- manage ncurses GUI for better user experience
- Multithreading in the client side allows for asynchronous I/O

### **Client Threads**
---

The Input stream thread on the client side has the following functionalities:
- Reads user input character-by-character
- Supports backspace, enter, ESC
- Sends commands using send()
- Updates output window with the submitted user input (If the user types say$ <msg>, their output window will show *"you: <msg>"*
- Also writes to a Debug window, although most of the debug prints have been taken out for the submission

The Server Output stream thread implements the following functions:
- Receives server messages using recv()
- Displays messages asynchronously (it can always display. Will talk about timeout later)
- Handles server message encoding in order to decide whether the output should go to the *output window* or the *input window*

### **Ncurses Windows**
---

The interface is divided into four windows: Input Window, Output Window, Info Window and Debug Window.

All do what their name implies. The info window is special, and part of the *groups* extension. More commands were implemented so the client can get information about which group they are in, and which groups are available to join. It also allows the user to list all the clients that are currently in their group. The output of all group-related instructions is printed in the info window.

All ncurses calls are protected by mutexes to ensure thread safety.

## Build Instructions
### Compiling the server
**Compiling the Server**
```
gcc tcp_server.c utils.c -o server -lpthread -lncurses
```
**Compiling the Client**
```
gcc tcp_client_n.c utils.c -o client_n -lpthread -lncurses
```
### Running the Application
**Start the Server**
```
./server
```
**Start a Normal Client**
```
./client_n
```
**Start an Admin Client (required for kick$)**
```
./client_n admin
```
Admin privileges are determined by the client’s port number (ADMIN_PORT_NUMBER = 2000).

---
