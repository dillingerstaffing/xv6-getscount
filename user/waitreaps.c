// waitreaps: verify that wait() reaps a zombie child exactly once, so
// a second wait() with no children left returns -1.
//
// Fundamental truth: in xv6, a child that calls exit() becomes a
// zombie holding its exit status; the parent's wait() collects that
// status and frees the zombie, and it does so exactly once. Once the
// zombie is freed there is no child left, so the very next wait()
// must report -1. The observable sequence for one child is therefore:
// first wait returns the child's pid, second wait returns -1. Any
// other sequence (double reap of the same zombie, or a lost -1)
// would contradict the exactly-once reaping.
//
// Checks: fork returns a positive pid; the parent's first wait()
// returns exactly the child's pid; the reaped status is the child's
// exit code 0; the parent's second wait() returns -1. PASS prints
// only when every check holds with 0 mismatches.
//
// No kernel code was changed; the test exercises xv6's existing
// wait/exit path from user space. The child exits immediately and
// prints nothing so the console transcript is deterministic; the
// parent prints every measured value.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

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
  int pid, wpid1, wpid2, status;
  uint64 sum;

  (void)argc;
  (void)argv;

  // The child exits immediately: by the time the parent calls wait,
  // the child is a zombie, so the first wait must reap it.
  pid = fork();
  if (pid < 0) {
    printf("check 1: FAIL: fork returned %d\n", pid);
    exit(1);
  }
  if (pid == 0)
    exit(0);

  checks++;
  if (pid > 0)
    printf("check 1: fork returned child pid %d\n", pid);
  else {
    printf("check 1: FAIL: fork returned %d, expected a positive pid\n",
           pid);
    mismatches++;
  }

  // First wait: must reap the zombie and hand back its pid.
  wpid1 = wait(&status);

  checks++;
  if (wpid1 == pid)
    printf("check 2: first wait returned pid %d, the reaped child\n",
           wpid1);
  else {
    printf("check 2: FAIL: first wait returned pid %d, expected %d\n",
           wpid1, pid);
    mismatches++;
  }

  // The reaped status must be the child's own exit code.
  checks++;
  if (wpid1 == pid && status == 0)
    printf("check 3: reaped status %d, the child's exit code\n",
           status);
  else {
    printf("check 3: FAIL: reaped status %d, expected 0\n", status);
    mismatches++;
  }

  // Second wait: the zombie was reaped exactly once, so nothing is
  // left and wait must report -1.
  wpid2 = wait(&status);

  checks++;
  if (wpid2 == -1)
    printf("check 4: second wait returned -1, the zombie was reaped exactly once\n");
  else {
    printf("check 4: FAIL: second wait returned %d, expected -1\n",
           wpid2);
    mismatches++;
  }

  // FNV-1a over every measured value: the reap-once evidence
  // collapsed to one checkable value.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, (char *)&pid, sizeof(pid));
  sum = fnv1a64bytes(sum, (char *)&wpid1, sizeof(wpid1));
  sum = fnv1a64bytes(sum, (char *)&status, sizeof(status));
  sum = fnv1a64bytes(sum, (char *)&wpid2, sizeof(wpid2));
  printf("checksum: 0x%lx\n", sum);

  printf("wait-reap results: child=%d reaped=%d status=%d secondwait=%d\n",
         pid, wpid1, status, wpid2);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: wait() reaped the zombie child exactly once, then returned -1\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
