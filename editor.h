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

// Change the mode of the terminal.
void enableRawMode(void);
void enableMonitorMode(void);
void disableModes(void);

// Functions for the command line editor.

// Refreshes the screen after each keypress.
void editorRefreshScreen(char* command);

// Handles the left and right keys for moving the cursor around the command string.
void editorMoveCursor(int c, char* command);

// Handles adding characters to the command string.
void editorAddCharacter(char** command, int c);

// Handles the backspace and delete key to remove characters from the command string.
void editorDeleteCharacter(char** command, bool is_del);

// Handles the up and down arrow keys which retrieve previous command strings.
void editorGetHistoryCommand(char* command, int arrow);

// Handles the tab key which auto-completes the command str.
void editorTabComplete(char** command);

// Reads a keypress from editorReadKey and decides what to do with it.
bool editorProcessKeypress(char** command, bool monitor_f);

// Get all of the arguments in the commands string.
char** editorGetArgs(char* command);

 /**
 * Read key from the keyboard. Returns an integer type instead of a char type
 * to handle control key presses instead of a character key presses.
 */
int editorReadKey(void);

#endif
