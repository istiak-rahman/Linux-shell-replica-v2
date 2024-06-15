#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <readline/readline.h>
#include <errno.h>

#include "builtin.h"
#include "parse.h"

/*******************************************
 * Set to 1 to view the command line parse *
 *******************************************/
#define DEBUG_PARSE 0

Job* J;
int job_num = 0;
int our_tty;


void print_banner ()
{
    printf ("                    ________   \n");
    printf ("_________________________  /_  \n");
    printf ("___  __ \\_  ___/_  ___/_  __ \\ \n");
    printf ("__  /_/ /(__  )_(__  )_  / / / \n");
    printf ("_  .___//____/ /____/ /_/ /_/  \n");
    printf ("/_/ Type 'exit' or ctrl+c to quit\n\n");
}


/* **returns** a string used to build the prompt
 * (DO NOT JUST printf() IN HERE!)
 *
 * Note:
 *   If you modify this function to return a string on the heap,
 *   be sure to free() it later when appropirate!  */
static char* build_prompt ()
{
    char *cwd = malloc(sizeof(char) * PATH_MAX);
    if (getcwd(cwd, PATH_MAX) == NULL) {
      perror("could not return cwd\n");
      exit(EXIT_FAILURE);
    }
    
    strcat(cwd, "$ ");
    
    return cwd;
}


/* return true if command is found, either:
 *   - a valid fully qualified path was supplied to an existing file
 *   - the executable file was found in the system's PATH
 * false is returned otherwise */
static int command_found (const char* cmd)
{
    char* dir;
    char* tmp;
    char* PATH;
    char* state;
    char probe[PATH_MAX];

    int ret = 0;

    if (access (cmd, X_OK) == 0)
        return 1;

    PATH = strdup (getenv("PATH"));

    for (tmp=PATH; ; tmp=NULL) {
        dir = strtok_r (tmp, ":", &state);
        if (!dir)
            break;

        strncpy (probe, dir, PATH_MAX-1);
        strncat (probe, "/", PATH_MAX-1);
        strncat (probe, cmd, PATH_MAX-1);

        if (access (probe, X_OK) == 0) {
            ret = 1;
            break;
        }
    }

    free (PATH);
    return ret;
}

void set_fg_pgrp(int pgrp)
{
    void (*sav)(int sig);

    if (pgrp == 0)
        pgrp = getpgrp();

    sav = signal(SIGTTOU, SIG_IGN);
    tcsetpgrp(our_tty, pgrp);
    signal(SIGTTOU, sav);
}

int find_availability() 
{
    int i;
    for (i=0; i<job_num; i++) {
        if (J[i].name == NULL && J[i].npids == 0)
            return i;
    }

    return job_num - 1;
}

int find_job(pid_t chld)
{
    int i, j, index = -1;
    for (i=0; i<job_num; i++) {
        for (j=0; j<J[i].npids; j++) {
            if (J[i].pids[j] == chld) {
                index = i;
                break;
            }
        }
        if (index != -1) {
            break;
        }
    }

    return index;
}

void print_jobs() 
{
    if (getpgrp() != tcgetpgrp(STDOUT_FILENO)) {
        // ensure shell is the fg group
        set_fg_pgrp(0);
    }

    int i;
    for (i=0; i<job_num; i++) {
        if (J[i].name && J[i].pgid) {
            char state[15];
            if (J[i].status == FG || J[i].status == BG) {
                strcpy(state, "running");
            }
            else if (J[i].status == STOPPED) {
                strcpy(state, "stopped");
            }
            printf("[%d] + %s\t%s\n", i, state, J[i].name);
        }
    }
}

void remove_job(int k)
{
    // if (J[k].name == NULL && J[k].pgid == 0) {
    //     printf("Job %d not found.\n", k);
    //     return;
    // }

    free(J[k].name);
    free(J[k].pids);

    J[k].name = NULL;
    J[k].pids = NULL;
    J[k].pgid = 0;
    J[k].npids = 0;
    J[k].nfinishedtasks = 0;
    J[k].status = TERM;

    job_num--;
}


void handler(int sig)
{
    pid_t chld;
    int status, idx;

    switch(sig) {
    case SIGTTOU:
        while(tcgetpgrp(STDOUT_FILENO) != getpgrp())
            pause();
        break;
    case SIGCHLD:
        while( (chld = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0 ) {
            idx = find_job(chld);
            if (WIFCONTINUED(status)) {
                set_fg_pgrp(0);
                printf("[%d] + continued\t%s\n", idx, J[idx].name);
                return;
            } else if (WIFSTOPPED(status)) {
                set_fg_pgrp(0);
                printf("\n[%d] + suspended\t%s\n", idx, J[idx].name);
                J[idx].status = STOPPED;
                return;
            } else { // waited on terminated children
                set_fg_pgrp(0);
                J[idx].nfinishedtasks++;
                //printf("Job Index: %d   NPIDS: %d   Chld: %d\n", idx, J[idx].npids, chld);
                if (J[idx].nfinishedtasks == J[idx].npids) {
                    if (J[idx].status == BG) {
                        printf("\n[%d] + done\t%s\n", idx, J[idx].name);
                    }
                    remove_job(idx);
                }
            }
        }

        break;

    default:
        break;
    }

}

void builtin_kill (Task T)
{
    pid_t pid;
    int sig, i, job_id, j;
    int s_flag = 0;
    int l_flag = 0;

    // no command line arguments are provided
    if (T.argv[0] != NULL && T.argv[1] == NULL) {
        printf("Usage: kill [-s <signal>] <pid> | %%<job> ...\n");
    }
    // -l flag is specified
    else if (T.argv[2] == NULL && strcmp(T.argv[1], "-l") == 0) {
        l_flag = 1;
        for (i=0; i < 31; i++) {
            printf("%2d) SIG%-14s%s\n", i+1, sigabbrev(i+1), strsignal(i+1));
        }
    }
    // no specific signal is provided using -s
    else if (strcmp(T.argv[1], "-s")) {
        sig = 15; // SIGTERM
    }
    // specific signal is provided using -s
    else if (!strcmp(T.argv[1], "-s")) {
        s_flag = 1;
        sig = atoi(T.argv[2]);
    }

    for (i=1; T.argv[i] != NULL; i++) {
        // kill specified job
        if (T.argv[i][0] == '%') {
            char* token = strtok(T.argv[i], "%");
            job_id = atoi(token);
            if (job_id < 0 || job_id > job_num) {
                printf("pssh: invalid job number: [%d]\n", job_id);
            }
            else {
                for (j=0; j<J[job_id].npids; j++) {
                    if (kill(J[job_id].pids[j], sig) == -1) {
                        printf("pssh: could not send SIG%s to job %d\n", sigabbrev(sig), job_id);
                    }
                }
            }
        }
        // kill specified process
        else {
            if (!strcmp(T.argv[i], "-s") || l_flag || atoi(T.argv[i]) == sig)
                continue;
            
            pid = atoi(T.argv[i]);
            if (find_job(pid) == -1 && !l_flag && !s_flag) {
                printf("pssh: invalid pid: [%d]\n", pid);
            }
            else if (sig == 0) {
                if (kill(pid, sig) == 0) {
                    printf("pssh: PID %d exists and is able to receive signals\n", pid);
                }
                else {
                    if (errno == ESRCH) {
                        printf("pssh: PID %d does not exist\n", pid);
                    }
                    else if (errno == EPERM) {
                        printf("pssh: PID %d exists, but we can't send it signals\n", pid);
                    }
                    else {
                        printf("pssh: an invalid signal was specified\n");
                    }
                }
            }
            else {
                if (kill(pid, sig) == -1) {
                    printf("pssh: could not send SIG%s to pid %d\n", sigabbrev(sig), pid);
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}


/* Called upon receiving a successful parse.
 * This function is responsible for cycling through the
 * tasks, and forking, executing, etc as necessary to get
 * the job done! */
void execute_tasks (Parse* P, char* cmdline)
{
    unsigned int t;
    int pipe_fd[P->ntasks-1][2]; // for pipe between each pair of tasks
    pid_t job_pids[P->ntasks];
    int job_idx;
    int via_bg_cmd = 0;

    // install handlers
    signal(SIGTTOU, handler);
    signal(SIGCHLD, handler);
    signal(SIGINT, handler);
    signal(SIGTSTP, handler);
    signal(SIGTTIN, handler);
    
    if (!isatty(STDOUT_FILENO)) {
        printf("STDOUT_FILENO is not a tty.\n");
        exit(EXIT_FAILURE);
    }

    our_tty = dup(STDOUT_FILENO);
    job_num++;
    job_idx = find_availability();

    J[job_idx].name = malloc(strlen(cmdline)+1);
    J[job_idx].pids = malloc(sizeof(int)*100);
    strcpy(J[job_idx].name, cmdline);
    J[job_idx].nfinishedtasks = 0;

    for (t = 0; t < P->ntasks; t++) {
        if (is_builtin (P->tasks[t].cmd)) {
            Task T = P->tasks[t];
            if (!strcmp(T.cmd, "jobs")) {
                print_jobs();
                remove_job(job_idx);
                return;
            }
            else if (!strcmp(P->tasks[t].cmd, "kill")) {
                builtin_kill(T);
                remove_job(job_idx);
                return;
            }
            else if (!strcmp(T.cmd, "fg") || !strcmp(T.cmd, "bg")) {
                if (T.argv[0] != NULL && T.argv[1] == NULL) {
                    printf("Usage: %s %%<job number>\n", T.cmd);
                }
                else {
                    via_bg_cmd = 1;
                    char* token = strtok(cmdline, "%");
                    token = strtok(NULL, "%");
                    int num = atoi(token);
                    //set_fg_pgrp(0);
                    if (!J[num].name) {
                        printf("pssh: invalid job number: [%d]\n", num);
                    }
                    else if (!strcmp(T.cmd, "fg")) {
                        P->background = 0;
                        J[num].status = FG;
                        set_fg_pgrp(J[num].pgid);
                        kill(-J[num].pgid, SIGCONT);
                        remove_job(job_idx);
                        return;
                    }
                    else {
                        P->background = 1;
                        J[num].status = BG;
                        set_fg_pgrp(0);
                        kill(-J[num].pgid, SIGCONT);
                        remove_job(job_idx);
                        return;
                    }
                }
            }
            else {
                builtin_execute (P->tasks[t], P->infile, P->outfile);
            }
        }
        else if (command_found (P->tasks[t].cmd)) {
            //printf ("pssh: found but can't exec: %s\n", P->tasks[t].cmd);
            
            if (t < P->ntasks - 1) {
                if (pipe(pipe_fd[t]) == -1) {
                    perror("failed to create pipe(s)\n");
                    exit(EXIT_FAILURE);
                }
            }

            job_pids[t] = fork();
            setpgid(job_pids[t], job_pids[0]); // place process into process group

            J[job_idx].pids[J[job_idx].npids] = job_pids[t];
            J[job_idx].npids++;
            
            if (P->background == 0) {
                set_fg_pgrp(job_pids[0]);
            }

            if (t == 0) {
                J[job_idx].pgid = job_pids[0]; // place pgrp id into Jobs array
                if (P->background)
                    J[job_idx].status = BG;
                else
                    J[job_idx].status = FG;
            }
            
            if (job_pids[t] == -1) {
                perror("error -- failed to fork()\n");
                exit(EXIT_FAILURE);
            }
            else if (job_pids[t] == 0) {
                close(our_tty);
                
                if (P->infile) {
                    infile_redirect(P->infile);
                }
                
                if (P->outfile) {
                    outfile_redirect(P->outfile);
                }
                
                if (t > 0) {
                    close(pipe_fd[t-1][1]);
                    if (dup2(pipe_fd[t-1][0], STDIN_FILENO) == -1) {
                        perror("dup2() 1 failed\n");
                        exit(EXIT_FAILURE);
                    }
                    close(pipe_fd[t-1][0]);
                    //printf("reading from prev works\n");
                }
                
                if (t < P->ntasks - 1) {
                    close(pipe_fd[t][0]);
                    if (dup2(pipe_fd[t][1], STDOUT_FILENO) == -1) {
                        perror("dup2() 4 failed\n");
                        exit(EXIT_FAILURE);
                    }
                    close(pipe_fd[t][1]);
                    //printf("writing to next works\n");
                }
                
                // execute task(s)
                execvp(P->tasks[t].cmd, P->tasks[t].argv);
                printf("pssh: child -- failed to exec!\n");
                exit(EXIT_FAILURE);
            }
            
            // close parent-side read/write endpoints
            if (t < P->ntasks - 1) {
                close(pipe_fd[t][1]);
            }
            if (t > 0) {
                close(pipe_fd[t-1][0]);
            }
        }
        else {
            printf ("pssh: command not found: %s\n", P->tasks[t].cmd);
            break;
        }
    }

    // check that all child processes have been terminated
    if (!P->background) {
        for (t=0; t<P->ntasks; t++) {
            while (!kill(job_pids[t], 0) && J[job_idx].status == FG)
                pause();
        }
    }
    else {
        if (!via_bg_cmd) {
            printf("[%d] ", job_idx);
            for (t=0; t<J[job_idx].npids; t++) {
                printf("%d ", J[job_idx].pids[t]);
            }
            printf("\n");
        }
    }

    printf(" \n");

    close(our_tty);
}


int main (int argc, char** argv)
{
    char* cmdline;
    Parse* P;
    // initialize jobs array
    J = new_jobs();

    print_banner ();
    char *prompt;

    while (1) {
        fflush(stdout);
        prompt = build_prompt();
        cmdline = readline (prompt);

        char* cmd = malloc(strlen(cmdline)+1);
        strcpy(cmd, cmdline);

        if (!cmdline)       /* EOF (ex: ctrl-d) */
            exit (EXIT_SUCCESS);

        P = parse_cmdline (cmdline);
        if (!P)
            goto next;

        if (P->invalid_syntax) {
            printf ("pssh: invalid syntax\n");
            goto next;
        }

#if DEBUG_PARSE
        parse_debug (P);
#endif

        execute_tasks (P, cmd);

    next:
        parse_destroy (&P);
        free(cmd);
        free(cmdline);
        free(prompt);
    }
}
