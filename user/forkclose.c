// forkclose: verify that fork copies the descriptor table with
// independent close semantics.
//
// Fundamental truth: fork duplicates the parent's descriptor table by
// bumping each open file's reference count (kernel/proc.c, fork:
// filedup on every fd). The child gets its own table slots naming
// the same open file descriptions, so close(fd) in the child only
// drops the child's reference and cannot kill the parent's copy.
// Verification plan: the parent opens a new file for writing (fd 3)
// and forks; the child closes its inherited fd and exits; the parent
// waits for the child, then writes a fixed 32-byte known pattern
// through its own fd, closes, reopens the file read-only, and asserts
// the 32 bytes read back byte-exact. If the child's close had freed
// the shared open file, the parent's write would have failed or the
// file would be empty. PASS prints only when every check holds with
// 0 mismatches. No lseek exists on this xv6, so the reopen-read
// contiguity is the assertion mechanism.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// One fixed 32-byte pattern, every byte a known literal.
#define PAT "child-closed-but-parent-writes-y"
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

int
main(int argc, char *argv[])
{
  int fd, n, checks = 0, mismatches = 0;
  int p, w, cr, s, bad, i;
  char buf[PAT_LEN];
  uint64 sum;

  (void)argc;
  (void)argv;

  // A repeated run must start from an empty file: open with O_CREATE
  // does not truncate, and a leftover file would read back stale bytes.
  unlink("forkclose.out");

  // Check 1: the open lands on fd 3 (0-2 are the console).
  checks++;
  fd = open("forkclose.out", O_CREATE|O_RDWR);
  if (fd == 3)
    printf("check 1: open returned fd 3, as expected\n");
  else {
    printf("check 1: FAIL: open returned fd %d, expected 3\n", fd);
    mismatches++;
  }

  // Check 2: fork returns a positive child pid to the parent. Only
  // the parent runs the checks below; the child takes its own silent
  // branch first so the two processes never interleave prints on the
  // serial console (only the parent prints, so the transcript is
  // deterministic).
  p = fork();
  if (p == 0) {
    // The child closes its inherited copy of the descriptor and
    // exits. Its table slots are its own, so this close must not
    // touch the parent's descriptor. The child prints nothing; its
    // exit code is the parent's evidence for the close return
    // (0 means close returned 0).
    cr = close(fd);
    exit(cr == 0 ? 0 : 1);
  }
  checks++;
  if (p > 0)
    printf("check 2: fork returned child pid %d\n", p);
  else {
    printf("check 2: FAIL: fork returned %d, expected positive pid\n",
           p);
    mismatches++;
    printf("fork failed, cannot continue\n");
    exit(1);
  }

  // Check 3: wait() reaps exactly the child fork returned, and the
  // child's exit code 0 certifies that the child's close(fd)
  // returned 0 (the child's close is asserted through its exit
  // status, since only the parent prints).
  w = wait(&s);
  checks++;
  if (w == p && s == 0)
    printf("check 3: wait returned %d with status 0, child close returned 0\n",
           w);
  else {
    printf("check 3: FAIL: wait returned %d status %d, expected pid %d status 0\n",
           w, s, p);
    mismatches++;
  }

  // Check 4: the parent's own descriptor still works after the child
  // closed its copy: the fixed 32-byte pattern goes through.
  checks++;
  w = write(fd, PAT, PAT_LEN);
  if (w == PAT_LEN)
    printf("check 4: parent write through fd %d returned %d, all 32 bytes\n",
           fd, w);
  else {
    printf("check 4: FAIL: parent write returned %d, expected %d\n",
           w, PAT_LEN);
    mismatches++;
  }
  close(fd);

  // Check 5: reopening read-only yields all 32 bytes.
  checks++;
  n = -1;
  fd = open("forkclose.out", O_RDONLY);
  if (fd < 0) {
    printf("check 5: FAIL: could not open forkclose.out\n");
    mismatches++;
  } else {
    n = read(fd, buf, PAT_LEN);
    if (n == PAT_LEN)
      printf("check 5: read back %d bytes, matches %d bytes written\n",
             n, PAT_LEN);
    else {
      printf("check 5: FAIL: read %d bytes, expected %d\n", n,
             PAT_LEN);
      mismatches++;
    }
  }

  // Check 6: byte-exact compare against the fixed pattern. Any
  // cross-process close interference (a freed open file, a
  // redirected offset) would show up here as wrong or short bytes.
  checks++;
  bad = 0;
  if (n == PAT_LEN) {
    for (i = 0; i < PAT_LEN; i++)
      if (buf[i] != PAT[i])
        bad++;
    if (bad == 0)
      printf("check 6: all %d bytes match the pattern, 0 mismatches\n",
             PAT_LEN);
    else {
      printf("check 6: FAIL: %d of %d bytes differ from the pattern\n",
             bad, PAT_LEN);
      mismatches += bad;
    }
  } else {
    printf("check 6: skipped, read length was wrong\n");
    mismatches++;
  }
  if (fd >= 0)
    close(fd);

  // Hex dump of the readback: the byte-exact evidence, two hex
  // digits per byte, 16 bytes per line.
  printf("readback hex:\n");
  for (i = 0; i < n && i < PAT_LEN; i++) {
    int b = buf[i] & 0xff;
    printf("%c%c", hexdigits[(b >> 4) & 0xf], hexdigits[b & 0xf]);
    if (i % 16 == 15)
      printf("\n");
    else
      printf(" ");
  }
  if (n > 0 && (n - 1) % 16 != 15)
    printf("\n");

  // FNV-1a over every measured value: the child pid, the parent's
  // write count, and the bytes actually read back, in fixed order.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, (char *)&p, sizeof(p));
  sum = fnv1a64bytes(sum, (char *)&w, sizeof(w));
  sum = fnv1a64bytes(sum, buf, n > 0 ? n : 0);
  printf("checksum: 0x%lx\n", sum);

  printf("child pid: %d, write returned: %d, bytes read: %d\n",
         p, w, n);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: child close did not affect the parent's descriptor, 32 bytes written and read back byte-exact\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
