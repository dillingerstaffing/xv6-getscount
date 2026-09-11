// execfail: verify a failed exec returns -1 and leaves the calling
// process's memory intact.
//
// Fundamental truth: in xv6, sys_exec replaces the process image
// only after it has successfully opened and read the binary. When
// the path cannot be opened, exec returns -1 and the calling process
// continues running with its memory exactly as before. This test
// sets two global sentinels to known constants, calls exec on a
// nonexistent path, then checks that exec returned -1 and that both
// sentinels still hold their constants at their recorded addresses.
// Checks: both sentinels read back correctly before exec (the
// write/read path works), exec returned -1 exactly, and both
// sentinels read back unchanged after the failed exec. PASS prints
// only when every check holds with 0 mismatches.
//
// No kernel code was changed; the test exercises xv6's existing exec
// path from user space.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

static uint64 sentinel64;
static int sentinel32;

#define SENTINEL64 0xDEADBEEF12345678UL
#define SENTINEL32 0x9ABCDEF0

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

static char *args[] = { "execfail", 0 };

int
main(int argc, char *argv[])
{
  int checks = 0, mismatches = 0;
  int r;
  uint64 sum;

  (void)argc;
  (void)argv;

  // Set the sentinels and confirm the writes took, recording each
  // sentinel's address so the post-exec re-read can be compared
  // against the same location.
  sentinel64 = SENTINEL64;
  sentinel32 = SENTINEL32;

  checks++;
  if (sentinel64 == SENTINEL64)
    printf("check 1: sentinel64 at %p holds 0x%lx before exec\n",
           &sentinel64, sentinel64);
  else {
    printf("check 1: FAIL: sentinel64 is 0x%lx, expected 0x%lx\n",
           sentinel64, SENTINEL64);
    mismatches++;
  }

  checks++;
  if (sentinel32 == SENTINEL32)
    printf("check 2: sentinel32 at %p holds 0x%x before exec\n",
           &sentinel32, sentinel32);
  else {
    printf("check 2: FAIL: sentinel32 is 0x%x, expected 0x%x\n",
           sentinel32, SENTINEL32);
    mismatches++;
  }

  // This exec must fail: the path does not exist in fs.img. exec
  // returns only on failure.
  r = exec("/no/such/binary", args);

  checks++;
  if (r == -1)
    printf("check 3: exec returned %d (failed as expected)\n", r);
  else {
    printf("check 3: FAIL: exec returned %d, expected -1\n", r);
    mismatches++;
  }

  // The image must be untouched: re-read both sentinels at the same
  // addresses and compare against the constants.
  checks++;
  if (sentinel64 == SENTINEL64)
    printf("check 4: sentinel64 at %p still holds 0x%lx after failed exec\n",
           &sentinel64, sentinel64);
  else {
    printf("check 4: FAIL: sentinel64 is 0x%lx, expected 0x%lx\n",
           sentinel64, SENTINEL64);
    mismatches++;
  }

  checks++;
  if (sentinel32 == SENTINEL32)
    printf("check 5: sentinel32 at %p still holds 0x%x after failed exec\n",
           &sentinel32, sentinel32);
  else {
    printf("check 5: FAIL: sentinel32 is 0x%x, expected 0x%x\n",
           sentinel32, SENTINEL32);
    mismatches++;
  }

  // FNV-1a over the post-exec sentinel bytes and the exec return
  // value: the survival evidence collapsed to one checkable value.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, (char *)&sentinel64, sizeof(sentinel64));
  sum = fnv1a64bytes(sum, (char *)&sentinel32, sizeof(sentinel32));
  sum = fnv1a64bytes(sum, (char *)&r, sizeof(r));
  printf("checksum: 0x%lx\n", sum);

  printf("exec return: %d, sentinel64: 0x%lx, sentinel32: 0x%x\n",
         r, sentinel64, sentinel32);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: failed exec returned -1 and the image survived intact\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
