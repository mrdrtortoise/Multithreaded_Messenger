#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#define BUFFER_SIZE 1024
#define SERVER_PORT 12000
#define CLIENT_PORT 0

// Global variables
WINDOW *output_win, *input_win, *debug_win;
pthread_mutex_t ncurses_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t resize_needed = 0;

// Signal handler for window resize
void handle_resize(int sig) {
    resize_needed = 1;
}

// Thread-safe window recreation
void recreate_windows() {
    pthread_mutex_lock(&ncurses_mutex);

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // Clear and delete old windows
    werase(output_win);
    werase(input_win);
    werase(debug_win);
    delwin(output_win);
    delwin(input_win);
    delwin(debug_win);

    // Recreate windows with new dimensions
    output_win = newwin(max_y - 3, max_x, 0, 0);
    scrollok(output_win, TRUE);

    input_win = newwin(1, max_x, max_y - 1, 0);
    box(input_win, 0, 0);
    mvwprintw(input_win, 0, 2, " Input: ");

    debug_win = newwin(5, MIN(40, max_x - 2), 1, MAX(1, max_x - MIN(40, max_x - 2) - 1));
    box(debug_win, 0, 0);

    // Redraw everything
    wrefresh(output_win);
    wrefresh(input_win);
    wrefresh(debug_win);
    refresh();

    // Show resize message
    wprintw(output_win, "Terminal resized to %dx%d\n", max_x, max_y);
    wrefresh(output_win);

    pthread_mutex_unlock(&ncurses_mutex);
}

// Check for resize and handle it
void check_resize() {
    if (resize_needed) {
        resize_needed = 0;
        recreate_windows();
    }
}

int main() {
    // Set up signal handler for SIGWINCH (window resize)
    signal(SIGWINCH, handle_resize);

    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(1);

    // Create initial windows
    recreate_windows();

    // Main loop
    int ch;
    while (1) {
        check_resize();  // Check for resize events

        // Handle input
        timeout(100);  // Wait up to 100ms for input
        ch = getch();

        if (ch == 'q' || ch == 'Q') {
            break;  // Quit on 'q'
        }
        else if (ch != ERR) {
            // Handle other input
            pthread_mutex_lock(&ncurses_mutex);
            wprintw(output_win, "Key pressed: %c (%d)\n", ch, ch);
            wrefresh(output_win);
            pthread_mutex_unlock(&ncurses_mutex);
        }

        // Small delay to prevent busy waiting
        usleep(10000);
    }

    // Cleanup
    pthread_mutex_lock(&ncurses_mutex);
    delwin(output_win);
    delwin(input_win);
    delwin(debug_win);
    endwin();
    pthread_mutex_unlock(&ncurses_mutex);

    return 0;
}
