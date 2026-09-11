// sbrkgrow: exercise two successive positive sbrk grows from user space.
//
// sys_sbrk hands each increment to growproc, which advances the break
// by exactly the requested size. This program takes the initial break,
// grows it by 4096 bytes, grows it again by 2048 bytes, and requires
// the two increments to accumulate additively: the break must move
// by 4096 then by a further 2048, ending 6144 bytes above the start.
// It then writes a fixed 64-byte canary across all 6144 bytes from the
// initial break and requires a byte-exact readback, proving the whole
// grown region is usable. PASS prints only when every expectation
// holds with 0 mismatches, and the run exits nonzero otherwise. This
// complements user/sbrknoop.c (zero increment) and
// user/sbrkshrink.c (negative increment): this is the positive,
// additive-growth slice of sys_sbrk.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Fixed 64-byte canary pattern, tiled 96 times across 6144 bytes.
static const char pat[64] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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

// Write the 64-byte pattern tiled over nbytes starting at base, then
// read every byte back and count mismatches against the pattern.
// Returns the mismatch count.
static int
tile_and_verify(char *base, const char *p, int nbytes)
{
  int i, mismatches = 0;

  for (i = 0; i < nbytes; i++)
    base[i] = p[i % 64];
  for (i = 0; i < nbytes; i++) {
    if (base[i] != p[i % 64])
      mismatches++;
  }
  return mismatches;
}

int
main(int argc, char *argv[])
{
  uint64 b, r1, r2, c;
  int checks = 0, mismatches = 0, bad;
  uint64 checksum = 14695981039346656037UL;

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

  // Check 2: the second grow also returns the old break at call time,
  // which is now the break from step 1 plus 4096: the two increments
  // accumulate additively.
  checks++;
  r2 = (uint64)sbrk(2048);
  if (r2 == b + 4096)
    printf("check 2: sbrk(2048) returned 0x%lx == 0x%lx+4096, additive\n",
           r2, b);
  else {
    printf("check 2: FAIL: sbrk(2048) returned 0x%lx, expected 0x%lx\n",
           r2, b + 4096);
    mismatches++;
  }

  // Check 3: the break now reads exactly 6144 above the start.
  checks++;
  c = (uint64)sbrk(0);
  if (c == b + 6144)
    printf("check 3: sbrk(0) reads 0x%lx == 0x%lx+6144, sum of grows\n",
           c, b);
  else {
    printf("check 3: FAIL: sbrk(0) read 0x%lx, expected 0x%lx\n", c,
           b + 6144);
    mismatches++;
  }

  // Check 4: all 6144 grown bytes are writable and read back
  // byte-exact: tile the 64-byte canary over the whole region from b
  // and reread every byte.
  checks++;
  bad = tile_and_verify((char *)b, pat, 6144);
  if (bad == 0)
    printf("check 4: 6144-byte canary write/readback byte-exact\n");
  else {
    printf("check 4: FAIL: %d byte mismatches in canary readback\n", bad);
    mismatches++;
  }

  checksum = fnv1a64(checksum, b);
  checksum = fnv1a64(checksum, r1);
  checksum = fnv1a64(checksum, r2);
  checksum = fnv1a64(checksum, c);
  checksum = fnv1a64(checksum, (uint64)bad);
  printf("checksum: 0x%lx\n", checksum);
  printf("sbrk-grow values: b=0x%lx r1=0x%lx r2=0x%lx c=0x%lx\n",
         b, r1, r2, c);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: two positive sbrk grows accumulate additively "
           "(4096 then 2048 shifts the break by 6144), whole region usable\n");
  else
    printf("FAIL: additive sbrk growth\n");
  exit(mismatches == 0 ? 0 : 1);
}
