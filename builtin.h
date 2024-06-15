#ifndef _builtin_h_
#define _builtin_h_

#include "parse.h"

typedef enum {
    STOPPED,
    TERM,
    BG,
    FG,
} JobStatus;

typedef struct {
    char* name;
    pid_t* pids;
    unsigned int npids;
    pid_t pgid;
    unsigned int nfinishedtasks;
    JobStatus status;
} Job;

Job* new_jobs();
int is_builtin (char* cmd);
const char *sigabbrev(unsigned int sig);
void infile_redirect (char *infile);
void outfile_redirect (char *outfile);
void builtin_execute (Task T, char* infile, char* outfile);
void builtin_which (Task T, char* infile, char* outfile);
//void builtin_kill (Task T);

#endif /* _builtin_h_ */
