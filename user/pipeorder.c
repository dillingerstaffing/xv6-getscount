// pipeorder: verify an xv6 pipe is an ordered byte stream.
//
// A pipe must deliver bytes in the order they were written. This test
// moves a fixed 2048-byte sequence through a pipe (xv6's pipe buffer
// is 512 bytes, so the writer blocks on a full pipe and the reader
// drains it in 128-byte reads, exercising the block/wakeup path four
// times over). The byte pattern is fixed at compile time by a closed
// formula, not random: the child writes the sequence, closes the write
// end, and the parent reads until read returns 0 (EOF) and compares
// every byte against the same formula, counting mismatches.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 2048 bytes > xv6's 512-byte pipe buffer: the writer cannot fit the
// whole payload at once and must block until the reader drains.
#define NBYTES 2048
#define RBUFSZ 128

// Compile-time-fixed byte pattern: every output byte is a pure
// function of its index. Multiplying by 31 permutes the residues
// mod 256 (31 is odd, hence coprime to 256), so all 256 byte values
// appear; xoring with the index's high bits breaks runs of equal
// bytes. Both the writer and the reader call this same definition.
static char
pat(int i)
{
  return (char)(((i * 31 + 7) ^ (i >> 2)) & 0xff);
}

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
  int p[2], pid, n, total, i, bad, wstatus;
  int checks = 0, mismatches = 0;
  char wbuf[NBYTES];
  char rbuf[RBUFSZ];
  char head[16];   // first 16 bytes actually received
  char tail[16];   // last 16 bytes actually received
  uint64 sum;

  // Fill the write buffer from the fixed pattern.
  for (i = 0; i < NBYTES; i++)
    wbuf[i] = pat(i);

  // Self-check: the formula must cover the full byte range, otherwise
  // the stream test would under-exercise pipe data. 31 is coprime to
  // 256, so all 256 values must appear at least once in 2048 bytes.
  {
    int seen[256];
    int distinct = 0;
    for (i = 0; i < 256; i++)
      seen[i] = 0;
    for (i = 0; i < NBYTES; i++)
      seen[(int)wbuf[i] & 0xff] = 1;
    for (i = 0; i < 256; i++)
      distinct += seen[i];
    checks++;
    if (distinct == 256)
      printf("check 1: pattern covers all %d byte values 0x00-0xff\n",
             distinct);
    else {
      printf("check 1: FAIL: pattern covers only %d of 256 byte values\n",
             distinct);
      mismatches++;
    }
  }

  if (pipe(p) < 0) {
    printf("pipe failed\n");
    exit(1);
  }

  pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    // Child: writer. Closes the read end, pushes the whole fixed
    // sequence through the write end (blocking when the 512-byte
    // buffer fills), then closes the write end so the parent sees
    // EOF. Exit status 0 only if every byte was handed to the pipe.
    close(p[0]);
    for (i = 0; i < NBYTES; ) {
      n = write(p[1], wbuf + i, NBYTES - i);
      if (n <= 0) {
        fprintf(2, "child FAIL: write returned %d after %d of %d bytes\n",
                n, i, NBYTES);
        exit(1);
      }
      i += n;
    }
    close(p[1]);
    exit(0);
  }

  // Parent: reader. Closes the write end, reads in 128-byte chunks
  // until read returns 0, comparing each byte against the fixed
  // pattern at its absolute stream position.
  close(p[1]);
  total = 0;
  bad = 0;
  sum = 1469598103934665603UL; // FNV offset basis
  for (;;) {
    n = read(p[0], rbuf, RBUFSZ);
    if (n < 0) {
      printf("read failed\n");
      exit(1);
    }
    if (n == 0)
      break; // EOF: writer closed its end after 2048 bytes
    for (i = 0; i < n; i++) {
      if (rbuf[i] != pat(total + i))
        bad++;
    }
    // Keep the first and last 16 received bytes for the hex evidence.
    for (i = 0; i < n && total + i < 16; i++)
      head[total + i] = rbuf[i];
    for (i = 0; i < n; i++) {
      int j;
      for (j = 0; j < 15; j++)
        tail[j] = tail[j + 1];
      tail[15] = rbuf[i];
    }
    sum = fnv1a64bytes(sum, rbuf, n);
    total += n;
  }
  close(p[0]);
  wait(&wstatus);

  // Check 2: the child reported handing all 2048 bytes to the pipe.
  checks++;
  if (wstatus == 0)
    printf("check 2: writer handed all %d bytes to the pipe (exit 0)\n",
           NBYTES);
  else {
    printf("check 2: FAIL: writer exited with status %d\n", wstatus);
    mismatches++;
  }

  // Check 3: exactly 2048 bytes arrived before EOF, so the read side
  // drained the full-buffer stalls completely.
  checks++;
  if (total == NBYTES)
    printf("check 3: read %d bytes before EOF, matches %d written\n",
           total, NBYTES);
  else {
    printf("check 3: FAIL: read %d bytes, expected %d\n", total, NBYTES);
    mismatches++;
  }

  // Check 4: byte-exact ordering against the fixed pattern.
  checks++;
  if (total == NBYTES && bad == 0)
    printf("check 4: all %d bytes match the fixed pattern in order, 0 mismatches\n",
           NBYTES);
  else {
    printf("check 4: FAIL: %d of %d bytes differ from the fixed pattern\n",
           bad, total > 0 ? total : NBYTES);
    mismatches += bad > 0 ? bad : 1;
  }

  // Head and tail of the received stream: the concrete evidence of
  // order, 16 bytes from each end, hex.
  printf("stream head hex: ");
  for (i = 0; i < 16; i++)
    printf("%c%c ", hexdigits[(head[i] >> 4) & 0xf], hexdigits[head[i] & 0xf]);
  printf("\nstream tail hex: ");
  for (i = 0; i < 16; i++)
    printf("%c%c ", hexdigits[(tail[i] >> 4) & 0xf], hexdigits[tail[i] & 0xf]);
  printf("\n");

  printf("checksum: 0x%lx\n", sum);
  printf("bytes written: %d, bytes read: %d, mismatches: %d\n",
         NBYTES, total, bad);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: pipe delivered the 2048-byte stream byte-exact and in order\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
