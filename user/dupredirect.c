// dupredirect: verify xv6's dup redirects fd 1 (stdout) into a file.
//
// Fundamental truth: dup(fd) installs the file in the process's fd
// table at the lowest free descriptor. After close(1) and dup(f),
// descriptor 1 names the same open file as f, so writes through
// descriptor 1 (including printf) land in the file, not on the
// console. Verification plan: the child opens a new file, closes fd 1,
// dups the file fd (expecting exactly 1), records dup's return value
// in a side file, then printf-writes a fixed 64-byte literal through
// fd 1. The parent waits, reads the side file (dup must read back as
// 1), reads the data file, and compares all 64 bytes against the
// expected literal, counting mismatches. PASS prints only when dup
// returned 1, the file holds exactly the bytes written, and every byte
// matches.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// Fixed 64-byte payload: every byte is a known literal, no generated
// data. 26 lowercase + 26 uppercase + 10 digits + '!' + '@' = 64.
#define PAYLOAD "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@"
#define PAYLOAD_LEN 64

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
  int pid, fd, dfd, mfd, n, checks = 0, mismatches = 0;
  int wlen, bad, i;
  char buf[PAYLOAD_LEN];
  char rfd[4];
  uint64 sum;

  // A repeated run must start from empty files: open with O_CREATE does
  // not truncate, and a longer leftover file would read back extra
  // bytes past the new payload.
  unlink("duptest.out");
  unlink("duptest.meta");

  // Self-check: the literal really is 64 bytes; the rest of the test
  // compares against this length, so a miscount here would be a bug in
  // the test, not in dup.
  checks++;
  wlen = 0;
  while (PAYLOAD[wlen] != 0 && wlen <= PAYLOAD_LEN)
    wlen++;
  if (wlen == PAYLOAD_LEN)
    printf("check 1: payload literal is %d bytes, as defined\n", wlen);
  else {
    printf("check 1: FAIL: payload literal is %d bytes, expected %d\n",
           wlen, PAYLOAD_LEN);
    mismatches++;
  }

  pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    // Child: fd 0, 1, 2 are the console, so the first open lands on 3.
    fd = open("duptest.out", O_CREATE|O_RDWR);
    if (fd != 3) {
      fprintf(2, "child FAIL: open returned fd %d, expected 3\n", fd);
      exit(1);
    }
    // Close stdout, then dup: dup must take the lowest free slot, 1.
    close(1);
    dfd = dup(fd);
    if (dfd != 1) {
      fprintf(2, "child FAIL: dup returned fd %d, expected 1\n", dfd);
      exit(1);
    }
    close(fd);
    // Side file carries dup's return value out to the parent; printf
    // now writes to the data file, so the meta channel gets its own fd.
    mfd = open("duptest.meta", O_CREATE|O_RDWR);
    write(mfd, (char *)&dfd, sizeof(dfd));
    close(mfd);
    // This printf goes through fd 1, which is the file after the dup.
    printf("%s", PAYLOAD);
    exit(0);
  }

  wait(0);

  // Check 2: the side file must read back dup's return value as 1.
  checks++;
  dfd = -1;
  mfd = open("duptest.meta", O_RDONLY);
  if (mfd < 0 || read(mfd, rfd, sizeof(rfd)) != sizeof(rfd)) {
    printf("check 2: FAIL: could not read dup result from side file\n");
    mismatches++;
  } else {
    dfd = *(int *)rfd;
    if (dfd == 1)
      printf("check 2: dup returned fd 1, read back from side file\n");
    else {
      printf("check 2: FAIL: dup returned fd %d, expected 1\n", dfd);
      mismatches++;
    }
  }
  if (mfd >= 0)
    close(mfd);

  // Check 3: the data file holds exactly the bytes the child wrote.
  checks++;
  n = -1;
  fd = open("duptest.out", O_RDONLY);
  if (fd < 0) {
    printf("check 3: FAIL: could not open duptest.out\n");
    mismatches++;
  } else {
    n = read(fd, buf, PAYLOAD_LEN);
    if (n == wlen)
      printf("check 3: read back %d bytes, matches %d bytes written\n",
             n, wlen);
    else {
      printf("check 3: FAIL: read %d bytes, expected %d\n", n, wlen);
      mismatches++;
    }
    close(fd);
  }

  // Check 4: byte-exact compare of the readback against the literal.
  checks++;
  bad = 0;
  if (n == wlen) {
    for (i = 0; i < wlen; i++)
      if (buf[i] != PAYLOAD[i])
        bad++;
    if (bad == 0)
      printf("check 4: all %d bytes match the expected literal, 0 mismatches\n",
             wlen);
    else {
      printf("check 4: FAIL: %d of %d bytes differ\n", bad, wlen);
      mismatches += bad;
    }
  } else {
    printf("check 4: skipped, read length was wrong\n");
    mismatches++;
  }

  // Hex dump of the readback: the byte-exact evidence, two hex digits
  // per byte, 16 bytes per line.
  printf("readback hex:\n");
  for (i = 0; i < n && i < PAYLOAD_LEN; i++) {
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

  printf("dup fd: %d, bytes written: %d, bytes read: %d\n", dfd, wlen, n);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: dup redirected stdout into the file, readback is byte-exact\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
