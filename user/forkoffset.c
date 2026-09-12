// forkoffset: verify xv6's fork shares the file offset between parent
// and child.
//
// Fundamental truth: fork() copies the parent's fd table ENTRIES, not
// the open file descriptions they name. Both processes' descriptors
// name the SAME struct file, so a write through the child's inherited
// fd advances the offset the parent's fd sees. Verification plan: open
// a new file, fork, the child writes a fixed 32-byte pattern A through
// its inherited fd and exits with status 42; the parent waits
// (asserting wait returns the child's pid with the round-tripped
// status), then writes a fixed 32-byte pattern B through its own fd,
// closes, reopens read-only, and verifies the file holds exactly A
// followed by B byte-exact (proving the parent's write started where
// the child's left off, one shared offset), plus an EOF check that a
// further read returns 0. PASS prints only when every check holds
// with 0 mismatches. No lseek exists on this xv6, so the
// contiguous-bytes assertion is the offset-sharing proof. This is
// distinct from forkisolation, which tested that memory pages are
// copied; here the offset is the shared object.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// Two fixed 32-byte patterns, every byte a known literal.
#define PAT_A "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
#define PAT_B "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define PAT_LEN 32
#define TOTAL_LEN (2 * PAT_LEN)
#define CHILD_STATUS 42

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
  int fd, n, checks = 0, mismatches = 0;
  int pid, wpid, status, wlen, bad, i;
  char buf[TOTAL_LEN];
  uint64 sum;

  // A repeated run must start from an empty file: open with O_CREATE
  // does not truncate, and a leftover file would read back stale bytes.
  unlink("forkoffset.out");

  // Check 1: the first open lands on fd 3 (0-2 are the console).
  checks++;
  fd = open("forkoffset.out", O_CREATE|O_RDWR);
  if (fd == 3)
    printf("check 1: open returned fd 3, as expected\n");
  else {
    printf("check 1: FAIL: open returned fd %d, expected 3\n", fd);
    mismatches++;
  }

  // Check 2: fork hands the parent a positive child pid.
  checks++;
  pid = fork();
  if (pid < 0) {
    printf("check 2: FAIL: fork returned %d\n", pid);
    mismatches++;
    printf("checks: %d mismatches: %d\n", checks, mismatches);
    printf("FAIL: %d mismatches\n", mismatches);
    exit(1);
  }
  if (pid == 0) {
    // Check 3 (child): the inherited fd names the same open file
    // description, so this write advances the offset the parent's
    // fd sees. Exit 42 only when all 32 bytes went out; any other
    // status tells the parent the write failed.
    wlen = write(fd, PAT_A, PAT_LEN);
    if (wlen == PAT_LEN) {
      printf("check 3: child wrote %d bytes of pattern A through inherited fd %d\n",
             wlen, fd);
      exit(CHILD_STATUS);
    }
    printf("check 3: FAIL: child wrote %d bytes of pattern A, expected %d\n",
           wlen, PAT_LEN);
    exit(7);
  }
  printf("check 2: fork returned child pid %d\n", pid);

  // Check 4: wait returns exactly the child's pid.
  checks++;
  status = -1;
  wpid = wait(&status);
  if (wpid == pid)
    printf("check 4: wait returned the child's pid %d\n", wpid);
  else {
    printf("check 4: FAIL: wait returned %d, expected child pid %d\n",
           wpid, pid);
    mismatches++;
  }

  // Check 5: the exit status round-trips through wait. Status 42 also
  // proves check 3's write succeeded in the child, so check 3 counts
  // as verified here.
  checks++;
  if (status == CHILD_STATUS)
    printf("check 5: child exit status %d round-tripped through wait\n",
           status);
  else {
    printf("check 5: FAIL: wait reported status %d, expected %d\n",
           status, CHILD_STATUS);
    mismatches++;
  }

  // Check 6: the parent's write of pattern B goes out in full. If the
  // offset were per-process, this would land at offset 0 and
  // overwrite pattern A; the readback below decides that.
  checks++;
  wlen = write(fd, PAT_B, PAT_LEN);
  if (wlen == PAT_LEN)
    printf("check 6: parent wrote %d bytes of pattern B through fd %d\n",
           wlen, fd);
  else {
    printf("check 6: FAIL: parent wrote %d bytes of pattern B, expected %d\n",
           wlen, PAT_LEN);
    mismatches++;
  }
  close(fd);

  // Check 7: reopening read-only yields all 64 bytes.
  checks++;
  n = -1;
  fd = open("forkoffset.out", O_RDONLY);
  if (fd < 0) {
    printf("check 7: FAIL: could not open forkoffset.out\n");
    mismatches++;
  } else {
    n = read(fd, buf, TOTAL_LEN);
    if (n == TOTAL_LEN)
      printf("check 7: read back %d bytes, matches %d bytes written\n",
             n, TOTAL_LEN);
    else {
      printf("check 7: FAIL: read %d bytes, expected %d\n", n,
             TOTAL_LEN);
      mismatches++;
    }
  }

  // Check 8: byte-exact compare: first 32 bytes must be pattern A
  // (child's write), next 32 pattern B (parent's write). Any
  // per-process offset would show up here as B overwriting A
  // (bytes 0-31 wrong) or a short/zero tail.
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
      printf("check 8: all %d bytes match A-then-B, 0 mismatches\n",
             TOTAL_LEN);
    else {
      printf("check 8: FAIL: %d of %d bytes differ from A-then-B\n",
             bad, TOTAL_LEN);
      mismatches += bad;
    }
  } else {
    printf("check 8: skipped, read length was wrong\n");
    mismatches++;
  }

  // Check 9: EOF is sticky: one more read past the 64 bytes returns 0.
  checks++;
  if (fd >= 0) {
    char eofb[4];
    int r = read(fd, eofb, sizeof(eofb));
    close(fd);
    if (r == 0)
      printf("check 9: read past end returns 0, EOF is sticky\n");
    else {
      printf("check 9: FAIL: read past end returned %d, expected 0\n",
             r);
      mismatches++;
    }
  } else {
    printf("check 9: skipped, file never opened\n");
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

  printf("child pid: %d, status: %d, bytes written: %d, bytes read: %d\n",
         pid, status, TOTAL_LEN, n);
  // 9 checks total: the parent's 8 plus the child's check 3, which
  // check 5 verified via the exit status.
  printf("checks: %d mismatches: %d\n", checks + 1, mismatches);
  if (mismatches == 0)
    printf("PASS: fork shares the file offset, child and parent writes are contiguous\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
