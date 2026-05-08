#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
    printf("selftrace: about to call trace()\n");

    trace();

    printf("selftrace: trace() returned, calling getpid\n");

    int pid = getpid();

    printf("selftrace: pid = %d\n", pid);

    exit(0);
}
