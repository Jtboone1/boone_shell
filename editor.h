#ifndef EDITOR_H
#define EDITOR_H

#include <termios.h>
#include "boone.h"

#define CTRL_KEY(k) ((k) & 0x1f)

#define USER_ARG_SIZE 128
#define DELIMETERS " \t\r\n\a"
#define PROMPT " ; "

struct state 
{
    int x;
    int y;
    int cwd_str_len;
    int history_pos;
    int history_max;
    char* cwd;
};

enum editor_keys 
{
    BACKSPACE = 127,
    ARROW_UP = 1000,
    ARROW_DOWN,
    ARROW_LEFT,
    ARROW_RIGHT,
    DEL_K
};

extern struct termios orig_termios;
extern struct state editor_state;

void enableRawMode(void);
void enableMonitorMode(void);
void disableModes(void);

// Functions for the command line editor.
void editorRefreshScreen(char* command);
void editorMoveCursor(int c, char* command);
void editorAddCharacter(char** command, int c);
void editorDeleteCharacter(char** command, bool is_del);
void editorGetHistoryCommand(char* command, int arrow);
bool editorProcessKeypress(char** command, bool monitor_f);
int editorReadKey(void);

#endif
