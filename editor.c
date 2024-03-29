#include "editor.h"

struct termios orig_termios;
struct state editor_state;
bool first_prompt = true;

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

    if (first_prompt)
    {
        first_prompt = false;
        write(STDERR_FILENO, "A simple shell written by Jarrod Boone!\n\r", 41);
    }

    // Set CWD to cyan.
    write(STDOUT_FILENO, "\x1b[36m", 5);
    write(STDOUT_FILENO, editor_state.cwd, strlen(editor_state.cwd));

    // Set " ; " character to green.
    write(STDOUT_FILENO, "\x1b[32m", 5);
    write(STDOUT_FILENO, PROMPT, strlen(PROMPT));

    // Print the auto-completed tab command.
    write(STDOUT_FILENO, "\x1b[K", 3);
    write(STDOUT_FILENO, "\x1b[36m", 5);
    write(STDOUT_FILENO, "\x1b[2m", 4);
    write(STDOUT_FILENO, editor_state.tab_command, strlen(editor_state.tab_command));

    // Print the command.
    write(STDOUT_FILENO, "\x1b[0m", 4);
    sprintf(cursor, "\x1b[%d;%luH", editor_state.y, strlen(PROMPT) + strlen(editor_state.cwd) + 1);
    write(STDOUT_FILENO, cursor, 8);

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
         * or if all the processes that are running have been stopped.
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
                editorTabComplete(command, false);
                break;
            }

            default:
                if (!iscntrl(c))
                {
                    editorAddCharacter(command, c);
                }
        }
    }

    strcpy(editor_state.tab_command, *command);
    editorTabComplete(&editor_state.tab_command, true);
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

    FILE* file = fopen(program_wd, "r");
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

char** getFileNames(int* filec, char* directory_str)
{
    DIR* d;
    struct dirent* dir;
    char** file_names = malloc(512);

    d = opendir(directory_str);
    if (d) 
    {
        while ((dir = readdir(d)) != NULL)
        {
            *(file_names + *filec) = dir->d_name;
            ++*filec;
        }
        *(file_names + *filec) = NULL;
    }
    else
    {
        return NULL;
    }

    if (closedir(d) == -1)
    {
        return NULL;
    }

    return file_names;
}

void editorTabComplete(char** command, bool shadow_tab)
{
    char directory_and_command[512] = "";
    char directory_no_command[512] = "";
    char directory[512] = "";

    char last_arg[512] = "";
    char other_args[512] = "";
    bool beginning_slash = true;

    char* command_cpy = malloc(strlen(*command) + 1);
    strcpy(command_cpy, *command);

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
        free(*command);
        *command = command_cpy;
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

    if (directory[0] != '/' && directory[0] != '.')
    {
        beginning_slash = false;
        char copy[510];
        strcpy(copy, directory);
        sprintf(directory, "./%s", copy);
        sprintf(directory_no_command, "./%s", copy);
        sprintf(directory_and_command, "./%s", copy);
    }

    for (int i = strlen(directory) - 1; i >= 0; i--)
    {
        if (directory[i] == '/')
        {
            directory[i + 1] = '\0';
            directory_no_command[i + 1] = '\0';
            break;
        }
    }

    if (strcmp(directory_no_command, directory_and_command) == 0)
    {
        free(*command);
        *command = command_cpy;
        return;
    }

    /**
     * Below is the logic for auto-completing the command string.
     * We basically just add a character to the command one at a time.
     * If file name in our command ends up
     * matching a lower number of files than the amount
     * of matches we got at the beginning, then the auto-complete
     * is done, and we're ready to alter the command.
     */

    // Get file names and file count of directory.
    int filec = 0;
    char** file_names = getFileNames(&filec, directory);
    if (file_names == NULL)
    {
        return;
    }

    /**
     * We finish searching if the file name in our command gets as long as the longest file.
     * Or if we get less file name matches than what we got at the start.
     */
    bool finished_matching = false;

    // We use first search on our first run to get the largest file name.
    bool first_search = true;

    // If we don't find any matching files in the dir we can just end early.
    bool found_match = false;

    /**
     * If we ever match less files than what we find on our first search, than
     * we've autocompleted as far as we can go.
     */
    int minimum_matched_files = 0;

    // Number of characters we've added to our command. Starts at -1 for the first search.
    int chars_match = -1;

    int starting_len = strlen(directory_and_command) - strlen(directory_no_command);
    char largest_file[512] = "";

    while (!finished_matching)
    {
        int matched_files = 0;
        for (int i = 0; i < filec; i++)
        {
            // Create a command that just includes the last arg + the file name in the cwd.
            char* compare_command = malloc(strlen(directory_no_command) + strlen(*(file_names + i)) + 1);
            sprintf(compare_command, "%s%s", directory_no_command, *(file_names + i));

            // Compare and see if the characters leading up are equal, if so we've found a match.
            if (strncmp(directory_and_command, compare_command, strlen(directory_and_command)) == 0)
            {
                if (strlen(*(file_names + i)) > strlen(largest_file))
                {
                    strcpy(largest_file, *(file_names + i));
                }

                matched_files++;
                found_match = true;
            }
            free(compare_command);
        }

        if (!found_match)
        {
            free(*command);
            *command = command_cpy;
            return;
        }

        if (first_search)
        {
            first_search = false;
            minimum_matched_files = matched_files;
            chars_match++;
        }
        else
        {
            int new_starting_len = strlen(directory_and_command) - strlen(directory_no_command);
            if (matched_files < minimum_matched_files || new_starting_len == strlen(largest_file))
            {
                if (matched_files < minimum_matched_files)
                {
                    directory_and_command[strlen(directory_and_command) - 1] = '\0';
                    new_starting_len--;
                }

                if (!beginning_slash)
                {
                    char* tmp = malloc(strlen(directory_and_command) - 1);
                    sprintf(tmp, "%s", directory_and_command + 2);
                    strcpy(directory_and_command, tmp);
                    free(tmp);
                }

                char* new_command = malloc(strlen(other_args) + strlen(directory_and_command) + 2);
                sprintf(new_command, "%s%s", other_args, directory_and_command);

                if (new_starting_len == strlen(largest_file))
                {
                    DIR* d;
                    struct dirent* dir;

                    d = opendir(directory);
                    if (d) 
                    {
                        while ((dir = readdir(d)) != NULL)
                        {
                            if (strcmp(dir->d_name, largest_file) == 0 && dir->d_type == DT_DIR)
                            {
                                strcat(new_command, "/");
                                break;
                            }
                        }
                    }
                    else
                    {
                        perror("Could not open directory! ");
                    }

                    if (closedir(d) == -1)
                    {
                        perror("Could not close directory!");
                    }
                }

                free(*command);
                free(command_cpy);
                free(file_names);

               *command = new_command;

                if (!shadow_tab)
                {
                    editor_state.x = strlen(editor_state.cwd) + strlen(PROMPT) + strlen(*command) + 1;
                }
                finished_matching = true;
            }
            else
            {
                char new_char = (largest_file)[chars_match + starting_len];
                strncat(directory_and_command, &new_char, 1);
                chars_match++;
            }
        }
    }
}
