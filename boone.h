#ifndef BOONE_H
#define BOONE_H

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include "editor.h"

#define NO_CHILD_PID -100

extern pid_t child_pid;
extern bool is_suspended;
extern int process_ids[1024];
extern int process_idx;
extern char program_wd[256];

// Shell builtin commands.
int shell_exit(char** args);
int shell_cd(char **args);
int shell_history(char** args);
int shell_fg(char** args);

// Returns size of the shell command array.
size_t shell_commands_size(void);

// Returns a list of strings from the standard output of the shell.
char** read_user_line(void);

// Executes the process supplied by the user arguments.
int execute_process(char** user_args);

#endif
