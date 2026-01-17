#include "kernel/types.h"
#include "user/user.h"

int 
getToNewline(char* buf, int start)
{
    // returns how many bytes to read (includes a spot for a null terminator)
    if (start >= strlen(buf)) {
        return -1; // no more to read
    }
    // returns the number of characters read, -1 if no more to read
    for (int i = start; i < strlen(buf); i++) {
        if (buf[i] == '\n') {
            return i - start;
        }
    }
    return strlen(buf) - start;   // if we hit '\0'
}

int
splitSpace(char* line, char** args) {
    printf("In split space\n");
    printf("\tString: |%s|\n", line);
    int argCount = 0;
    int startOfStr = 0;
    printf("Entering for loop\n");
    for (int i = 0; i <= strlen(line); i++) {
        printf("\ti=%d\n", i);
        char out[2];
        out[0] = line[i];
        out[1] = '\0';
        printf("\tstr[i]=%s\n", out);
        if (line[i] == ' ' || line[i] == '\0') {
            printf("\tin if statement\n");
            char arg[60];   // max arg length
            memmove(arg, line + startOfStr, i - startOfStr); // move from the start string pointer to the args
            printf("\tmade past mem move\n");
            arg[i] = '\0';  // set null terminator
            printf("\targ: %s\n", arg);
            args[argCount] = arg;
            printf("\targs: %s\n", args[argCount]);
            argCount++;
            startOfStr = i + 1; // reset start string pointer
        }
    }
    return argCount;
}

int
main(int argc, char *argv[])
{
    char buf[512];
    if (read(0, buf, sizeof(buf)) < 0) {    // read from stdin
        fprintf(2, "xargs: error reading stdin\n");
        exit(1);
    }

    int start = 0;
    printf("calling get to new line\n");
    int len = getToNewline(buf, start);
    int count = 0;
    while (len != -1) { // iterate over each stdin line
        // get the substring
        char line[60];  // max line length
        memcpy(line, buf + start, len); // copy from the buffer + start to the line memory
        line[len]= '\0';
        printf("length: %d\n", len);
        printf("got line |%s|\n", line);
        char* args[100]; // max 100 arguments
        printf("calling split space\n");
        int writtenArgs = splitSpace(line, args);

        for (int i = 0; i < writtenArgs; i++) {
            printf("Got line %d parameter %d: %s\n", count, i, args[i]);
        }


        
        start += len + 1; // move start to next character after newline
        len = getToNewline(buf, start);
        count++;
    }
    // while we read from stdin
        // if it is a newline, fork
            // if parent
                // wait
                // continue
            // if child
                // get command
                // get xargs arguments
                // prepend to stdin arguments
                // exec
        // if it is EOF, break        
    
}