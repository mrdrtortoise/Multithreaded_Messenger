#include "tcp.h"

#define INPUT_Y_DIVIDER 6
#define INPUT_X_MULTIPLIER 0.75
#define DEBUG_Y_DIVIDER 4

int main(int argc, char *argv[])
{
    initscr();
    cbreak();
    int max_x, max_y;
    getmaxyx(stdscr, max_y, max_x);
    mvwprintw(stdscr, 0, max_x / 2 - 6, "Client Chat");
    refresh();

    int max_input_x, max_input_y;
    WINDOW *inputWindow = newwin(max_y / INPUT_Y_DIVIDER, max_x * INPUT_X_MULTIPLIER, max_y - (max_y / INPUT_Y_DIVIDER), 0);
    box(inputWindow, 0, 0);
    mvwprintw(inputWindow, 0, 1, "Input Window");
    wrefresh(inputWindow);
    getmaxyx(inputWindow, max_input_y, max_input_x);

    int max_output_x, max_output_y;
    WINDOW *outputWindow = newwin(max_y - (max_y / INPUT_Y_DIVIDER) - 1, max_x * INPUT_X_MULTIPLIER, 1, 0);
    scrollok(outputWindow, true);
    box(outputWindow, 0, 0);
    mvwprintw(outputWindow, 0, 1, "Output Window");
    wrefresh(outputWindow);
    getmaxyx(outputWindow, max_output_y, max_output_x);

    int max_debug_x, max_debug_y;
    WINDOW *debugWindow = newwin(max_y / DEBUG_Y_DIVIDER, max_x - max_output_x - 1, 1, max_output_x + 1);
    box(debugWindow, 0, 0);
    mvwprintw(debugWindow, 0, 1, "Debug Window");
    wmove(debugWindow, 1, 1);
    wrefresh(debugWindow);
    WINDOW *debugContent = derwin(debugWindow, (max_y / DEBUG_Y_DIVIDER) - 2, max_x - max_output_x - 3, 1, 1);
    scrollok(debugContent, true);
    wrefresh(debugContent);
    getmaxyx(debugWindow, max_debug_y, max_debug_x);

    wmove(inputWindow, (max_input_y / 2), 1);
    wprintw(inputWindow, "> ");
    wrefresh(inputWindow);

    int sd;
    if (argc > 1)
    {
        sd = tcp_socket_open(ADMIN_PORT_NUMBER, debugContent);
    }
    else
    {
        sd = tcp_socket_open(CLIENT_PORT, debugContent);
    }
    char buffer[BUFFER_SIZE];
    char retry;
    bool retryBool = true;
    int connection_attempts = 0;
    assert(sd > -1);

    struct sockaddr_in server_addr;
    set_sockdet_addr(&server_addr, "127.0.0.1", SERVER_PORT);

    threadArgs_t *tArgs = malloc(sizeof(threadArgs_t));
    tArgs->debugWindow = debugContent;
    tArgs->inputWindow = inputWindow;
    tArgs->outputWindow = outputWindow;
    tArgs->sd = sd;
    while (retryBool)
    {
        int result = connect(sd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (result >= 0)
        {
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, BUFFER_SIZE, "connection was successfull");
            printToWindow(debugContent, buffer);

            connection_attempts = 0;
            char client_request[BUFFER_SIZE], server_response[BUFFER_SIZE];
            pthread_t sendThread, recThread;

            pthread_create(&sendThread, NULL, streamUserInput, (void *)&sd);
            pthread_create(&recThread, NULL, streamServerOutput, (void *)&sd);

            pthread_join(sendThread, NULL);
            pthread_join(recThread, NULL);
        }
        else
        {
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, BUFFER_SIZE, "connection was unsuccessfull. press 'r' to try again");
            printToWindow(debugContent, buffer);
            retry = getch();
            if (retry == 'r')
            {
                retryBool = true;
            }
            else
            {
                retryBool = false;
            }
        }
    }
    endwin();
    free(tArgs);
    return 0;
}