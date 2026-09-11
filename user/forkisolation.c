// forkisolation: verify that fork gives the child a private copy of the
// parent's address space (the kernel's copyuvm path).
//
// Verification plan: the parent grows its heap by one page with sbrk and
// writes a canary word. It forks. The child must observe the same canary
// (the copy happened), then writes its own distinct word into the page.
// The parent waits for the child and must observe its own canary still
// intact (the child's write did not leak back into the parent's page).
// All three observed values are printed, so the isolation claim is
// checkable against the numbers on this page.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Page size for the riscv64 target (kernel/riscv.h PGSIZE); kept local so
// this user program does not depend on kernel headers.
#define PAGESZ 4096

#define PARENT_CANARY 0xcafef00d
#define CHILD_VALUE   0xdeadbeef

int
main(int argc, char *argv[])
{
  uint *page;
  int pid;
  uint child_seen, parent_after;
  int ok = 1;

  page = (uint *)sbrk(PAGESZ);
  if ((char *)page == (char *)-1) {
    printf("sbrk failed\n");
    exit(1);
  }
  page[0] = PARENT_CANARY;

  pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    // Child: the page must be a copy of the parent's page as of fork.
    child_seen = page[0];
    printf("child read: 0x%x (expect 0x%x)\n", child_seen, PARENT_CANARY);
    if (child_seen != PARENT_CANARY) {
      printf("child FAIL: canary mismatch\n");
      exit(1);
    }
    // Write the child's own value; the parent must never see this.
    page[0] = CHILD_VALUE;
    printf("child wrote: 0x%x\n", page[0]);
    exit(0);
  }

  // Parent: wait for the child, then check the parent's page is untouched.
  wait(0);
  parent_after = page[0];
  printf("parent read after child exit: 0x%x (expect 0x%x)\n",
         parent_after, PARENT_CANARY);
  if (parent_after != PARENT_CANARY) {
    printf("parent FAIL: page was not private\n");
    ok = 0;
  }
  if (ok)
    printf("PASS: fork copied the page, child write did not reach the parent\n");
  else
    printf("FAIL: memory isolation broken\n");
  exit(0);
}
