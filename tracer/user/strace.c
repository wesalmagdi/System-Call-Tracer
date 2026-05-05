#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

/*
 * strace — user-space front-end for the xv6 syscall tracer
 *
 * Usage:  strace <command> [args]
 * Example: strace cat README
 *
 * What it does:
 *   1. Opens /xv6_trace.log for writing (creates or truncates).
 *   2. Calls trace(pid, logfd) to tell the kernel:
 *        - which process to trace (this one)
 *        - which fd to write JSON lines to
 *   3. exec's the target command.
 *
 * After the command finishes, /xv6_trace.log in the xv6 filesystem
 * contains one JSON object per line — ready for the backend.
 *
 * JSON output also appears on the console (QEMU terminal) in real time.
 */

#define LOG_PATH "/xv6_trace.log"

int
main(int argc, char *argv[])
{
  if(argc < 2){
    fprintf(2, "usage: strace <command> [args]\n");
    exit(1);
  }

  /* open the log file — O_CREATE makes it if it doesn't exist,
   * O_TRUNC clears it if it does                                 */
  int logfd = open(LOG_PATH, O_WRONLY | O_CREATE | O_TRUNC);
  if(logfd < 0){
    /* can't open file — still trace to console only */
    fprintf(2, "strace: warning: cannot open %s, logging to console only\n",
            LOG_PATH);
    logfd = -1;
  }

  /* tell the kernel to start tracing this process
   * arg0 = our own pid
   * arg1 = log file fd (or -1 if file open failed)              */
  trace(getpid(), logfd);

  /* replace this process image with the target command
   * argv[1] = command name, &argv[1] = argv for that command    */
  exec(argv[1], &argv[1]);

  /* exec only returns if something went wrong */
  fprintf(2, "strace: exec %s failed\n", argv[1]);
  exit(1);
}
