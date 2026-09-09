// nprocs: exercise the nprocs() system call, which reports how many
// entries of the kernel's process table are currently in use.
//
// Verification plan: measure the count, fork 3 children that stay alive
// (sleeping), measure again (expect base + 3, since SLEEPING and ZOMBIE
// entries are in-use table slots), wait for them, measure a third time
// (expect base again, after the kernel frees the slots).
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int base, during, after;
  int i, pid;

  base = nprocs();
  printf("nprocs before fork = %d\n", base);

  for (i = 0; i < 3; i++) {
    pid = fork();
    if (pid < 0) {
      printf("fork failed\n");
      exit(1);
    }
    if (pid == 0) {
      pause(50); // stay in SLEEPING state while the parent counts
      exit(0);
    }
  }

  pause(5); // let the children get scheduled and block
  during = nprocs();
  printf("nprocs with 3 children alive = %d (expect %d)\n", during, base + 3);

  for (i = 0; i < 3; i++)
    wait(0);

  pause(2);
  after = nprocs();
  printf("nprocs after wait = %d (expect %d)\n", after, base);

  if (during == base + 3 && after == base)
    printf("PASS: nprocs tracks the process table exactly\n");
  else
    printf("FAIL: counts do not match the process table\n");

  exit(0);
}
