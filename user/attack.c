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

  int amount = 32;
  for (int i = 0; i < 1000; i++) {
    int* start = (int*) 0x0;
    char tmp[amount];
    memmove(tmp, start + (amount * i), amount);
    printf("Old start: %p, new start: %p: %d\n", start, (start + amount * i), *start);
  }
  // // write(2, secret, 8);

  exit(1);
}
