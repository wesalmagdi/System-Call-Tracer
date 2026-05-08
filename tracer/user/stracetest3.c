#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  uptime();
  sbrk(4096);
  pause(2);
  uptime();
  exit(0);
}
