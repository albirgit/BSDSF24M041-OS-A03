#include "shell.h"

// ---------- Command history storage ----------
static char* history[HISTORY_SIZE];
static int history_count = 0;   // number of commands currently stored

char* read_cmd(char* prompt, FILE* fp) {
    printf("%s", prompt);
    char* cmdline = (char*) malloc(sizeof(char) * MAX_LEN);
    int c, pos = 0;

    while ((c = getc(fp)) != EOF) {
        if (c == '\n') break;
        cmdline[pos++] = c;
    }

    if (c == EOF && pos == 0) {
        free(cmdline);
        return NULL; // Handle Ctrl+D
    }

    cmdline[pos] = '\0';
    return cmdline;
}

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
    char* start;
    int len;
    int argnum = 0;

    while (*cp != '\0' && argnum < MAXARGS) {
        while (*cp == ' ' || *cp == '\t') cp++; // Skip leading whitespace

        if (*cp == '\0') break; // Line was only whitespace

        start = cp;
        len = 1;
        while (*++cp != '\0' && !(*cp == ' ' || *cp == '\t')) {
            len++;
        }
        strncpy(arglist[argnum], start, len);
        arglist[argnum][len] = '\0';
        argnum++;
    }

    if (argnum == 0) { // No arguments were parsed
        for(int i = 0; i < MAXARGS + 1; i++) free(arglist[i]);
        free(arglist);
        return NULL;
    }

    arglist[argnum] = NULL;
    return arglist;
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
        printf("  jobs      Show background jobs (not yet implemented)\n");
        return 1;
    }

    if (strcmp(arglist[0], "jobs") == 0) {
        printf("Job control not yet implemented.\n");
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
