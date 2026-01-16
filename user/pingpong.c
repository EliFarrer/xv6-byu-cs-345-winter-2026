#include "kernel/types.h"
#include "user/user.h"

int
main()
{
    int fdsParent[2];
    int fdsChild[2];
    char buf[1];

    pipe(fdsParent);
    pipe(fdsChild);

    int pid = fork();
    
    if (pid != 0) { // parent
        int nWritten = write(fdsParent[1], "\n", 1);
        int nRead = read(fdsChild[0], buf, 1);
        printf("%d: received pong\n", getpid());

        if (nRead != nWritten) {
            printf("Parent Read-Write error\n");
            exit(1);
        }
        exit(0);
    } else {
        int nRead = read(fdsParent[0], buf, sizeof(buf));
        printf("%d: received ping\n", getpid());
        int nWritten = write(fdsChild[1], buf, sizeof(buf));
        
        if (nRead != nWritten) {
            printf("Child Read-Write error\n");
            exit(1);
        }
        exit(0);
    }
}