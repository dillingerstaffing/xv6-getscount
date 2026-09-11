// pipeeof: verify that once every write end of an xv6 pipe is
// closed, reads return 0 forever.
//
// A child writes a fixed 64-byte pattern in one write call, closes
// its write end, and exits. The parent closes its own write end,
// waits for the child, then drains the pipe: the first read must
// return all 64 bytes and the next must return 0 (EOF). Three more
// successive reads must each still return 0, proving EOF does not
// deliver phantom data or block after the first hit. The readback
// is checked byte-for-byte against the compile-time pattern, and
// an FNV-1a checksum over the received bytes pins the stream to a
// single checkable value.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define NBYTES 64

// Same closed-form pattern as user/pipepart.c: bytes 0..61 are
// printable ASCII, index 62 is 0x00, index 63 is 0xFF, so the
// length-driven compare must move raw bytes, not strings.
static char
pat(int i)
{
  if (i == 62)
    return 0;
  if (i == 63)
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
  int i, n, total, pid, wpid, status;
  int drain[8];      // read return values while draining
  int ndrain = 0;
  int eofr[3];       // the three post-EOF reads
  int bad = 0, eofbad = 0;
  char wbuf[NBYTES];
  char rbuf[NBYTES];
  char got[NBYTES];  // reassembled stream, in arrival order
  uint64 sum;

  (void)argc;
  (void)argv;

  for (i = 0; i < NBYTES; i++)
    wbuf[i] = pat(i);

  // Check 1: pipe creation hands back two distinct valid fds.
  checks++;
  if (pipe(p) == 0 && p[0] >= 0 && p[1] >= 0 && p[0] != p[1])
    printf("check 1: pipe() ok, read fd %d, write fd %d\n", p[0], p[1]);
  else {
    printf("check 1: FAIL: pipe() returned p[0]=%d p[1]=%d\n", p[0], p[1]);
    mismatches++;
  }

  // Check 2: the fork lands a child.
  checks++;
  pid = fork();
  if (pid < 0) {
    printf("check 2: FAIL: fork failed\n");
    mismatches++;
    exit(1);
  }
  if (pid == 0) {
    // Child: the only writer. Close the read end, move the whole
    // pattern in one write call, close the write end, exit. After
    // this no writer exists anywhere, so EOF is permanent.
    close(p[0]);
    n = write(p[1], wbuf, NBYTES);
    if (n != NBYTES) {
      printf("child: FAIL: write returned %d, expected %d\n", n, NBYTES);
      exit(1);
    }
    close(p[1]);
    exit(0);
  }
  printf("check 2: forked child\n");

  // Check 3: the parent gives up its write end too. After the child
  // exits, no fd anywhere refers to the write end.
  checks++;
  if (close(p[1]) == 0)
    printf("check 3: parent closed its write end\n");
  else {
    printf("check 3: FAIL: parent close of write end failed\n");
    mismatches++;
  }

  // Check 4: the child is reaped with exit status 0, so its single
  // write really moved 64 bytes and both closes succeeded. Waiting
  // first also makes the drain sequence deterministic: all 64 bytes
  // sit in the pipe and no writer remains before the first read.
  checks++;
  wpid = wait(&status);
  if (wpid == pid && status == 0)
    printf("check 4: child reaped, exit status 0 (write moved 64 bytes)\n");
  else {
    printf("check 4: FAIL: wait returned pid %d status %d\n", wpid, status);
    mismatches++;
  }

  // Check 5: drain. The first read must return the full 64 bytes
  // and the next read 0; that exact pair is the published sequence.
  sum = 1469598103934665603UL; // FNV offset basis
  total = 0;
  for (;;) {
    n = read(p[0], rbuf, NBYTES);
    if (n < 0) {
      printf("read failed\n");
      exit(1);
    }
    if (ndrain < 8)
      drain[ndrain++] = n;
    if (n == 0)
      break; // EOF
    for (i = 0; i < n; i++) {
      if (total + i < NBYTES) {
        if (rbuf[i] != pat(total + i))
          bad = 1;
        got[total + i] = rbuf[i];
      } else {
        bad = 1; // more bytes arrived than were ever written
      }
    }
    sum = fnv1a64bytes(sum, rbuf, n);
    total += n;
  }
  printf("drain read return values:");
  for (i = 0; i < ndrain; i++)
    printf(" %d", drain[i]);
  printf("\n");
  checks++;
  if (ndrain == 2 && drain[0] == NBYTES && drain[1] == 0 && !bad)
    printf("check 5: drain sequence 64, 0 as predicted, bytes match pattern in order\n");
  else {
    printf("check 5: FAIL: unexpected drain sequence or byte mismatch\n");
    mismatches++;
  }

  // Check 6: exactly 64 bytes arrived before EOF.
  checks++;
  if (total == NBYTES)
    printf("check 6: read %d bytes before EOF, matches %d written\n",
           total, NBYTES);
  else {
    printf("check 6: FAIL: read %d bytes, expected %d\n", total, NBYTES);
    mismatches++;
  }

  // Check 7: EOF is sticky: three successive reads must each return
  // 0. A pipe whose EOF only fires once would hand back phantom
  // data or block here.
  for (i = 0; i < 3; i++) {
    eofr[i] = read(p[0], rbuf, NBYTES);
    if (eofr[i] != 0)
      eofbad = 1;
  }
  printf("EOF read return values: %d %d %d\n", eofr[0], eofr[1], eofr[2]);
  checks++;
  if (!eofbad)
    printf("check 7: three reads after EOF each returned 0\n");
  else {
    printf("check 7: FAIL: a post-EOF read returned nonzero\n");
    mismatches++;
  }

  // Check 8: the received stream is byte-exact against the pattern,
  // including the NUL at index 62 and the 0xFF at index 63.
  {
    int eq = (total == NBYTES);
    for (i = 0; i < NBYTES; i++)
      if (got[i] != wbuf[i])
        eq = 0;
    checks++;
    if (eq)
      printf("check 8: received 64 bytes equal the pattern exactly\n");
    else {
      printf("check 8: FAIL: received bytes differ from the pattern\n");
      mismatches++;
    }
  }

  // Check 9: the read end closes cleanly.
  checks++;
  if (close(p[0]) == 0)
    printf("check 9: read end closed\n");
  else {
    printf("check 9: FAIL: close of read end failed\n");
    mismatches++;
  }

  printf("checksum: 0x%lx\n", sum);
  printf("bytes written: %d, bytes read: %d\n", NBYTES, total);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: reads returned 0 forever once all write ends closed (drain 64, 0; EOF reads 0 0 0)\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
