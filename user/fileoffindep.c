// fileoffindep: verify each open file descriptor carries its own file offset.
//
// Fundamental truth: in xv6 each open() installs a separate entry in the
// process's open file table (its own struct file, with its own off
// field), so two descriptors opened for the same path must not share
// position. Verification plan: open the same new file twice, write a
// fixed 32-byte pattern A through fd 3, then read through fd 4. If fd 4
// has its own offset (still 0), the read returns the 32 A bytes; a
// shared offset would read at offset 32 (EOF) and return 0. Then write
// a fixed 32-byte pattern B through fd 4 (lands at file offsets
// 32..63) and read through fd 3: if fd 3's offset stayed at 32 while fd
// 4 advanced to 64, the read returns the 32 B bytes. PASS prints only
// when every check holds with 0 mismatches. No lseek exists on this
// xv6, so read/write positions are the only offset observations.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// Two fixed 32-byte patterns, every byte a known literal.
#define PAT_A "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
#define PAT_B "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define PAT_LEN 32

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

// Print one 32-byte buffer as two hex lines, 16 bytes per line.
static void
hexdump(char *b, int n)
{
  for (int i = 0; i < n; i++) {
    int c = b[i] & 0xff;
    printf("%c%c", hexdigits[(c >> 4) & 0xf], hexdigits[c & 0xf]);
    if (i % 16 == 15)
      printf("\n");
    else
      printf(" ");
  }
  if (n > 0 && (n - 1) % 16 != 15)
    printf("\n");
}

int
main(int argc, char *argv[])
{
  int fd3, fd4, wlen, n1, n2, checks = 0, mismatches = 0;
  int bad, i;
  char r1[PAT_LEN], r2[PAT_LEN];
  uint64 sum1, sum2;

  // A repeated run must start from an empty file: open with O_CREATE
  // does not truncate, and a leftover file would read back stale bytes.
  unlink("offindep.dat");

  // Check 1: the first open lands on fd 3 (0-2 are the console).
  checks++;
  fd3 = open("offindep.dat", O_CREATE|O_RDWR);
  if (fd3 == 3)
    printf("check 1: first open returned fd 3, as expected\n");
  else {
    printf("check 1: FAIL: first open returned fd %d, expected 3\n",
           fd3);
    mismatches++;
  }

  // Check 2: the second open of the same path is a separate open file
  // description and takes the lowest free descriptor, 4.
  checks++;
  fd4 = open("offindep.dat", O_RDWR);
  if (fd4 == 4)
    printf("check 2: second open returned fd 4, the lowest free slot\n");
  else {
    printf("check 2: FAIL: second open returned fd %d, expected 4\n",
           fd4);
    mismatches++;
  }

  // Check 3: writing pattern A through fd 3 writes all 32 bytes.
  checks++;
  wlen = write(fd3, PAT_A, PAT_LEN);
  if (wlen == PAT_LEN)
    printf("check 3: wrote %d bytes of pattern A through fd %d\n",
           wlen, fd3);
  else {
    printf("check 3: FAIL: wrote %d bytes of pattern A, expected %d\n",
           wlen, PAT_LEN);
    mismatches++;
  }

  // Check 4: reading 32 bytes through fd 4 must return pattern A
  // byte-exact. fd 3's offset is now 32; if fd 4 shared it, this read
  // would start at EOF and return 0. Returning A proves fd 4 read
  // from its own offset 0.
  checks++;
  n1 = -1;
  if (fd4 >= 0)
    n1 = read(fd4, r1, PAT_LEN);
  bad = 0;
  if (n1 == PAT_LEN) {
    for (i = 0; i < PAT_LEN; i++)
      if (r1[i] != PAT_A[i])
        bad++;
    if (bad == 0)
      printf("check 4: read 32 bytes through fd %d, matches pattern A\n",
             fd4);
    else {
      printf("check 4: FAIL: %d of 32 bytes differ from pattern A\n",
             bad);
      mismatches += bad;
    }
  } else {
    printf("check 4: FAIL: read %d bytes through fd %d, expected 32\n",
           n1, fd4);
    mismatches++;
  }

  // Check 5: writing pattern B through fd 4 writes all 32 bytes. fd
  // 4's offset is 32 (after its 32-byte read), so B lands at file
  // offsets 32..63.
  checks++;
  wlen = write(fd4, PAT_B, PAT_LEN);
  if (wlen == PAT_LEN)
    printf("check 5: wrote %d bytes of pattern B through fd %d\n",
           wlen, fd4);
  else {
    printf("check 5: FAIL: wrote %d bytes of pattern B, expected %d\n",
           wlen, PAT_LEN);
    mismatches++;
  }

  // Check 6: reading 32 bytes through fd 3 must return pattern B
  // byte-exact. fd 3's offset stayed at 32 (its own write left it
  // there) while fd 4 advanced to 64, so it reads the B region. If
  // the offset were shared, fd 3 would read at 64 (EOF) and return 0.
  checks++;
  n2 = -1;
  if (fd3 >= 0)
    n2 = read(fd3, r2, PAT_LEN);
  bad = 0;
  if (n2 == PAT_LEN) {
    for (i = 0; i < PAT_LEN; i++)
      if (r2[i] != PAT_B[i])
        bad++;
    if (bad == 0)
      printf("check 6: read 32 bytes through fd %d, matches pattern B\n",
             fd3);
    else {
      printf("check 6: FAIL: %d of 32 bytes differ from pattern B\n",
             bad);
      mismatches += bad;
    }
  } else {
    printf("check 6: FAIL: read %d bytes through fd %d, expected 32\n",
           n2, fd3);
    mismatches++;
  }

  close(fd4);
  close(fd3);
  unlink("offindep.dat");

  // Hex dumps of both readbacks: the byte-exact evidence.
  printf("readback 1 hex (fd 4 read):\n");
  hexdump(r1, n1 > 0 ? n1 : 0);
  printf("readback 2 hex (fd 3 read):\n");
  hexdump(r2, n2 > 0 ? n2 : 0);

  // Check 7: FNV-1a of both readbacks matches the host-recomputed
  // values (computed on the build machine from the pattern literals
  // with an independent Python implementation), so the on-guest fold
  // is verified against an outside oracle.
  checks++;
  sum1 = 1469598103934665603UL; // FNV offset basis
  sum1 = fnv1a64bytes(sum1, r1, n1 > 0 ? n1 : 0);
  sum2 = 1469598103934665603UL;
  sum2 = fnv1a64bytes(sum2, r2, n2 > 0 ? n2 : 0);
  printf("checksum 1: 0x%lx\n", sum1);
  printf("checksum 2: 0x%lx\n", sum2);
  if (n1 == PAT_LEN && n2 == PAT_LEN &&
      sum1 == 0x2D90D329EE4C2823UL &&
      sum2 == 0x8CE713CF2ECE4783UL)
    printf("check 7: checksums match host recomputation\n");
  else {
    printf("check 7: FAIL: checksum mismatch with host recomputation\n");
    mismatches++;
  }

  printf("fd3: %d, fd4: %d, read1 bytes: %d, read2 bytes: %d\n",
         fd3, fd4, n1, n2);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: each open descriptor keeps its own offset; fd 3 and fd 4 advance independently\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
