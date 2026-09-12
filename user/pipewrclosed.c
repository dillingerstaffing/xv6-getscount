// pipewrclosed: verify that writing to a pipe whose read end has
// been closed returns -1, and that the writer is not killed.
//
// Fundamental truth: an xv6 pipe whose last read end closes has no
// consumer left. A write then has nowhere to deliver bytes, so the
// write path refuses the call and returns -1 instead of blocking
// forever or killing the writer (xv6 has no SIGPIPE; the error
// comes back as an ordinary return value). The observable facts
// are: write(p[1], buf, 16) returns exactly -1 after close(p[0]),
// and the process is still alive to print that fact. The control
// pipe proves the -1 comes from the closed read end and not from a
// broken pipe implementation: with the read end open, the same
// 16-byte write returns 16 and the bytes read back byte-exact. A
// final pipe() must hand back the expected low fds (3, 4), proving
// the failed write leaked nothing.
//
// Checks: pipe(p) ok; close(p[0]) ok; write(p[1], wbuf, 16) == -1;
// close(p[1]) ok; control pipe(q) ok; write(q[1], wbuf, 16) == 16;
// read(q[0], rbuf, 16) == 16 and byte-exact; both q ends close;
// pipe(r) returns fds 3 and 4 (no leak). PASS prints only when every
// check holds with 0 mismatches.
//
// No kernel code was changed; the test exercises xv6's existing
// pipewrite path from user space. The program is single-process
// and sequential, so the console transcript is deterministic; it
// prints every measured value.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define NBYTES 16

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
  int p[2], q[2], r[2];
  int checks = 0, mismatches = 0;
  int i, wret, w2, n2;
  int bad = 0;
  char wbuf[NBYTES];
  char rbuf[NBYTES];
  uint64 sum;

  (void)argc;
  (void)argv;

  // Fixed printable pattern, one byte per index, so the readback
  // compare is byte-exact and position-sensitive.
  for (i = 0; i < NBYTES; i++)
    wbuf[i] = (char)('A' + i);

  // Check 1: pipe creation hands back two distinct valid fds.
  checks++;
  if (pipe(p) == 0 && p[0] >= 0 && p[1] >= 0 && p[0] != p[1])
    printf("check 1: pipe() ok, read fd %d, write fd %d\n", p[0], p[1]);
  else {
    printf("check 1: FAIL: pipe() returned p[0]=%d p[1]=%d\n", p[0], p[1]);
    mismatches++;
  }

  // Check 2: the read end closes cleanly. From here no consumer
  // exists on this pipe.
  checks++;
  if (close(p[0]) == 0)
    printf("check 2: read end closed\n");
  else {
    printf("check 2: FAIL: close of read end failed\n");
    mismatches++;
  }

  // Check 3: the core assertion. The write must refuse with -1.
  // Reaching the printf below at all proves the writer was not
  // killed: a SIGPIPE-style kill would have ended the process here.
  wret = write(p[1], wbuf, NBYTES);
  checks++;
  if (wret == -1)
    printf("check 3: write() with read end closed returned -1, writer still alive\n");
  else {
    printf("check 3: FAIL: write() returned %d, expected -1\n", wret);
    mismatches++;
  }

  // Check 4: the write end closes cleanly after the refused write.
  checks++;
  if (close(p[1]) == 0)
    printf("check 4: write end closed\n");
  else {
    printf("check 4: FAIL: close of write end failed\n");
    mismatches++;
  }

  // Check 5: control pipe creation hands back valid fds.
  checks++;
  if (pipe(q) == 0 && q[0] >= 0 && q[1] >= 0 && q[0] != q[1])
    printf("check 5: control pipe() ok, read fd %d, write fd %d\n",
           q[0], q[1]);
  else {
    printf("check 5: FAIL: pipe() returned q[0]=%d q[1]=%d\n", q[0], q[1]);
    mismatches++;
  }

  // Check 6: with the read end open, the identical 16-byte write
  // must move all 16 bytes. This is what makes the -1 above
  // attributable to the closed read end, not to a broken pipe.
  w2 = write(q[1], wbuf, NBYTES);
  checks++;
  if (w2 == NBYTES)
    printf("check 6: control write() with read end open returned %d\n", w2);
  else {
    printf("check 6: FAIL: control write() returned %d, expected %d\n",
           w2, NBYTES);
    mismatches++;
  }

  // Check 7: the 16 bytes read back are exactly what was written,
  // in order.
  n2 = read(q[0], rbuf, NBYTES);
  for (i = 0; i < NBYTES; i++)
    if (i < n2 && rbuf[i] != wbuf[i])
      bad = 1;
  checks++;
  if (n2 == NBYTES && !bad)
    printf("check 7: read back %d bytes, byte-exact against the pattern\n", n2);
  else {
    printf("check 7: FAIL: read returned %d, byte-exact=%d\n", n2, !bad);
    mismatches++;
  }

  // Check 8: both control ends close cleanly.
  checks++;
  if (close(q[0]) == 0 && close(q[1]) == 0)
    printf("check 8: control pipe both ends closed\n");
  else {
    printf("check 8: FAIL: close of control ends failed\n");
    mismatches++;
  }

  // Check 9: a fresh pipe() hands back the expected low fds 3 and
  // 4, so the refused write leaked no descriptor.
  checks++;
  if (pipe(r) == 0 && r[0] == 3 && r[1] == 4) {
    printf("check 9: new pipe() got fds %d and %d, no descriptor leaked\n",
           r[0], r[1]);
    close(r[0]);
    close(r[1]);
  } else {
    printf("check 9: FAIL: new pipe() returned r[0]=%d r[1]=%d\n",
           r[0], r[1]);
    mismatches++;
  }

  // FNV-1a over every measured value: the refused write return,
  // the control write and read returns, the readback bytes, and the
  // final fd pair, collapsed to one checkable value.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, (char *)&wret, sizeof(wret));
  sum = fnv1a64bytes(sum, (char *)&w2, sizeof(w2));
  sum = fnv1a64bytes(sum, (char *)&n2, sizeof(n2));
  sum = fnv1a64bytes(sum, rbuf, n2 > 0 ? n2 : 0);
  sum = fnv1a64bytes(sum, (char *)&r[0], sizeof(r[0]));
  sum = fnv1a64bytes(sum, (char *)&r[1], sizeof(r[1]));
  printf("checksum: 0x%lx\n", sum);

  printf("write results: closed-readend=%d control-write=%d control-read=%d final-fds=%d,%d\n",
         wret, w2, n2, r[0], r[1]);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("RESULT: PASS (checks=%d)\n", checks);
  else
    printf("RESULT: FAIL (checks=%d, mismatches=%d)\n", checks, mismatches);
  exit(0);
}
