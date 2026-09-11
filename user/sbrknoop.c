// sbrknoop: exercise sbrk(0) as a pure break query from user space.
//
// The program reads the break twice with sbrk(0) and requires both
// reads to agree, grows the heap one page, then reads the break twice
// more and requires both to equal the old break plus 4096 exactly.
// It also tiles a fixed 64-byte canary over the new page and requires
// a byte-exact readback, proving the grown page is usable. Every
// check compares a real sbrk return value or break reading against
// the expected arithmetic; PASS prints only when all six expectations
// hold with 0 mismatches. This complements user/sbrkshrink.c, which
// covered negative increments and regrow, not zero-increment
// idempotence plus the additive shift.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Page size for the riscv64 target (kernel/riscv.h PGSIZE); kept local so
// this user program does not depend on kernel headers.
#define PAGESZ 4096

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

// Fill the 4096 bytes at base with the 64-byte pattern tiled 64 times,
// then read all 4096 bytes back and count mismatches against the
// pattern. Returns the mismatch count.
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
  uint64 a, b, g, c, d;
  int checks = 0, mismatches = 0, bad;
  uint64 checksum = 14695981039346656037UL;

  // Check 1: the initial break is page-aligned, so a 4096-byte
  // sbrk lands on exactly one new page.
  checks++;
  a = (uint64)sbrk(0);
  if (a % PAGESZ == 0)
    printf("check 1: sbrk(0) call A: break 0x%lx is page-aligned\n", a);
  else {
    printf("check 1: FAIL: sbrk(0) call A: break 0x%lx not page-aligned\n",
           a);
    mismatches++;
  }

  // Check 2: a second sbrk(0) returns the same break; the zero
  // increment changed nothing.
  checks++;
  b = (uint64)sbrk(0);
  if (b == a)
    printf("check 2: sbrk(0) call B: 0x%lx == call A, unchanged\n", b);
  else {
    printf("check 2: FAIL: sbrk(0) call B: 0x%lx, expected 0x%lx\n", b, a);
    mismatches++;
  }

  // Check 3: sbrk(4096) returns the old break (growproc hands out
  // memory starting at the old top).
  checks++;
  g = (uint64)sbrk(PAGESZ);
  if (g == a)
    printf("check 3: sbrk(4096) returned old break 0x%lx\n", g);
  else {
    printf("check 3: FAIL: sbrk(4096) returned 0x%lx, expected 0x%lx\n",
           g, a);
    mismatches++;
  }

  // Check 4: the first post-growth sbrk(0) reads the shifted break.
  checks++;
  c = (uint64)sbrk(0);
  if (c == a + PAGESZ)
    printf("check 4: sbrk(0) call C: 0x%lx == 0x%lx+4096, matches\n", c, a);
  else {
    printf("check 4: FAIL: sbrk(0) call C: 0x%lx, expected 0x%lx\n", c,
           a + PAGESZ);
    mismatches++;
  }

  // Check 5: a second post-growth sbrk(0) agrees; zero increments
  // stay no-ops after the shift.
  checks++;
  d = (uint64)sbrk(0);
  if (d == a + PAGESZ)
    printf("check 5: sbrk(0) call D: 0x%lx == 0x%lx+4096, unchanged\n",
           d, a);
  else {
    printf("check 5: FAIL: sbrk(0) call D: 0x%lx, expected 0x%lx\n", d,
           a + PAGESZ);
    mismatches++;
  }

  // Check 6: the grown page is writable and reads back byte-exact:
  // tile the 64-byte canary over all 4096 bytes and reread every byte.
  checks++;
  bad = tile_and_verify((char *)a, pat);
  if (bad == 0)
    printf("check 6: 4096-byte canary write/readback byte-exact\n");
  else {
    printf("check 6: FAIL: %d byte mismatches in canary readback\n", bad);
    mismatches++;
  }

  checksum = fnv1a64(checksum, a);
  checksum = fnv1a64(checksum, b);
  checksum = fnv1a64(checksum, g);
  checksum = fnv1a64(checksum, c);
  checksum = fnv1a64(checksum, d);
  checksum = fnv1a64(checksum, (uint64)bad);
  printf("checksum: 0x%lx\n", checksum);
  printf("sbrk-noop values: A=0x%lx B=0x%lx grow=0x%lx C=0x%lx D=0x%lx\n",
         a, b, g, c, d);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: sbrk(0) queries the break without changing it, "
           "growth shifts it by exactly 4096\n");
  else
    printf("FAIL: sbrk zero-increment / additive shift\n");
  exit(mismatches == 0 ? 0 : 1);
}
