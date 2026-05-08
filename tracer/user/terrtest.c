#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
    printf("terrtest: starting error-returns test\n");

    open("nonexistent_file", 0);
    kill(99999);
    unlink("nonexistent_file");

    int pid = getpid();

    printf("terrtest: my pid is %d\n", pid);

    exit(0);
}
