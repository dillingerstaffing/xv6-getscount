// sbrkoom: exercise xv6's growproc ceiling from user space.
//
// The program grows the heap one page at a time with sbrk(4096) until
// sbrk refuses (returns -1), then checks the ceiling is a stable limit:
// sbrk(0) reports the break at the last successful page, a further
// sbrk(4096) still returns -1, and the heap end is page-aligned. Every
// printed number is a measurement of this build's growproc path; PASS
// prints only when all five expectations hold with 0 mismatches.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Page size for the riscv64 target (kernel/riscv.h PGSIZE); kept local so
// this user program does not depend on kernel headers.
#define PAGESZ 4096

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

int
main(int argc, char *argv[])
{
  uint64 base, end, grown, sum;
  uint64 pages = 0;
  int checks = 0, mismatches = 0;
  char *p;

  base = (uint64)sbrk(0);

  // Check 1: keep growing one page at a time until sbrk refuses with -1.
  for (;;) {
    p = sbrk(PAGESZ);
    if (p == (char *)-1)
      break;
    pages++;
  }
  checks++;
  end = (uint64)sbrk(0);
  grown = pages * PAGESZ;
  printf("check 1: growth stopped with -1 after %lu pages (%lu bytes)\n",
         pages, grown);
  if (pages == 0) {
    printf("check 1: FAIL: first sbrk already refused\n");
    mismatches++;
  }

  // Check 2: sbrk(0) reports the break exactly pages*4096 bytes above base,
  // i.e. the break sits on the last successful page.
  checks++;
  if (end == base + grown)
    printf("check 2: sbrk(0)=0x%lx == base(0x%lx)+%lu pages*4096, matches\n",
           end, base, pages);
  else {
    printf("check 2: FAIL: sbrk(0)=0x%lx, expected base+pages*4096=0x%lx\n",
           end, base + grown);
    mismatches++;
  }

  // Check 3: the limit is stable, not transient: a further sbrk(4096)
  // still returns -1 after the break was observed.
  checks++;
  p = sbrk(PAGESZ);
  if (p == (char *)-1)
    printf("check 3: second sbrk(4096) still returns -1, ceiling stable\n");
  else {
    printf("check 3: FAIL: second sbrk(4096) succeeded at 0x%lx\n",
           (uint64)p);
    mismatches++;
  }

  // Check 4: the heap end is page-aligned.
  checks++;
  if (end % PAGESZ == 0)
    printf("check 4: heap end 0x%lx is page-aligned\n", end);
  else {
    printf("check 4: FAIL: heap end 0x%lx not page-aligned\n", end);
    mismatches++;
  }

  // Check 5: the size accounting is consistent: pages grown times 4096
  // equals the distance from the initial break to the final break.
  checks++;
  if (pages > 0 && grown == end - base)
    printf("check 5: %lu pages * 4096 = %lu bytes, base 0x%lx to end 0x%lx\n",
           pages, grown, base, end);
  else {
    printf("check 5: FAIL: size accounting inconsistent\n");
    mismatches++;
  }

  // FNV-1a over the observed (pages, break, fail) triple, so the numbers
  // on this page can be re-checked against one value. The third element
  // is the constant 1 marking that a refusal was observed.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64(sum, pages);
  sum = fnv1a64(sum, end);
  sum = fnv1a64(sum, 1);
  printf("checksum: 0x%lx\n", sum);

  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: growproc ceiling is stable\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
