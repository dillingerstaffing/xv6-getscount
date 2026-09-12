// getpidunique: verify that a parent and its three forked children
// hold four pairwise-distinct, nonzero pids.
//
// Fundamental truth: each new process gets a distinct pid from xv6's
// monotonic pid allocator, and getpid() reads back the calling
// process's own entry. Four live processes therefore report four
// pairwise-distinct pids. If two processes ever shared a pid, or any
// reported 0, the allocator or getpid would be broken.
//
// Checks: the parent's own getpid() is nonzero; each fork() hands
// the parent a positive child pid; the pid each child reports via
// getpid() (through a pipe) equals the pid fork() gave the parent
// for it; wait() hands back exactly the three forked pids; the four
// pids are pairwise distinct. The children write their pids through
// the pipe and print nothing, and the parent sorts the pids before
// printing, so the transcript is byte-identical run to run. PASS
// prints only when every check holds with 0 mismatches.
//
// No kernel code was changed; the test exercises xv6's existing
// fork/getpid/wait path from user space.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

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

// Insertion sort of 3 ints: puts the pids in a fixed order so the
// transcript does not depend on scheduling order.
static void
sort3(int *a)
{
  for (int i = 1; i < 3; i++) {
    int key = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > key) {
      a[j + 1] = a[j];
      j--;
    }
    a[j + 1] = key;
  }
}

int
main(int argc, char *argv[])
{
  int checks = 0, mismatches = 0;
  int p[2];
  int mypid;
  int forked[3];    // pids fork() returned to the parent
  int reported[3];  // pids the children measured with getpid(), via pipe
  int reaped[3];    // pids wait() returned
  int status;
  int got;
  uint64 sum;

  (void)argc;
  (void)argv;

  if (pipe(p) < 0) {
    printf("FAIL: pipe returned -1\n");
    exit(1);
  }

  mypid = getpid();

  checks++;
  if (mypid > 0)
    printf("check 1: parent getpid() = %d, nonzero\n", mypid);
  else {
    printf("check 1: FAIL: parent getpid() = %d, expected nonzero\n",
           mypid);
    mismatches++;
  }

  // Fork three children. Each child measures its own pid with
  // getpid() and hands it to the parent through the pipe, then
  // exits. Children print nothing, so console order is fixed.
  for (int i = 0; i < 3; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("FAIL: fork %d returned %d\n", i + 1, pid);
      exit(1);
    }
    if (pid == 0) {
      int mine = getpid();
      close(p[0]);
      if (write(p[1], (char *)&mine, sizeof(mine)) != sizeof(mine))
        exit(2);
      close(p[1]);
      exit(0);
    }
    forked[i] = pid;
  }
  close(p[1]);

  checks++;
  if (forked[0] > 0 && forked[1] > 0 && forked[2] > 0)
    printf("check 2: fork handed the parent pids %d %d %d, all positive\n",
           forked[0], forked[1], forked[2]);
  else {
    printf("check 2: FAIL: fork pids %d %d %d, expected all positive\n",
           forked[0], forked[1], forked[2]);
    mismatches++;
  }

  // Collect the three getpid() reports from the pipe.
  got = 0;
  while (got < 3) {
    int r = read(p[0], ((char *)reported) + got * sizeof(int),
                 (3 - got) * sizeof(int));
    if (r <= 0)
      break;
    got += r / sizeof(int);
  }
  close(p[0]);

  // Sort the arrival-ordered reports now so the transcript does not
  // depend on the order the scheduler ran the children in.
  sort3(reported);

  checks++;
  if (got == 3 && reported[0] > 0 && reported[1] > 0 &&
      reported[2] > 0)
    printf("check 3: pipe delivered 3 nonzero getpid() reports: %d %d %d\n",
           reported[0], reported[1], reported[2]);
  else {
    printf("check 3: FAIL: pipe delivered %d reports: %d %d %d, expected 3 nonzero\n",
           got, reported[0], reported[1], reported[2]);
    mismatches++;
  }

  // Reap all three children.
  for (int i = 0; i < 3; i++)
    reaped[i] = wait(&status);

  // Sort the remaining pid arrays so comparisons and output do
  // not depend on the order the scheduler ran the children in
  // (reported was sorted above, before it was printed).
  sort3(forked);
  sort3(reaped);

  checks++;
  if (reported[0] == forked[0] && reported[1] == forked[1] &&
      reported[2] == forked[2])
    printf("check 4: each child's getpid() matches the pid fork() reported: %d %d %d\n",
           reported[0], reported[1], reported[2]);
  else {
    printf("check 4: FAIL: child getpid() reports %d %d %d, fork() said %d %d %d\n",
           reported[0], reported[1], reported[2],
           forked[0], forked[1], forked[2]);
    mismatches++;
  }

  checks++;
  if (reaped[0] == forked[0] && reaped[1] == forked[1] &&
      reaped[2] == forked[2])
    printf("check 5: wait() reaped exactly the three forked pids: %d %d %d\n",
           reaped[0], reaped[1], reaped[2]);
  else {
    printf("check 5: FAIL: wait() reaped %d %d %d, forked were %d %d %d\n",
           reaped[0], reaped[1], reaped[2],
           forked[0], forked[1], forked[2]);
    mismatches++;
  }

  // The four pids must be pairwise distinct and nonzero.
  int distinct = 1;
  if (mypid <= 0)
    distinct = 0;
  {
    int all[4];
    all[0] = mypid;
    all[1] = forked[0];
    all[2] = forked[1];
    all[3] = forked[2];
    for (int i = 0; i < 4; i++)
      for (int j = i + 1; j < 4; j++)
        if (all[i] == all[j])
          distinct = 0;
  }

  checks++;
  if (distinct)
    printf("check 6: 4 pids are pairwise distinct and nonzero: parent %d, children %d %d %d\n",
           mypid, forked[0], forked[1], forked[2]);
  else {
    printf("check 6: FAIL: pids not pairwise distinct/nonzero: parent %d, children %d %d %d\n",
           mypid, forked[0], forked[1], forked[2]);
    mismatches++;
  }

  // FNV-1a over the four measured pids (sorted order, so it is
  // scheduling-independent): the uniqueness evidence collapsed to
  // one checkable value.
  sum = 1469598103934665603UL; // FNV offset basis
  sum = fnv1a64bytes(sum, (char *)&mypid, sizeof(mypid));
  sum = fnv1a64bytes(sum, (char *)forked, sizeof(forked));
  printf("checksum: 0x%lx\n", sum);

  printf("pid-unique results: parent=%d children=%d,%d,%d\n",
         mypid, forked[0], forked[1], forked[2]);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: parent and three children hold four pairwise-distinct nonzero pids\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
