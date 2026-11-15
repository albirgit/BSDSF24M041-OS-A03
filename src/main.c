#include "shell.h"

int main() {
    char* cmdline;
    char** arglist;

    init_readline();    // set up tab completion

    // readline() prints the prompt, handles line editing, arrow keys and
    // TAB completion. It returns a malloc'ed line, or NULL on Ctrl+D.
    while ((cmdline = readline(PROMPT)) != NULL) {

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

        if ((arglist = tokenize(cmdline)) != NULL) {
            add_history(cmdline);       // readline's history (Up/Down arrows)
            add_to_history(cmdline);    // our own history (history, !n)

            command_t cmds[MAXARGS];
            int ncmds = parse_pipeline(arglist, cmds);

            if (ncmds > 0) {
                // A plain command (no pipe, no redirection) that is a
                // built-in must run inside the shell process itself.
                if (ncmds == 1 && cmds[0].infile == NULL &&
                    cmds[0].outfile == NULL && handle_builtin(cmds[0].argv)) {
                    // handled, nothing more to do
                } else {
                    execute(cmds, ncmds);
                }
            }

            // Free the memory allocated by tokenize()
            for (int i = 0; arglist[i] != NULL; i++) {
                free(arglist[i]);
            }
            free(arglist);
        }
        free(cmdline);
    }

    printf("\nShell exited.\n");
    return 0;
}
