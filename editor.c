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
        // Handle keypresses when a process is running.
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
        /**
         * Handle keypresses if there are no processes running, 
         * or if all the processes that are running are stoped.
         */
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
                    shell_fg(NULL);
                    break;
                }

            case CTRL_KEY('i'):
            {
                editorTabComplete(command);
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

    // This monitors if the character is an escape sequence,
    // like the left arrow for example "\x1b[D".
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
    char* new_command = malloc(command_len + 1);
    strcpy(new_command, *command);

    /**
     * Copy over all of the characters from the old string to the new
     * except the one at our cursor's position.
     */
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

     /**
     * Allocate enough memory for additional character. 
     * We add two for the new 'c' character and the terminating '\0' character.
     */
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

    // Handles if we are at the last entry in history.
    if (editor_state.history_pos < 0)
    {
        editor_state.history_pos++;
        return;
    }

    if (arrow == ARROW_DOWN)
    {
        editor_state.history_pos++;

        // Handles when we are at the first entry in history.
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

char** editorGetArgs(char* command)
{
    int user_arg_size = USER_ARG_SIZE;
    int idx = 0;
    char* token = "";
    char** tokens = malloc(user_arg_size * sizeof(char*));

    token = strtok(command, DELIMETERS);
    while (token != NULL)
    {
        tokens[idx] = token;
        idx++;

        // Re-allocation incase the user supplies a lot of arguments.
        if (idx >= USER_ARG_SIZE) 
        {
            user_arg_size += USER_ARG_SIZE;
            tokens = realloc(tokens, user_arg_size * sizeof(char*));
            if (!tokens) 
            {
                perror("Could not allocate! ");
                exit(EXIT_FAILURE);
            }
        }
        token = strtok(NULL, DELIMETERS);
    }
    tokens[idx] = NULL;

    return tokens;
}

void editorTabComplete(char** command)
{
    char directory_and_command[512] = "";
    char directory_no_command[512] = "";
    char directory[512] = "";
    char last_arg[512] = "";
    char other_args[512] = "";

    /**
     * Gather the command args in an array, and separate
     * the last arg from the rest since that's the one we
     * need to complete with tab.
     */

    char** args = editorGetArgs(*command);
    int argc = 0;
    while (args[argc] != NULL)
    {
        argc++;
    }

    if (argc == 0)
    {
        return;
    }

    // Get last arg.
    strcpy(last_arg, args[argc - 1]);

    // Gather the other args into a string.
    args[--argc] = NULL;
    for (int i = 0; i < argc; i++)
    {
        strcat(other_args, args[i]);
        strcat(other_args, " ");
    }

    strcpy(directory, last_arg);
    strcat(directory_and_command, last_arg);
    strcat(directory_no_command, last_arg);

    if (directory[0] == '/' || directory[1] == '/')
    {
        for (int i = strlen(directory) - 1; i >= 0; i--)
        {
            if (directory[i] == '/')
            {
                directory[i + 1] = '\0';
                directory_no_command[i + 1] = '\0';
                break;
            }
        }
    }
    else
    {
        strcpy(directory_no_command, "");
        strcpy(directory, ".");
    }

    if (strcmp(directory_no_command, directory_and_command) == 0)
    {
        return;
    }

    // Get file names and file count of directory.
    DIR *d;
    struct dirent *dir;
    char** file_names = malloc(512);
    int filec = 0;
    d = opendir(directory);
    if (d) 
    {
        while ((dir = readdir(d)) != NULL)
        {
            *(file_names + filec) = dir->d_name;
            filec++;
        }
        *(file_names + filec) = NULL;
    }

    if (closedir(d) == -1)
    {
        perror("Could not close directory!");
    }

    int chars_match = -1;
    int len_diff = strlen(directory_and_command) - strlen(directory_no_command);
    bool finished_matching = false;
    bool first_search = true;
    char matched_file[512] = "......................................................................................";

    while (!finished_matching)
    {
        bool found_match = false;
        for (int i = 0; i < filec; i++)
        {
            // Create a command that just includes the last arg + the file name in the cwd.
            char* compare_command = malloc(strlen(directory_no_command) + strlen(*(file_names + i)) + 1);
            sprintf(compare_command, "%s%s", directory_no_command, *(file_names + i));

            // Compare and see if the characters leading up are equal, if so we've found a match.
            if (strncmp(directory_and_command, compare_command, strlen(directory_and_command)) == 0)
            {
                // Store the shorted name-length matched file for use when we add the command back together.
                if (strlen(*(file_names + i)) < strlen(matched_file))
                {
                    strcpy(matched_file, *(file_names + i));
                }


                found_match = true;
            }
            free(compare_command);
        }

        if (!found_match)
        {
            return;
        }

        if (first_search)
        {
            first_search = false;
            chars_match++;

        }
        else
        {
            int last_arg_size = strlen(directory_and_command) - strlen(directory_no_command);
            if (last_arg_size == strlen(matched_file))
            {
                finished_matching = true;

                bool found_multiple = false;
                for (int i = 0; i < filec; i++)
                {
                    char* x = *(file_names + i);
                    if (
                        strcmp(*(file_names + i), matched_file) != 0 && 
                        strncmp(*(file_names + i), matched_file, last_arg_size - 1) == 0 && 
                        strlen(*(file_names + i)) == strlen(matched_file)
                    ) 
                    {
                        found_multiple = true;
                        break;
                    }
                }

                // Add the other args back onto the last arg.
                char* new_command = malloc(strlen(other_args) + strlen(directory_no_command) + chars_match + 1);
                char file_name_match[512];
                strcpy(file_name_match, matched_file);
                sprintf(new_command, "%s%s%s", other_args, directory_no_command, file_name_match);
                
                if (found_multiple)
                {
                    new_command[strlen(new_command) - 1] = '\0';
                }

                free(*command);
                *command = new_command;
                editor_state.x = strlen(editor_state.cwd) + strlen(PROMPT) + strlen(*command) + 1;

            }
            else
            {
                char new_char = (matched_file)[chars_match + len_diff];
                strcat(directory_and_command, &new_char);
                chars_match++;
            }
        }
    }
}
