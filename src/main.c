#include "shell.h"

// 1 if the line contains only spaces/tabs
static int is_blank(const char* s) {
    for (; *s != '\0'; s++)
        if (*s != ' ' && *s != '\t')
            return 0;
    return 1;
}

// Tokenize, parse and run ONE command (the text between two ';').
static void run_command(char* segment) {
    char** arglist = tokenize(segment);
    if (arglist == NULL)
        return;                             // empty segment, nothing to do

    int background = strip_background(arglist);   // trailing '&' ?

    command_t cmds[MAXARGS];
    int ncmds = parse_pipeline(arglist, cmds);

    if (ncmds > 0) {
        // A plain foreground command that is a built-in must run
        // inside the shell process itself.
        if (!background && ncmds == 1 && cmds[0].infile == NULL &&
            cmds[0].outfile == NULL && handle_builtin(cmds[0].argv)) {
            // handled, nothing more to do
        } else {
            execute(cmds, ncmds, background, segment);
        }
    }

    // Free the memory allocated by tokenize()
    for (int i = 0; arglist[i] != NULL; i++)
        free(arglist[i]);
    free(arglist);
}

int main() {
    char* cmdline;

    init_readline();    // set up tab completion

    while (1) {
        // Before every prompt: collect finished background jobs (no zombies)
        reap_jobs();

        // readline() prints the prompt, handles line editing, arrow keys and
        // TAB completion. It returns a malloc'ed line, or NULL on Ctrl+D.
        cmdline = readline(PROMPT);
        if (cmdline == NULL)
            break;

        // Handle !n BEFORE tokenizing and BEFORE saving to history:
        // replace the "!n" line with the command stored at position n.
        if (cmdline[0] == '!') {
            char* recalled = recall_history(cmdline);
            free(cmdline);
            if (recalled == NULL)
                continue;               // invalid !n, show prompt again
            cmdline = recalled;
            printf("%s\n", cmdline);    // show what is being re-run
        }

        // Save the whole line once, exactly as typed
        if (!is_blank(cmdline)) {
            add_history(cmdline);       // readline's history (Up/Down arrows)
            add_to_history(cmdline);    // our own history (history, !n)
        }

        // Split at ';' and run each piece one after the other
        char* segments[MAXARGS];
        int nseg = split_commands(cmdline, segments, MAXARGS);
        for (int i = 0; i < nseg; i++)
            run_command(segments[i]);

        free(cmdline);
    }

    printf("\nShell exited.\n");
    return 0;
}
