#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "builtin.h"
#include "parse.h"

static char* builtin[] = {
    "exit",   /* exits the shell */
    "which",  /* displays full path to command */
    "kill",   /* send signals to specific processes*/
    "fg",     /* foreground process group*/
    "bg",     /* background process group*/
    "jobs",   /* prints all active jobs to stdout*/
    NULL
};

Job* new_jobs()
{
    Job* jobs = malloc(100*sizeof(Job));
    int i;
    for (i=0; i<100; i++) {
        jobs[i].name = NULL;
        jobs[i].pids = NULL;
        jobs[i].npids = 0;
        jobs[i].pgid = 0;
        jobs[i].nfinishedtasks = 0;
        jobs[i].status = TERM;
    }

    return jobs;
}

const char *sigabbrev(unsigned int sig)
{
    const char *sigs[31] = { "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT",
        "BUS", "FPE", "KILL", "USR1", "SEGV", "USR2", "PIPE", "ALRM",
        "TERM", "STKFLT", "CHLD", "CONT", "STOP", "TSTP", "TTIN",
        "TTOU", "URG", "XCPU", "XFSZ", "VTALRM", "PROF", "WINCH", "IO",
        "PWR", "SYS" };
  
    if (sig == 0 || sig > 31)
        return NULL;
    
    return sigs[sig-1];
}

int is_builtin (char* cmd)
{
    int i;

    for (i=0; builtin[i]; i++) {
        if (!strcmp (cmd, builtin[i]))
            return 1;
    }

    return 0;
}

void infile_redirect (char *infile)
{
    int in_fd;
    in_fd = open(infile, O_RDONLY);
    if (in_fd == -1) {
        perror("could not read input file\n");
        exit(EXIT_FAILURE);
    }
    if (dup2(in_fd, STDIN_FILENO) == -1) {
        perror("dup2() for infile redirection failed\n");
        exit(EXIT_FAILURE);
    }
    
    close(in_fd);
}

void outfile_redirect (char *outfile)
{
    int out_fd;
    out_fd = open(outfile, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (out_fd == -1) {
        perror("could not read output file\n");
        exit(EXIT_FAILURE);
    }
    if (dup2(out_fd, STDOUT_FILENO) == -1) {
        perror("dup2() for outfile redirection failed\n");
        exit(EXIT_FAILURE);
    }

    close(out_fd);
}


void builtin_which (Task T, char *infile, char *outfile)
{
    char* PATH;
    char* dir;
    char* tmp;
    char* state;
    char probe[PATH_MAX];
        
    switch (fork()) {
    case -1:
        perror("error -- failed to vfork()\n");
        exit(EXIT_FAILURE);
        
    case 0:
        if (infile) {
            infile_redirect(infile);
        }  

        if (outfile) {
            outfile_redirect(outfile);
        }
        
        if (is_builtin (T.argv[1])) {
            printf("%s: shell built-in command\n", T.argv[1]);
            exit(EXIT_SUCCESS);
        }
        
        PATH = strdup (getenv("PATH"));
        for (tmp=PATH; ; tmp=NULL) {
            dir = strtok_r (tmp, ":", &state);
            if (!dir)
                break;
            
            strncpy (probe, dir, PATH_MAX-1);
            strncat (probe, "/", PATH_MAX-1);
            strncat (probe, T.argv[1], PATH_MAX-1);
    
            if (access (probe, X_OK) == 0) {
                printf("%s\n", probe);
                exit(EXIT_SUCCESS);
            }
        }
    free(PATH);
       
    default:
        break;
    }
    wait(NULL);
}

void builtin_execute (Task T, char* infile, char *outfile)
{
    if (!strcmp (T.cmd, "exit")) {
        exit (EXIT_SUCCESS);
    }
    else if (!strcmp (T.cmd, "which")) {
        builtin_which(T, infile, outfile);
    }
    else {
        printf ("pssh: builtin command: %s (not implemented!)\n", T.cmd);
    }
}


