#ifdef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#define DEBUG 0
#include "shell.h"

sigset_t sigchld_mask;

static void sigint_handler(int sig) {
  /* No-op handler, we just need break read() call with EINTR. */
  (void)sig;
}

/* Rewrite closed file descriptors to -1,
 * to make sure we don't attempt do close them twice. */
static void MaybeClose(int *fdp) {
  if (*fdp < 0)
    return;
  Close(*fdp);
  *fdp = -1;
}

/* Consume all tokens related to redirection operators.
 * Put opened file descriptors into inputp & output respectively. */
static int do_redir(token_t *token, int ntokens, int *inputp, int *outputp) {
  token_t mode = NULL; /* T_INPUT, T_OUTPUT or NULL */
  int n = 0;           /* number of tokens after redirections are removed */

  for (int i = 0; i < ntokens; i++) {
    /* TODO: Handle tokens and open files as requested. */
#ifdef STUDENT
    if (token[i] == T_INPUT) {
      // if a token is a '<', we change mode
      mode = T_INPUT;
    } else if (token[i] == T_OUTPUT) {
      // same with '>'
      mode = T_OUTPUT;
    } else if (mode) {
      // if it is just a command part
      if (mode == T_INPUT) {
        // if an input was before the current token
        MaybeClose(inputp);
        // we close previous fds
        *inputp = Open(token[i], O_RDONLY, 0);
        // and we enable reading from fd
        if (*inputp < 0)
          app_error("ERROR: Cannot open input file %s!", token[i]);
      } else if (mode == T_OUTPUT) {
        // same with output
        MaybeClose(outputp);
        *outputp = Open(token[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (*outputp < 0)
          app_error("ERROR: Cannot open output file %s!", token[i]);
      }
      mode = NULL;
      // we reset mode after checking conditions
    } else {
      token[n] = token[i];
      // if a token was input/output related, we skip it in array
      n++;
      // and we increment the number of unrelated tokens
    }
#endif /* !STUDENT */
  }

  token[n] = NULL;
  return n;
}

/* Execute internal command within shell's process or execute external command
 * in a subprocess. External command can be run in the background. */
static int do_job(token_t *token, int ntokens, bool bg) {
  int input = -1, output = -1;
  int exitcode = 0;

  ntokens = do_redir(token, ntokens, &input, &output);

  if (!bg) {
    if ((exitcode = builtin_command(token)) >= 0)
      return exitcode;
  }

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start a subprocess, create a job and monitor it. */
#ifdef STUDENT
  pid_t pid = fork();
  if (pid == 0) { // child process
    Signal(SIGINT, SIG_DFL);
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTTIN, SIG_DFL);
    Signal(SIGTTOU, SIG_DFL);
    Sigprocmask(SIG_SETMASK, &mask, NULL);
    // default signal handlers
    if (input != -1) {
      // file descriptors handling
      dup2(input, STDIN_FILENO);
      Close(input);
    }
    if (output != -1) {
      dup2(output, STDOUT_FILENO);
      Close(output);
    }
    external_command(token);
    // we are in a subprocess, so we deal with external commands
  } else {
    // parent process
    setpgid(pid, pid);
    // pid is now the leader of the group
    int job = addjob(pid, bg);
    // addjob
    addproc(job, pid, token);
    // and addproc like in the task
    if (!bg) {
      exitcode = monitorjob(&mask);
      // monitoring fg processes
    } else {
      msg("[%d] running '%s'\n", job, jobcmd(job));
      // appropriate message
    }
  }
#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

/* Start internal or external command in a subprocess that belongs to pipeline.
 * All subprocesses in pipeline must belong to the same process group. */
static pid_t do_stage(pid_t pgid, sigset_t *mask, int input, int output,
                      token_t *token, int ntokens, bool bg) {
  ntokens = do_redir(token, ntokens, &input, &output);

  if (ntokens == 0)
    app_error("ERROR: Command line is not well formed!");

  /* TODO: Start a subprocess and make sure it's moved to a process group. */
  pid_t pid = Fork();
#ifdef STUDENT
  if (pid == 0) { // child process
    Signal(SIGINT, SIG_DFL);
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTTIN, SIG_DFL);
    Signal(SIGTTOU, SIG_DFL);
    Sigprocmask(SIG_SETMASK, mask, NULL);
    // signal handling
    if (input != -1) {
      // fd handling
      dup2(input, STDIN_FILENO);
      Close(input);
    }
    if (output != -1) {
      dup2(output, STDOUT_FILENO);
      Close(output);
    }
    setpgid(0, pgid);
    // changing the leader of the group
    external_command(token);
    // subprocess, so external command
  } else {
    setpgid(pid, pgid);
    // changing leader
    MaybeClose(&input);
    // closing descriptors to avoid leaks
    MaybeClose(&output);
  }
#endif /* !STUDENT */

  return pid;
}

static void mkpipe(int *readp, int *writep) {
  int fds[2];
  Pipe(fds);
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  *readp = fds[0];
  *writep = fds[1];
}

/* Pipeline execution creates a multiprocess job. Both internal and external
 * commands are executed in subprocesses. */
static int do_pipeline(token_t *token, int ntokens, bool bg) {
  pid_t pid, pgid = 0;
  int job = -1;
  int exitcode = 0;

  int input = -1, output = -1, next_input = -1;

  mkpipe(&next_input, &output);

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start pipeline subprocesses, create a job and monitor it.
   * Remember to close unused pipe ends! */
#ifdef STUDENT
  int index_in_token = 0;
  // where are we in token array
  int index_in_part = 0;
  // where are we in a pipe part
  int i = 0;
  while (i < ntokens) {
    if (token[i] == T_PIPE) {
      // if a token is a '|'
      pid = do_stage(pgid, &mask, input, output, token + index_in_token,
                     index_in_part, bg);
      // create a pipe stage
      if (job == -1) {
        // if this is the first process
        pgid = pid;
        job = addjob(pgid, bg);
      }
      addproc(job, pid, token + index_in_token);
      input = next_input;
      // preparing to create next pipe
      mkpipe(&next_input, &output);
      // creating next pipe
      index_in_part = 0;
      // reset to beginning of the next part
      index_in_token = i + 1;
      // where are we in the token array
    } else if (i == ntokens - 1 ||
               (i == ntokens - 2 && token[ntokens - 1] == T_BGJOB)) {
      // if the last part
      Close(next_input);
      // closing descriptors
      Close(output);
      output = -1;
      pid = do_stage(pgid, &mask, input, output, token + index_in_token,
                     index_in_part + 1, bg);
      // create last stage
      addproc(job, pid, token + index_in_token);
      // add it
    } else {
      index_in_part++;
      // if just a command, move on
    }
    i++;
  }
  if (!bg) {
    exitcode = monitorjob(&mask);
  }
#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

static bool is_pipeline(token_t *token, int ntokens) {
  for (int i = 0; i < ntokens; i++)
    if (token[i] == T_PIPE)
      return true;
  return false;
}

static void eval(char *cmdline) {
  bool bg = false;
  int ntokens;
  token_t *token = tokenize(cmdline, &ntokens);

  if (ntokens > 0 && token[ntokens - 1] == T_BGJOB) {
    token[--ntokens] = NULL;
    bg = true;
  }

  if (ntokens > 0) {
    if (is_pipeline(token, ntokens)) {
      do_pipeline(token, ntokens, bg);
    } else {
      do_job(token, ntokens, bg);
    }
  }

  free(token);
}

#ifndef READLINE
static char *readline(const char *prompt) {
  static char line[MAXLINE]; /* `readline` is clearly not reentrant! */

  write(STDOUT_FILENO, prompt, strlen(prompt));

  line[0] = '\0';

  ssize_t nread = read(STDIN_FILENO, line, MAXLINE);
  if (nread < 0) {
    if (errno != EINTR)
      unix_error("Read error");
    msg("\n");
  } else if (nread == 0) {
    return NULL; /* EOF */
  } else {
    if (line[nread - 1] == '\n')
      line[nread - 1] = '\0';
  }

  return strdup(line);
}
#endif

int main(int argc, char *argv[]) {
  /* `stdin` should be attached to terminal running in canonical mode */
  if (!isatty(STDIN_FILENO))
    app_error("ERROR: Shell can run only in interactive mode!");

#ifdef READLINE
  rl_initialize();
#endif

  sigemptyset(&sigchld_mask);
  sigaddset(&sigchld_mask, SIGCHLD);

  if (getsid(0) != getpgid(0))
    Setpgid(0, 0);

  initjobs();

  struct sigaction act = {
    .sa_handler = sigint_handler,
    .sa_flags = 0, /* without SA_RESTART read() will return EINTR */
  };
  Sigaction(SIGINT, &act, NULL);

  Signal(SIGTSTP, SIG_IGN);
  Signal(SIGTTIN, SIG_IGN);
  Signal(SIGTTOU, SIG_IGN);

  while (true) {
    char *line = readline("# ");

    if (line == NULL)
      break;

    if (strlen(line)) {
#ifdef READLINE
      add_history(line);
#endif
      eval(line);
    }
    free(line);
    watchjobs(FINISHED);
  }

  msg("\n");
  shutdownjobs();

  return 0;
}
