#include "shell.h"

typedef struct proc {
  pid_t pid;    /* process identifier */
  int state;    /* RUNNING or STOPPED or FINISHED */
  int exitcode; /* -1 if exit status not yet received */
} proc_t;

typedef struct job {
  pid_t pgid;            /* 0 if slot is free */
  proc_t *proc;          /* array of processes running in as a job */
  struct termios tmodes; /* saved terminal modes */
  int nproc;             /* number of processes */
  int state;             /* changes when live processes have same state */
  char *command;         /* textual representation of command line */
} job_t;

static job_t *jobs = NULL;          /* array of all jobs */
static int njobmax = 1;             /* number of slots in jobs array */
static int tty_fd = -1;             /* controlling terminal file descriptor */
static struct termios shell_tmodes; /* saved shell terminal modes */

static void sigchld_handler(int sig) {
  int old_errno = errno;
  pid_t pid;
  int status;
  /* TODO: Change state (FINISHED, RUNNING, STOPPED) of processes and jobs.
   * Bury all children that finished saving their status in jobs. */
#ifdef STUDENT
  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
    // basically, we go job by job and check the state of its processes
    for (int j = 0; j < njobmax; j++) {
      job_t *job = &jobs[j];
      if (job->pgid == 0) { // empty job slot
        continue;
      }
      for (int i = 0; i < job->nproc; i++) {
        // process by process, checking and updating state and exitcode
        proc_t *proc = &job->proc[i];
        if (proc->pid == pid) {
          if (WIFEXITED(status)) {
            // finished by exiting (ctrl+d)
            proc->state = FINISHED;
            proc->exitcode = status;
          } else if (WIFSIGNALED(status)) {
            // finished by signal (ctrl+c)
            proc->state = FINISHED;
            proc->exitcode = status;
          } else if (WIFSTOPPED(status)) {
            // stopped by signal (ctrl+z)
            proc->state = STOPPED;
          } else if (WIFCONTINUED(status)) {
            // resumed by signal (fg command)
            proc->state = RUNNING;
          }
          break;
        }
      }
      int running = 0, stopped = 0, finished = 0;
      // we count the number of finished, running and stopped
      // processes in a job
      for (int i = 0; i < job->nproc; i++) {
        if (job->proc[i].state == RUNNING) {
          running++;
        } else if (job->proc[i].state == STOPPED) {
          stopped++;
        } else if (job->proc[i].state == FINISHED) {
          finished++;
        }
      }
      if (finished == job->nproc)
        // if all processes are finished, job is finished
        job->state = FINISHED;
      else if (stopped > 0)
        // if there is a stopped process, the job is
        // considered as stopped
        job->state = STOPPED;
      else // same but running
        job->state = RUNNING;
    }
  }
#endif /* !STUDENT */
  errno = old_errno;
}

/* When pipeline is done, its exitcode is fetched from the last process. */
static int exitcode(job_t *job) {
  return job->proc[job->nproc - 1].exitcode;
}

static int allocjob(void) {
  /* Find empty slot for background job. */
  for (int j = BG; j < njobmax; j++)
    if (jobs[j].pgid == 0)
      return j;

  /* If none found, allocate new one. */
  jobs = realloc(jobs, sizeof(job_t) * (njobmax + 1));
  memset(&jobs[njobmax], 0, sizeof(job_t));
  return njobmax++;
}

static int allocproc(int j) {
  job_t *job = &jobs[j];
  job->proc = realloc(job->proc, sizeof(proc_t) * (job->nproc + 1));
  return job->nproc++;
}

int addjob(pid_t pgid, int bg) {
  int j = bg ? allocjob() : FG;
  job_t *job = &jobs[j];
  /* Initial state of a job. */
  job->pgid = pgid;
  job->state = RUNNING;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
  job->tmodes = shell_tmodes;
  return j;
}

static void deljob(job_t *job) {
  assert(job->state == FINISHED);
  free(job->command);
  free(job->proc);
  job->pgid = 0;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
}

static void movejob(int from, int to) {
  assert(jobs[to].pgid == 0);
  memcpy(&jobs[to], &jobs[from], sizeof(job_t));
  memset(&jobs[from], 0, sizeof(job_t));
}

static void mkcommand(char **cmdp, char **argv) {
  if (*cmdp)
    strapp(cmdp, " | ");

  for (strapp(cmdp, *argv++); *argv; argv++) {
    strapp(cmdp, " ");
    strapp(cmdp, *argv);
  }
}

void addproc(int j, pid_t pid, char **argv) {
  assert(j < njobmax);
  job_t *job = &jobs[j];

  int p = allocproc(j);
  proc_t *proc = &job->proc[p];
  /* Initial state of a process. */
  proc->pid = pid;
  proc->state = RUNNING;
  proc->exitcode = -1;
  mkcommand(&job->command, argv);
}

/* Returns job's state.
 * If it's finished, delete it and return exitcode through statusp. */
static int jobstate(int j, int *statusp) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  int state = job->state;

  /* TODO: Handle case where job has finished. */
#ifdef STUDENT
  if (state == FINISHED) {
    *statusp = exitcode(job);
    deljob(job);
    // if a job is finished, return appropriate exit code and
    // delete from a list
  }
#endif /* !STUDENT */

  return state;
}

char *jobcmd(int j) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  return job->command;
}

/* Continues a job that has been stopped. If move to foreground was requested,
 * then move the job to foreground and start monitoring it. */
bool resumejob(int j, int bg, sigset_t *mask) {
  if (j < 0) {
    for (j = njobmax - 1; j > 0 && jobs[j].state == FINISHED; j--)
      continue;
  }

  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;

    /* TODO: Continue stopped job. Possibly move job to foreground slot. */
#ifdef STUDENT
  job_t *job = &jobs[j];
  job->state = RUNNING;
  if (!bg) {
    // if foreground
    Tcsetattr(tty_fd, 0, &shell_tmodes);
    // change attributes
    movejob(j, 0);
    // moving to fg
    setfgpgrp(jobs[0].pgid);
    // setting fg process group
    kill(-jobs[0].pgid, SIGCONT);
    // resuming all processes in a group
    msg("[%d] continue '%s'\n", j, jobcmd(0));
    monitorjob(mask);
    // after all we start monitoring it
  } else {
    kill(-job->pgid, SIGCONT);
    // we just resume processes
    msg("[%d] continue '%s'\n", j, jobcmd(j));
  }
#endif /* !STUDENT */

  return true;
}

/* Kill the job by sending it a SIGTERM. */
bool killjob(int j) {
  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;
  debug("[%d] killing '%s'\n", j, jobs[j].command);

  /* TODO: I love the smell of napalm in the morning. */
#ifdef STUDENT
  job_t *job = &jobs[j];
  if (job->state == STOPPED) {
    // we search for stopped jobs
    setfgpgrp(job->pgid);
    // we change terminal attribute to job group
    Tcsetattr(tty_fd, TCSAFLUSH, &jobs[j].tmodes);
    kill(-job->pgid, SIGTERM);
    // after that we kill
    kill(-job->pgid, SIGCONT);
    // processes need to be woke up to answer the SIGTERM
    setfgpgrp(getpid());
    // at least we get back shell attributes and control
    Tcsetattr(tty_fd, TCSAFLUSH, &shell_tmodes);
  }
  kill(-job->pgid, SIGTERM);
  // and then we kill
#endif /* !STUDENT */

  return true;
}

/* Report state of requested background jobs. Clean up finished jobs. */
void watchjobs(int which) {
  for (int j = BG; j < njobmax; j++) {
    if (jobs[j].pgid == 0)
      continue;

      /* TODO: Report job number, state, command and exit code or signal. */
#ifdef STUDENT
    job_t *job = &jobs[j];
    if (which != ALL && job->state != which) {
      continue;
    }
    if (job->state == RUNNING) {
      // we print an appropriate message depends on a state
      printf("[%d] running '%s'\n", j, job->command);
    } else if (job->state == STOPPED) {
      printf("[%d] suspended '%s'\n", j, job->command);
    } else {
      // handling finished, we can finish the job by signal or by just
      // exiting the shell
      if (WIFSIGNALED(exitcode(job))) {
        msg("[%d] killed '%s' by signal %d\n", j, job->command,
            WTERMSIG(exitcode(job)));
      } else if (WIFEXITED(exitcode(job))) {
        msg("[%d] exited '%s', status=%d\n", j, job->command,
            WEXITSTATUS(exitcode(job)));
      }
      deljob(job);
    }
#endif /* !STUDENT */
  }
}

/* Monitor job execution. If it gets stopped move it to background.
 * When a job has finished or has been stopped move shell to foreground. */
int monitorjob(sigset_t *mask) {
  int exitcode = 0, state;

  /* TODO: Following code requires use of Tcsetpgrp of tty_fd. */
#ifdef STUDENT
  job_t *job = &jobs[0];
  Tcsetpgrp(tty_fd, job->pgid);
  // we give control over terminal to a pgid of a job
  state = jobstate(0, &exitcode);
  // we save job state
  while (state == RUNNING) {
    Sigsuspend(mask);
    // if running, we turn off the signal mask
    state = jobstate(0, &exitcode);
  }
  if (state == STOPPED) {
    Tcgetattr(tty_fd, &jobs[0].tmodes);
    // we save terminal settings to bring it back then
    int new = allocjob();
    // we allocate a place in jobs
    movejob(0, new);
    // and move the stopped job there
  }
  Tcsetattr(tty_fd, 0, &shell_tmodes);
  // bring back terminal settings
  Tcsetpgrp(tty_fd, getpgrp());
  // bring back control
#endif /* !STUDENT */

  return exitcode;
}

/* Called just at the beginning of shell's life. */
void initjobs(void) {
  struct sigaction act = {
    .sa_flags = SA_RESTART,
    .sa_handler = sigchld_handler,
  };

  /* Block SIGINT for the duration of `sigchld_handler`
   * in case `sigint_handler` does something crazy like `longjmp`. */
  sigemptyset(&act.sa_mask);
  sigaddset(&act.sa_mask, SIGINT);
  Sigaction(SIGCHLD, &act, NULL);

  jobs = calloc(sizeof(job_t), 1);

  /* Assume we're running in interactive mode, so move us to foreground.
   * Duplicate terminal fd, but do not leak it to subprocesses that execve. */
  assert(isatty(STDIN_FILENO));
  tty_fd = Dup(STDIN_FILENO);
  fcntl(tty_fd, F_SETFD, FD_CLOEXEC);

  /* Take control of the terminal. */
  Tcsetpgrp(tty_fd, getpgrp());

  /* Save default terminal attributes for the shell. */
  Tcgetattr(tty_fd, &shell_tmodes);
}

/* Called just before the shell finishes. */
void shutdownjobs(void) {
  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Kill remaining jobs and wait for them to finish. */
#ifdef STUDENT
  for (int i = 1; i < njobmax; i++) {
    // if job is finished, we do nothing, if not, we kill it
    if (jobs[i].pgid == 0 || jobs[i].state == FINISHED) {
      continue;
    }
    killjob(i);
    while (jobs[i].state != FINISHED) {
      // suspending signals for the killing process
      Sigsuspend(&mask);
    }
  }
#endif /* !STUDENT */

  watchjobs(FINISHED);

  Sigprocmask(SIG_SETMASK, &mask, NULL);

  Close(tty_fd);
}

/* Sets foreground process group to `pgid`. */
void setfgpgrp(pid_t pgid) {
  Tcsetpgrp(tty_fd, pgid);
}
