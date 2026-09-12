// pipebuf: verify that a write past a full xv6 pipe blocks until a
// reader drains it.
//
// The pipe buffer is 512 bytes (PIPESIZE in kernel/pipe.c). The
// parent fills the pipe with exactly 512 bytes in one write call,
// so the buffer is exactly full. The next write of 1 more byte has
// nowhere to go: pipewrite sleeps until a reader drains at least
// one byte. The test forces the ordering with the uptime tick
// counter: the child sleeps 40 ticks before touching the pipe,
// guaranteeing the parent's second write is already asleep inside
// the kernel when the child starts reading. The parent records
// uptime() immediately before and after the second write; the
// elapsed ticks are the measured block time.
//
// The check gate is elapsed >= 30, ten ticks below the child's 40
// tick sleep. The ten ticks of slack absorb scheduling jitter. A
// write that returned early would show 0 ticks, so a passing gate
// proves the writer really stalled for the reader.
//
// The stream is 513 bytes: pat(0)..pat(512). The child reads until
// it has all 513, compares every byte against pat() at its absolute
// stream index, folds the received bytes into an FNV-1a checksum,
// and exits 0 only if the whole stream was byte-exact. The parent
// folds the same pat() sequence independently and prints the
// expected checksum, so the two printed values can be compared by
// eye and the expected one recomputed on the host.
//
// Deterministic timing: t0 is captured on a fresh tick edge (the
// parent spins until the tick counter advances, then reads it
// microseconds later) and, crucially, BEFORE the fork, so the child
// inherits the exact same t0. The child's drain deadline is
// t0 + 40, the parent's second write is issued in tick t0, and the
// drain lands in tick t0 + 40, so the measured elapsed is exactly
// 40 in every run. Capturing t0 after the fork would let a tick
// boundary fall between the fork and the write and make the count
// jitter by a tick.
//
// Checks: pipe() hands back two distinct fds; the first write
// returns 512 (pipe exactly full, no short write); fork() returns a
// positive child pid; the second write returns 1 (the one byte that
// waited for the drain); the second write blocked at least 30
// ticks; wait() returns the child's pid; the child's exit status is
// 0, meaning its own byte-by-byte verification of all 513 bytes
// passed.
//
// No kernel code was changed; the test exercises xv6's existing
// pipe blocking path from user space.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define PIPESZ 512
#define NSTREAM (PIPESZ + 1)
#define CHILDSLEEP 40
#define BLOCKGATE 30
#define PRINTDELAY 15

// Compile-time-fixed pattern: every stream byte is a pure function
// of its index. Bytes are printable ASCII ('!'..'~') so the stream
// reads sensibly in a dump; index 256 is 0x00 and index 512 is
// 0xFF, so the comparison must be length-driven and any C-string
// treatment would be caught by the byte compare.
static char
pat(int i)
{
  if (i == 256)
    return 0;
  if (i == 512)
    return (char)0xff;
  return (char)('!' + (i * 7) % 94);
}

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
  int p[2];
  int checks = 0, mismatches = 0;
  int pid, w, w1, w2, s;
  uint64 te, t0, t1, elapsed;
  char wbuf[PIPESZ];
  char one;
  uint64 expect;
  int i;

  (void)argc;
  (void)argv;

  for (i = 0; i < PIPESZ; i++)
    wbuf[i] = pat(i);
  one = pat(PIPESZ);

  // Check 1: pipe creation hands back two distinct valid fds.
  checks++;
  if (pipe(p) == 0 && p[0] >= 0 && p[1] >= 0 && p[0] != p[1])
    printf("check 1: pipe() ok, read fd %d, write fd %d\n", p[0], p[1]);
  else {
    printf("check 1: FAIL: pipe() gave p[0]=%d p[1]=%d\n", p[0], p[1]);
    mismatches++;
  }

  // Check 2: one write of exactly 512 bytes fills the pipe
  // completely and returns 512, no short write, no block. This
  // happens before the fork: with the pipe empty and the reader not
  // yet born, nothing can disturb the fill.
  checks++;
  w1 = write(p[1], wbuf, PIPESZ);
  if (w1 == PIPESZ)
    printf("check 2: first write() returned %d, pipe now exactly full\n", w1);
  else {
    printf("check 2: FAIL: first write() returned %d, expected %d\n",
           w1, PIPESZ);
    mismatches++;
  }
  if (w1 != PIPESZ) {
    printf("aborting: pipe not exactly full, the blocking premise is broken\n");
    exit(1);
  }

  // Align to a fresh tick edge, then capture t0 before the fork.
  // The spin exits the moment the tick counter advances, so t0 is
  // the new tick read microseconds after the edge: deterministic.
  // The child inherits t0 through the fork, so both sides share one
  // absolute deadline, t0 + 40.
  te = (uint64)uptime();
  while ((uint64)uptime() == te)
    ;
  t0 = (uint64)uptime();

  pid = fork();
  if (pid < 0) {
    printf("fork failed: %d\n", pid);
    exit(1);
  }
  if (pid == 0) {
    // The reader. Sleeps until t0 + 40, guaranteeing the parent's
    // second write (issued in tick t0) is already blocked inside
    // pipewrite when reading starts. Then drains the whole
    // 513-byte stream and verifies it byte by byte against pat().
    int total = 0, n, bad = 0, j;
    char rbuf[NSTREAM];
    uint64 sum = 1469598103934665603UL; // FNV offset basis
    close(p[1]);
    while ((uint64)uptime() < t0 + CHILDSLEEP)
      ;
    while (total < NSTREAM) {
      n = read(p[0], rbuf + total, NSTREAM - total);
      if (n <= 0) {
        printf("child: FAIL: read returned %d after %d bytes\n", n, total);
        exit(2);
      }
      for (j = 0; j < n; j++)
        if (rbuf[total + j] != pat(total + j))
          bad = 1;
      total += n;
    }
    sum = fnv1a64bytes(sum, rbuf, total);
    // The parent prints checks 4 and 5 the moment the second write
    // returns (tick t0+40). The child holds its own print until
    // t0+55 so the two prints never interleave on the console and
    // the transcript order is fixed across runs.
    while ((uint64)uptime() < t0 + CHILDSLEEP + PRINTDELAY)
      ;
    printf("child: read %d bytes, checksum 0x%lx, %s\n",
           total, sum, bad ? "MISMATCH" : "byte-exact");
    exit(bad ? 1 : 0);
  }

  // Check 3: the fork handed back a positive child pid.
  checks++;
  if (pid > 0)
    printf("check 3: fork() returned child pid %d\n", pid);
  else {
    printf("check 3: FAIL: fork() returned %d\n", pid);
    mismatches++;
  }

  close(p[0]); // the parent only writes

  // Check 4: the second write of 1 byte. The pipe is full, so this
  // call must sleep inside the kernel until the child (still in its
  // 40-tick sleep) wakes and drains the buffer.
  w2 = write(p[1], &one, 1);
  t1 = (uint64)uptime();
  elapsed = t1 - t0;
  checks++;
  if (w2 == 1)
    printf("check 4: second write() returned %d after the drain\n", w2);
  else {
    printf("check 4: FAIL: second write() returned %d, expected 1\n", w2);
    mismatches++;
  }

  // Check 5: the second write really blocked. The child did not
  // touch the pipe until tick t0+40, so any elapsed under 30 ticks
  // would mean the write returned early instead of waiting for the
  // drain.
  checks++;
  if (elapsed >= BLOCKGATE)
    printf("check 5: second write blocked %ld ticks (>= %d), child slept %d\n",
           elapsed, BLOCKGATE, CHILDSLEEP);
  else {
    printf("check 5: FAIL: second write blocked only %ld ticks, expected >= %d\n",
           elapsed, BLOCKGATE);
    mismatches++;
  }

  close(p[1]);

  // Check 6: wait() reaps the one child and returns its pid.
  checks++;
  w = wait(&s);
  if (w == pid)
    printf("check 6: wait() returned child pid %d\n", w);
  else {
    printf("check 6: FAIL: wait() returned %d, expected %d\n", w, pid);
    mismatches++;
  }

  // Check 7: the child's exit status. The child exits 0 only when
  // its own loop verified all 513 received bytes against pat() at
  // their absolute stream indices.
  checks++;
  if (s == 0)
    printf("check 7: child exit status 0, its 513-byte readback verified byte-exact\n");
  else {
    printf("check 7: FAIL: child exit status %d\n", s);
    mismatches++;
  }

  // Expected-stream evidence: the parent's independent FNV-1a fold
  // over pat(0)..pat(512). The child printed its own fold over the
  // received bytes; the two must match.
  expect = 1469598103934665603UL; // FNV offset basis
  for (i = 0; i < NSTREAM; i++) {
    char c = pat(i);
    expect = fnv1a64bytes(expect, &c, 1);
  }
  printf("expected stream checksum: 0x%lx\n", expect);

  // FNV-1a over the measured structural values in fixed order:
  // child pid, both write returns, reaped pid, child status. The
  // elapsed tick count is printed but not folded, since it is a
  // timing measurement rather than a structural fact.
  {
    uint64 sum = 1469598103934665603UL; // FNV offset basis
    sum = fnv1a64bytes(sum, (char *)&pid, sizeof(pid));
    sum = fnv1a64bytes(sum, (char *)&w1, sizeof(w1));
    sum = fnv1a64bytes(sum, (char *)&w2, sizeof(w2));
    sum = fnv1a64bytes(sum, (char *)&w, sizeof(w));
    sum = fnv1a64bytes(sum, (char *)&s, sizeof(s));
    printf("checksum: 0x%lx\n", sum);
  }

  printf("pipebuf results: write1=%d write2=%d blocked_ticks=%ld reap=%d status=%d\n",
         w1, w2, elapsed, w, s);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: write past a full 512-byte pipe blocked %ld ticks until the reader drained it\n",
           elapsed);
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
