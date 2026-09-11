// waitexit: verify that the exit code a child hands to exit() is
// exactly the status the parent's wait() reports, and that wait()
// returns the child's pid.
//
// Fundamental truth: in xv6, exit(status) stores the status in the
// exiting process's control block, and wait(&status) copies that
// stored value into the caller's address space and returns the
// reaped child's pid. So a fixed, known code must round-trip
// unchanged: fork, exit(42) in the child, wait(&status) in the
// parent must hand back the child's pid and the value 42. A second
// child exiting with 0 shows the round-trip is exact in both
// directions, not just for one lucky nonzero value.
//
// Checks: fork returns a positive pid for each child; each wait()
// returns exactly the pid it reaped; the stored status equals the
// value the corresponding child passed to exit(). PASS prints only
// when every check holds with 0 mismatches.
//
// No kernel code was changed; the test exercises xv6's existing
// wait/exit path from user space. The child prints nothing so the
// console transcript is deterministic; the parent prints every
// measured value.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define CHILD1_STATUS 42
#define CHILD2_STATUS 0

// FNV-1a 64-bit over raw bytes: the standard offset basis and prime,
// one fold per byte.
static uint64
fnv1a64bytes(uint64 h, char *b, int n)
{
  for (int i = 0; i < n; i++) {
    h ^= (uint64)(b[i] & 0xff);
    h *= 1099511628211UL;
  }
  return h;
}

int
main(int argc, char *argv[])
{
  int checks = 0, mismatches = 0;
  int pid1, pid2, wpid1, wpid2, status1, status2;
  uint64 sum;

  (void)argc;
  (void)argv;

  // First child: exits with the fixed nonzero status 42.
  pid1 = fork();
  if (pid1 < 0) {
    printf("check 1: FAIL: fork returned %d\n", pid1);
    exit(1);
  }
  if (pid1 == 0)
    exit(CHILD1_STATUS);

  checks++;
  if (pid1 > 0)
    printf("check 1: fork returned child pid %d\n", pid1);
  else {
    printf("check 1: FAIL: fork returned %d, expected a positive pid\n",
           pid1);
    mismatches++;
  }

  // wait() must return the reaped child's pid and store 42.
  wpid1 = wait(&status1);

  checks++;
  if (wpid1 == pid1)
    printf("check 2: wait returned pid %d, the first child's pid\n",
           wpid1);
  else {
    printf("check 2: FAIL: wait returned pid %d, expected %d\n",
           wpid1, pid1);
    mismatches++;
  }

  checks++;
  if (wpid1 == pid1 && status1 == CHILD1_STATUS)
    printf("check 3: wait stored status %d for child %d\n",
           status1, wpid1);
  else {
    printf("check 3: FAIL: wait stored status %d, expected %d\n",
           status1, CHILD1_STATUS);
    mismatches++;
  }

  // Second child: exits with 0, so the round-trip is shown exact at
  // the low end too. It is forked only after the first child is
  // fully reaped, so wait's ordering is deterministic.
  pid2 = fork();
  if (pid2 < 0) {
    printf("check 4: FAIL: fork returned %d\n", pid2);
    exit(1);
  }
  if (pid2 == 0)
    exit(CHILD2_STATUS);

  checks++;
  if (pid2 > 0)
    printf("check 4: fork returned child pid %d\n", pid2);
  else {
    printf("check 4: FAIL: fork returned %d, expected a positive pid\n",
           pid2);
    mismatches++;
  }

  wpid2 = wait(&status2);

  checks++;
  if (wpid2 == pid2)
    printf("check 5: wait returned pid %d, the second child's pid\n",
           wpid2);
  else {
    printf("check 5: FAIL: wait returned pid %d, expected %d\n",
           wpid2, pid2);
    mismatches++;
  }

  checks++;
  if (wpid2 == pid2 && status2 == CHILD2_STATUS)
    printf("check 6: wait stored status %d for child %d\n",
           status2, wpid2);
  else {
    printf("check 6: FAIL: wait stored status %d, expected %d\n",
           status2, CHILD2_STATUS);
    mismatches++;
  }

  // FNV-1a over every measured value: the round-trip evidence
  // collapsed to one checkable value.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, (char *)&pid1, sizeof(pid1));
  sum = fnv1a64bytes(sum, (char *)&wpid1, sizeof(wpid1));
  sum = fnv1a64bytes(sum, (char *)&status1, sizeof(status1));
  sum = fnv1a64bytes(sum, (char *)&pid2, sizeof(pid2));
  sum = fnv1a64bytes(sum, (char *)&wpid2, sizeof(wpid2));
  sum = fnv1a64bytes(sum, (char *)&status2, sizeof(status2));
  printf("checksum: 0x%lx\n", sum);

  printf("wait results: pid1=%d status=%d, pid2=%d status=%d\n",
         wpid1, status1, wpid2, status2);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: wait() returned each child's pid with its exact exit status\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
