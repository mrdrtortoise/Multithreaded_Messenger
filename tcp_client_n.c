#include "tcp.h"
#include <pthread.h>

#define INPUT_Y_DIVIDER 6
#define INPUT_X_MULTIPLIER 0.75
#define DEBUG_Y_DIVIDER 2

pthread_mutex_t ncurses_mutex = PTHREAD_MUTEX_INITIALIZER;

extern volatile sig_atomic_t running;

void safe_wprintw(WINDOW *win, const char *message)
{
    pthread_mutex_lock(&ncurses_mutex);
    wprintw(win, "%s", message);
    wrefresh(win);
    pthread_mutex_unlock(&ncurses_mutex);
}

// refresh input area
void safe_refresh_input(WINDOW *input_win, const char *prompt, const char *current_input)
{
    pthread_mutex_lock(&ncurses_mutex);

    int max_y, max_x;
    getmaxyx(input_win, max_y, max_x);

    for (int y = 1; y < max_y - 1; y++)
    {
        wmove(input_win, y, 1);
        wclrtoeol(input_win);
    }

    box(input_win, 0, 0);

    mvwprintw(input_win, 0, 1, "Input Window");
    mvwprintw(input_win, 1, 1, "%s%s", prompt, current_input);
    wclrtoeol(input_win);
    wmove(input_win, 1, 1 + strlen(prompt) + strlen(current_input));
    wrefresh(input_win);

    pthread_mutex_unlock(&ncurses_mutex);
}

// Thread-safe user input thread - handles character-by-character input
void *safe_streamUserInput(void *arguments)
{
    threadArgs_t *args = (threadArgs_t *)arguments;
    char input_buffer[BUFFER_SIZE] = "";
    int pos = 0;
    int ch;
    int argc, parseResult;
    char arg[BUFFER_SIZE], recClientName[MAX_CLIENT_NAME];

    safe_wprintw(args->debugWindow, "Input thread started\n");
    safe_refresh_input(args->inputWindow, "> ", input_buffer);

    while (running)
    {
        pthread_mutex_lock(&ncurses_mutex);
        wtimeout(args->inputWindow, 100); // Wait up to 100ms for input
        ch = wgetch(args->inputWindow);
        pthread_mutex_unlock(&ncurses_mutex);

        if (ch != ERR)
        {
            if (ch == '\n' || ch == '\r')
            {
                // Send message
                input_buffer[pos] = '\0';
                if (pos > 0)
                {
                    char debug_msg[BUFFER_SIZE + 50];
                    if (strcmp(input_buffer, "exit") == 0)
                    {
                        endwin();
                        exit(0);
                    }
                    // snprintf(debug_msg, sizeof(debug_msg), "Sending: '%s' (length: %d)\n", input_buffer, pos);
                    // safe_wprintw(args->debugWindow, debug_msg);
                    ssize_t amount_sent = send(args->sd, input_buffer, strlen(input_buffer), 0);
                    if (amount_sent < 0)
                    {
                        safe_wprintw(args->debugWindow, "Send failed\n");
                    }
                    else
                    {
                        char sent_msg[BUFFER_SIZE + 10];
                        parseResult = parseClientRequest(&argc, arg, recClientName, input_buffer, false);
                        if (parseResult == 1 || parseResult == 2)
                        {
                            snprintf(sent_msg, BUFFER_SIZE + 10, "You: %s\n", arg);
                        }
                        safe_wprintw(args->outputWindow, sent_msg);
                    }
                }

                memset(input_buffer, 0, sizeof(input_buffer));
                pos = 0;
                safe_refresh_input(args->inputWindow, "> ", input_buffer);
            }
            else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b')
            {
                // Handle backspace
                if (pos > 0)
                {
                    pos--;
                    input_buffer[pos] = '\0';
                    safe_refresh_input(args->inputWindow, "> ", input_buffer);
                }
            }
            else if (ch == 27)
            { // ESC key
                safe_wprintw(args->debugWindow, "Input thread exiting\n");
                endwin();
                exit(0);
            }
            else if (pos < BUFFER_SIZE - 1 && ch >= 32 && ch <= 126)
            {
                // Regular printable character
                input_buffer[pos] = ch;
                pos++;
                safe_refresh_input(args->inputWindow, "> ", input_buffer);
            }
        }

        // Small delay to prevent busy waiting
        usleep(10000);
    }

    return NULL;
}

// Thread-safe server output thread
void *safe_streamServerOutput(void *arguments)
{
    threadArgs_t *args = (threadArgs_t *)arguments;
    char serverResponse[MAX_SERVER_RESPONSE];
    int i = 0;

    safe_wprintw(args->debugWindow, "Output thread started\n");

    while (running)
    {
        int readResult = recv(args->sd, serverResponse, MAX_SERVER_RESPONSE - 1, 0);
        if (readResult > 0)
        {
            serverResponse[readResult] = '\0';
            if (serverResponse[0] == '8' && serverResponse[1] == '#')
            {
                // displayig to info win
                for (i = 0; i < strlen(serverResponse) - 2; i++)
                {
                    serverResponse[i] = serverResponse[i + 2];
                }
                serverResponse[i - 1] = '\n';
                serverResponse[i] = '\n';
                serverResponse[i + 1] = '\0';
                safe_wprintw(args->infoWindow, serverResponse);
            }
            else
            {
                safe_wprintw(args->outputWindow, serverResponse);
            }
        }
        else if (readResult == 0)
        {
            safe_wprintw(args->outputWindow, "Connection closed by server\n");
            safe_wprintw(args->debugWindow, "Server disconnected\n");
            break;
        }
        else
        {
            safe_wprintw(args->debugWindow, "Connection error\n");
            break;
        }
    }

    return NULL;
}

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
    keypad(inputWindow, true);
    mvwprintw(inputWindow, 0, 1, "Input Window");
    wtimeout(inputWindow, 100); // Make input non-blocking with 100ms timeout
    wrefresh(inputWindow);
    getmaxyx(inputWindow, max_input_y, max_input_x);

    int max_output_x, max_output_y;
    WINDOW *outputWindow = newwin(max_y - (max_y / INPUT_Y_DIVIDER) - 1, max_x * INPUT_X_MULTIPLIER, 1, 0);
    box(outputWindow, 0, 0);
    mvwprintw(outputWindow, 0, 1, "Output Window");
    wrefresh(outputWindow);
    WINDOW *outputContent = derwin(outputWindow, max_y - (max_y / INPUT_Y_DIVIDER) - 3, max_x * INPUT_X_MULTIPLIER - 2, 1, 1);
    scrollok(outputContent, true);
    wrefresh(outputContent);
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
    getmaxyx(debugContent, max_debug_y, max_debug_x);

    // making new windo for listing available groups and stuff like that. also all the people in your group and so on
    int max_info_x, max_info_y;
    WINDOW *listWindow = newwin(max_y - (max_y / DEBUG_Y_DIVIDER) - 1, max_x - max_output_x - 1, max_debug_y + 3, max_output_x + 1);
    box(listWindow, 0, 0);
    mvwprintw(listWindow, 0, 1, "Info Window");
    wmove(listWindow, 1, 1);
    wrefresh(listWindow);
    WINDOW *listContent = derwin(listWindow, max_y - (max_y / DEBUG_Y_DIVIDER) - 3, max_x - max_output_x - 3, 1, 1);
    scrollok(listContent, true);
    wrefresh(listContent);
    getmaxyx(listContent, max_info_y, max_info_x);

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
    int curr_x, curr_y;
    int connection_attempts = 0;
    assert(sd > -1);

    struct sockaddr_in server_addr;
    set_sockdet_addr(&server_addr, "127.0.0.1", SERVER_PORT);

    threadArgs_t *tArgs = malloc(sizeof(threadArgs_t));
    tArgs->debugWindow = debugContent;
    tArgs->inputWindow = inputWindow;
    tArgs->outputWindow = outputContent;
    tArgs->infoWindow = listContent;
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

            // Create thread-safe input and output threads
            pthread_create(&sendThread, NULL, safe_streamUserInput, (void *)tArgs);
            pthread_create(&recThread, NULL, safe_streamServerOutput, (void *)tArgs);

            pthread_join(sendThread, NULL);
            pthread_join(recThread, NULL);
        }
        else
        {
            memset(buffer, 0, sizeof(buffer));
            snprintf(buffer, BUFFER_SIZE, "Connection failed. Retrying in 5 seconds...");
            printToWindow(debugContent, buffer);
            sleep(5);
            retryBool = true;
        }
    }
    endwin();
    free(tArgs);
    return 0;
}