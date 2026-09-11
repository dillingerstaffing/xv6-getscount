// execargv_echo: the exec destination for the execargv test.
//
// It does no verification itself; it just reports what exec handed it:
// the argument count, each argv[i] with its byte length, and whether
// argv[argc] is NULL. The runner (user/execargv.c) compares this output
// byte-exact against an expectation built from the same constants, so
// the target stays a dumb echo and every judgment lives in the runner.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int i;

  printf("argc: %d\n", argc);
  for (i = 0; i < argc; i++)
    printf("argv[%d] len=%d: %s\n", i, (int)strlen(argv[i]), argv[i]);
  if (argv[argc] == 0)
    printf("argv[%d] is NULL: yes\n", argc);
  else
    printf("argv[%d] is NULL: no\n", argc);
  exit(0);
}
