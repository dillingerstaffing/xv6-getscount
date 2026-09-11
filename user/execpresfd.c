// execpresfd: verify an open file descriptor survives exec.
//
// The fact under test: exec replaces the address space but keeps the
// caller's open file descriptors. The runner creates a pipe, forks
// a child, and the child execs user/execpresfd_hlp, passing the
// pipe's write-end fd number as an argv string. The helper converts
// the string back to an int, writes a FIXED 64-byte pattern
// (byte i = (i * 37 + 11) mod 256, i in 0..63) to that fd, reports
// the fd number it saw on its own stdout (wired to a second pipe),
// closes the fd, and exits. The parent closes the write end, drains
// the data pipe to EOF, and byte-exact compares the received bytes
// against the same pattern built in memory, and asserts the helper's
// reported fd number equals the number the child passed. If exec
// closed the descriptor, the helper's write would fail and no
// 64 bytes would arrive.
//
// Distinct from user/execargv (which tested argv delivery) and
// user/execfail (which tested that a failed exec preserves the
// image): this tests that the open file table survives a successful
// exec.
//
// No kernel code was changed; the test exercises xv6's existing exec
// path from user space.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PATLEN 64

static char hexdigits[] = "0123456789abcdef";

static void
build_pattern(char *b)
{
  int i;

  for (i = 0; i < PATLEN; i++)
    b[i] = (char)((i * 37 + 11) & 0xff);
}

// Tiny decimal formatter for non-negative ints (userland has no
// sprintf); used to pass the fd number through argv.
static void
emitint(char *buf, int *p, int v)
{
  char tmp[12];
  int n = 0;

  if (v == 0)
    tmp[n++] = '0';
  else {
    while (v > 0) {
      tmp[n++] = '0' + (v % 10);
      v /= 10;
    }
  }
  while (n > 0)
    buf[(*p)++] = tmp[--n];
}

// Parse a non-negative decimal at *pp; advance past it. Returns -1 on
// any malformed input.
static int
parseint(char **pp)
{
  char *p = *pp;
  int v = 0;

  if (*p < '0' || *p > '9')
    return -1;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (*p - '0');
    p++;
  }
  *pp = p;
  return v;
}

// FNV-1a 64-bit over raw bytes.
static uint64
fnv1a64bytes(uint64 h, char *b, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    h ^= (uint64)(b[i] & 0xff);
    h *= 1099511628211UL;
  }
  return h;
}

int
main(int argc, char *argv[])
{
  int dpfds[2], cpfds[2], pid, i, r, n, checks = 0, mismatches = 0;
  int wfdno, hfd, hwrote, alen, clen;
  char act[128], exp[128], cap[256], fdstr[8];
  int fp = 0, ep = 0;
  char *p;
  uint64 sum;

  (void)argc;
  (void)argv;

  // Build the expectation in memory from the same formula the helper
  // uses.
  build_pattern(exp);
  ep = PATLEN;

  // Check 1: setup self-check. The pattern is 64 bytes, the formula
  // is pinned on two hand-computed bytes (i=0 -> 11, i=63 ->
  // (63*37+11) mod 256 = 2342 mod 256 = 38), so a transcription slip
  // in the formula fails here, not in the exec test.
  checks++;
  if (ep == PATLEN && (exp[0] & 0xff) == 11 && (exp[63] & 0xff) == 38)
    printf("check 1: expectation built (%d bytes), formula pinned at bytes 0 and 63\n",
           ep);
  else {
    printf("check 1: FAIL: expectation broken (ep=%d, b0=%d, b63=%d)\n",
           ep, exp[0] & 0xff, exp[63] & 0xff);
    mismatches++;
  }

  if (pipe(dpfds) < 0 || pipe(cpfds) < 0) {
    printf("pipe failed\n");
    exit(1);
  }
  wfdno = dpfds[1];

  // The write end handed to the child must be a real pipe fd, not one
  // of the standard descriptors, so the helper cannot be reading
  // bytes that went to stdout by accident.
  checks++;
  if (wfdno != 1 && wfdno > 2)
    printf("check 2: write-end fd is %d, distinct from stdout\n", wfdno);
  else {
    printf("check 2: FAIL: write-end fd %d is not a clean pipe fd\n", wfdno);
    mismatches++;
  }

  pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    // Child: the data pipe's write end keeps its number wfdno across
    // the exec; the helper's stdout goes to the capture pipe.
    char *args[3];
    close(dpfds[0]);
    close(cpfds[0]);
    close(1);
    if (dup(cpfds[1]) != 1) {
      fprintf(2, "child FAIL: dup returned wrong fd\n");
      exit(1);
    }
    close(cpfds[1]);
    emitint(fdstr, &fp, wfdno);
    fdstr[fp] = 0;
    args[0] = "execpresfd_hlp";
    args[1] = fdstr;
    args[2] = 0;
    exec("execpresfd_hlp", args);
    fprintf(2, "child FAIL: exec failed\n");
    exit(1);
  }

  // Parent: close both write ends, then drain the data pipe to EOF
  // (EOF arrives only when the helper closes the write end after its
  // 64-byte write), then drain the helper's stdout capture.
  close(dpfds[1]);
  close(cpfds[1]);
  n = 0;
  while ((r = read(dpfds[0], act + n, sizeof(act) - n)) > 0)
    n += r;
  close(dpfds[0]);
  alen = n;
  n = 0;
  while ((r = read(cpfds[0], cap + n, sizeof(cap) - n)) > 0)
    n += r;
  close(cpfds[0]);
  clen = n;
  wait(0);

  // Check 3: the helper saw exactly the fd number the child passed,
  // and reported writing all 64 bytes. This is the direct evidence
  // that the descriptor the helper wrote through is the one that
  // existed before exec.
  checks++;
  p = cap;
  hfd = -1;
  hwrote = -1;
  if (clen > 9 && memcmp(p, "helper fd: ", 11) == 0) {
    p += 11;
    hfd = parseint(&p);
    if (hfd >= 0 && memcmp(p, ", wrote ", 8) == 0) {
      p += 8;
      hwrote = parseint(&p);
      if (hwrote < 0 || memcmp(p, " bytes\n", 7) != 0 ||
          (p + 7 - cap) != clen)
        hwrote = -1;
    }
  }
  if (hfd == wfdno && hwrote == PATLEN)
    printf("check 3: helper saw fd %d (matches passed %d), wrote %d bytes\n",
           hfd, wfdno, hwrote);
  else {
    printf("check 3: FAIL: helper reported fd %d wrote %d (passed fd %d, capture %d bytes)\n",
           hfd, hwrote, wfdno, clen);
    mismatches++;
  }

  // Check 4: exactly 64 bytes arrived on the data pipe.
  checks++;
  if (alen == PATLEN)
    printf("check 4: received %d bytes on the data pipe\n", alen);
  else {
    printf("check 4: FAIL: received %d bytes, expected %d\n", alen, PATLEN);
    mismatches++;
  }

  // Check 5: the 64 bytes are byte-exact against the in-memory
  // expectation. If the fd had been closed by exec, the helper's
  // write would have failed and this compare could not pass.
  checks++;
  if (alen == ep && memcmp(act, exp, ep) == 0)
    printf("check 5: %d bytes byte-exact against the expectation\n", alen);
  else {
    printf("check 5: FAIL: data pipe bytes differ from expectation\n");
    mismatches++;
  }

  // Hex dump of the capture: the byte-exact evidence.
  printf("capture hex:\n");
  for (i = 0; i < alen; i++) {
    int b = act[i] & 0xff;
    printf("%c%c", hexdigits[(b >> 4) & 0xf], hexdigits[b & 0xf]);
    if (i % 16 == 15)
      printf("\n");
    else
      printf(" ");
  }
  if (alen > 0 && (alen - 1) % 16 != 15)
    printf("\n");

  // FNV-1a over the bytes actually received, so the whole capture
  // collapses to one checkable value.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, act, alen > 0 ? alen : 0);
  printf("checksum: 0x%lx\n", sum);

  printf("fd passed: %d, bytes received: %d, bytes expected: %d\n",
         wfdno, alen, ep);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: open fd %d survived exec, 64-byte pattern intact\n", wfdno);
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
