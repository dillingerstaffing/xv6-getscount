// sbrksubpage: exercise a sub-page sbrk increment from user space.
//
// sbrk(n) reaches sys_sbrk as the eager call, which hands the
// increment to growproc (kernel/proc.c). growproc calls
// uvmalloc(pagetable, sz, sz+n, PTE_W) (kernel/vm.c); uvmalloc
// allocates physical pages in whole-page steps but returns newsz
// unrounded, and growproc records that exact value in p->sz. So a
// sub-page request moves the break by exactly the requested byte
// count, while the single page underneath it is fully mapped and
// zeroed. This program takes the initial break B, calls sbrk(100),
// requires the return to be B, requires the break to read exactly
// B+100 afterwards (the exact shift, not a page roundup), then tiles
// a fixed 64-byte canary over the whole new page [B, B+4096) and
// requires a byte-exact readback, proving the page-granular
// allocation beneath the sub-page increment is usable. PASS prints
// only when every expectation holds with 0 mismatches, and the run
// exits nonzero otherwise. This complements user/sbrkgrow.c
// (whole-page increments) and user/sbrknoop.c (zero increment): this
// is the sub-page slice of the eager sbrk path.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Page size for the riscv64 target (kernel/riscv.h PGSIZE); kept local so
// this user program does not depend on kernel headers.
#define PAGESZ 4096

// The sub-page increment under test: one byte over a power of ten,
// deliberately not a page multiple.
#define INC 100

// Fixed 64-byte canary pattern, tiled 64 times across the new page.
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

// Write the 64-byte pattern tiled over the 4096 bytes at base, then
// read every byte back and count mismatches against the pattern.
// Returns the mismatch count.
static int
tile_and_verify(char *base, const char *p)
{
  int i, mismatches = 0;

  for (i = 0; i < PAGESZ; i++)
    base[i] = p[i % 64];
  for (i = 0; i < PAGESZ; i++) {
    if (base[i] != p[i % 64])
      mismatches++;
  }
  return mismatches;
}

int
main(int argc, char *argv[])
{
  uint64 b, r, c, d;
  int checks = 0, mismatches = 0, bad;
  uint64 checksum = 14695981039346656037UL;

  // Check 1: the initial break is page-aligned, so the single page
  // uvmalloc allocates for the sub-page increment is exactly
  // [B, B+4096).
  checks++;
  b = (uint64)sbrk(0);
  if (b % PAGESZ == 0)
    printf("check 1: sbrk(0) baseline: break 0x%lx is page-aligned\n", b);
  else {
    printf("check 1: FAIL: sbrk(0) baseline: break 0x%lx not "
           "page-aligned\n", b);
    mismatches++;
  }

  // Check 2: sbrk(100) returns the old break (growproc hands out
  // memory starting at the old top).
  checks++;
  r = (uint64)sbrk(INC);
  if (r == b)
    printf("check 2: sbrk(100) returned old break 0x%lx\n", r);
  else {
    printf("check 2: FAIL: sbrk(100) returned 0x%lx, expected 0x%lx\n",
           r, b);
    mismatches++;
  }

  // Check 3: the break now reads exactly 100 above the start.
  // uvmalloc returns newsz unrounded, so growproc records p->sz =
  // B+100: the break moves by the requested byte count, not by a
  // page roundup.
  checks++;
  c = (uint64)sbrk(0);
  if (c == b + INC)
    printf("check 3: sbrk(0) reads 0x%lx == 0x%lx+100, exact shift\n",
           c, b);
  else {
    printf("check 3: FAIL: sbrk(0) read 0x%lx, expected 0x%lx\n", c,
           b + INC);
    mismatches++;
  }

  // Check 4: a second sbrk(0) agrees; the break is stable after the
  // sub-page increment.
  checks++;
  d = (uint64)sbrk(0);
  if (d == b + INC)
    printf("check 4: sbrk(0) re-read 0x%lx == 0x%lx+100, unchanged\n",
           d, b);
  else {
    printf("check 4: FAIL: sbrk(0) re-read 0x%lx, expected 0x%lx\n", d,
           b + INC);
    mismatches++;
  }

  // Check 5: the whole page beneath the sub-page increment is usable:
  // uvmalloc allocates in whole-page steps, so it mapped [B, B+4096)
  // even though the break only moved 100 bytes. Tile the 64-byte
  // canary over all 4096 bytes and reread every byte.
  checks++;
  bad = tile_and_verify((char *)b, pat);
  if (bad == 0)
    printf("check 5: 4096-byte canary over the page under the 100-byte "
           "grow is byte-exact\n");
  else {
    printf("check 5: FAIL: %d byte mismatches in canary readback\n", bad);
    mismatches++;
  }

  checksum = fnv1a64(checksum, b);
  checksum = fnv1a64(checksum, r);
  checksum = fnv1a64(checksum, c);
  checksum = fnv1a64(checksum, d);
  checksum = fnv1a64(checksum, (uint64)bad);
  printf("checksum: 0x%lx\n", checksum);
  printf("sbrk-subpage values: b=0x%lx r=0x%lx c=0x%lx d=0x%lx\n",
         b, r, c, d);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: sbrk(100) moves the break by exactly 100 and the "
           "page beneath is fully usable\n");
  else
    printf("FAIL: sub-page sbrk increment\n");
  exit(mismatches == 0 ? 0 : 1);
}
