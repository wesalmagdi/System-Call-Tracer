#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
    printf("forktracetest: parent pid = %d\n", getpid());

    for (int i = 0; i < 3; i++) {
        int pid = fork();

        if (pid < 0) {
            printf("fork %d failed\n", i);
            exit(1);
        }

        if (pid == 0) {
            int mypid = getpid();

            printf("child %d: pid=%d\n", i, mypid);

            exit(i + 10);
        }
    }

    for (int i = 0; i < 3; i++) {
        int status;

        int pid = wait(&status);

        printf("parent: child pid=%d status=%d\n", pid, status);
    }

    exit(0);
}
