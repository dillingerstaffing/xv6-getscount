// dupshared: verify xv6's dup shares the file offset between descriptors.
//
// Fundamental truth: dup(fd) installs a second descriptor naming the
// SAME open file description, not a copy of it. The two descriptors
// share one file offset, so a write through either descriptor advances
// the offset seen by the other. Verification plan: open a new file,
// dup its fd, write a fixed 32-byte pattern A through fd1 and a fixed
// 32-byte pattern B through fd2, close both, reopen read-only, and
// verify the file holds exactly A followed by B byte-exact (proving
// the second write started where the first left off, one shared
// offset), plus an EOF check that a further read returns 0. PASS prints
// only when every check holds with 0 mismatches. No lseek exists on
// this xv6, so the contiguous-bytes assertion is the offset-sharing
// proof.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// Two fixed 32-byte patterns, every byte a known literal.
#define PAT_A "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
#define PAT_B "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define PAT_LEN 32
#define TOTAL_LEN (2 * PAT_LEN)

static char hexdigits[] = "0123456789abcdef";

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
  int fd1, fd2, fd, n, checks = 0, mismatches = 0;
  int wlen, bad, i;
  char buf[TOTAL_LEN];
  uint64 sum;

  // A repeated run must start from an empty file: open with O_CREATE
  // does not truncate, and a leftover file would read back stale bytes.
  unlink("dupshared.out");

  // Check 1: the first open lands on fd 3 (0-2 are the console).
  checks++;
  fd1 = open("dupshared.out", O_CREATE|O_RDWR);
  if (fd1 == 3)
    printf("check 1: open returned fd 3, as expected\n");
  else {
    printf("check 1: FAIL: open returned fd %d, expected 3\n", fd1);
    mismatches++;
  }

  // Check 2: dup takes the lowest free descriptor, 4.
  checks++;
  fd2 = dup(fd1);
  if (fd2 == 4)
    printf("check 2: dup returned fd 4, the lowest free slot\n");
  else {
    printf("check 2: FAIL: dup returned fd %d, expected 4\n", fd2);
    mismatches++;
  }

  // Check 3: writing pattern A through fd1 writes all 32 bytes.
  checks++;
  wlen = write(fd1, PAT_A, PAT_LEN);
  if (wlen == PAT_LEN)
    printf("check 3: wrote %d bytes of pattern A through fd %d\n",
           wlen, fd1);
  else {
    printf("check 3: FAIL: wrote %d bytes of pattern A, expected %d\n",
           wlen, PAT_LEN);
    mismatches++;
  }

  // Check 4: writing pattern B through fd2 writes all 32 bytes. If the
  // offset were per-descriptor, this write would land at offset 0 and
  // overwrite pattern A; the readback below decides that.
  checks++;
  wlen = write(fd2, PAT_B, PAT_LEN);
  if (wlen == PAT_LEN)
    printf("check 4: wrote %d bytes of pattern B through fd %d\n",
           wlen, fd2);
  else {
    printf("check 4: FAIL: wrote %d bytes of pattern B, expected %d\n",
           wlen, PAT_LEN);
    mismatches++;
  }
  close(fd2);
  close(fd1);

  // Check 5: reopening read-only yields all 64 bytes.
  checks++;
  n = -1;
  fd = open("dupshared.out", O_RDONLY);
  if (fd < 0) {
    printf("check 5: FAIL: could not open dupshared.out\n");
    mismatches++;
  } else {
    n = read(fd, buf, TOTAL_LEN);
    if (n == TOTAL_LEN)
      printf("check 5: read back %d bytes, matches %d bytes written\n",
             n, TOTAL_LEN);
    else {
      printf("check 5: FAIL: read %d bytes, expected %d\n", n,
             TOTAL_LEN);
      mismatches++;
    }
  }

  // Check 6: byte-exact compare: first 32 bytes must be pattern A,
  // next 32 pattern B. Any per-descriptor offset would show up here as
  // B overwriting A (bytes 0-31 wrong) or a short/zero tail.
  checks++;
  bad = 0;
  if (n == TOTAL_LEN) {
    for (i = 0; i < PAT_LEN; i++)
      if (buf[i] != PAT_A[i])
        bad++;
    for (i = 0; i < PAT_LEN; i++)
      if (buf[PAT_LEN + i] != PAT_B[i])
        bad++;
    if (bad == 0)
      printf("check 6: all %d bytes match A-then-B, 0 mismatches\n",
             TOTAL_LEN);
    else {
      printf("check 6: FAIL: %d of %d bytes differ from A-then-B\n",
             bad, TOTAL_LEN);
      mismatches += bad;
    }
  } else {
    printf("check 6: skipped, read length was wrong\n");
    mismatches++;
  }

  // Check 7: EOF is sticky: one more read past the 64 bytes returns 0.
  checks++;
  if (fd >= 0) {
    char eofb[4];
    int r = read(fd, eofb, sizeof(eofb));
    close(fd);
    if (r == 0)
      printf("check 7: read past end returns 0, EOF is sticky\n");
    else {
      printf("check 7: FAIL: read past end returned %d, expected 0\n",
             r);
      mismatches++;
    }
  } else {
    printf("check 7: skipped, file never opened\n");
    mismatches++;
  }

  // Hex dump of the readback: the byte-exact evidence, two hex digits
  // per byte, 16 bytes per line.
  printf("readback hex:\n");
  for (i = 0; i < n && i < TOTAL_LEN; i++) {
    int b = buf[i] & 0xff;
    printf("%c%c", hexdigits[(b >> 4) & 0xf], hexdigits[b & 0xf]);
    if (i % 16 == 15)
      printf("\n");
    else
      printf(" ");
  }
  if (n > 0 && (n - 1) % 16 != 15)
    printf("\n");

  // FNV-1a over the bytes actually read back, so the whole readback
  // collapses to one checkable value.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, buf, n > 0 ? n : 0);
  printf("checksum: 0x%lx\n", sum);

  printf("fd1: %d, fd2: %d, bytes written: %d, bytes read: %d\n",
         fd1, fd2, TOTAL_LEN, n);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: dup shares the file offset, writes through both fds are contiguous\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
