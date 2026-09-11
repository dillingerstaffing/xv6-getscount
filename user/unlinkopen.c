// unlinkopen: verify an open file descriptor keeps working after the
// path is unlinked, and that a fresh open of the unlinked path fails.
//
// Mechanism under test: in xv6, unlink removes the directory entry
// and drops the inode's link count, but the in-memory inode stays
// alive while an open struct file still references it (the file's
// ip pointer). The observable consequences, both checked here: (a)
// reads through the still-open descriptor after unlink return the
// file's bytes byte-exact, and (b) after the descriptor is closed,
// namei finds no entry and open returns -1. This test writes a fixed
// 128-byte pattern, unlinks the path while the read descriptor is
// open, reads back through that descriptor, then closes it and opens
// the same path fresh. PASS prints only when every check holds with
// 0 mismatches.
//
// No kernel code was changed; the test exercises xv6's existing
// unlink and inode-reference paths (sys_unlink, the file table's
// ip reference) from user space.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define DATAFILE "unlinkopen.data"
#define DATALEN 128

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
  int fdw, fd, fd2, i, n, bad, rc;
  char wbuf[DATALEN], rbuf[DATALEN];
  uint64 sum;

  (void)argc;
  (void)argv;

  // Fixed write pattern; the readback compare must be byte-exact.
  for (i = 0; i < DATALEN; i++)
    wbuf[i] = 'A' + (i * 7) % 26;

  // 1: create+open must land on fd 3, the lowest free descriptor
  // after the console's 0, 1, 2.
  fdw = open(DATAFILE, O_CREATE | O_RDWR);
  checks++;
  if (fdw == 3)
    printf("check 1: create+open returned fd %d (lowest free)\n", fdw);
  else {
    printf("check 1: FAIL: create+open returned fd %d, expected 3\n",
           fdw);
    mismatches++;
  }

  // 2: the full 128-byte pattern goes through the new descriptor.
  checks++;
  if (fdw >= 0) {
    n = write(fdw, wbuf, DATALEN);
    if (n == DATALEN)
      printf("check 2: wrote %d bytes through fd %d\n", n, fdw);
    else {
      printf("check 2: FAIL: write returned %d, expected %d\n",
             n, DATALEN);
      mismatches++;
    }
  } else {
    printf("check 2: FAIL: no descriptor to write through\n");
    mismatches++;
  }
  if (fdw >= 0)
    close(fdw);

  // 3: reopening read-only must again land on fd 3.
  fd = open(DATAFILE, O_RDONLY);
  checks++;
  if (fd == 3)
    printf("check 3: reopen read-only returned fd %d\n", fd);
  else {
    printf("check 3: FAIL: reopen returned fd %d, expected 3\n", fd);
    mismatches++;
  }

  // 4: unlinking the path while fd is open must succeed (return 0).
  // The directory entry is gone from here on.
  checks++;
  if (fd >= 0) {
    rc = unlink(DATAFILE);
    if (rc == 0)
      printf("check 4: unlink returned %d with fd %d still open\n",
             rc, fd);
    else {
      printf("check 4: FAIL: unlink returned %d, expected 0\n", rc);
      mismatches++;
    }
  } else {
    printf("check 4: FAIL: no open descriptor, cannot unlink under it\n");
    mismatches++;
  }

  // 5: the still-open descriptor must read back the full pattern
  // byte-exact. The inode has to stay alive behind the unlinked
  // path for this to hold.
  checks++;
  if (fd >= 0) {
    for (i = 0; i < DATALEN; i++)
      rbuf[i] = 0;
    n = read(fd, rbuf, DATALEN);
    if (n != DATALEN) {
      printf("check 5: FAIL: read returned %d, expected %d\n",
             n, DATALEN);
      mismatches++;
    } else {
      bad = 0;
      for (i = 0; i < DATALEN; i++)
        if (rbuf[i] != wbuf[i])
          bad++;
      if (bad == 0)
        printf("check 5: read %d bytes through the unlinked fd, "
               "byte-exact\n", n);
      else {
        printf("check 5: FAIL: %d byte mismatches after unlink\n",
               bad);
        mismatches++;
      }
    }
  } else {
    n = -1;
    printf("check 5: FAIL: no descriptor to read from\n");
    mismatches++;
  }

  // 6: the descriptor closes cleanly, dropping the last reference to
  // the now-unlinked inode.
  checks++;
  if (fd >= 0) {
    rc = close(fd);
    if (rc == 0)
      printf("check 6: close returned %d\n", rc);
    else {
      printf("check 6: FAIL: close returned %d, expected 0\n", rc);
      mismatches++;
    }
  } else {
    printf("check 6: FAIL: no descriptor to close\n");
    mismatches++;
  }

  // 7: with the last reference gone, a fresh open of the same path
  // must fail with exactly -1; the entry is really unlinked.
  fd2 = open(DATAFILE, O_RDONLY);
  checks++;
  if (fd2 == -1)
    printf("check 7: post-close open of the unlinked path returned %d\n",
           fd2);
  else {
    printf("check 7: FAIL: post-close open returned %d, expected -1\n",
           fd2);
    mismatches++;
  }

  // FNV-1a over the measured returns and the readback bytes: the
  // whole result collapsed to one checkable value.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, (char *)&fdw, sizeof(fdw));
  sum = fnv1a64bytes(sum, (char *)&fd, sizeof(fd));
  sum = fnv1a64bytes(sum, (char *)&fd2, sizeof(fd2));
  sum = fnv1a64bytes(sum, rbuf, DATALEN);
  printf("checksum: 0x%lx\n", sum);

  printf("readback bytes: %d, post-close open: %d\n", n, fd2);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: unlinked path read back byte-exact through the open "
           "fd, fresh open returned -1\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
