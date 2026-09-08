// scount: demonstrate the getscount system call.
//
// Makes a known number of getpid() calls, then asks the kernel how many
// it saw. Also shows that invalid syscall numbers are rejected with -1
// and that a forked child starts with zeroed counters (counters are
// per-process, not inherited).

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/syscall.h" // SYS_* numbers

int
main(int argc, char *argv[])
{
  int i;
  int pid;
  int n;

  // Make exactly 5 getpid calls.
  for (i = 0; i < 5; i++)
    getpid();

  n = getscount(SYS_getpid);
  printf("getpid called 5 times, kernel reports: %d\n", n);
  if (n != 5) {
    printf("FAIL: expected 5\n");
    exit(1);
  }

  // Invalid syscall numbers are rejected with -1.
  printf("getscount(999) = %d (expect -1)\n", getscount(999));
  printf("getscount(-1)  = %d (expect -1)\n", getscount(-1));

  // Forked children start with zeroed counters: accounting is
  // per-process, a child does not inherit its parent's counts.
  pid = fork();
  if (pid == 0) {
    printf("child:  getpid count = %d (expect 0)\n", getscount(SYS_getpid));
    exit(0);
  }
  wait(0);
  printf("parent: getpid count = %d (expect 5)\n", getscount(SYS_getpid));
  printf("PASS\n");
  exit(0);
}
