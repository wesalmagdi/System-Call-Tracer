#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    if(argc < 2){
        fprintf(2, "usage: strace <command> [args]\n");
        exit(1);
    }

    // enable tracing for this process
    trace(1);

    // exec the target command
    exec(argv[1], &argv[1]);

    // if exec fails
    fprintf(2, "strace: exec %s failed\n", argv[1]);
    exit(1);
}