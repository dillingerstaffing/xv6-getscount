// execargv: verify exec delivers the argv vector verbatim to the new
// program.
//
// Fundamental truth: exec replaces the process image, so the argv
// array handed to exec must appear byte-exact in the new program's
// argv. The runner forks a child that execs user/execargv_echo with
// a known vector: a chosen argv[0] name, a normal word, a longer
// string, an empty string, and a string with spaces. The target prints
// argc, each argv[i] with its byte length, and whether argv[argc] is
// NULL. The parent captures the child's stdout through a pipe and
// compares it byte-exact against an expectation built from the same
// constants. Checks: argc matches, each argv[i] matches byte-for-byte
// with the right length, argv[argc] is NULL, and the whole captured
// output matches the expectation byte-exact. PASS prints only when
// every check holds with 0 mismatches.
//
// No kernel code was changed; the test exercises xv6's existing exec
// path from user space.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// The known vector, NULL-terminated as exec requires. Four distinct
// argument shapes after argv[0]: a normal word, a longer string, an
// empty string, and a string with spaces.
static char *args[] = {
  "argv0name",
  "hello",
  "a longer string with spaces",
  "",
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
  0,
};
#define NARGS 5

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

// Tiny decimal formatter for non-negative ints, used to build the
// expected output in memory (userland has no sprintf). The target
// prints the same lines with printf %d, so identical inputs must
// produce identical bytes; the byte-exact compare at the end would
// expose any formatting skew here as a mismatch.
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

static void
emitstr(char *buf, int *p, char *s)
{
  while (*s)
    buf[(*p)++] = *s++;
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

int
main(int argc, char *argv[])
{
  int pfds[2], pid, i, r, n, checks = 0, mismatches = 0;
  char act[512], exp[512];
  int ep = 0, alen;
  char *p;
  uint64 sum;

  (void)argc;
  (void)argv;

  // Build the expectation from the constants, mirroring the target's
  // printf layout exactly: "argc: %d\n", then one
  // "argv[i] len=L: value\n" line per argument, then the NULL line.
  emitstr(exp, &ep, "argc: ");
  emitint(exp, &ep, NARGS);
  emitstr(exp, &ep, "\n");
  for (i = 0; i < NARGS; i++) {
    emitstr(exp, &ep, "argv[");
    emitint(exp, &ep, i);
    emitstr(exp, &ep, "] len=");
    emitint(exp, &ep, (int)strlen(args[i]));
    emitstr(exp, &ep, ": ");
    emitstr(exp, &ep, args[i]);
    emitstr(exp, &ep, "\n");
  }
  emitstr(exp, &ep, "argv[");
  emitint(exp, &ep, NARGS);
  emitstr(exp, &ep, "] is NULL: yes\n");

  // Self-check: the expected buffer must be non-empty and the 48-x
  // literal must really be 48 bytes; the rest of the test compares
  // against these constants, so a miscount here would be a bug in the
  // test, not in exec.
  checks++;
  if (ep > 0 && strlen(args[4]) == 48)
    printf("check 1: expectation built (%d bytes), long arg is 48 bytes\n", ep);
  else {
    printf("check 1: FAIL: expectation broken (ep=%d, arglen=%d)\n",
           ep, (int)strlen(args[4]));
    mismatches++;
  }

  if (pipe(pfds) < 0) {
    printf("pipe failed\n");
    exit(1);
  }
  pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    // Child: wire stdout to the pipe, then exec. After exec the
    // target's printf output flows to the parent.
    close(pfds[0]);
    close(1);
    if (dup(pfds[1]) != 1) {
      fprintf(2, "child FAIL: dup returned wrong fd\n");
      exit(1);
    }
    close(pfds[1]);
    exec("execargv_echo", args);
    fprintf(2, "child FAIL: exec failed\n");
    exit(1);
  }

  // Parent: drain the pipe until the child closes its end at exit.
  close(pfds[1]);
  n = 0;
  while ((r = read(pfds[0], act + n, sizeof(act) - n)) > 0)
    n += r;
  close(pfds[0]);
  wait(0);
  alen = n;

  // Check: argc line parses to the expected count.
  checks++;
  p = act;
  if (alen < 8 || memcmp(p, "argc: ", 6) != 0)
    goto bad_argc;
  p += 6;
  if (parseint(&p) != NARGS || *p != '\n')
    goto bad_argc;
  printf("check 2: argc parsed as %d, expected %d\n", NARGS, NARGS);
  goto ok_argc;
bad_argc:
  printf("check 2: FAIL: could not parse argc line\n");
  mismatches++;
ok_argc:
  p++;

  // Checks: each argv[i] line parses, has the right index and length,
  // and carries the exact bytes of the constant.
  for (i = 0; i < NARGS; i++) {
    int idx, ln, blen;
    checks++;
    if (memcmp(p, "argv[", 5) != 0) {
      printf("check %d: FAIL: argv line %d missing prefix\n", checks, i);
      mismatches++;
      break;
    }
    p += 5;
    idx = parseint(&p);
    if (idx != i || memcmp(p, "] len=", 6) != 0) {
      printf("check %d: FAIL: argv line %d malformed index\n", checks, i);
      mismatches++;
      break;
    }
    p += 6;
    ln = parseint(&p);
    blen = (int)strlen(args[i]);
    if (ln != blen || memcmp(p, ": ", 2) != 0) {
      printf("check %d: FAIL: argv[%d] length %d, expected %d\n",
             checks, i, ln, blen);
      mismatches++;
      break;
    }
    p += 2;
    if (memcmp(p, args[i], blen) != 0 || p[blen] != '\n') {
      printf("check %d: FAIL: argv[%d] bytes differ\n", checks, i);
      mismatches++;
      break;
    }
    p += blen + 1;
    if (blen == 0)
      printf("check %d: argv[%d] byte-exact, len 0 (empty string)\n",
             checks, i);
    else
      printf("check %d: argv[%d] byte-exact, len %d (\"%s\")\n", checks,
             i, blen, args[i]);
  }

  // Check: the trailing line reports argv[argc] == NULL.
  checks++;
  {
    char tail[32];
    int tp = 0;
    emitstr(tail, &tp, "argv[");
    emitint(tail, &tp, NARGS);
    emitstr(tail, &tp, "] is NULL: yes\n");
    if ((p - act) + tp == alen && memcmp(p, tail, tp) == 0) {
      printf("check %d: argv[%d] is NULL, confirmed\n", checks, NARGS);
      p += tp;
    } else {
      printf("check %d: FAIL: argv[%d] NULL line wrong or missing\n",
             checks, NARGS);
      mismatches++;
    }
  }

  // Check: the whole captured output is byte-exact against the
  // expectation, with matching length.
  checks++;
  if (alen == ep && memcmp(act, exp, ep) == 0)
    printf("check %d: captured %d bytes, byte-exact against expectation\n",
           checks, alen);
  else {
    printf("check %d: FAIL: captured %d bytes vs expected %d\n",
           checks, alen, ep);
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

  // FNV-1a over the bytes actually captured, so the whole capture
  // collapses to one checkable value.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, act, alen > 0 ? alen : 0);
  printf("checksum: 0x%lx\n", sum);

  printf("argc: %d, bytes captured: %d, bytes expected: %d\n", NARGS, alen, ep);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: exec delivered the argv vector verbatim, %d args\n", NARGS);
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
