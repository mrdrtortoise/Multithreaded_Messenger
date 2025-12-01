#include "tcp.h"

int main(){
    initscr();
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    refresh();
    WINDOW *win = newwin(max_y-2, max_x, 0, 0);
    wrefresh(win);
    box(win, 0,0);
    wprintw(win, "hello");
    wrefresh(win);
    move(1,1);
    char str[64];
    wmove(win, 1, 1);
    while(1){
        wgetnstr(win, str, 64);
    }

    endwin();
    return 0;
}