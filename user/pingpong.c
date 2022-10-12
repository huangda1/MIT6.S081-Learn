#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int p1[2], p2[2];
    pipe(p1); // p2c
    pipe(p2); // c2p
    int pid = fork();
    if (!pid) {
        char buf;
        read(p1[0], &buf, 1);
        printf("%d: received ping\n", getpid());
        write(p2[1], &buf, 1);
    } else {
        write(p1[1], ".", 1);
        char buf;
        read(p2[0], &buf, 1);
        printf("%d: received pong\n", getpid());
    }

    exit(0);
}