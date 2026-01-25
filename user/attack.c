#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  // your code here.  you should write the secret to fd 2 using write
  // (e.g., write(2, secret, 8)
  // attack stack lives at 84215045

  // char arr[2];
  // arr[0] = '9';
  // arr[1] = '\0';
  char *start = sbrk(PGSIZE*32);  // get the start of the memory
  char* secret = start + (PGSIZE * 16) + 32;
  // printf("%s", start + (PGSIZE * 16) + 32);
  // start += (16 * PGSIZE);
  // for (int j = 0; j < 1; j++) {
  //   printf("-----------------PAGE %d-----------------\n", j);
  //   for (int i = 32; i < 45; i++) {
  //     arr[0] = start[i];
  //     printf("%s", arr);
  //   }
  //   printf("\n");
  //   start += PGSIZE;
  // }

  write(2, secret, 8);

  exit(1);
}
