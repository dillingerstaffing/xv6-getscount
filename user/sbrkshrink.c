// sbrkshrink: exercise xv6's shrink path from user space.
//
// The program grows the heap by one page, fills the new page with a
// fixed 64-byte canary tiled over all 4096 bytes, shrinks the heap
// back with sbrk(-4096), then grows again and verifies the regrown
// page works with a different canary. Every check compares a real
// sbrk return value or break reading against the expected arithmetic;
// PASS prints only when all nine expectations hold with 0 mismatches.
// The negative growth exercises the growproc -> deallocuvm path, which
// the growth-ceiling test (user/sbrkoom.c) never touches.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Page size for the riscv64 target (kernel/riscv.h PGSIZE); kept local so
// this user program does not depend on kernel headers.
#define PAGESZ 4096

// Two fixed 64-byte canary patterns, tiled across the new page.
// patB is patA reversed, so a stale reread of the first pattern after
// the shrink/regrow cycle cannot pass the second readback check.
static const char patA[64] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char patB[64] =
  "/+9876543210zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA";

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

// Fill the 4096 bytes at base with the 64-byte pattern tiled 64 times,
// then read all 4096 bytes back and count mismatches against the
// pattern. Returns the mismatch count.
static int
tile_and_verify(char *base, const char *pat)
{
  int i, mismatches = 0;

  for (i = 0; i < PAGESZ; i++)
    base[i] = pat[i % 64];
  for (i = 0; i < PAGESZ; i++) {
    if (base[i] != pat[i % 64])
      mismatches++;
  }
  return mismatches;
}

int
main(int argc, char *argv[])
{
  uint64 b0, g1, e1, s1, e2, g2, e3;
  int checks = 0, mismatches = 0, bad;
  uint64 checksum = 14695981039346656037UL;

  // Check 1: the initial break is page-aligned, so a 4096-byte
  // sbrk lands on exactly one new page.
  checks++;
  b0 = (uint64)sbrk(0);
  if (b0 % PAGESZ == 0)
    printf("check 1: initial break 0x%lx is page-aligned\n", b0);
  else {
    printf("check 1: FAIL: initial break 0x%lx not page-aligned\n", b0);
    mismatches++;
  }

  // Check 2: sbrk(4096) returns the old break (growproc hands out
  // memory starting at the old top).
  checks++;
  g1 = (uint64)sbrk(PAGESZ);
  if (g1 == b0)
    printf("check 2: sbrk(4096) returned old break 0x%lx\n", g1);
  else {
    printf("check 2: FAIL: sbrk(4096) returned 0x%lx, expected 0x%lx\n",
           g1, b0);
    mismatches++;
  }

  // Check 3: the break moved up by exactly one page.
  checks++;
  e1 = (uint64)sbrk(0);
  if (e1 == b0 + PAGESZ)
    printf("check 3: sbrk(0)=0x%lx == 0x%lx+4096, matches\n", e1, b0);
  else {
    printf("check 3: FAIL: sbrk(0)=0x%lx, expected 0x%lx\n", e1,
           b0 + PAGESZ);
    mismatches++;
  }

  // Check 4: the new page is writable and reads back byte-exact:
  // tile the 64-byte canary over all 4096 bytes and reread every byte.
  checks++;
  bad = tile_and_verify((char *)b0, patA);
  if (bad == 0)
    printf("check 4: 4096-byte canary write/readback byte-exact\n");
  else {
    printf("check 4: FAIL: %d byte mismatches in canary readback\n", bad);
    mismatches++;
  }

  // Check 5: sbrk(-4096) returns the break as it was before the
  // shrink (growproc reports the old top even when shrinking).
  checks++;
  s1 = (uint64)sbrk(-PAGESZ);
  if (s1 == b0 + PAGESZ)
    printf("check 5: sbrk(-4096) returned old break 0x%lx\n", s1);
  else {
    printf("check 5: FAIL: sbrk(-4096) returned 0x%lx, expected 0x%lx\n",
           s1, b0 + PAGESZ);
    mismatches++;
  }

  // Check 6: the break moved back down to the original break.
  checks++;
  e2 = (uint64)sbrk(0);
  if (e2 == b0)
    printf("check 6: sbrk(0)=0x%lx back at initial break, shrink released the page\n",
           e2);
  else {
    printf("check 6: FAIL: sbrk(0)=0x%lx, expected 0x%lx\n", e2, b0);
    mismatches++;
  }

  // Check 7: growing again hands back the same address the shrink
  // released.
  checks++;
  g2 = (uint64)sbrk(PAGESZ);
  if (g2 == b0)
    printf("check 7: sbrk(4096) after shrink returned 0x%lx, same page reused\n",
           g2);
  else {
    printf("check 7: FAIL: regrow returned 0x%lx, expected 0x%lx\n", g2,
           b0);
    mismatches++;
  }

  // Check 8: the break is back up one page.
  checks++;
  e3 = (uint64)sbrk(0);
  if (e3 == b0 + PAGESZ)
    printf("check 8: sbrk(0)=0x%lx == 0x%lx+4096, matches\n", e3, b0);
  else {
    printf("check 8: FAIL: sbrk(0)=0x%lx, expected 0x%lx\n", e3,
           b0 + PAGESZ);
    mismatches++;
  }

  // Check 9: the regrown page is usable: tile the second, different
  // canary over it and reread byte-exact. (No assumption that the
  // first pattern survived; xv6 zeroes fresh pages.)
  checks++;
  bad = tile_and_verify((char *)b0, patB);
  if (bad == 0)
    printf("check 9: regrown page canary write/readback byte-exact\n");
  else {
    printf("check 9: FAIL: %d byte mismatches in regrown readback\n", bad);
    mismatches++;
  }

  checksum = fnv1a64(checksum, b0);
  checksum = fnv1a64(checksum, g1);
  checksum = fnv1a64(checksum, e1);
  checksum = fnv1a64(checksum, (uint64)bad); // second readback mismatch count (0)
  checksum = fnv1a64(checksum, s1);
  checksum = fnv1a64(checksum, e2);
  checksum = fnv1a64(checksum, g2);
  checksum = fnv1a64(checksum, e3);
  printf("checksum: 0x%lx\n", checksum);
  printf("sbrk-shrink values: b0=0x%lx grow=0x%lx end1=0x%lx "
         "shrink=0x%lx end2=0x%lx regrow=0x%lx end3=0x%lx\n",
         b0, g1, e1, s1, e2, g2, e3);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: sbrk(-4096) releases the page, regrow works\n");
  else
    printf("FAIL: sbrk shrink/regrow\n");
  exit(mismatches == 0 ? 0 : 1);
}
