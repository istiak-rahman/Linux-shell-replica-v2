#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <readline/readline.h>

#include "builtin.h"
#include "parse.h"

/*******************************************
 * Set to 1 to view the command line parse *
 *******************************************/
#define DEBUG_PARSE 0

Job* J;
int job_num = 0;
int job_idx;
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

int find_availability() {
    int i;
    for (i=0; i<job_num; i++) {
        if (J[i].name == NULL)
            return i;
    }

    return job_num;
}

void remove_job(int k) {
    if (J[k].name == NULL || J[k].pgid == 0) {
        printf("Job %d not found.\n", k);
    }

    free(J[k].name);
    free(J[k].pids);
    J[k].pgid = 0;
    J[k].npids = 0;
    J[k].status = STOPPED;

    job_num--;
}

void set_fg_pgrp(pid_t pgrp)
{
    void (*sav)(int sig);

    if (pgrp == 0)
        pgrp = getpgrp();

    sav = signal(SIGTTOU, SIG_IGN);
    tcsetpgrp(our_tty, pgrp);
    signal(SIGTTOU, sav);
}

void handler(int sig)
{
    pid_t chld;
    int status, i;

    switch(sig) {
    case SIGTTOU:
        while(tcgetpgrp(STDOUT_FILENO) != getpgrp())
            pause();

        break;
    case SIGCHLD:
        while( (chld = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0 ) {
            if (WIFSTOPPED(status)) {
                set_fg_pgrp(0);
                printf("Parent: Child %d stopped! Continuing it!\n", chld);
                set_fg_pgrp(getpgid(chld));
                kill(chld, SIGCONT);
            } else { // waited on terminated children
                if (getpgrp() != tcgetpgrp(STDOUT_FILENO)) {
                    // return the fg to the shell
                    set_fg_pgrp(0);
                }

                if (J[job_num].status == BG) {
                    printf("[%d] + done     %s\n", job_num, J[job_num].name);
                    printf("\n");
                }

                printf("waitpid returned: %d\n", chld);

                if (WIFEXITED(status)) {
                    printf("Parent: Child %d has terminated with exit status %d\n", chld, WEXITSTATUS(status));
                }

                printf("Parent: Child %d has terminated\n", chld);
                J[job_num].status = TERM;
            }
        }

        break;

    default:
        break;
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
    
    if (!isatty(STDOUT_FILENO)) {
        printf("STDOUT_FILENO is not a tty.\n");
        exit(EXIT_FAILURE);
    }

    our_tty = dup(STDOUT_FILENO);

    J[job_num].name = malloc(strlen(cmdline)+1);
    J[job_num].pids = malloc(sizeof(int)*100);
    strcpy(J[job_num].name, cmdline);
//    printf("FG Process Group: %d\n", tcgetpgrp(STDOUT_FILENO));
//    printf("Parent Process Group: %d\n", getpgrp());
//    printf("-----\n");
    for (t = 0; t < P->ntasks; t++) {
        if (is_builtin (P->tasks[t].cmd)) {
            builtin_execute (P->tasks[t], P->infile, P->outfile);
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

            J[job_num].pids[J[job_num].npids] = job_pids[t];
            printf("calling process pid: %d\n", J[job_num].pids[J[job_num].npids]);
            J[job_num].npids++; // increment pid counter for job
            
            if (P->background == 0) {
                // sav = signal(SIGTTOU, SIG_IGN);
                // tcsetpgrp(STDOUT_FILENO, getpgrp());
                // signal(SIGTTOU, sav);
                set_fg_pgrp(job_pids[0]);
            }

            if (t == 0) {
                J[job_num].pgid = job_pids[0]; // place pgrp id into Jobs array
                if (P->background)
                    J[job_num].status = BG;
                else
                    J[job_num].status = FG;
                //printf("status: %d\n", J[job_num].status);
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

    // install handlers
    signal(SIGTTOU, handler);
    signal(SIGCHLD, handler);
    // signal(SIGTERM, handler);
    // signal(SIGTSTP, handler);
    // signal(SIGINT, handler);

    // check that all child processes have been terminated
    if (P->background == 0) {
        for (t=0; t<P->ntasks; t++)
            while (!kill(job_pids[t], 0))
                pause();
    }
    else {
        printf("[%d] ", job_num);
        for (t=0; t<J[job_num].npids; t++) {
            printf("%d ", J[job_num].pids[t]);
        }
        printf("\n");
    }

    job_num++;
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

