// waitorder: verify that wait() reaps several already-zombied children
// in process-table (pid) order, not in the order they exited.
//
// Fundamental truth: xv6's wait() scans the process table from its
// start and returns the first ZOMBIE child it finds (kernel/proc.c,
// wait). When several children have already exited, the parent's
// three wait() calls therefore return the children in ascending pid
// order, which is the fork order here, no matter which child exited
// first. The test forces the exit order to be the exact reverse of
// the fork order (child 1 spins until tick base+30, child 2 until
// base+20, child 3 until base+10), so exit order and reap order are
// distinguishable: the child exit prints in the console must read
// p3, p2, p1 while the three wait() returns must read p1, p2, p3.
//
// Checks: the three forks return positive pids; the parent's first
// wait() returns p1 with status 11; the second wait() returns p2 with
// status 22; the third wait() returns p3 with status 33. The parent
// sleeps 40 ticks before waiting so all three children are zombies,
// which is what makes the reap order depend on the scan order rather
// than on exit timing. PASS prints only when every check holds with
// 0 mismatches.
//
// No kernel code was changed; the test exercises xv6's existing
// exit/wait path from user space. The child exit prints are the
// exit-order evidence; the parent prints every measured value, and
// the console transcript is byte-identical across runs.
//
// Related: user/waitreaps.c (one child reaped exactly once, then -1);
// this module covers the ordering claim for several children.
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
  int p1, p2, p3;
  int w1, w2, w3, s1, s2, s3;
  uint64 sum, base;

  (void)argc;
  (void)argv;

  // No user-space sleep call exists in this xv6 variant, so ordering
  // is driven by the uptime tick counter: the base is read before any
  // fork and inherited by the children, and each child spins until an
  // absolute deadline. Absolute deadlines keep the exit order fixed
  // (base+10, base+20, base+30) no matter when each child was forked.
  base = (uint64)uptime();

  // Fork three children in order, recording the fork-return pids.
  p1 = fork();
  if (p1 < 0) {
    printf("fork 1 failed: %d\n", p1);
    exit(1);
  }
  if (p1 == 0) {
    // First child exits last: longest deadline, so it exits after p3, p2.
    while ((uint64)uptime() < base + 30)
      ;
    printf("exit %d\n", getpid());
    exit(11);
  }
  p2 = fork();
  if (p2 < 0) {
    printf("fork 2 failed: %d\n", p2);
    exit(1);
  }
  if (p2 == 0) {
    while ((uint64)uptime() < base + 20)
      ;
    printf("exit %d\n", getpid());
    exit(22);
  }
  p3 = fork();
  if (p3 < 0) {
    printf("fork 3 failed: %d\n", p3);
    exit(1);
  }
  if (p3 == 0) {
    while ((uint64)uptime() < base + 10)
      ;
    printf("exit %d\n", getpid());
    exit(33);
  }

  printf("fork order: p1=%d p2=%d p3=%d\n", p1, p2, p3);

  checks++;
  if (p1 > 0 && p2 > 0 && p3 > 0)
    printf("check 1: three forks returned positive pids %d %d %d\n",
           p1, p2, p3);
  else {
    printf("check 1: FAIL: fork returned non-positive pid(s) %d %d %d\n",
           p1, p2, p3);
    mismatches++;
  }

  // Wait until every child has exited: spin on uptime until past the
  // last child's deadline, so all three children are zombies when the
  // first wait() runs and the reap order is the scan order.
  while ((uint64)uptime() < base + 40)
    ;

  w1 = wait(&s1);
  checks++;
  if (w1 == p1)
    printf("check 2: first wait() returned %d, the first forked child\n", w1);
  else {
    printf("check 2: FAIL: first wait() returned %d, expected %d\n", w1, p1);
    mismatches++;
  }

  checks++;
  if (w1 == p1 && s1 == 11)
    printf("check 3: first reaped status %d, the first child's exit code\n", s1);
  else {
    printf("check 3: FAIL: first reaped status %d, expected 11\n", s1);
    mismatches++;
  }

  w2 = wait(&s2);
  checks++;
  if (w2 == p2)
    printf("check 4: second wait() returned %d, the second forked child\n", w2);
  else {
    printf("check 4: FAIL: second wait() returned %d, expected %d\n", w2, p2);
    mismatches++;
  }

  checks++;
  if (w2 == p2 && s2 == 22)
    printf("check 5: second reaped status %d, the second child's exit code\n", s2);
  else {
    printf("check 5: FAIL: second reaped status %d, expected 22\n", s2);
    mismatches++;
  }

  w3 = wait(&s3);
  checks++;
  if (w3 == p3)
    printf("check 6: third wait() returned %d, the third forked child\n", w3);
  else {
    printf("check 6: FAIL: third wait() returned %d, expected %d\n", w3, p3);
    mismatches++;
  }

  checks++;
  if (w3 == p3 && s3 == 33)
    printf("check 7: third reaped status %d, the third child's exit code\n", s3);
  else {
    printf("check 7: FAIL: third reaped status %d, expected 33\n", s3);
    mismatches++;
  }

  // FNV-1a over every measured value: fork pids, reap pids, and
  // reaped statuses, in fixed order so nothing depends on the
  // scheduler.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, (char *)&p1, sizeof(p1));
  sum = fnv1a64bytes(sum, (char *)&p2, sizeof(p2));
  sum = fnv1a64bytes(sum, (char *)&p3, sizeof(p3));
  sum = fnv1a64bytes(sum, (char *)&w1, sizeof(w1));
  sum = fnv1a64bytes(sum, (char *)&w2, sizeof(w2));
  sum = fnv1a64bytes(sum, (char *)&w3, sizeof(w3));
  sum = fnv1a64bytes(sum, (char *)&s1, sizeof(s1));
  sum = fnv1a64bytes(sum, (char *)&s2, sizeof(s2));
  sum = fnv1a64bytes(sum, (char *)&s3, sizeof(s3));
  printf("checksum: 0x%lx\n", sum);

  printf("wait-order results: fork=%d,%d,%d reap=%d,%d,%d status=%d,%d,%d\n",
         p1, p2, p3, w1, w2, w3, s1, s2, s3);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: wait() reaped three zombies in pid order, not exit order\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
