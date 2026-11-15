#include "shell.h"

// Runs a pipeline of ncmds commands: cmd0 | cmd1 | ... | cmdN
// (ncmds == 1 means a plain command, possibly with < and > redirection).
int execute(command_t* cmds, int ncmds) {
    pid_t pids[MAXARGS];
    int started = 0;        // how many children we have forked
    int prev_read = -1;     // read end of the previous pipe (-1 = none)

    fflush(stdout);         // don't let children inherit unflushed output

    for (int i = 0; i < ncmds; i++) {
        int fd[2] = {-1, -1};

        // Every command except the last one writes into a new pipe
        if (i < ncmds - 1 && pipe(fd) == -1) {
            perror("pipe");
            break;
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
            if (fd[0] != -1) { close(fd[0]); close(fd[1]); }
            break;
        }

        if (pid == 0) {                         // ---- child ----
            // stdin comes from the previous pipe
            if (prev_read != -1) {
                dup2(prev_read, STDIN_FILENO);
                close(prev_read);
            }
            // stdout goes into the new pipe
            if (fd[1] != -1) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[0]);
                close(fd[1]);
            }
            // '<' and '>' override the pipe if both are given
            if (cmds[i].infile != NULL) {
                int f = open(cmds[i].infile, O_RDONLY);
                if (f == -1) { perror(cmds[i].infile); exit(1); }
                dup2(f, STDIN_FILENO);
                close(f);
            }
            if (cmds[i].outfile != NULL) {
                int f = open(cmds[i].outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (f == -1) { perror(cmds[i].outfile); exit(1); }
                dup2(f, STDOUT_FILENO);
                close(f);
            }
            // Built-ins inside a pipeline/redirection run in the child
            if (handle_builtin(cmds[i].argv))
                exit(0);

            execvp(cmds[i].argv[0], cmds[i].argv);
            perror("Command not found");        // only reached if execvp fails
            exit(1);
        }

        // ---- parent ----
        pids[started++] = pid;
        if (prev_read != -1) close(prev_read);  // child has its own copy
        if (fd[1] != -1) close(fd[1]);          // parent never writes
        prev_read = fd[0];                      // next child reads from here
    }

    if (prev_read != -1) close(prev_read);      // only if we broke out early

    int status;
    for (int i = 0; i < started; i++)
        waitpid(pids[i], &status, 0);
    return 0;
}
