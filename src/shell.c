#include "shell.h"
#include <dirent.h>
#include <limits.h>

// ---------- Command history storage ----------
static char* history[HISTORY_SIZE];
static int history_count = 0;   // number of commands currently stored

char** tokenize(char* cmdline) {
    // Edge case: empty command line
    if (cmdline == NULL || cmdline[0] == '\0' || cmdline[0] == '\n') {
        return NULL;
    }

    char** arglist = (char**)malloc(sizeof(char*) * (MAXARGS + 1));
    for (int i = 0; i < MAXARGS + 1; i++) {
        arglist[i] = (char*)malloc(sizeof(char) * ARGLEN);
        bzero(arglist[i], ARGLEN);
    }

    char* cp = cmdline;
    int argnum = 0;

    while (*cp != '\0' && argnum < MAXARGS) {
        while (*cp == ' ' || *cp == '\t') cp++;   // Skip leading whitespace

        if (*cp == '\0') break;                   // Line was only whitespace

        int len = 0;
        if (*cp == '<' || *cp == '>' || *cp == '|' || *cp == '&') {
            // Special operators are always a token of their own,
            // so "ls>out" and "ls > out" mean the same thing.
            arglist[argnum][len++] = *cp++;
        } else {
            char quote = 0;                       // current quote char, or 0
            while (*cp != '\0') {
                if (quote) {
                    if (*cp == quote) { quote = 0; cp++; continue; }
                } else {
                    if (*cp == '"' || *cp == '\'') { quote = *cp; cp++; continue; }
                    if (*cp == ' ' || *cp == '\t' ||
                        *cp == '<' || *cp == '>' || *cp == '|' || *cp == '&') break;
                }
                if (len < ARGLEN - 1)
                    arglist[argnum][len++] = *cp;
                cp++;
            }
        }
        arglist[argnum][len] = '\0';
        argnum++;
    }

    if (argnum == 0) { // No arguments were parsed
        for(int i = 0; i < MAXARGS + 1; i++) free(arglist[i]);
        free(arglist);
        return NULL;
    }

    // Free the unused slots, then terminate the list
    for (int i = argnum; i < MAXARGS + 1; i++) free(arglist[i]);
    arglist[argnum] = NULL;
    return arglist;
}

static int is_operator(const char* t) {
    return strcmp(t, "<") == 0 || strcmp(t, ">") == 0 ||
           strcmp(t, "|") == 0 || strcmp(t, "&") == 0;
}

// Split the token list into a pipeline of commands.
//   ls -l | grep x > out.txt   ->   cmds[0] = {ls -l}, cmds[1] = {grep x, outfile=out.txt}
// Returns the number of commands, or -1 on a syntax error.
// cmds[i].argv/infile/outfile point INTO arglist (no copies are made).
int parse_pipeline(char** arglist, command_t* cmds) {
    int n = 0;          // index of the command being filled
    int argc = 0;       // number of args stored in cmds[n].argv
    memset(&cmds[0], 0, sizeof(command_t));

    for (int i = 0; arglist[i] != NULL; i++) {
        char* t = arglist[i];

        if (strcmp(t, "|") == 0) {
            if (argc == 0) {
                fprintf(stderr, "syntax error: missing command before '|'\n");
                return -1;
            }
            cmds[n].argv[argc] = NULL;
            n++;
            argc = 0;
            memset(&cmds[n], 0, sizeof(command_t));
        }
        else if (strcmp(t, "<") == 0 || strcmp(t, ">") == 0) {
            char* file = arglist[i + 1];
            if (file == NULL || is_operator(file)) {
                fprintf(stderr, "syntax error: expected a filename after '%s'\n", t);
                return -1;
            }
            if (t[0] == '<') cmds[n].infile = file;
            else             cmds[n].outfile = file;
            i++;                        // skip the filename token
        }
        else if (strcmp(t, "&") == 0) {
            // a trailing '&' was already removed by strip_background()
            fprintf(stderr, "syntax error: '&' is only allowed at the end of a command\n");
            return -1;
        }
        else {
            cmds[n].argv[argc++] = t;
        }
    }

    if (argc == 0) {
        fprintf(stderr, "syntax error: missing command\n");
        return -1;
    }
    cmds[n].argv[argc] = NULL;
    return n + 1;
}

// Split a line at every ';' that is NOT inside quotes.
// The ';' characters are overwritten with '\0', and segments[] receives
// pointers to the pieces. Returns the number of pieces.
//   echo a ; sleep 2 ; echo b   ->   "echo a ", " sleep 2 ", " echo b"
int split_commands(char* cmdline, char** segments, int max) {
    int n = 0;
    char quote = 0;
    char* start = cmdline;

    for (char* p = cmdline; ; p++) {
        if (*p == '\0' || (!quote && *p == ';')) {
            int at_end = (*p == '\0');
            *p = '\0';
            if (n < max)
                segments[n++] = start;
            if (at_end)
                break;
            start = p + 1;
        }
        else if (quote) {
            if (*p == quote) quote = 0;
        }
        else if (*p == '"' || *p == '\'') {
            quote = *p;
        }
    }
    return n;
}

// If the last token is "&", remove it and return 1 (run in background).
// Otherwise return 0.
int strip_background(char** arglist) {
    int last = 0;
    while (arglist[last] != NULL)
        last++;
    last--;                                   // index of the last token

    if (last >= 0 && strcmp(arglist[last], "&") == 0) {
        free(arglist[last]);
        arglist[last] = NULL;
        return 1;
    }
    return 0;
}

// Returns 1 if the command was a built-in (and was handled here),
// returns 0 if it is not a built-in (caller should fork/exec it).
int handle_builtin(char** arglist) {
    if (arglist == NULL || arglist[0] == NULL)
        return 0;

    if (strcmp(arglist[0], "exit") == 0) {
        printf("Shell exited.\n");
        exit(0);
    }

    if (strcmp(arglist[0], "cd") == 0) {
        if (arglist[1] == NULL) {
            // no argument: go to HOME, like bash
            char* home = getenv("HOME");
            if (home == NULL || chdir(home) != 0)
                perror("cd");
        } else if (chdir(arglist[1]) != 0) {
            perror("cd");   // e.g. "cd: No such file or directory"
        }
        return 1;
    }

    if (strcmp(arglist[0], "history") == 0) {
        show_history();
        return 1;
    }

    if (strcmp(arglist[0], "help") == 0) {
        printf("Built-in commands:\n");
        printf("  cd <dir>  Change the current directory\n");
        printf("  exit      Exit the shell\n");
        printf("  help      Show this help message\n");
        printf("  history   Show command history (use !n to re-run command n)\n");
        printf("  jobs      List running background jobs\n");
        return 1;
    }

    if (strcmp(arglist[0], "jobs") == 0) {
        print_jobs();
        return 1;
    }

    return 0;   // not a built-in
}

// Store a command. If the array is full, drop the oldest command
// and shift everything one place left.
void add_to_history(const char* cmdline) {
    if (history_count == HISTORY_SIZE) {
        free(history[0]);
        for (int i = 1; i < HISTORY_SIZE; i++)
            history[i - 1] = history[i];
        history_count--;
    }
    history[history_count++] = strdup(cmdline);
}

// Print all stored commands, numbered from 1.
void show_history(void) {
    for (int i = 0; i < history_count; i++)
        printf("%d  %s\n", i + 1, history[i]);
}

// cmdline looks like "!n". Returns a newly malloc'ed copy of the n-th
// command, or NULL (after printing an error) if it is invalid.
char* recall_history(const char* cmdline) {
    const char* p = cmdline + 1;       // skip the '!'
    if (*p == '\0') {
        fprintf(stderr, "!: event number required\n");
        return NULL;
    }
    for (const char* q = p; *q != '\0'; q++) {
        if (*q < '0' || *q > '9') {
            fprintf(stderr, "%s: event not found\n", cmdline);
            return NULL;
        }
    }
    int n = atoi(p);
    if (n < 1 || n > history_count) {
        fprintf(stderr, "!%d: event not found\n", n);
        return NULL;
    }
    return strdup(history[n - 1]);
}

// ---------- Tab completion (GNU Readline) ----------
static char** cmd_list = NULL;   // names of all commands we can complete
static int cmd_count = 0;
static int cmd_cap = 0;

static void add_command(const char* name) {
    if (cmd_count == cmd_cap) {
        cmd_cap = (cmd_cap == 0) ? 256 : cmd_cap * 2;
        cmd_list = realloc(cmd_list, cmd_cap * sizeof(char*));
    }
    cmd_list[cmd_count++] = strdup(name);
}

// Build the list once at startup: our built-ins + every executable in PATH.
static void build_command_list(void) {
    const char* builtins[] = {"cd", "exit", "help", "history", "jobs", NULL};
    for (int i = 0; builtins[i] != NULL; i++)
        add_command(builtins[i]);

    char* path = getenv("PATH");
    if (path == NULL) return;

    char* copy = strdup(path);          // strtok_r modifies its input
    char* saveptr;
    for (char* dir = strtok_r(copy, ":", &saveptr); dir != NULL;
         dir = strtok_r(NULL, ":", &saveptr)) {
        DIR* d = opendir(dir);
        if (d == NULL) continue;
        struct dirent* e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            char full[PATH_MAX];
            snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
            if (access(full, X_OK) == 0)
                add_command(e->d_name);
        }
        closedir(d);
    }
    free(copy);
}

// Readline calls this again and again (state = 0 on the first call)
// and each call returns the next matching name, or NULL when finished.
static char* command_generator(const char* text, int state) {
    static int index;
    size_t len = strlen(text);

    if (state == 0)
        index = 0;

    while (index < cmd_count) {
        const char* name = cmd_list[index++];
        if (strncmp(name, text, len) == 0)
            return strdup(name);        // readline frees this
    }
    return NULL;
}

// Called by readline when TAB is pressed.
// First word on the line -> complete command names.
// Anywhere else (or no command matches) -> default filename completion.
static char** shell_completion(const char* text, int start, int end) {
    (void)end;
    if (start == 0)
        return rl_completion_matches(text, command_generator);
    return NULL;   // NULL = let readline do its default filename completion
}

void init_readline(void) {
    build_command_list();
    rl_attempted_completion_function = shell_completion;
}
