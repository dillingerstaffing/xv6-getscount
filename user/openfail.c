// openfail: verify a failed open allocates no file descriptor, so the
// next successful open lands on the lowest free fd.
//
// Mechanism under test: in xv6, sys_open returns -1 before fdalloc()
// when the path lookup fails (namei returns 0 for a nonexistent
// path), so the fd table is left untouched. The observable
// consequence: after a failed open, creating and opening a new file
// must land on the lowest free descriptor, fd 3, since fds 0, 1, 2
// are the console. This test opens a nonexistent path twice (both
// must return -1), then creates a file with O_CREATE|O_RDWR and
// checks it arrives on fd 3, writes a fixed 64-byte pattern through
// it, closes, re-opens read-only, and reads the bytes back exactly.
// PASS prints only when every check holds with 0 mismatches.
//
// No kernel code was changed; the test exercises xv6's existing open
// path (the namei failure return in sys_open) from user space.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define DATAFILE "openfail.data"
#define DATALEN 64

// FNV-1a 64-bit over raw bytes: the standard offset basis and prime,
// one fold per byte.
static uint64
fnv1a64bytes(uint64 h, char *b, int n)
{
  int i;
  for (i = 0; i < n; i++) {
    h ^= (uint64)(b[i] & 0xff);
    h *= 1099511628211UL;
  }
  return h;
}

int
main(int argc, char *argv[])
{
  int checks = 0, mismatches = 0;
  int r1, r2, fd, fd2, i, n, bad;
  char wbuf[DATALEN], rbuf[DATALEN];
  uint64 sum;

  (void)argc;
  (void)argv;

  // Fixed write pattern; the readback compare must be byte-exact.
  for (i = 0; i < DATALEN; i++)
    wbuf[i] = 'A' + (i * 5) % 26;

  // 1: a nonexistent path must fail with exactly -1.
  r1 = open("/no/such/file", O_RDONLY);
  checks++;
  if (r1 == -1)
    printf("check 1: open of nonexistent path returned %d\n", r1);
  else {
    printf("check 1: FAIL: open returned %d, expected -1\n", r1);
    mismatches++;
  }

  // 2: a second failed open also returns -1; repeated failures
  // allocate nothing between them.
  r2 = open("/no/such/file2", O_RDONLY);
  checks++;
  if (r2 == -1)
    printf("check 2: second failed open returned %d\n", r2);
  else {
    printf("check 2: FAIL: open returned %d, expected -1\n", r2);
    mismatches++;
  }

  // 3: the next successful open must land on fd 3, the lowest free
  // descriptor after the console's 0, 1, 2. If either failed open
  // had leaked a descriptor, this would be 4 or higher.
  fd = open(DATAFILE, O_CREATE | O_RDWR);
  checks++;
  if (fd == 3)
    printf("check 3: create+open returned fd %d (lowest free)\n", fd);
  else {
    printf("check 3: FAIL: create+open returned fd %d, expected 3\n",
           fd);
    mismatches++;
  }

  // 4: the full 64-byte pattern goes through the new descriptor.
  checks++;
  if (fd >= 0) {
    n = write(fd, wbuf, DATALEN);
    if (n == DATALEN)
      printf("check 4: wrote %d bytes through fd %d\n", n, fd);
    else {
      printf("check 4: FAIL: write returned %d, expected %d\n",
             n, DATALEN);
      mismatches++;
    }
  } else {
    printf("check 4: FAIL: no descriptor to write through\n");
    mismatches++;
  }
  if (fd >= 0)
    close(fd);

  // 5: after closing, re-opening read-only must again land on fd 3.
  fd2 = open(DATAFILE, O_RDONLY);
  checks++;
  if (fd2 == 3)
    printf("check 5: reopen returned fd %d (fd freed by close)\n", fd2);
  else {
    printf("check 5: FAIL: reopen returned fd %d, expected 3\n", fd2);
    mismatches++;
  }

  // 6-7: the reopened file reads back exactly what was written.
  checks++;
  if (fd2 >= 0) {
    for (i = 0; i < DATALEN; i++)
      rbuf[i] = 0;
    n = read(fd2, rbuf, DATALEN);
    if (n == DATALEN)
      printf("check 6: read back %d bytes from fd %d\n", n, fd2);
    else {
      printf("check 6: FAIL: read returned %d, expected %d\n",
             n, DATALEN);
      mismatches++;
    }
  } else {
    n = -1;
    printf("check 6: FAIL: no descriptor to read from\n");
    mismatches++;
  }

  checks++;
  if (n == DATALEN) {
    bad = 0;
    for (i = 0; i < DATALEN; i++)
      if (rbuf[i] != wbuf[i])
        bad++;
    if (bad == 0)
      printf("check 7: readback is byte-exact against the pattern\n");
    else {
      printf("check 7: FAIL: %d byte mismatches in readback\n", bad);
      mismatches++;
    }
  } else {
    printf("check 7: FAIL: short read, no compare possible\n");
    mismatches++;
  }
  if (fd2 >= 0)
    close(fd2);

  // FNV-1a over the measured fd numbers, both failed-open returns,
  // and the readback bytes: the whole result collapsed to one
  // checkable value.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, (char *)&r1, sizeof(r1));
  sum = fnv1a64bytes(sum, (char *)&r2, sizeof(r2));
  sum = fnv1a64bytes(sum, (char *)&fd, sizeof(fd));
  sum = fnv1a64bytes(sum, (char *)&fd2, sizeof(fd2));
  sum = fnv1a64bytes(sum, rbuf, DATALEN);
  printf("checksum: 0x%lx\n", sum);

  printf("failed open returns: %d %d, create fd: %d, reopen fd: %d\n",
         r1, r2, fd, fd2);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: failed opens returned -1 and the fd table was untouched\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
