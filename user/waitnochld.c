// waitnochld: verify that wait() returns -1 immediately when the
// calling process has no children to reap, in three positions: a
// freshly forked child that has never forked itself, a process
// whose only child has just been reaped (the drained path), and a
// second immediate wait in each to show the no-child report sticks.
//
// Fundamental truth: xv6's sys_wait scans the process table for a
// child of the caller; when it finds none, it returns -1 without
// sleeping. A process that has never forked has no children, so its
// first wait must report -1 immediately, and a second wait must
// report -1 again. After wait reaps a process's only child, the
// same scan finds nothing and reports -1 again. This is the
// complement of waitexit (which reaps live children) and killreap
// (which reaps a killed child): those exercised wait with children
// present; this exercises the scan finding nothing.
//
// Checks: fork returns a positive pid; the parent reaps the child
// and the child exits 0, meaning its own two wait calls each
// returned -1; the parent's wait after reaping its only child
// returns -1; a second parent wait returns -1. PASS prints only when
// every check holds with 0 mismatches.
//
// No kernel code was changed; the test exercises xv6's existing
// sys_wait path from user space. The child prints nothing so the
// console transcript is deterministic; the parent prints every
// measured value.
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
  int pid, wpid, status, drain1, drain2;
  uint64 sum;

  (void)argc;
  (void)argv;

  // The child has never forked, so it has no children at all. Its
  // first wait must report -1 immediately (fresh-process path), and
  // a second immediate wait must also report -1 (sticky no-child).
  // The exit status carries which assertion held: 0 only if both
  // did, 1 if the first wait was wrong, 2 if the second was.
  pid = fork();
  if (pid < 0) {
    printf("check 1: FAIL: fork returned %d\n", pid);
    exit(1);
  }
  if (pid == 0) {
    int w1, w2, st;
    w1 = wait(&st);
    w2 = wait(&st);
    if (w1 == -1 && w2 == -1)
      exit(0);
    exit(w1 == -1 ? 2 : 1);
  }

  checks++;
  if (pid > 0)
    printf("check 1: fork returned child pid %d\n", pid);
  else {
    printf("check 1: FAIL: fork returned %d, expected a positive pid\n",
           pid);
    mismatches++;
  }

  // The parent reaps the child normally; wait must return the
  // child's pid.
  wpid = wait(&status);

  checks++;
  if (wpid == pid)
    printf("check 2: wait returned pid %d, the child's pid\n", wpid);
  else {
    printf("check 2: FAIL: wait returned pid %d, expected %d\n",
           wpid, pid);
    mismatches++;
  }

  // Status 0 from the child means both of its wait calls returned
  // -1: the fresh-process no-child path and the sticky second wait.
  checks++;
  if (wpid == pid && status == 0)
    printf("check 3: child exited 0, so its wait() calls returned -1, -1\n");
  else {
    printf("check 3: FAIL: child exit status %d, expected 0 (child wait returns were not both -1)\n",
           status);
    mismatches++;
  }

  // The parent's only child is now reaped: the drained path must
  // also report -1.
  drain1 = wait(&status);

  checks++;
  if (drain1 == -1)
    printf("check 4: wait after reaping the only child returned -1\n");
  else {
    printf("check 4: FAIL: drained wait returned %d, expected -1\n",
           drain1);
    mismatches++;
  }

  // A second immediate wait must report -1 as well.
  drain2 = wait(&status);

  checks++;
  if (drain2 == -1)
    printf("check 5: second wait returned -1, still no children\n");
  else {
    printf("check 5: FAIL: second wait returned %d, expected -1\n",
           drain2);
    mismatches++;
  }

  // FNV-1a over every measured value: the no-child evidence
  // collapsed to one checkable value.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, (char *)&pid, sizeof(pid));
  sum = fnv1a64bytes(sum, (char *)&wpid, sizeof(wpid));
  sum = fnv1a64bytes(sum, (char *)&status, sizeof(status));
  sum = fnv1a64bytes(sum, (char *)&drain1, sizeof(drain1));
  sum = fnv1a64bytes(sum, (char *)&drain2, sizeof(drain2));
  printf("checksum: 0x%lx\n", sum);

  printf("wait-no-child results: child=%d reaped=%d childstatus=%d drained=%d drained2=%d\n",
         pid, wpid, status, drain1, drain2);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: wait() returned -1 with no children to reap, fresh process and drained alike\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
