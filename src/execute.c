#include "shell.h"

// ---------- Background job table ----------
typedef struct {
    int id;                 // job number shown to the user: [1], [2], ...
    pid_t pid;              // PID of the (last process of the) job
    char cmd[MAX_LEN];      // the command text, for display
} job_t;

static job_t jobs[MAX_JOBS];
static int job_count = 0;
static int next_job_id = 1;

// Remember a new background job. Returns its job id, or -1 if the table is full.
static int add_job(pid_t pid, const char* cmdtext) {
    if (job_count == MAX_JOBS)
        return -1;

    while (*cmdtext == ' ' || *cmdtext == '\t')     // skip leading spaces
        cmdtext++;

    job_t* j = &jobs[job_count++];
    j->id = next_job_id++;
    j->pid = pid;
    strncpy(j->cmd, cmdtext, MAX_LEN - 1);
    j->cmd[MAX_LEN - 1] = '\0';

    size_t len = strlen(j->cmd);                    // strip trailing spaces
    while (len > 0 && (j->cmd[len - 1] == ' ' || j->cmd[len - 1] == '\t'))
        j->cmd[--len] = '\0';

    return j->id;
}

// Collect every finished child WITHOUT blocking (WNOHANG), so no zombies
// are left behind, and remove finished jobs from the table.
void reap_jobs(void) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].pid == pid) {
                printf("[%d] Done    %s\n", jobs[i].id, jobs[i].cmd);
                for (int k = i + 1; k < job_count; k++)   // close the gap
                    jobs[k - 1] = jobs[k];
                job_count--;
                break;
            }
        }
    }
    if (job_count == 0)
        next_job_id = 1;    // numbering starts again when nothing is running
}

// The 'jobs' built-in
void print_jobs(void) {
    reap_jobs();            // so finished jobs are not shown as running
    if (job_count == 0) {
        printf("No background jobs.\n");
        return;
    }
    for (int i = 0; i < job_count; i++)
        printf("[%d] %d Running  %s\n", jobs[i].id, (int)jobs[i].pid, jobs[i].cmd);
}

// ---------- Running commands ----------
// Runs a pipeline of ncmds commands: cmd0 | cmd1 | ... | cmdN
// (ncmds == 1 means a plain command, possibly with < and > redirection).
// background != 0: do not wait, record the job and return immediately.
int execute(command_t* cmds, int ncmds, int background, const char* cmdtext) {
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
            // A background job must not fight the shell for the keyboard
            if (background && i == 0 && cmds[i].infile == NULL) {
                int nf = open("/dev/null", O_RDONLY);
                if (nf != -1) { dup2(nf, STDIN_FILENO); close(nf); }
            }
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
            // Built-ins inside a pipeline/redirection/background run in the child
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

    if (background) {
        // Do NOT wait. Remember the job and give the prompt back at once.
        if (started > 0) {
            pid_t last = pids[started - 1];
            int id = add_job(last, cmdtext);
            if (id == -1)
                fprintf(stderr, "warning: job table full, job is not tracked\n");
            else
                printf("[%d] %d\n", id, (int)last);
        }
        return 0;
    }

    // Wait for every child. The exit status of the LAST process is the
    // status of the whole command/pipeline (needed by if-then-else-fi).
    int status;
    int result = (started > 0) ? 0 : 1;
    for (int i = 0; i < started; i++) {
        waitpid(pids[i], &status, 0);
        if (i == started - 1) {
            if (WIFEXITED(status))
                result = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                result = 128 + WTERMSIG(status);
        }
    }
    return result;
}
