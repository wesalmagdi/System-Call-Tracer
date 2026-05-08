#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  int fd = open("README", 0);
  if(fd < 0)
    exit(1);

  struct stat st;
  fstat(fd, &st);

  int fd2 = dup(fd);
  close(fd2);
  close(fd);

  exit(0);
}
