#include "editor.h"

struct termios orig_termios;
struct state editor_state;

void enableRawMode(void)
{
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    {
        perror("Could not enable raw mode! ");
        exit(1);
    }
}

void enableMonitorMode(void)
{
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ISIG | ICANON | ECHO);

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    {
        perror("Could not enable monitor mode! ");
        exit(1);
    }
}

void disableModes(void) 
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    {
        perror("Could not disable raw mode! ");
        exit(1);
    }
}

void editorRefreshScreen(char* command) 
{
    // Set cursor in the correct Y position
    char cursor[32];
    sprintf(cursor, "\x1b[%d;1H", editor_state.y);

    // Write the CWD, prompt characters, and user command.
    write(STDOUT_FILENO, cursor, 8);

    // Set CWD to cyan.
    write(STDOUT_FILENO, "\x1b[36m", 5);
    write(STDOUT_FILENO, editor_state.cwd, strlen(editor_state.cwd));

    // Set " ; " character to green.
    write(STDOUT_FILENO, "\x1b[32m", 5);
    write(STDOUT_FILENO, PROMPT, strlen(PROMPT));
    write(STDOUT_FILENO, "\x1b[0m", 4);

    // Print command.
    write(STDOUT_FILENO, "\x1b[K", 3);
    write(STDOUT_FILENO, command, strlen(command));
    sprintf(cursor, "\x1b[%d;%dH", editor_state.y, editor_state.x);
    write(STDOUT_FILENO, cursor, 8);
}

bool editorProcessKeypress(char** command, bool monitor_f)
{
    int c = editorReadKey();

    if (monitor_f)
    {
        switch(c)
        {
            case CTRL_KEY('c'):
                kill(child_pid, SIGKILL);
                editor_state.y++;
                break;

            case CTRL_KEY('z'):
                kill(child_pid, SIGSTOP);
                process_ids[process_idx] = child_pid;
                child_pid = -100;

                printf("[%d] %s\n", process_idx, "Program Suspended!");
                process_idx++;
                break;
        }
    }
    else
    {
        switch (c)
        {
            case '\r':
                editor_state.y++;
                return true;

            case ARROW_LEFT:
            case ARROW_RIGHT:
                editorMoveCursor(c, *command);
                break;

            case ARROW_UP:
                editorGetHistoryCommand(*command, ARROW_UP);
                break;

            case ARROW_DOWN:
                editorGetHistoryCommand(*command, ARROW_DOWN);
                break;

            case DEL_K:
                editorDeleteCharacter(command, true);
                break;

            case BACKSPACE:
                editorDeleteCharacter(command, false);
                break;

            case CTRL_KEY('b'):
                if (process_idx > 0)
                {
                    enableMonitorMode();
                    process_idx--;
                    child_pid = process_ids[process_idx];
                    kill(child_pid, SIGCONT);
                    printf("\n[%d] %s\n", process_idx, "Program Resumed!");
                    break;
                }
            default:
                if (!iscntrl(c))
                {
                    editorAddCharacter(command, c);
                }
        }
    }
    return false;
}

int editorReadKey(void)
{
    int nread = 0;
    char c;

    while (nread != 1)
    {
        nread = read(STDIN_FILENO, &c, 1);
        if (nread == -1 && errno != EAGAIN)
        {
            perror("Could not read user input! ");
            exit(1);
        }
    }

    if (c == '\x1b')
    {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) 
        {
            return '\x1b';
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
        {
            return '\x1b';
        }

        if (seq[0] == '[') 
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                {
                    return '\x1b';
                }

                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                        case '3': return DEL_K;
                        case '8': return BACKSPACE;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                }
            }
        }

        return '\x1b';
    }

    return c;
}

void editorDeleteCharacter(char** command, bool is_del)
{
    size_t command_len = strlen(*command);
    int str_pos;
    
    if (is_del)
    {
        str_pos = editor_state.x - editor_state.cwd_str_len;
    }
    else
    {
        editor_state.x--;
        str_pos = editor_state.x - editor_state.cwd_str_len;
    }

    if (str_pos < 0)
    {
        editor_state.x++;
        return;
    }

    if (command_len == 0 || str_pos == command_len)
    {
        return;
    }

    char* tmp = *command;
    char* new_command = malloc(command_len - 1);
    strcpy(new_command, *command);

    // Copy over all of the characters from the old string to the new
    // except the one at our cursor's position.
    int pos = 0;
    while (pos != str_pos)
    {
        char c = *(*command + pos);
        new_command[pos] = c;
        pos++;
    }
    pos++;
    while (pos < command_len)
    {
        char c = *(*command + pos);
        new_command[pos - 1] = c;
        pos++;
    }
    new_command[pos - 1] = '\0';
    *command = new_command;

    free(tmp);
}

void editorAddCharacter(char** command, int c)
{
    size_t command_len = strlen(*command);
    int str_pos = editor_state.x - editor_state.cwd_str_len;
    char* tmp = *command;

    // Allocate enough memory for additional character. 
    // We add two for the new 'c' character and the terminating '\0' character.
    char* new_command = malloc(command_len + 2);

    int pos = 0;
    while (pos != str_pos)
    {
        char old_c = *(*command + pos);
        new_command[pos] = old_c;
        pos++;
    }
    new_command[pos] = c;
    pos++;
    while (pos < command_len + 1)
    {
        char c = *(*command + pos - 1);
        new_command[pos] = c;
        pos++;
    }
    new_command[pos] = '\0';
    *command = new_command;

    // Free old one and move cursor forward.
    editor_state.x++;
    free(tmp);
}

void editorMoveCursor(int c, char* command)
{
    switch (c)
    {
        case ARROW_LEFT:
            if (editor_state.x > editor_state.cwd_str_len)
                editor_state.x--;
            break;

        case ARROW_RIGHT:
            if (editor_state.x < editor_state.cwd_str_len + strlen(command))
                editor_state.x++;
            break;
    }
}

void editorGetHistoryCommand(char* command, int arrow)
{
    if (arrow == ARROW_UP)
    {
        editor_state.history_pos--;
    }

    if (editor_state.history_pos < 0)
    {
        editor_state.history_pos++;
        return;
    }

    if (arrow == ARROW_DOWN)
    {
        editor_state.history_pos++;
        if (editor_state.history_pos >= editor_state.history_max)
        {
            editor_state.x = strlen(editor_state.cwd) + strlen(PROMPT) + 1;
            strcpy(command, "");

            if (editor_state.history_pos > editor_state.history_max)
            {
                editor_state.history_pos--;
            }
            return;
        }
    }

    char* tmp = malloc(256);
    strcpy(tmp, command);

    FILE* file = fopen("/history.txt", "r");
    if (file == NULL)
    {
        // Return state back to what it was before the file opens.
        if (arrow == ARROW_UP)
        {
            editor_state.history_pos++;
        }
        else
        {
            editor_state.history_pos--;
        }

        perror("Could not open history file! ");
        return;
    }

    int line_num = 0;
    size_t len = 0;
    char* line = NULL;

    while ((getline(&line, &len, file)) != -1 && line_num != editor_state.history_pos)
    {
        line_num++;
    }
    fclose(file);

    int newline_idx = strlen(line) - 1;
    strcpy(command, line);

    if (command[newline_idx] == '\n')
    {
        command[newline_idx] = '\0';
    }

    // This skips over duplicate commands.
    if (strcmp(command, tmp) == 0)
    {
        editorGetHistoryCommand(command, arrow);
        free(tmp);
        return;
    }

    free(tmp);
    editor_state.x = strlen(editor_state.cwd) + strlen(PROMPT) + strlen(command) + 1;
    return;
}
