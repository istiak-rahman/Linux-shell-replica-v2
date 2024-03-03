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


/* Called upon receiving a successful parse.
 * This function is responsible for cycling through the
 * tasks, and forking, executing, etc as necessary to get
 * the job done! */
void execute_tasks (Parse* P)
{
    unsigned int t;
    pid_t job_pids[P->ntasks];
    int pipe_fd[P->ntasks-1][2]; // for pipe between each pair of tasks
    
    for (t = 0; t < P->ntasks; t++) {
        if (is_builtin (P->tasks[t].cmd)) {
            builtin_execute (P->tasks[t], P->infile, P->outfile);
        }
        else if (command_found (P->tasks[t].cmd)) {
            //printf ("pssh: found but can't exec: %s\n", P->tasks[t].cmd);
            
            int in_fd, out_fd;
            
            if (t < P->ntasks - 1) {
                if (pipe(pipe_fd[t]) == -1) {
                    perror("failed to create pipe(s)\n");
                    exit(EXIT_FAILURE);
                }
            }
            
            job_pids[t] = fork();
            //setpgid(job_pids[t], job_pids[0]); // place process into process group
            
            switch (job_pids[t]) {
            case -1:
                perror("error -- failed to fork()\n");
                exit(EXIT_FAILURE);
            
            case 0:
                setpgid(0, job_pids[0]); // set process group ID to the first child's PID
                if (P->infile) {
                    in_fd = open(P->infile, O_RDONLY);
                    if (in_fd == -1) {
                        perror("could not read input file\n");
                        exit(EXIT_FAILURE);
                    }
                    if (dup2(in_fd, STDIN_FILENO) == -1) {
                        perror("dup2() 2 failed\n");
                        exit(EXIT_FAILURE);
                    }
                    
                    close(in_fd);
                }
                  
                if (P->outfile) {
                    out_fd = open(P->outfile, O_RDWR | O_CREAT | O_TRUNC, 0644);
                    if (out_fd == -1) {
                        perror("could not read output file\n");
                        exit(EXIT_FAILURE);
                    }
                    if (dup2(out_fd, STDOUT_FILENO) == -1) {
                        perror("dup2() 3 failed\n");
                        exit(EXIT_FAILURE);
                    }
                
                    close(out_fd);
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
                
                execvp(P->tasks[t].cmd, P->tasks[t].argv);
                printf("pssh: child -- failed to exec!\n");
                exit(EXIT_FAILURE);
            
            default:
            
                printf("Parent  PID: %d\n", getpid());
                printf("Parent PGRP: %d\n", getpgrp());
                
                printf("\n");
//                if (getpgid(job_pids[t]) == getpgrp()) {
//                    printf("This is the group leaer: %d\n", job_pids[0]);
//                    setpgid(job_pids[t], job_pids[0]);
//                }

                printf("Child  PID: %d\n", job_pids[t]);
                printf("Child PGRP: %d\n", getpgid(job_pids[t]));
                
                printf("\n");
                
                // close parent-side read/write endpoints
                if (t < P->ntasks - 1) {
                    close(pipe_fd[t][1]);
                }
                if (t > 0) {
                    close(pipe_fd[t - 1][0]);
                }
                
                wait(NULL);
                
                break;
            }
            //wait(NULL);
        }
        else {
            printf ("pssh: command not found: %s\n", P->tasks[t].cmd);
            break;
        }
        
    }
}


int main (int argc, char** argv)
{
    char* cmdline;
    Parse* P;

    print_banner ();
    char *prompt;

    while (1) {
        prompt = build_prompt();
        cmdline = readline (prompt);
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

        execute_tasks (P);

    next:
        parse_destroy (&P);
        free(cmdline);
        free(prompt);
    }
}
