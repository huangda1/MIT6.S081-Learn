#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void seive(int* pl) {
    // already close pl[1], so if parent write is done(close p[1]), read get zero
    // prime is the first element of pl
    int prime;
    if (read(pl[0], &prime, sizeof(int)) == 0) {
        exit(0);
    }
    printf("prime %d\n", prime);

    int pr[2];
    pipe(pr);

    if (fork() == 0) {
        close(pr[1]);
        seive(pr);
    } else {
        close(pr[0]);
        int v;
        while (read(pl[0], &v, sizeof(int)) != 0) {
            if (v % prime) write(pr[1], &v, sizeof(int));
        }
        close(pr[1]);
        wait(0);
        exit(0);
    }
}

int main(int argc, char *argv[]) 
{
    int p[2];
    pipe(p);

    if (fork() == 0) {
        close(p[1]);
        seive(p);
    } else {
        close(p[0]);
        for (int i = 2; i <= 35; i++) {
            write(p[1], &i, sizeof i);
        }
        close(p[1]);
        wait(0);
    }
    exit(0);
}