#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/syscall.h"
#include "user/user.h"

static struct {
  char *name;
  int   num;
} nametable[] = {
  { "fork",   SYS_fork   },
  { "exit",   SYS_exit   },
  { "wait",   SYS_wait   },
  { "pipe",   SYS_pipe   },
  { "read",   SYS_read   },
  { "kill",   SYS_kill   },
  { "exec",   SYS_exec   },
  { "fstat",  SYS_fstat  },
  { "chdir",  SYS_chdir  },
  { "dup",    SYS_dup    },
  { "getpid", SYS_getpid },
  { "sbrk",   SYS_sbrk   },
  { "pause",  SYS_pause  },
  { "uptime", SYS_uptime },
  { "open",   SYS_open   },
  { "write",  SYS_write  },
  { "mknod",  SYS_mknod  },
  { "unlink", SYS_unlink },
  { "link",   SYS_link   },
  { "mkdir",  SYS_mkdir  },
  { "close",  SYS_close  },
};

#define NTABLE (sizeof(nametable) / sizeof(nametable[0]))

static int
parse_mask(char *filter)
{
  if(filter[0] == '\0')
    return -2;

  uint mask = 0;
  char *p = filter;

  while(*p){
    char token[32];
    int i = 0;
    while(*p && *p != ',' && i < 31)
      token[i++] = *p++;
    token[i] = '\0';
    if(*p == ',') p++;

    if(i == 0) continue;

    int found = 0;
    for(uint j = 0; j < NTABLE; j++){
      if(strcmp(token, nametable[j].name) == 0){
        mask |= (1 << nametable[j].num);
        found = 1;
        break;
      }
    }
    if(!found){
      fprintf(2, "strace: unknown syscall name '%s'\n", token);
      return -1;
    }
  }
  return (int)mask;
}

int
main(int argc, char *argv[])
{
  int mask = 0;
  int cmdstart = 1;

  for(int i = 1; i < argc; i++){
    if(strcmp(argv[i], "-e") == 0){
      i++;
      if(i >= argc || memcmp(argv[i], "trace=", 6) != 0){
        fprintf(2, "strace: expected 'trace=<syscalls>' after -e\n");
        exit(1);
      }
      char *filter = argv[i] + 6;
      int m = parse_mask(filter);
      if(m == -1)
        exit(1);
      if(m == -2)
        mask = 1 << 31;
      else
        mask = m;
      cmdstart = i + 1;
    } else {
      cmdstart = i;
      break;
    }
  }

  if(cmdstart >= argc){
    fprintf(2, "usage: strace [-e trace=syscall,...] command [args]\n");
    exit(1);
  }

  trace(mask);
  exec(argv[cmdstart], &argv[cmdstart]);
  fprintf(2, "strace: exec %s failed\n", argv[cmdstart]);
  exit(1);
}