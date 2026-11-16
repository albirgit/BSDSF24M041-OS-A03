#include "shell.h"

#define MAX_BLOCK 64

// 1 if the line contains only spaces/tabs
static int is_blank(const char* s) {
    for (; *s != '\0'; s++)
        if (*s != ' ' && *s != '\t')
            return 0;
    return 1;
}

// 1 if the first word of s is exactly kw (e.g. "then").
// *rest is set to the text that follows the keyword.
static int starts_with_word(char* s, const char* kw, char** rest) {
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(kw);
    if (strncmp(s, kw, n) != 0)
        return 0;
    if (s[n] != '\0' && s[n] != ' ' && s[n] != '\t')
        return 0;                       // "file" must not match "fi"
    *rest = s + n;
    return 1;
}

// Tokenize, parse and run ONE command (the text between two ';').
// Returns its exit status (0 = success).
static int run_command(char* segment) {
    char** arglist = tokenize(segment);
    if (arglist == NULL)
        return 0;                               // empty segment

    int status = 0;
    int background = strip_background(arglist); // trailing '&' ?

    command_t cmds[MAXARGS];
    int ncmds = parse_pipeline(arglist, cmds);

    if (ncmds < 0) {
        status = 2;                             // syntax error
    } else if (!background && ncmds == 1 && cmds[0].infile == NULL &&
               cmds[0].outfile == NULL && handle_builtin(cmds[0].argv)) {
        status = 0;                             // built-in ran in the shell
    } else {
        status = execute(cmds, ncmds, background, segment);
    }

    for (int i = 0; arglist[i] != NULL; i++)
        free(arglist[i]);
    free(arglist);
    return status;
}

// Split a line at ';' and run every piece. Returns the LAST status.
static int run_line(char* line) {
    char* segments[MAXARGS];
    int nseg = split_commands(line, segments, MAXARGS);
    int status = 0;
    for (int i = 0; i < nseg; i++)
        status = run_command(segments[i]);
    return status;
}

// Run an array of command strings one after the other. Returns the last status.
static int run_segments(char** segs, int n) {
    int status = 0;
    for (int i = 0; i < n; i++)
        status = run_command(segs[i]);
    return status;
}

// ---------- if / then / else / fi ----------
enum { ST_COND, ST_THEN, ST_ELSE };

typedef struct {
    char* cond[MAX_BLOCK];  int ncond;     // commands after 'if'
    char* thenb[MAX_BLOCK]; int nthen;     // commands between then and else/fi
    char* elseb[MAX_BLOCK]; int nelse;     // commands between else and fi
} if_block_t;

// Add one command string to the buffer of the current state.
static int add_cmd(if_block_t* b, int state, char* s) {
    char** arr;
    int* n;
    if (state == ST_COND)      { arr = b->cond;  n = &b->ncond; }
    else if (state == ST_THEN) { arr = b->thenb; n = &b->nthen; }
    else                       { arr = b->elseb; n = &b->nelse; }

    if (*n == MAX_BLOCK) {
        fprintf(stderr, "error: if-block is too long\n");
        return 0;
    }
    arr[(*n)++] = s;
    return 1;
}

// firstline starts with "if". Keep reading lines (prompt "> ") until the
// matching "fi", store the commands in three buffers, then run them.
// The caller still owns (and frees) firstline.
static void handle_if(char* firstline) {
    if_block_t b;
    memset(&b, 0, sizeof(b));

    char* lines[MAX_BLOCK];     // extra lines read by us (we must free them)
    int nlines = 0;
    char* segs[MAXARGS];        // pieces of the current line
    char* tail[MAXARGS];        // pieces that come after "fi" on its line
    int ntail = 0;

    int state = ST_COND;
    int error = 0, finished = 0, first = 1;
    char* line = firstline;

    while (!finished) {
        if (!first) {
            line = readline("> ");
            if (line == NULL) {
                fprintf(stderr, "syntax error: unexpected end of input, 'fi' expected\n");
                error = 1;
                break;
            }
            if (nlines == MAX_BLOCK) {
                fprintf(stderr, "error: if-block is too long\n");
                free(line);
                error = 1;
                break;
            }
            lines[nlines++] = line;
        }

        // 'if true; then echo a; fi' on one line works because of this split
        int nseg = split_commands(line, segs, MAXARGS);

        for (int i = 0; i < nseg && !finished; i++) {
            char* s = segs[i];
            char* rest = NULL;

            if (first && i == 0) {
                starts_with_word(s, "if", &rest);       // known to match
                if (!is_blank(rest) && !add_cmd(&b, state, rest))
                    error = 1;
            }
            else if (starts_with_word(s, "if", &rest)) {
                fprintf(stderr, "syntax error: nested 'if' is not supported\n");
                error = 1;
            }
            else if (starts_with_word(s, "then", &rest)) {
                if (state != ST_COND) {
                    fprintf(stderr, "syntax error: unexpected 'then'\n");
                    error = 1;
                } else if (b.ncond == 0) {
                    fprintf(stderr, "syntax error: missing command after 'if'\n");
                    error = 1;
                } else {
                    state = ST_THEN;
                    if (!is_blank(rest) && !add_cmd(&b, state, rest))
                        error = 1;
                }
            }
            else if (starts_with_word(s, "else", &rest)) {
                if (state != ST_THEN) {
                    fprintf(stderr, "syntax error: unexpected 'else'\n");
                    error = 1;
                } else {
                    state = ST_ELSE;
                    if (!is_blank(rest) && !add_cmd(&b, state, rest))
                        error = 1;
                }
            }
            else if (starts_with_word(s, "fi", &rest)) {
                if (state == ST_COND) {
                    fprintf(stderr, "syntax error: 'then' expected before 'fi'\n");
                    error = 1;
                }
                finished = 1;
                for (int j = i + 1; j < nseg; j++)      // commands after fi
                    tail[ntail++] = segs[j];
            }
            else {
                if (!add_cmd(&b, state, s))
                    error = 1;
            }
        }
        first = 0;
    }

    if (finished && !error) {
        int status = run_segments(b.cond, b.ncond);    // run the condition
        if (status == 0)
            run_segments(b.thenb, b.nthen);            // exit status 0  -> then
        else if (b.nelse > 0)
            run_segments(b.elseb, b.nelse);            // non-zero       -> else
        run_segments(tail, ntail);                     // anything after "fi"
    }

    for (int i = 0; i < nlines; i++)
        free(lines[i]);
}

int main() {
    char* cmdline;

    init_readline();    // set up tab completion

    while (1) {
        // Before every prompt: collect finished background jobs (no zombies)
        reap_jobs();

        cmdline = readline(PROMPT);
        if (cmdline == NULL)
            break;

        // Handle !n BEFORE tokenizing and BEFORE saving to history
        if (cmdline[0] == '!') {
            char* recalled = recall_history(cmdline);
            free(cmdline);
            if (recalled == NULL)
                continue;
            cmdline = recalled;
            printf("%s\n", cmdline);
        }

        // Save the whole line once, exactly as typed
        if (!is_blank(cmdline)) {
            add_history(cmdline);
            add_to_history(cmdline);
        }

        // A line starting with "if" begins a multi-line control block
        char* rest;
        if (starts_with_word(cmdline, "if", &rest)) {
            handle_if(cmdline);
            free(cmdline);
            continue;
        }

        run_line(cmdline);
        free(cmdline);
    }

    printf("\nShell exited.\n");
    return 0;
}
