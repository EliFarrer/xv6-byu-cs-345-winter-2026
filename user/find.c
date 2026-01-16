#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

char*
fmtname(char *path)
{
    static char buf[DIRSIZ+1];
    char *p;

    // Find first character after last slash.
    for(p=path+strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;
    return p;
    // Return blank-padded name.
    if(strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
    return buf;
}

void
findInDir(char* path, char* filename)
{
    // much of this is taken from `ls.c`
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    // open the path in readonly and get the file descriptor
    if((fd = open(path, O_RDONLY)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    // read the path into the stat structure
    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot convert to stat %s\n", path);
        close(fd);
        return;
    }

    switch(st.type){
    case T_DEVICE:
        // printf("Device (skip): %s\n", path);
        break;  // skip devices
    case T_FILE:    // if its a file, check if it matches filename
        // printf("File: %s|\n", path);
        // printf("Comparing %s to %s\n", fmtname(path), filename);
        if (strcmp(fmtname(path), filename) == 0) {
            // printf("FOUND FILE\n");
            printf("%s\n", path);
        }
        // printf("DIDNT find file\n");
        break;
    case T_DIR: // if its a directory, read through the directory and recurse into directories that are not . or ..
        // printf("DIR: %s\n", path);
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
            printf("ls: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf+strlen(buf);
        *p++ = '/'; // add a / and then increment
        while(read(fd, &de, sizeof(de)) == sizeof(de)){ // reading each entry in the directory
            if(de.inum == 0)    // it is empty
                continue;
            if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {   // skip . and ..
                // printf(".. or . directory\n");
                continue;
            }
            memmove(p, de.name, DIRSIZ);    // moves the DIRSIZ bytes to the p buffer, includes garbage if it is less than DIRSIZ
            p[DIRSIZ] = 0;  // adds a null terminator if the name length is equal to DIRSIZ
            if(stat(buf, &st) < 0){ // get the stat structure for the new path
                // printf("find: cannot stat %s\n", buf);
                continue;
            }
            // printf("Recursing into %s|\n", buf);
            findInDir(buf, filename); // recurse into the new path
        }
        break;
    }
    close(fd);
}

int
main(int argc, char *argv[])
{
    // check length of arguments to be 3
    if (argc != 3) {
        printf("Usage: find <path> <filename>\n");
        exit(1);
    }
    // call findInDir
    // printf("Finding %s in %s\n", argv[2], argv[1]);
    findInDir(argv[1], argv[2]);
    exit(0);
}