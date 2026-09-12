// forkpidorder: verify that successive forks hand the parent
// strictly increasing child pids.
//
// Fundamental truth: in xv6, allocproc() draws each new pid from a
// single global counter, nextpid, and increments it immediately
// (kernel/proc.c, allocpid: pid = nextpid; nextpid = nextpid + 1,
// under pid_lock). The parent therefore observes the counter's
// advance directly: three forks in strict program order must return
// p1 < p2 < p3, and a fourth fork after all three children have
// been reaped must return p4 > p3, since the counter is never
// rewound when a zombie is freed. A reused or batch-ordered pid
// scheme would contradict the counter's behavior; the observed
// sequence is the counter, read through fork().
//
// Checks: three forks return positive pids; the three are strictly
// increasing in fork order; the three are pairwise distinct; the
// three wait()s reap exactly the three forked pids; a fourth fork
// after the reaps still gets a larger pid. PASS prints only when
// every check holds with 0 mismatches.
//
// No kernel code was changed; the test exercises xv6's existing
// fork/wait path from user space. Each child exits immediately and
// prints nothing, so the serial console transcript is
// deterministic; the parent prints every measured value.
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
  int p1, p2, p3, p4;
  int w1, w2, w3, status;
  uint64 sum;

  (void)argc;
  (void)argv;

  // Fork three children in strict program order; the parent records
  // each returned pid the moment fork() returns, so no scheduling
  // can reorder the observations. Each child exits immediately and
  // prints nothing.
  p1 = fork();
  if (p1 < 0) {
    printf("check 1: FAIL: first fork returned %d\n", p1);
    exit(1);
  }
  if (p1 == 0)
    exit(0);

  p2 = fork();
  if (p2 < 0) {
    printf("check 1: FAIL: second fork returned %d\n", p2);
    exit(1);
  }
  if (p2 == 0)
    exit(0);

  p3 = fork();
  if (p3 < 0) {
    printf("check 1: FAIL: third fork returned %d\n", p3);
    exit(1);
  }
  if (p3 == 0)
    exit(0);

  checks++;
  if (p1 > 0 && p2 > 0 && p3 > 0)
    printf("check 1: three forks returned positive pids %d %d %d\n",
           p1, p2, p3);
  else {
    printf("check 1: FAIL: fork pids %d %d %d, expected all positive\n",
           p1, p2, p3);
    mismatches++;
  }

  // The counter advances by exactly one per allocproc, so the pids
  // must be strictly increasing in fork order.
  checks++;
  if (p1 < p2)
    printf("check 2: p1=%d < p2=%d, second fork got the larger pid\n",
           p1, p2);
  else {
    printf("check 2: FAIL: p1=%d not less than p2=%d\n", p1, p2);
    mismatches++;
  }

  checks++;
  if (p2 < p3)
    printf("check 3: p2=%d < p3=%d, third fork got the larger pid\n",
           p2, p3);
  else {
    printf("check 3: FAIL: p2=%d not less than p3=%d\n", p2, p3);
    mismatches++;
  }

  // Strict increase already implies distinctness, but assert the
  // full pairwise relation outright.
  checks++;
  if (p1 != p2 && p1 != p3 && p2 != p3)
    printf("check 4: pids %d %d %d are pairwise distinct\n",
           p1, p2, p3);
  else {
    printf("check 4: FAIL: duplicate pid among %d %d %d\n",
           p1, p2, p3);
    mismatches++;
  }

  // Reap the three children; wait() must return exactly the pids
  // the three forks handed back, in pid order.
  w1 = wait(&status);
  w2 = wait(&status);
  w3 = wait(&status);

  checks++;
  if (w1 == p1 && w2 == p2 && w3 == p3)
    printf("check 5: three wait()s reaped exactly the forked pids %d %d %d\n",
           w1, w2, w3);
  else {
    printf("check 5: FAIL: wait()s returned %d %d %d, expected %d %d %d\n",
           w1, w2, w3, p1, p2, p3);
    mismatches++;
  }

  // Control: fork a fourth child after all three zombies were
  // reaped. Freeing a zombie does not rewind nextpid, so the new
  // child must still get a larger pid than the whole batch.
  p4 = fork();
  if (p4 < 0) {
    printf("check 6: FAIL: fourth fork returned %d\n", p4);
    exit(1);
  }
  if (p4 == 0)
    exit(0);

  checks++;
  if (p4 > 0 && p4 > p3)
    printf("check 6: fourth fork after the reaps returned pid %d > p3=%d, the allocator kept advancing\n",
           p4, p3);
  else {
    printf("check 6: FAIL: fourth fork returned %d, expected larger than p3=%d\n",
           p4, p3);
    mismatches++;
  }
  wait(&status);

  // FNV-1a over every measured value: the four pids and the three
  // reaped wait results.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, (char *)&p1, sizeof(p1));
  sum = fnv1a64bytes(sum, (char *)&p2, sizeof(p2));
  sum = fnv1a64bytes(sum, (char *)&p3, sizeof(p3));
  sum = fnv1a64bytes(sum, (char *)&p4, sizeof(p4));
  sum = fnv1a64bytes(sum, (char *)&w1, sizeof(w1));
  sum = fnv1a64bytes(sum, (char *)&w2, sizeof(w2));
  sum = fnv1a64bytes(sum, (char *)&w3, sizeof(w3));
  printf("checksum: 0x%lx\n", sum);

  printf("fork-pid results: p1=%d p2=%d p3=%d p4=%d reaped=%d,%d,%d\n",
         p1, p2, p3, p4, w1, w2, w3);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: successive forks returned strictly increasing pids %d < %d < %d, and a post-reap fork advanced past them to %d\n",
           p1, p2, p3, p4);
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
