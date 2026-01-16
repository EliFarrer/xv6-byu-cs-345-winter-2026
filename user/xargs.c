#include "kernel/types.h"
#include "user/user.h"

int
getNewlineIdx(char* buf, int start) {
    // read from buf until newline or EOF, return length or -1 if error
    int i = start;
    while (buf[i] != '\n' && buf[i] != '\0') {
        i++;
    }

    if (buf[i] == '\0') {
        return -1;  // indicate EOF
    }
    return i;

}

int
main(int argc, char *argv[])
{
    int const MAX_BUF = 512;
    char buf[MAX_BUF];
    int const MAX_ARGS = 32;
    int i = 0;
    char* start = buf;

    // verify we have a command
    if (argc < 2) {
        fprintf(2, "Usage: xargs command [initial-arguments]\n");
        exit(1);
    }
    
    const char* command = argv[1];  // get the command to execute for each line
    
    // create new arguments to pass into exec
    char* args[MAX_ARGS];
    for (int j = 2; j < argc; j++) {
        args[j - 2] = argv[j];
    }
    
    printf("Starting\n");
    while (read(0, start, 1) == 1 && i < MAX_BUF) { // read while we get 1 byte and we haven't overwritten the buffer
        if (buf[i] == '\n' || buf[i] == '\0') {
            printf("read a newline\n");
            int pid = fork();
            if (pid == -1) {
                printf("xargs: fork failed\n");
                exit(1);
            }

            if (pid == 0) { // if child
                printf("executing command: %s\n", command);
                printf("with argument: %s\n", args[0]);
                printf("with argument: %s\n", args[1]);

                args[argc] = 
                exec(command, args);
                // if fail
                printf("xargs: exec error\n");
                exit(1);
            }
            else {  // if parent
                wait(&pid);
                continue;
            }
        }
    }

    // while we read from stdin
        // if it is a newline, fork
            // if parent
                // wait
                // continue
            // if child, exec with the command and the arguments plus the read line
        // if it is EOF, break        
    
    exit(0);

}