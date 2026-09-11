// pipepart: verify an xv6 pipe is a byte stream with no message
// boundaries, so short reads return exact prefixes in order.
//
// The test writes a fixed 64-byte pattern into a pipe with one
// write call, closes the write end, then reads the stream back in
// 7-byte chunks. 64 = 9*7 + 1, so a conforming read side must
// deliver nine 7-byte chunks and one 1-byte chunk, then 0 (EOF).
// Each chunk is compared byte-for-byte against the pattern at its
// absolute stream position, and the reassembled stream is compared
// against the original buffer in full. The pattern deliberately
// carries a NUL byte and a 0xFF byte (index 62 and 63) so the
// comparison must be length-driven; any C-string treatment would
// mis-handle them and be caught by the byte compare.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define NBYTES 64
#define CHUNKSZ 7

// Compile-time-fixed pattern: every output byte is a pure function
// of its index. Bytes 0..61 are printable ASCII ('!'..'~') so the
// stream reads sensibly in a hex dump; index 62 is 0x00 and index 63
// is 0xFF, proving the pipe moves raw bytes, not strings.
static char
pat(int i)
{
  if (i == 62)
    return 0;
  if (i == 63)
    return (char)0xff;
  return (char)('!' + (i * 7) % 94);
}

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

static char hexdigits[] = "0123456789abcdef";

int
main(int argc, char *argv[])
{
  int p[2];
  int checks = 0, mismatches = 0;
  int i, n, total, chunkno, w;
  int expected[10] = {7,7,7,7,7,7,7,7,7,1}; // 64 = 9*7 + 1
  int bad = 0;
  char wbuf[NBYTES];
  char rbuf[CHUNKSZ];
  char got[NBYTES];   // reassembled stream, in arrival order
  uint64 sum;

  (void)argc;
  (void)argv;

  for (i = 0; i < NBYTES; i++)
    wbuf[i] = pat(i);

  // Self-check: the fixed pattern really does carry the NUL and the
  // 0xFF at the promised indices; without them the test would
  // under-exercise non-string byte handling.
  checks++;
  if (wbuf[62] == 0 && (wbuf[63] & 0xff) == 0xff)
    printf("check 1: pattern carries NUL at 62 and 0xFF at 63\n");
  else {
    printf("check 1: FAIL: pattern[62]=0x%x pattern[63]=0x%x\n",
           wbuf[62] & 0xff, wbuf[63] & 0xff);
    mismatches++;
  }

  // Check 2: pipe creation hands back two distinct valid fds.
  checks++;
  if (pipe(p) == 0 && p[0] >= 0 && p[1] >= 0 && p[0] != p[1])
    printf("check 2: pipe() ok, read fd %d, write fd %d\n", p[0], p[1]);
  else {
    printf("check 2: FAIL: pipe() returned p[0]=%d p[1]=%d\n", p[0], p[1]);
    mismatches++;
  }

  // Check 3: the whole 64-byte payload leaves in ONE write call,
  // exactly 64 bytes, no short write.
  checks++;
  w = write(p[1], wbuf, NBYTES);
  if (w == NBYTES)
    printf("check 3: single write() returned %d of %d\n", w, NBYTES);
  else {
    printf("check 3: FAIL: write() returned %d, expected %d\n", w, NBYTES);
    mismatches++;
  }

  // Close the write end before reading: with no writer left, read
  // returns 0 exactly when the 64 bytes are drained, giving a clean
  // EOF to delimit the stream.
  checks++;
  if (close(p[1]) == 0)
    printf("check 4: write end closed before reading\n");
  else {
    printf("check 4: FAIL: close of write end failed\n");
    mismatches++;
  }

  // Check 5: the chunk sequence. Reads in 7-byte chunks must come
  // back as 7,7,7,7,7,7,7,7,7,1, each chunk's bytes matching the
  // pattern at the chunk's absolute stream position.
  sum = 1469598103934665603UL; // FNV offset basis
  total = 0;
  chunkno = 0;
  printf("chunk sequence:");
  for (;;) {
    n = read(p[0], rbuf, CHUNKSZ);
    if (n < 0) {
      printf("\nread failed\n");
      exit(1);
    }
    if (n == 0)
      break; // EOF
    printf(" %d", n);
    if (chunkno < 10) {
      if (n != expected[chunkno]) {
        printf("\ncheck 5: FAIL: chunk %d returned %d bytes, expected %d\n",
               chunkno, n, expected[chunkno]);
        bad = 1;
      }
    } else {
      printf("\ncheck 5: FAIL: extra chunk %d of %d bytes past the 64\n",
             chunkno, n);
      bad = 1;
    }
    for (i = 0; i < n; i++) {
      if (rbuf[i] != pat(total + i))
        bad = 1;
      got[total + i] = rbuf[i];
    }
    sum = fnv1a64bytes(sum, rbuf, n);
    total += n;
    chunkno++;
  }
  printf("\n");
  if (chunkno != 10)
    bad = 1;
  checks++;
  if (!bad)
    printf("check 5: chunk sequence 7,7,7,7,7,7,7,7,7,1, each chunk an exact stream prefix in order\n");
  else {
    printf("check 5: FAIL: chunk counts or byte order broke\n");
    mismatches++;
  }

  // Check 6: exactly 64 bytes arrived before EOF.
  checks++;
  if (total == NBYTES)
    printf("check 6: read %d bytes before EOF, matches %d written\n",
           total, NBYTES);
  else {
    printf("check 6: FAIL: read %d bytes, expected %d\n", total, NBYTES);
    mismatches++;
  }

  // Check 7: EOF is sticky: another read still returns 0.
  checks++;
  n = read(p[0], rbuf, CHUNKSZ);
  if (n == 0)
    printf("check 7: second read after EOF returned 0\n");
  else {
    printf("check 7: FAIL: read after EOF returned %d\n", n);
    mismatches++;
  }

  // Check 8: the reassembled stream is byte-exact against the
  // original buffer, including the NUL and 0xFF.
  {
    int eq = 1;
    for (i = 0; i < NBYTES; i++)
      if (got[i] != wbuf[i])
        eq = 0;
    checks++;
    if (eq)
      printf("check 8: reassembled 64 bytes equal the original buffer exactly\n");
    else {
      printf("check 8: FAIL: reassembled stream differs from the original\n");
      mismatches++;
    }
  }

  // Check 9: the read end closes cleanly.
  checks++;
  if (close(p[0]) == 0)
    printf("check 9: read end closed\n");
  else {
    printf("check 9: FAIL: close of read end failed\n");
    mismatches++;
  }

  // Received-stream evidence: head and tail in hex, then the FNV-1a
  // fold over the captured bytes, pinning the whole stream to one
  // checkable value.
  printf("stream head hex: ");
  for (i = 0; i < 16; i++)
    printf("%c%c ", hexdigits[(got[i] >> 4) & 0xf], hexdigits[got[i] & 0xf]);
  printf("\nstream tail hex: ");
  for (i = 48; i < 64; i++)
    printf("%c%c ", hexdigits[(got[i] >> 4) & 0xf], hexdigits[got[i] & 0xf]);
  printf("\n");
  printf("checksum: 0x%lx\n", sum);
  printf("bytes written: %d, bytes read: %d\n", NBYTES, total);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: pipe delivered 64 bytes as 7,7,7,7,7,7,7,7,7,1 prefixes in order, byte-exact\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
