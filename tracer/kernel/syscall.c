#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

int trace_target_pid = -1; /* -1 = trace all pids */

/* ---------- helpers used by other parts of kernel ---------- */

int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz)
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

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
  case 0: return p->trapframe->a0;
  case 1: return p->trapframe->a1;
  case 2: return p->trapframe->a2;
  case 3: return p->trapframe->a3;
  case 4: return p->trapframe->a4;
  case 5: return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

void argint(int n, int *ip)   { *ip = argraw(n); }
void argaddr(int n, uint64 *ip) { *ip = argraw(n); }

int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  argaddr(n, &addr);
  return fetchstr(addr, buf, max);
}

/* ---------- syscall dispatch table ---------- */

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
/* function prototype for sys_trace (defined in sysproc.c) */
extern uint64 sys_trace(void);

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

/* maps syscall number → human-readable name (index = syscall number) */
static char *syscall_names[] = {
  "",         /* 0  unused */
  "fork",     /* 1  SYS_fork */
  "exit",     /* 2  SYS_exit */
  "wait",     /* 3  SYS_wait */
  "pipe",     /* 4  SYS_pipe */
  "read",     /* 5  SYS_read */
  "kill",     /* 6  SYS_kill */
  "exec",     /* 7  SYS_exec */
  "fstat",    /* 8  SYS_fstat */
  "chdir",    /* 9  SYS_chdir */
  "dup",      /* 10 SYS_dup */
  "getpid",   /* 11 SYS_getpid */
  "sbrk",     /* 12 SYS_sbrk */
  "sleep",    /* 13 SYS_pause */
  "uptime",   /* 14 SYS_uptime */
  "open",     /* 15 SYS_open */
  "write",    /* 16 SYS_write */
  "mknod",    /* 17 SYS_mknod */
  "unlink",   /* 18 SYS_unlink */
  "link",     /* 19 SYS_link */
  "mkdir",    /* 20 SYS_mkdir */
  "close",    /* 21 SYS_close */
};

/* =================================================================
 * Simple kernel string builder (kbuf)
 * -----------------------------------------------------------------
 * xv6 kernel has no snprintf, so we build JSON strings by appending
 * characters, strings, and integers into a fixed-size buffer.
 * ================================================================= */

struct kbuf {
  char data[512]; /* buffer — big enough for one JSON line */
  int  pos;       /* next write position */
};

/* append a single character */
static void
kb_char(struct kbuf *b, char c)
{
  if(b->pos < (int)sizeof(b->data) - 1)
    b->data[b->pos++] = c;
}

/* append a null-terminated string */
static void
kb_str(struct kbuf *b, const char *s)
{
  while(*s)
    kb_char(b, *s++);
}

/* append a signed integer (decimal) */
static void
kb_int(struct kbuf *b, long v)
{
  char tmp[24];
  int  j = 0;

  if(v < 0){
    kb_char(b, '-');
    v = -v;
  }
  if(v == 0){
    kb_char(b, '0');
    return;
  }
  /* build digits in reverse order */
  while(v > 0){
    tmp[j++] = '0' + (int)(v % 10);
    v /= 10;
  }
  /* append in correct order */
  while(--j >= 0)
    kb_char(b, tmp[j]);
}

/* =================================================================
 * trace_write_log
 * -----------------------------------------------------------------
 * Writes the JSON string from a kbuf to the process's log file fd.
 *
 * Problem: filewrite() expects a *user* virtual address, but our
 * JSON is in a kernel buffer.  Solution: copy the JSON into the
 * unused tail of the trapframe page, which is mapped at TRAPFRAME
 * in the process's user address space, then pass that user VA to
 * filewrite().
 *
 * The trapframe struct is 288 bytes; the page is 4096 bytes, so
 * there are ~3808 bytes of scratch space available.
 * ================================================================= */
static void
trace_write_log(struct proc *p, struct kbuf *b)
{
  int fd  = p->trace_logfd;
  int len = b->pos;

  /* nothing to do if no log fd or empty buffer */
  if(fd < 0 || fd >= NOFILE || p->ofile[fd] == 0 || len <= 0)
    return;

  /* clamp to available scratch space */
  int maxspace = PGSIZE - (int)sizeof(struct trapframe);
  if(len > maxspace)
    len = maxspace;

  /* copy JSON from kernel buffer into trapframe scratch area
   * (kernel VA = p->trapframe + sizeof trapframe) */
  char *scratch = (char*)p->trapframe + sizeof(struct trapframe);
  memmove(scratch, b->data, len);

  /* write from the user VA of the same scratch area to the log file */
  filewrite(p->ofile[fd], TRAPFRAME + sizeof(struct trapframe), len);
}

/* =================================================================
 * trace_syscall
 * -----------------------------------------------------------------
 * Called after (or before exit) every syscall when trace_enabled=1.
 * Builds one JSON line and:
 *   1. prints it to the console (always — useful in QEMU)
 *   2. writes it to the log file fd (if trace_logfd >= 0)
 * ================================================================= */
void
trace_syscall(struct proc *p, int num, uint64 *args)
{
  /* bounds-check syscall number */
  if(num < 1 || num >= NELEM(syscall_names))
    return;

  extern uint ticks; /* xv6 global clock counter */
  char strbuf[64];   /* scratch buffer for string args from user space */
  struct kbuf b;
  b.pos = 0;

  /* --- build JSON header fields --- */
  kb_str(&b, "{\"timestamp\":");
  kb_int(&b, (long)ticks);
  kb_str(&b, ",\"pid\":");
  kb_int(&b, (long)p->pid);
  kb_str(&b, ",\"process\":\"");
  kb_str(&b, p->name);
  /* NOTE: syscall name is quoted with \" on both sides */
  kb_str(&b, "\",\"syscall\":\"");
  kb_str(&b, syscall_names[num]);
  kb_str(&b, "\",\"args\":[");

  /* --- per-syscall argument formatting --- */

  /* open (15): path string, flags int, mode int */
  if(num == 15){
    if(copyinstr(p->pagetable, strbuf, args[0], sizeof(strbuf)) == 0){
      kb_char(&b, '"'); kb_str(&b, strbuf); kb_char(&b, '"');
    } else {
      kb_int(&b, (long)args[0]); /* fallback: print raw pointer */
    }
    kb_char(&b, ','); kb_int(&b, (long)args[1]);
    kb_char(&b, ','); kb_int(&b, (long)args[2]);
  }

  /* exec (7): path string, argv pointer */
  else if(num == 7){
    kb_char(&b, '"'); kb_str(&b, p->name); kb_char(&b, '"');
    kb_char(&b, ','); kb_int(&b, (long)args[1]);
  }

  /* chdir(9) mknod(17) unlink(18) mkdir(20): first arg is a path string */
  else if(num == 9 || num == 17 || num == 18 || num == 20){
    if(copyinstr(p->pagetable, strbuf, args[0], sizeof(strbuf)) == 0){
      kb_char(&b, '"'); kb_str(&b, strbuf); kb_char(&b, '"');
    } else {
      kb_int(&b, (long)args[0]);
    }
    kb_char(&b, ','); kb_int(&b, (long)args[1]);
    kb_char(&b, ','); kb_int(&b, (long)args[2]);
  }

  /* link (19): two path string args */
  else if(num == 19){
    char strbuf2[64];
    if(copyinstr(p->pagetable, strbuf,  args[0], sizeof(strbuf))  == 0 &&
       copyinstr(p->pagetable, strbuf2, args[1], sizeof(strbuf2)) == 0){
      kb_char(&b, '"'); kb_str(&b, strbuf);  kb_char(&b, '"');
      kb_char(&b, ',');
      kb_char(&b, '"'); kb_str(&b, strbuf2); kb_char(&b, '"');
    } else {
      kb_int(&b, (long)args[0]);
      kb_char(&b, ',');
      kb_int(&b, (long)args[1]);
    }
  }

  /* fork (1), getpid (11), uptime (14): no args */
  else if(num == 1 || num == 11 || num == 14){
    /* empty args array */
  }

  /* read (5): fd, buf_ptr, count */
  else if(num == 5){
    kb_int(&b, (long)args[0]); kb_char(&b, ',');
    kb_int(&b, (long)args[1]); kb_char(&b, ',');
    kb_int(&b, (long)args[2]);
  }

  /* write (16): fd, buf_ptr, count */
  else if(num == 16){
    kb_int(&b, (long)args[0]); kb_char(&b, ',');
    kb_int(&b, (long)args[1]); kb_char(&b, ',');
    kb_int(&b, (long)args[2]);
  }

  /* close (21), dup (10), kill (6): one or two args */
  else if(num == 21 || num == 10){
    /* close(fd) and dup(fd): one arg */
    kb_int(&b, (long)args[0]);
  }
  else if(num == 6){
    /* kill(pid, sig): two args */
    kb_int(&b, (long)args[0]); kb_char(&b, ',');
    kb_int(&b, (long)args[1]);
  }

  /* wait (3), pipe (4), fstat (8), sbrk (12), sleep (13): two args */
  else if(num == 3 || num == 4 || num == 8 || num == 12 || num == 13){
    kb_int(&b, (long)args[0]); kb_char(&b, ',');
    kb_int(&b, (long)args[1]);
  }

  /* exit (2): one arg (exit status) */
  else if(num == 2){
    kb_int(&b, (long)args[0]);
  }

  /* default: print first 3 args as raw integers */
  else {
    kb_int(&b, (long)args[0]); kb_char(&b, ',');
    kb_int(&b, (long)args[1]); kb_char(&b, ',');
    kb_int(&b, (long)args[2]);
  }

  /* --- close JSON: return value + newline --- */
  kb_str(&b, "],\"return\":");
  kb_int(&b, (long)(int)p->trapframe->a0);
  kb_str(&b, "}\n");
  b.data[b.pos] = 0; /* null-terminate for printf */

  /* 1. print to console (always — visible in QEMU terminal) */
  printf("%s", b.data);

  /* 2. write to log file if a log fd was configured */
  trace_write_log(p, &b);
}

/* =================================================================
 * syscall  (main dispatcher)
 * -----------------------------------------------------------------
 * Called from usertrap() for every syscall.
 * Saves args before execution so we can log exit() correctly
 * (exit never returns, so we must log it before it runs).
 * ================================================================= */
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7; /* syscall number is always in a7 */

  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {

    /* save args BEFORE execution — a0 gets overwritten with return value */
    uint64 saved_args[6];
    saved_args[0] = p->trapframe->a0;
    saved_args[1] = p->trapframe->a1;
    saved_args[2] = p->trapframe->a2;
    saved_args[3] = p->trapframe->a3;
    saved_args[4] = p->trapframe->a4;
    saved_args[5] = p->trapframe->a5;

    /* exit (2) must be traced BEFORE it runs — process dies after */
    if(p->trace_enabled && num == SYS_exit){
      if(trace_target_pid == -1 || p->pid == trace_target_pid)
        trace_syscall(p, num, saved_args);
    }

    p->trapframe->a0 = syscalls[num](); /* execute the syscall */

    /* trace all other syscalls AFTER execution (return value is now in a0) */
    if(p->trace_enabled && num != SYS_exit){
      if(trace_target_pid == -1 || p->pid == trace_target_pid)
        trace_syscall(p, num, saved_args);
    }

  } else {
    printf("%d %s: unknown sys call %d\n", p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
