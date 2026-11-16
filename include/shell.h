#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_LEN 512
#define MAXARGS 30
#define ARGLEN 256
#define PROMPT "FCIT> "
#define HISTORY_SIZE 20
#define MAX_JOBS 64

// One command of a pipeline, e.g. "sort < in.txt" or "grep root > out.txt"
typedef struct {
    char* argv[MAXARGS + 1];   // NULL-terminated argument vector for execvp
    char* infile;              // file after '<'  (NULL if none)
    char* outfile;             // file after '>'  (NULL if none)
} command_t;

// Function prototypes
void init_readline(void);
char** tokenize(char* cmdline);
int parse_pipeline(char** arglist, command_t* cmds);
int split_commands(char* cmdline, char** segments, int max);
int strip_background(char** arglist);
int execute(command_t* cmds, int ncmds, int background, const char* cmdtext);
void reap_jobs(void);
void print_jobs(void);
int handle_builtin(char** arglist);
void add_to_history(const char* cmdline);
void show_history(void);
char* recall_history(const char* cmdline);

#endif // SHELL_H
