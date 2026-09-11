// killreap: verify that kill() marks a child for death and wait()
// reaps exactly that child: killing a spinning child must make
// wait return the child's pid, and the stored status must be -1
// (killed), not a normal exit status. A second wait() must return
// -1, because no children are left.
//
// Fundamental truth: in xv6, kill(pid) sets the process's killed
// flag, and the next trap return path calls exit(-1) for a killed
// process, moving it to ZOMBIE; wait() then copies the stored
// status, returns the reaped pid, and frees the slot. So a child
// that spins forever and never exits on its own can still be
// reaped, but only through the kill path: kill must return 0, and
// wait must hand back exactly that child's pid with status -1.
// A second child is never created here (unlike waitexit), because
// the point is that reaping a killed process leaves nothing behind.
//
// Checks: fork returns a positive pid; kill returns 0; wait
// returns the killed child's pid; the stored status is -1; a
// second wait returns -1. PASS prints only when every check holds
// with 0 mismatches.
//
// No kernel code was changed; the test exercises xv6's existing
// kill/wait path (the killed flag in kill, the exit(-1) in
// usertrap, the reaping in sys_wait) from user space. The child
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
  int pid, kret, wpid, status, wpid2;
  volatile int spin;
  uint64 sum;

  (void)argc;
  (void)argv;

  // The child spins forever; it never exits on its own, so the only
  // way the parent can ever reap it is through kill.
  pid = fork();
  if (pid < 0) {
    printf("check 1: FAIL: fork returned %d\n", pid);
    exit(1);
  }
  if (pid == 0) {
    for (spin = 0;; spin++)
      ;
    exit(1); // unreachable
  }

  checks++;
  if (pid > 0)
    printf("check 1: fork returned child pid %d\n", pid);
  else {
    printf("check 1: FAIL: fork returned %d, expected a positive pid\n",
           pid);
    mismatches++;
  }

  // Burn user time so the child gets scheduled and is running when
  // kill lands; this xv6 variant exposes no user-space sleep call.
  // Correctness does not depend on the timing: the child spins
  // forever either way, and wait blocks until it is reaped.
  for (spin = 0; spin < 3000000; spin++)
    ;

  // kill must find the spinning child and mark it for death.
  kret = kill(pid);

  checks++;
  if (kret == 0)
    printf("check 2: kill returned 0 for child pid %d\n", pid);
  else {
    printf("check 2: FAIL: kill returned %d, expected 0\n", kret);
    mismatches++;
  }

  // wait must reap exactly the killed child. A killed process exits
  // with status -1, so the stored status distinguishes a reaped
  // kill from a normal exit.
  wpid = wait(&status);

  checks++;
  if (wpid == pid)
    printf("check 3: wait returned pid %d, the killed child's pid\n",
           wpid);
  else {
    printf("check 3: FAIL: wait returned pid %d, expected %d\n",
           wpid, pid);
    mismatches++;
  }

  checks++;
  if (wpid == pid && status == -1)
    printf("check 4: wait stored status %d for child %d\n",
           status, wpid);
  else {
    printf("check 4: FAIL: wait stored status %d, expected -1\n",
           status);
    mismatches++;
  }

  // No children are left; a second wait must report -1.
  wpid2 = wait(&status);

  checks++;
  if (wpid2 == -1)
    printf("check 5: second wait returned -1, no children left\n");
  else {
    printf("check 5: FAIL: second wait returned %d, expected -1\n",
           wpid2);
    mismatches++;
  }

  // FNV-1a over every measured value: the kill-reap evidence
  // collapsed to one checkable value.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, (char *)&pid, sizeof(pid));
  sum = fnv1a64bytes(sum, (char *)&kret, sizeof(kret));
  sum = fnv1a64bytes(sum, (char *)&wpid, sizeof(wpid));
  sum = fnv1a64bytes(sum, (char *)&status, sizeof(status));
  sum = fnv1a64bytes(sum, (char *)&wpid2, sizeof(wpid2));
  printf("checksum: 0x%lx\n", sum);

  printf("kill results: child=%d kill=%d wait=%d status=%d wait2=%d\n",
         pid, kret, wpid, status, wpid2);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: kill() marked the child and wait() reaped its pid with status -1\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
