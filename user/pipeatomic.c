// pipeatomic: verify that concurrent fixed-size writes into one xv6
// pipe land as intact records with no byte interleaving.
//
// The kernel's pipewrite holds the pipe lock for the whole write
// call, so two writes cannot interleave byte-wise: one writer's
// bytes always form one contiguous run in the stream. This test
// exercises that from user space with 4 children, each writing one
// 128-byte record filled with a single label byte ('0'..'3'), all
// racing into the same pipe. The parent reads all 512 bytes and
// checks the stream is four 128-byte uniform blocks in SOME order,
// with each label present exactly once. Any interleaving (a block
// whose bytes are not all identical, or a duplicated/missing label)
// is a mismatch.
//
// Each child writes in exactly one write() call of 128 bytes. 4*128
// = 512 = PIPESIZE, so no writer ever blocks on a full buffer and no
// reader blocking is needed; contention is real (all children run
// concurrently on a 3-hart machine) but each write is one atomic
// pipewrite call.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define NCHILD 4
#define RECSZ 128
#define TOTAL (NCHILD * RECSZ)   // 512 == PIPESIZE
#define CHUNKSZ 64

// FNV-1a 64-bit over raw bytes: the standard offset basis and prime,
// one fold per byte.
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
  int p[2];
  int checks = 0, mismatches = 0;
  int i, j, n, total, status, wpid, w;
  int got[NCHILD];          // fork pids of the children
  int labels[NCHILD];       // per-block label bytes, in stream order
  int seen[NCHILD];         // which labels arrived, indexed label-'0'
  int uniform;
  char wbuf[RECSZ];
  char rbuf[CHUNKSZ];
  char gotbuf[TOTAL];       // the full 512-byte stream, in arrival order
  uint64 sum;

  (void)argc;
  (void)argv;

  // Check 1: pipe creation hands back two distinct valid fds.
  checks++;
  if (pipe(p) == 0 && p[0] >= 0 && p[1] >= 0 && p[0] != p[1])
    printf("check 1: pipe() ok, read fd %d, write fd %d\n", p[0], p[1]);
  else {
    printf("check 1: FAIL: pipe() returned p[0]=%d p[1]=%d\n", p[0], p[1]);
    mismatches++;
  }

  // Fork the 4 writers. Each child closes the read end, writes its
  // one 128-byte record in a single write() call, closes the write
  // end, and exits with 0 only if the write returned exactly 128.
  // Children print nothing, so the console transcript stays
  // deterministic except for the block order, which is the point.
  w = 1;
  for (i = 0; i < NCHILD; i++) {
    int pid = fork();
    if (pid < 0) {
      w = 0;
      got[i] = -1;
    } else if (pid == 0) {
      int k, wret;
      close(p[0]);
      for (k = 0; k < RECSZ; k++)
        wbuf[k] = (char)('0' + i);
      wret = write(p[1], wbuf, RECSZ);
      close(p[1]);
      exit(wret == RECSZ ? 0 : 1);
    } else {
      got[i] = pid;
    }
  }

  // Check 2: all 4 forks returned positive pids.
  checks++;
  if (w)
    printf("check 2: 4 children forked\n");
  else {
    printf("check 2: FAIL: a fork returned -1\n");
    mismatches++;
  }

  // The parent closes its write end now that the children all hold
  // theirs (forked fds share the same file objects, so the pipe only
  // reports EOF once every writer, parent included, has closed).
  if (close(p[1]) != 0) {
    printf("parent close of write end failed\n");
    exit(1);
  }

  // Read the whole 512 bytes in 64-byte chunks.
  sum = 1469598103934665603UL; // FNV offset basis
  total = 0;
  while (total < TOTAL) {
    n = read(p[0], rbuf, CHUNKSZ);
    if (n <= 0) {
      printf("read failed or early EOF at %d bytes\n", total);
      exit(1);
    }
    for (i = 0; i < n; i++)
      gotbuf[total + i] = rbuf[i];
    sum = fnv1a64bytes(sum, rbuf, n);
    total += n;
  }

  // Check 3: exactly 512 bytes arrived before EOF.
  checks++;
  if (total == TOTAL)
    printf("check 3: read %d bytes before EOF, matches 4*128 written\n",
           total);
  else {
    printf("check 3: FAIL: read %d bytes, expected %d\n", total, TOTAL);
    mismatches++;
  }

  // Check 4: EOF is sticky: another read still returns 0.
  checks++;
  n = read(p[0], rbuf, CHUNKSZ);
  if (n == 0)
    printf("check 4: read after EOF returned 0\n");
  else {
    printf("check 4: FAIL: read after EOF returned %d\n", n);
    mismatches++;
  }

  // Check 5: the 4 blocks of 128 bytes are each byte-uniform. A block
  // whose bytes are not all identical is an interleaving and fails.
  checks++;
  uniform = 1;
  for (i = 0; i < NCHILD; i++) {
    labels[i] = gotbuf[i * RECSZ] & 0xff;
    for (j = 1; j < RECSZ; j++) {
      if ((gotbuf[i * RECSZ + j] & 0xff) != labels[i]) {
        uniform = 0;
        break;
      }
    }
  }
  if (uniform)
    printf("check 5: all 4 128-byte blocks byte-uniform\n");
  else {
    printf("check 5: FAIL: a block has non-uniform bytes (interleaved)\n");
    mismatches++;
  }

  // Check 6: the four block labels are exactly '0','1','2','3', each
  // once. A duplicated or missing label is a lost or split record.
  checks++;
  for (i = 0; i < NCHILD; i++)
    seen[i] = 0;
  n = 0; // reuse as "out of range labels" counter
  for (i = 0; i < NCHILD; i++) {
    if (labels[i] >= '0' && labels[i] <= '3')
      seen[labels[i] - '0']++;
    else
      n++;
  }
  for (i = 0; i < NCHILD; i++)
    if (seen[i] != 1)
      n++;
  if (n == 0) {
    printf("check 6: labels present exactly once each:");
    for (i = 0; i < NCHILD; i++)
      printf(" %c", labels[i]);
    printf(" (stream order for this run)\n");
  } else {
    printf("check 6: FAIL: label multiset wrong (out-of-range or duplicated/missing)\n");
    mismatches++;
  }

  // Check 7: the read end closes cleanly.
  checks++;
  if (close(p[0]) == 0)
    printf("check 7: read end closed\n");
  else {
    printf("check 7: FAIL: close of read end failed\n");
    mismatches++;
  }

  // Check 8: all 4 children reaped, each with exit status 0, meaning
  // every child's single write() returned exactly 128 bytes.
  checks++;
  w = 1;
  for (i = 0; i < NCHILD; i++) {
    if (got[i] < 0) {
      w = 0;
      continue;
    }
    wpid = wait(&status);
    if (wpid <= 0 || status != 0)
      w = 0;
  }
  if (w)
    printf("check 8: all 4 children reaped with exit status 0 (each write returned 128)\n");
  else {
    printf("check 8: FAIL: a wait returned <= 0 or a child exited nonzero\n");
    mismatches++;
  }

  printf("checksum: 0x%lx\n", sum);
  printf("bytes written: %d, bytes read: %d\n", TOTAL, total);
  printf("checks: %d mismatches: %d\n", checks, mismatches);
  if (mismatches == 0)
    printf("PASS: 4 concurrent 128-byte writes landed as 4 intact uniform records, no interleaving\n");
  else
    printf("FAIL: %d mismatches\n", mismatches);
  exit(0);
}
