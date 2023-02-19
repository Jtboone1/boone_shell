#include "boone.h"

pid_t child_pid = -100;
bool is_suspended = false;
int process_ids[1024];
int process_idx = 0;

char* shell_commands[] = {"cd", "history", "exit"};

int (*shell_functions[]) (char **) = {
    &shell_cd,
    &shell_history,
    &shell_exit
};

int shell_exit(char** args)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    printf("%s", "Exited with status code: 0\n");

    char cursor[32];
    sprintf(cursor, "\x1b[%d;1H", editor_state.y);
    printf("%s\n", cursor);

    exit(0);
}

int shell_cd(char **args)
{
    if (args[1] == NULL) 
    {
        perror("No argument provided! ");
    } 
    else
    {
        if (chdir(args[1]) != 0)
        {
            perror("Could not change directory! ");
        }
    }

    editor_state.cwd = getcwd(NULL, 0);
    return 1;
}

int shell_history(char** args)
{
    FILE* file = fopen("/history.txt", "r");
    if (file == NULL)
    {
        perror("Could not open history file! ");
        return -1;
    }

    int line_num = 0;
    char line[256];

    while (fgets(line, sizeof(line), file)) 
    {
        printf("\r%d %s",  line_num, line); 
        line_num++;
    }

    fclose(file);
    printf("%s", "\n");

    return 1;
}

size_t shell_commands_size(void) 
{
    return sizeof(shell_commands) / sizeof(char *);
}

char** read_user_line(void)
{
    FILE* fd;
    size_t buffer_size = 0;
    size_t idx = 0;
    int user_arg_size = USER_ARG_SIZE;

    char* empty = "";
    char* line = malloc(1); 
    strcpy(line, empty);

    editor_state.cwd = getcwd(NULL, 0);
    int len = strlen(editor_state.cwd) + strlen(PROMPT);

    // Initialize command line state
    editor_state.x = len + 1;
    editor_state.cwd_str_len = len + 1;

    char* token = "";
    char** tokens = malloc(user_arg_size * sizeof(char*));
    int previous_sig = 0;
    
    bool enter_pressed = false;
    do
    {
        int status;
        if (child_pid == -100 || waitpid(child_pid, &status, WNOHANG) != 0 || is_suspended)
        {
            enableRawMode();

            if ( child_pid != -100 && WIFEXITED(status) )
            {
                int code = WEXITSTATUS(status);
                status = -1;
                printf("Exited with status code: %d\n", code);
            }

            if ( child_pid != -100 && WIFSIGNALED(status) )
            {
                int code = WTERMSIG(status);
                status = -1;
                printf("Signaled with status code: %d\n", code);
            }

            editorRefreshScreen(line);
            enter_pressed = editorProcessKeypress(&line, false);
        }
        else
        {
            enableMonitorMode();
            editorProcessKeypress(&line, true);
        }
    }
    while(!enter_pressed);

    char cursor[32];
    sprintf(cursor, "\x1b[%d;1H", editor_state.y);
    printf("%s\n", cursor);

    // If no arguments provided we just signal by returning NULL.
    if (strcmp(line, "") == 0)
    {
        return NULL;
    }

    fd = fopen("/history.txt", "a+");
    if (fd == NULL)
    {
        perror("Could not open file! ");
    }

    if (fprintf(fd, "\n%s", line) < 0)
    {
        perror("Could not write to history! ");
    }

    editor_state.history_max++;
    editor_state.history_pos = editor_state.history_max;
    fclose(fd);

    token = strtok(line, DELIMETERS);
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

int execute_process(char** user_args)
{
    // First check if were trying to execute a shell command.
    for (size_t i = 0; i < shell_commands_size(); i++)
    {
        if (strcmp(user_args[0], shell_commands[i]) == 0)
        {
            return (*shell_functions[i])(user_args);
        }
    }

    // Then just execute the command the user wants.
    pid_t parent_pid;
    int status;

    child_pid = fork();

    switch(child_pid)
    {
        case 0: 
            if (execvp(user_args[0], user_args) < 1)
            {
                perror("Error executing program! ");
                return 1;
            }
            break;

        case -1:
            perror("Could not fork! ");
            break;
    }

    return 0;
}

int main(int argc, char* argv[])
{
    editor_state.y = 50;
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableModes);
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    FILE* fp = fopen("/history.txt", "r+");
    if (fp == NULL)
    {
        perror("Could not open file history.txt! ");
        return 0;
    }
 
    // Get # history lines and set it in our editor.
    char c;
    editor_state.history_max = 0;
    for (c = getc(fp); c != EOF; c = getc(fp))
    {
        if (c == '\n')
        {
            editor_state.history_max++;
        }
    }
    editor_state.history_max++;
    editor_state.history_pos = editor_state.history_max - 1;
    fclose(fp);

    while (true)
    {
        char** user_args = read_user_line();

        if (user_args != NULL)
        {
            if (execute_process(user_args) == 1)
            {
                return 1;
            }
        }
    }

    return 0;
}