#include "tcp.h"

#define INPUT_Y_DIVIDER 6
#define INPUT_X_MULTIPLIER 0.75

int main(int argc, char *argv[]){
    initscr();
    int max_x, max_y;
    getmaxyx(stdscr, max_y, max_x);
    mvwprintw(stdscr, 0, max_x/2 - 6, "Client Chat");
    refresh();
    
    int max_input_x, max_input_y;
    WINDOW *inputWindow = newwin(max_y/INPUT_Y_DIVIDER, max_x*INPUT_X_MULTIPLIER, max_y-(max_y/INPUT_Y_DIVIDER), 0);
    box(inputWindow, 0, 0);
    mvwprintw(inputWindow, 0, 1, "Input Window");
    wrefresh(inputWindow);
    getmaxyx(inputWindow, max_input_y, max_input_x);

    int max_output_x, max_output_y;
    WINDOW *outputWindow = newwin(max_y-(max_y/INPUT_Y_DIVIDER)-1, max_x*INPUT_X_MULTIPLIER, 1, 0);
    scrollok(outputWindow, true);
    box(outputWindow, 0, 0);
    mvwprintw(outputWindow, 0, 1, "Output Window");
    wrefresh(outputWindow);
    getmaxyx(outputWindow, max_output_y, max_output_x);

    int max_debug_x, max_debug_y;
    WINDOW *debugWindow = newwin(max_y-max_input_y, max_x-max_output_x-1, 1, max_output_x+1);
    scrollok(debugWindow, true);
    box(debugWindow, 0, 0);
    mvwprintw(debugWindow, 0, 1, "Debug Window");
    wrefresh(debugWindow);
    getmaxyx(debugWindow, max_debug_y, max_debug_x);

    wmove(inputWindow, (max_input_y/2), 1);
    wprintw(inputWindow, "> ");
    wrefresh(inputWindow);

    char buffer[BUFFER_SIZE];
    while(1){
        mvwgetnstr(inputWindow, max_input_y/2, 3, buffer, BUFFER_SIZE);
        wmove(inputWindow, max_input_y/2, 3);
        for(int i = 0; i < strlen(buffer); i++){
            waddch(inputWindow, ' ');
        }
    }


    int sd;
    if(argc > 1){
        sd = tcp_socket_open(ADMIN_PORT_NUMBER);
    }
    else{
        sd = tcp_socket_open(CLIENT_PORT);
    }
    int connection_attempts = 0;
    assert(sd > -1);

    struct sockaddr_in server_addr;
    set_sockdet_addr(&server_addr, "127.0.0.1", SERVER_PORT);

    threadArgs_t *tArgs = malloc(sizeof(threadArgs_t));
    tArgs->debugWindow = debugWindow;
    tArgs->inputWindow = inputWindow;
    tArgs->outputWindow = outputWindow;
    tArgs->sd = sd;

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
    endwin();
    free(tArgs);
    return 0;
}