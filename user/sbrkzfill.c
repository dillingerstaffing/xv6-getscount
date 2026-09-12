// sbrkzfill: verify that newly grown pages come back zero-filled,
// and that a later grow does not disturb an earlier page.
//
// growproc hands each positive sbrk increment to uvmalloc, which
// allocates fresh physical pages with kalloc. kalloc zeroes every
// page it hands out (memset in kernel/kalloc.c), so a page the
// process has never touched must read all zeros. This program tests
// that guarantee from user space: it grows by exactly one page
// (4096 bytes), reads all 4096 bytes and requires every one to be
// zero, tiles a 32-byte canary over the page and requires a
// byte-exact readback, grows a second page, requires the second
// page to read all zeros, then re-reads the first page and requires
// the canary to be intact, proving the second grow left the first
// page alone.
//
// The zero scan is kept honest two ways: a count of nonzero bytes
// (must be 0) and a running byte sum folded alongside (must also
// be 0). Both are printed. A scan that silently skipped bytes would
// still have to fake both, and the canary readback on the same
// addresses pins the page contents down independently.
//
// Checks: sbrk(4096) returns the old break; page 1 scans zero;
// canary tiles page 1 byte-exact; sbrk(4096) again returns
// old+4096; page 2 scans zero; page 1 canary still byte-exact;
// sbrk(0) reads old+8192.
//
// No kernel code was changed; the test exercises xv6's existing
// zero-fill path (kalloc -> uvmalloc -> growproc) from user space.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSZ 4096
#define CANARYLEN 32

// Fixed 32-byte canary: no zero bytes, no 0xFF, printable and
// distinct from the zero-fill under test so a confused readback
// can never accidentally match.
static const char canary[CANARYLEN] =
  "0123456789ABCDEFabcdef!@#$%^&*()";

// FNV-1a 64-bit: fold one 64-bit word, least-significant byte first,
// into the running hash.
static uint64
fnv1a64(uint64 h, uint64 v)
{
  for (int i = 0; i < 8; i++) {
    h ^= (v >> (i * 8)) & 0xff;
    h *= 1099511628211UL;
  }
  return h;
}

// Scan nbytes starting at base: count nonzero bytes and accumulate
// their sum. Both must be 0 for a zero-filled page.
static void
scan_zero(char *base, int nbytes, int *nonzero, uint64 *sum)
{
  int i;
  *nonzero = 0;
  *sum = 0;
  for (i = 0; i < nbytes; i++) {
    if (base[i] != 0)
      (*nonzero)++;
    *sum += (uint64)(base[i] & 0xff);
  }
}

// Tile the canary over nbytes starting at base, then reread every
// byte and count mismatches against the pattern. Returns the
// mismatch count.
static int
tile_and_verify(char *base, const char *p, int nbytes)
{
  int i, mismatches = 0;

  for (i = 0; i < nbytes; i++)
    base[i] = p[i % CANARYLEN];
  for (i = 0; i < nbytes; i++) {
    if (base[i] != p[i % CANARYLEN])
      mismatches++;
  }
  return mismatches;
}

int
main(int argc, char *argv[])
{
  uint64 b, r1, r2, c;
  int checks = 0, mismatches = 0;
  int nz1, nz2, bad1, bad2;
  uint64 sum1, sum2;
  uint64 checksum = 14695981039346656037UL;

  (void)argc;
  (void)argv;

  // Baseline: read the current break.
  b = (uint64)sbrk(0);

  // Check 1: sbrk(4096) returns the old break (growproc hands out
  // memory starting at the old top).
  checks++;
  r1 = (uint64)sbrk(4096);
  if (r1 == b)
    printf("check 1: sbrk(4096) returned old break 0x%lx\n", r1);
  else {
    printf("check 1: FAIL: sbrk(4096) returned 0x%lx, expected 0x%lx\n",
           r1, b);
    mismatches++;
  }

  // Check 2: the freshly grown page reads all zeros. Fresh pages
  // come from kalloc, which zeroes them, so any nonzero byte here
  // is a leak of stale physical-page contents.
  checks++;
  scan_zero((char *)r1, PGSZ, &nz1, &sum1);
  if (nz1 == 0 && sum1 == 0)
    printf("check 2: first page (%d bytes) scanned: nonzero=%d sum=%ld, all zero\n",
           PGSZ, nz1, sum1);
  else {
    printf("check 2: FAIL: first page: %d nonzero bytes, byte sum %ld\n",
           nz1, sum1);
    mismatches++;
  }

  // Check 3: the page is writable and the canary reads back
  // byte-exact, so the zero scan above ran over real, mapped,
  // usable memory rather than a faulting hole.
  checks++;
  bad1 = tile_and_verify((char *)r1, canary, PGSZ);
  if (bad1 == 0)
    printf("check 3: %d-byte canary tiled over first page, readback byte-exact\n",
           PGSZ);
  else {
    printf("check 3: FAIL: %d byte mismatches in first-page canary readback\n",
           bad1);
    mismatches++;
  }

  // Check 4: the second grow also returns the old break at call
  // time, exactly one page above the first.
  checks++;
  r2 = (uint64)sbrk(4096);
  if (r2 == b + PGSZ)
    printf("check 4: sbrk(4096) returned 0x%lx == 0x%lx+4096\n", r2, b);
  else {
    printf("check 4: FAIL: sbrk(4096) returned 0x%lx, expected 0x%lx\n",
           r2, b + PGSZ);
    mismatches++;
  }

  // Check 5: the second fresh page also reads all zeros.
  checks++;
  scan_zero((char *)r2, PGSZ, &nz2, &sum2);
  if (nz2 == 0 && sum2 == 0)
    printf("check 5: second page (%d bytes) scanned: nonzero=%d sum=%ld, all zero\n",
           PGSZ, nz2, sum2);
  else {
    printf("check 5: FAIL: second page: %d nonzero bytes, byte sum %ld\n",
           nz2, sum2);
    mismatches++;
  }

  // Check 6: re-read the first page. The canary must be intact,
  // proving the second grow mapped new pages instead of
  // disturbing the first.
  checks++;
  {
    int i, bad = 0;
    char *p1 = (char *)r1;
    for (i = 0; i < PGSZ; i++) {
      if (p1[i] != canary[i % CANARYLEN])
        bad++;
    }
    bad2 = bad;
  }
  if (bad2 == 0)
    printf("check 6: first-page canary intact after second sbrk, %d bytes re-verified\n",
           PGSZ);
  else {
    printf("check 6: FAIL: %d byte mismatches re-reading first-page canary\n",
           bad2);
    mismatches++;
  }

  // Check 7: the break now reads exactly two pages above the start.
  checks++;
  c = (uint64)sbrk(0);
  if (c == b + 2 * PGSZ)
    printf("check 7: sbrk(0) reads 0x%lx == 0x%lx+8192, sum of grows\n",
           c, b);
  else {
    printf("check 7: FAIL: sbrk(0) read 0x%lx, expected 0x%lx\n", c,
           b + 2 * PGSZ);
    mismatches++;
  }

  checksum = fnv1a64(checksum, b);
  checksum = fnv1a64(checksum, r1);
  checksum = fnv1a64(checksum, r2);
  checksum = fnv1a64(checksum, c);
  checksum = fnv1a64(checksum, (uint64)nz1);
  checksum = fnv1a64(checksum, (uint64)nz2);
  checksum = fnv1a64(checksum, (uint64)bad1);
  checksum = fnv1a64(checksum, (uint64)bad2);
  checksum = fnv1a64(checksum, sum1);
  checksum = fnv1a64(checksum, sum2);
  printf("checksum: 0x%lx\n", checksum);
  printf("sbrkzfill values: b=0x%lx r1=0x%lx r2=0x%lx c=0x%lx nz1=%d nz2=%d\n",
         b, r1, r2, c, nz1, nz2);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: two fresh sbrk pages read all zero (%d bytes scanned, 0 nonzero), "
           "first-page canary intact after the second grow\n", 2 * PGSZ);
  else
    printf("FAIL: sbrk zero-fill\n");
  exit(mismatches == 0 ? 0 : 1);
}
