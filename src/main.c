#include "shell.h"

int main() {
    char* cmdline;
    char** arglist;

    while ((cmdline = read_cmd(PROMPT, stdin)) != NULL) {

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
            add_to_history(cmdline);    // only real (non-empty) commands

            // Built-ins run inside the shell process (no fork)
            if (handle_builtin(arglist) == 0) {
                execute(arglist);
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
