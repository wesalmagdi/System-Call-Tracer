#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"
int trace_target_pid = -1; // -1 = trace all
void trace_syscall(struct proc *p, int num, uint64 *args, int ret);

// Fetch the uint64 at addr from the current process.
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz) // both tests needed, in case of overflow
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  if(copyinstr(p->pagetable, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
void
argint(int n, int *ip)
{
  *ip = argraw(n);
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
void
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  argaddr(n, &addr);
  return fetchstr(addr, buf, max);
}

// Prototypes for the functions that handle system calls.
extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_kill(void);
extern uint64 sys_exec(void);
extern uint64 sys_fstat(void);
extern uint64 sys_chdir(void);
extern uint64 sys_dup(void);
extern uint64 sys_getpid(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_pause(void);
extern uint64 sys_uptime(void);
extern uint64 sys_open(void);
extern uint64 sys_write(void);
extern uint64 sys_mknod(void);
extern uint64 sys_unlink(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_close(void);
// add to dispatch table
extern uint64 sys_trace(void);
// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_pause]   sys_pause,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_trace]   sys_trace,
};

// syscall names indexed by syscall number (see syscall.h)
static char *syscall_names[] = {
[SYS_fork]    "fork",
[SYS_exit]    "exit",
[SYS_wait]    "wait",
[SYS_pipe]    "pipe",
[SYS_read]    "read",
[SYS_kill]    "kill",
[SYS_exec]    "exec",
[SYS_fstat]   "fstat",
[SYS_chdir]   "chdir",
[SYS_dup]     "dup",
[SYS_getpid]  "getpid",
[SYS_sbrk]    "sbrk",
[SYS_pause]   "pause",
[SYS_uptime]  "uptime",
[SYS_open]    "open",
[SYS_write]   "write",
[SYS_mknod]   "mknod",
[SYS_unlink]  "unlink",
[SYS_link]    "link",
[SYS_mkdir]   "mkdir",
[SYS_close]   "close",
[SYS_trace]   "trace",
};

// number of arguments each syscall reads from a0..a5
static int syscall_nargs[] = {
[SYS_fork]    0,
[SYS_exit]    1,
[SYS_wait]    1,
[SYS_pipe]    1,
[SYS_read]    3,
[SYS_kill]    1,
[SYS_exec]    2,
[SYS_fstat]   2,
[SYS_chdir]   1,
[SYS_dup]     1,
[SYS_getpid]  0,
[SYS_sbrk]    2,
[SYS_pause]   1,
[SYS_uptime]  0,
[SYS_open]    2,
[SYS_write]   3,
[SYS_mknod]   3,
[SYS_unlink]  1,
[SYS_link]    2,
[SYS_mkdir]   1,
[SYS_close]   1,
[SYS_trace]   0,
};

// returns 1 if argument i of syscall num is a user-space path string.
// SYS_exec is excluded: exec replaces user memory before trace_syscall runs,
// so we pre-fetch its path in syscall() instead.
static int
arg_is_path(int num, int i)
{
  if(i == 0)
    return num == SYS_open || num == SYS_mkdir ||
           num == SYS_chdir || num == SYS_unlink || num == SYS_link ||
           num == SYS_mknod;
  if(i == 1)
    return num == SYS_link;
  return 0;
}

void
trace_syscall(struct proc *p, int num, uint64 *args, int ret)
{
  if(num <= 0 || num >= NELEM(syscall_names) || syscall_names[num] == 0)
    return;

  char buf[64];
  int n = syscall_nargs[num];

  printf("%d: syscall %s(", p->pid, syscall_names[num]);
  for(int i = 0; i < n; i++){
    if(i > 0)
      printf(", ");
    if(arg_is_path(num, i) && fetchstr(args[i], buf, sizeof(buf)) >= 0)
      printf("\"%s\"", buf);
    else
      printf("%d", (int)args[i]);
  }
  printf(") -> %d\n", ret);
}

void
syscall(void)
{
  struct proc *p = myproc();
  int num = p->trapframe->a7;
  

  if(num <= 0 || num >= NELEM(syscalls) || syscalls[num] == 0){
    printf("%d %s: unknown sys call %d\n",
           p->pid, p->name, num);
    p->trapframe->a0 = -1;
    return;
  }

  uint64 saved_args[3];
  saved_args[0] = p->trapframe->a0;
  saved_args[1] = p->trapframe->a1;
  saved_args[2] = p->trapframe->a2;

  // exec replaces user memory, so the path string at saved_args[0] is
  // unreadable after the call returns. Snapshot it now.
  char exec_path[64];
  int have_exec_path = 0;
  if(num == SYS_exec)
    have_exec_path = (fetchstr(saved_args[0], exec_path, sizeof(exec_path)) >= 0);

  int do_trace =
      p->trace_enabled &&
      (trace_target_pid == -1 || p->pid == trace_target_pid);

  int ret = syscalls[num]();
  p->trapframe->a0 = ret;

  // skip 1-byte writes to stdout/stderr — these are xv6 printf's
  // per-character writes and would flood the trace.
  int noisy = (num == SYS_write &&
               (saved_args[0] == 1 || saved_args[0] == 2) &&
               saved_args[2] == 1);

  if(do_trace && !noisy){
    if(num == SYS_exec && have_exec_path)
      printf("%d: syscall exec(\"%s\", %d) -> %d\n",
             p->pid, exec_path, (int)saved_args[1], ret);
    else
      trace_syscall(p, num, saved_args, ret);
  }
}
