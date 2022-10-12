#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

#define MAX_ARG_LEN 1024

int main(int argc, char *argv[]) 
{
    // stdout | xargs command args

    if (argc < 2) {
        fprintf(2, "usage: xargs <command> ...\n");
        exit(0);
    }

    char *args[MAXARG], arg[MAX_ARG_LEN];
    for (int i = 1; i < argc; i++) {
        args[i - 1] = argv[i];
    }
    args[argc] = 0;

    //接受stdin输入，以\n为分隔符
    char buf;
    int arg_index = 0;

    while (read(0, &buf, 1) > 0) {
        if (buf == '\n') {
            arg[arg_index] = 0;
            arg_index = 0;

            int pid = fork();
            if (pid < 0) {
                fprintf(2, "fork error...\n");
                exit(0);
            } 
            else if (pid == 0) {
                args[argc - 1] = arg;
                exec(argv[1], args);
            } 
            else {
                wait(0);
            }

        } 
        else {
            arg[arg_index++] = buf;
        }
    }

    exit(0);
}