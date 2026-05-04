#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc,char *argv[]){


if(argc<2){
fprintf(2, "usage:space <command> [args]\n");
exit(1);

}

// enable tracing on this process
trace(getpid());
// exac the target command
exec(argv[1], &argv[1]);
//If exec fails:
fprintf(2, "strace: exec %s failed\n",argv[1]);
exit(1);

}

