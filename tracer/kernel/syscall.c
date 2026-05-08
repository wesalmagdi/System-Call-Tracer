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

//++
// syscall names table - index matches syscall numbers in syscall.h
static char *syscall_names[] = {
"",           // 0 is not a syscall
"fork",       // 1 is SYS_fork
"exit",       // 2 is SYS_exit
"wait",       // 3 is SYS_wait
"pipe",       // 4 is SYS_pipe
"read",       // 5 is SYS_read
"kill",       // 6 is SYS_kill
"exec",       // 7 is SYS_exec
"fstat",      // 8 is SYS_fstat
"chdir",      // 9 is SYS_chdir
"dup",        // 10 is SYS_dup
"getpid",     // 11 is SYS_getpid
"sbrk",       // 12 is SYS_sbrk
"sleep",      // 13 is SYS_pause
"uptime",     // 14 is SYS_uptime
"open",       // 15 is SYS_open
"write",      // 16 is SYS_write
"mknod",      // 17 is SYS_mknod
"unlink",     // 18 is SYS_unlink
"link",       // 19 is SYS_link
"mkdir",      // 20 is SYS_mkdir
"close",      // 21 is SYS_close

};

void trace_syscall(struct proc *p, int num, uint64 *args, int ret)
{
  if(num <= 0 || num >= NELEM(syscall_names))
    return;

  printf("%d: syscall %s(", p->pid, syscall_names[num]);

  switch(num) {

  case SYS_exec: {
    char path[64];
    fetchstr(args[0], path, sizeof(path));
    printf("\"%s\", ...", path);
    break;
  }

  case SYS_open: {
    char path[64];
    fetchstr(args[0], path, sizeof(path));
    printf("\"%s\", %d, %d",
           path,
           (int)args[1],
           (int)args[2]);
    break;
  }

  case SYS_read:
  case SYS_write:
  case SYS_close:
  default:
    printf("%ld, %ld, %ld",
           (long)args[0],
           (long)args[1],
           (long)args[2]);
  }

  printf(") = %d\n", ret);
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

  // save args
  uint64 saved_args[6];
  saved_args[0] = p->trapframe->a0;
  saved_args[1] = p->trapframe->a1;
  saved_args[2] = p->trapframe->a2;
  saved_args[3] = p->trapframe->a3;
  saved_args[4] = p->trapframe->a4;
  saved_args[5] = p->trapframe->a5;
  

  int do_trace =
      p->trace_enabled &&
      (trace_target_pid == -1 || p->pid == trace_target_pid);

  // execute syscall
  int ret = syscalls[num]();
  p->trapframe->a0 = ret;

  // trace ONLY ONCE (clean + correct)
  if(do_trace &&
   num != SYS_write &&
   num != SYS_read){
    trace_syscall(p, num, saved_args, ret);
}
}
