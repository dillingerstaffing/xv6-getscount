<!-- PROOF-HEADER
Checks: 6
Mismatches: 0
Checksum: 0xEE21A7602C5F5347
Environment: QEMU 8.2.2
Verdict: PASS
-->

# PROOF: a parent and three forked children hold four pairwise-distinct, nonzero pids (user-space test)

## What was built

A user-space test program, `user/getpidunique.c`, added to `UPROGS`
in the Makefile so it ships in `fs.img`. The program:

1. Records its own pid via `getpid()` and asserts it is nonzero.
2. Forks three children. Each child calls `getpid()` itself, writes
   the value to the parent through a pipe (children print nothing,
   so console order is fixed), and exits.
3. Asserts the three `fork()` returns to the parent are all positive.
4. Reads the three child `getpid()` reports from the pipe and
   asserts all three arrived and are nonzero.
5. Waits for all three children and asserts `wait()` handed back
   exactly the three forked pids.
6. Sorts the pid arrays (so nothing depends on the order the
   scheduler ran the children in), then asserts each child's own
   `getpid()` equals the pid `fork()` reported for it, and that the
   four pids (parent plus three children) are pairwise distinct and
   nonzero.

No kernel code was changed; the test exercises xv6's existing
fork/getpid/wait path from user space. This is distinct from
`user/forkisolation.c` (fork copies memory pages) and
`user/waitreaps.c` (wait reaps exactly once): here the measured
property is the pid allocator's uniqueness, observed through
`getpid()` on both sides of the fork.

## Build (real log)

Built with the Ubuntu riscv64-unknown-elf 13.2.0 toolchain
(`~/workspace/toolchains/ubuntu-rv64/usr/bin` first on PATH), since
the xPack 15.2.0 `ld` mis-links xv6 user objects. Kernel was already
built; `make fs.img` rebuilt the new module and repacked the image,
exit 0:

```
$ make fs.img
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O \
  -fno-omit-frame-pointer -ggdb -gdwarf-2 -ffile-prefix-map=.=. \
  -march=rv64gc -std=gnu99 -MD -mcmodel=medany -ffreestanding \
  -fno-common -nostdlib [builtin suppressions] -Wno-main \
  -fno-stack-protector -fno-pie -no-pie \
  -c -o user/getpidunique.o user/getpidunique.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld \
  -o user/_getpidunique user/getpidunique.o user/ulib.o user/usys.o \
  user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_getpidunique > user/getpidunique.asm
riscv64-unknown-elf-objdump -t user/_getpidunique | sed ... > user/getpidunique.sym
mkfs/mkfs fs.img README ... user/_forkoffset user/_getpidunique
nmeta 47 (boot, super, log blocks 31, inode blocks 13, bitmap blocks 1) blocks 2953 total 3000
```

No warnings, no errors. The Makefile change is one line in `UPROGS`:
`$U/_getpidunique`.

## Run (real QEMU console output)

QEMU emulator version 8.2.2, `-machine virt -bios none`, 128M, 3
CPUs. Three fresh boots, one run per boot. The test writes nothing
to the filesystem, so the same fresh `fs.img` served all three runs.
Run 2026-09-11. Output captured verbatim from the emulated serial
console:

```
$ getpidunique
check 1: parent getpid() = 3, nonzero
check 2: fork handed the parent pids 4 5 6, all positive
check 3: pipe delivered 3 nonzero getpid() reports: 4 5 6
check 4: each child's getpid() matches the pid fork() reported: 4 5 6
check 5: wait() reaped exactly the three forked pids: 4 5 6
check 6: 4 pids are pairwise distinct and nonzero: parent 3, children 4 5 6
checksum: 0xEE21A7602C5F5347
pid-unique results: parent=3 children=4,5,6
checks: 6 mismatches: 0
PASS: parent and three children hold four pairwise-distinct nonzero pids
```

Runs 2 and 3 are byte-identical to run 1: same parent pid (3),
same child pids (4, 5, 6), same checksum.

## Reading the numbers

- `check 1` (parent pid 3): pid 1 is `init`, pid 2 is `sh`; the test
  is the third live process, so `getpid()` returning 3 is exactly
  the allocator's expected value, and nonzero.
- `check 2` (fork pids 4, 5, 6): the allocator handed out the next
  three monotonic pids, all positive.
- `check 3` (pipe reports 4, 5, 6): each child measured its own pid
  with `getpid()` and all three reports arrived nonzero. The pipe,
  not console printing, carries the child measurements, so the
  transcript is scheduling-independent.
- `check 4` (child getpid() == fork() pid): the pid the allocator
  assigned (observed by the parent at fork time) is the same pid
  the child's own `getpid()` returns. Both views agree, 3 of 3.
- `check 5` (wait reaped 4, 5, 6): `wait()` handed back exactly the
  three forked pids, no more, no fewer.
- `check 6` (pairwise distinct): the 6 pairwise comparisons among
  {3, 4, 5, 6} all differ; no pid is shared and none is 0.
- `checksum 0xEE21A7602C5F5347`: FNV-1a 64-bit over the four
  measured pids (sorted order, scheduling-independent), printed by
  the test and independently recomputed on the host with Python
  over the little-endian bytes of (3, 4, 5, 6) with the same offset
  basis and prime: 0xee21a7602c5f5347, match.

Checks: 6. Mismatches: 0 across all 3 runs.

## Scope (honest)

Verified: the pid allocator hands each new process a distinct
nonzero pid, `getpid()` agrees with `fork()`'s report on both sides,
and `wait()` reaps exactly the forked children. Not verified: pid
wrap-around or reuse after many forks (the allocator here only moved
forward), or pids of processes that never ran (sleeping/zombie
visibility is covered by `nprocs`). Four live processes, one fixed
fork pattern: the whole verification surface fits in this file.
