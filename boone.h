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
#include "editor.h"

// Shell builtin commands.
int shell_cd(char **args);
int shell_history(char** args);
int shell_exit(char** args);

extern pid_t child_pid;
extern bool is_suspended;
extern int process_ids[1024];
extern int process_idx;

// Returns size of the shell command array.
size_t shell_commands_size(void);

// Returns a list of strings from the standard output of the shell.
char** read_user_line(void);

// Executes the process supplied by the user arguments.
int execute_process(char** user_args);

#endif
