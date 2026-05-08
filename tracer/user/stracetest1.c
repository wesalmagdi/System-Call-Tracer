#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  getpid();

  int pid = fork();
  if(pid == 0)
    exit(7);

  int st;
  wait(&st);
  exit(0);
}
