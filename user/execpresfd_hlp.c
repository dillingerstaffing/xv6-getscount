// execpresfd_hlp: the exec destination for the execpresfd test.
//
// It converts argv[1] back to an integer file descriptor, writes a
// FIXED 64-byte pattern to that descriptor, reports what it saw on
// stdout, closes the descriptor, and exits. If the descriptor did
// not survive exec, the write would fail (or land on the wrong
// descriptor) and the runner would see short or garbage bytes. The
// runner (user/execpresfd.c) compares the 64 bytes byte-exact against
// the same pattern built in memory, and checks the reported fd
// number against the number the child passed.
//
// The 64-byte pattern is fully specified here: byte i of the pattern,
// i in 0..63, is (i * 37 + 11) mod 256. The runner builds the
// identical bytes from the same formula, so any difference shows up
// as a mismatch, never as a judgment call.
//
// No kernel code was changed; the test exercises xv6's existing exec
// path (which replaces the address space but keeps the caller's open
// file table) from user space.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PATLEN 64

static void
build_pattern(char *b)
{
  int i;

  for (i = 0; i < PATLEN; i++)
    b[i] = (char)((i * 37 + 11) & 0xff);
}

// Parse a decimal string fully; -1 on any malformed input.
static int
parseint(char *p)
{
  int v = 0;

  if (*p < '0' || *p > '9')
    return -1;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (*p - '0');
    p++;
  }
  if (*p != 0)
    return -1;
  return v;
}

int
main(int argc, char *argv[])
{
  int fd, w, n;
  char pat[PATLEN];

  if (argc != 2) {
    printf("helper FAIL: argc %d, expected 2\n", argc);
    exit(1);
  }
  fd = parseint(argv[1]);
  if (fd < 0 || fd > 15) {
    printf("helper FAIL: bad fd string \"%s\"\n", argv[1]);
    exit(1);
  }
  build_pattern(pat);
  w = 0;
  while (w < PATLEN) {
    n = write(fd, pat + w, PATLEN - w);
    if (n <= 0) {
      printf("helper FAIL: write to fd %d failed at byte %d\n", fd, w);
      exit(1);
    }
    w += n;
  }
  printf("helper fd: %d, wrote %d bytes\n", fd, w);
  close(fd);
  exit(0);
}
