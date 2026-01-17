#include "kernel/types.h"
#include "user/user.h"

int 
getLine(char* buf, int start)
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
splitLine(char* line, char** args) {
    int argCount = 0;
    int startOfStr = 0;
    int newLength = strlen(line);
    for (int i = 0; i <= newLength; i++) {
        if (line[i] == ' ' || line[i] == '\0') {
            line[i] = '\0';
            args[argCount++] = line + startOfStr;
            startOfStr = i + 1; // skip the new null terminator
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
    char* command = argv[1];
    int commandLength = 1;
    argv += commandLength;

    int start = 0;
    int len = getLine(buf, start);
    int count = 0;
    while (len != -1) { // iterate over each stdin line
        
        int pid = fork();
        if (pid == 0) { // if child
            // get the substring
            char line[60];  // max line length
            memcpy(line, buf + start, len); // copy from the buffer + start to the line memory
            line[len]= '\0';

            char* args[100]; // max 100 arguments

            // copy the xargs arguments over
            int newArgc = argc - commandLength;
            for (int i = 0; i < newArgc; i++) {
                args[i] = argv[i];
            }

            // copy the line args over
            splitLine(line, args + newArgc);    // pass in the pointer commandLength ahead

            exec(command, args);
            printf("xargs: failed exec\n");
        } else {        // if parent
            wait((int *) 0);
            // continue
            start += len + 1; // move start to next character after newline
            len = getLine(buf, start);
            count++;
        }
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