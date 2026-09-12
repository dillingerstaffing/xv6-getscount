<!-- PROOF-HEADER
Checks: 6
Mismatches: 0
Checksum: 0x8212203C396064D4
Environment: QEMU 8.2.2
Verdict: PASS
-->

# PROOF: successive forks hand the parent strictly increasing pids (user-space test)

## What was built

A user-space test program, `user/forkpidorder.c`, added to `UPROGS`
in the Makefile so it ships in `fs.img`. The program:

1. Forks three children in strict program order, recording each pid
   returned to the parent at the moment `fork()` returns, so no
   scheduling can reorder the observations. Each child exits
   immediately and prints nothing.
2. Asserts the three forks returned positive pids (check 1), that
   p1 < p2 (check 2) and p2 < p3 (check 3), and that all three are
   pairwise distinct (check 4).
3. Reaps the three children with `wait()` and asserts the three
   returned pids are exactly p1, p2, p3 (check 5).
4. Forks a fourth child after all three zombies were reaped and
   asserts p4 > p3 (check 6): the control that proves the monotonic
   counter was not rewound when the zombies were freed.
5. Folds all seven measured values (p1..p4, the three wait results)
   into an FNV-1a 64-bit checksum printed at the end.

No kernel code was changed; the test exercises xv6's existing
fork/wait path from user space. The allocation rule it evidences is
`kernel/proc.c`, `allocpid()`: `pid = nextpid; nextpid = nextpid +
1;` under `pid_lock`, with `int nextpid = 1;` at the top of the
file (lines 15 and 94-103). Every pid the test observes is that
counter read back through `fork()`.

## Build (real log)

Built with the Ubuntu riscv64-unknown-elf 13.2.0 toolchain
(`~/workspace/toolchains/ubuntu-rv64/usr/bin` first on PATH), since
the xPack 15.2.0 `ld` mis-links xv6 user objects. Kernel was already
built; `make fs.img` rebuilt the new module and repacked the image,
exit 0 (full log in `bench-logs/forkpidorder-build.log`):

```
$ make fs.img
riscv64-unknown-elf-gcc -Wall -Werror ... -c -o user/forkpidorder.o user/forkpidorder.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld \
  -o user/_forkpidorder user/forkpidorder.o user/ulib.o user/usys.o \
  user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_forkpidorder > user/forkpidorder.asm
riscv64-unknown-elf-objdump -t user/_forkpidorder | sed ... > user/forkpidorder.sym
mkfs/mkfs fs.img README ... user/_forkclose user/_forkpidorder user/_sbrksubpage
nmeta 47 (boot, super, log blocks 31, inode blocks 13, bitmap blocks 1) blocks 2953 total 3000
balloc: first 2401 blocks have been allocated
```

No warnings, no errors, no mkfs complaint (2953 of 3000 blocks
used; FSSIZE 3000 still holds). The Makefile change is one line in
`UPROGS`: `$U/_forkpidorder`.

## Run (real QEMU console output)

QEMU emulator version 8.2.2, `-machine virt -bios none`, 128M, 3
CPUs. Three fresh boots, one run per boot, run 2026-09-12. Output
captured verbatim from the emulated serial console (raw transcripts
in `bench-logs/forkpidorder-run1.log` through
`forkpidorder-run3.log`):

```
$ forkpidorder
check 1: three forks returned positive pids 4 5 6
check 2: p1=4 < p2=5, second fork got the larger pid
check 3: p2=5 < p3=6, third fork got the larger pid
check 4: pids 4 5 6 are pairwise distinct
check 5: three wait()s reaped exactly the forked pids 4 5 6
check 6: fourth fork after the reaps returned pid 7 > p3=6, the allocator kept advancing
checksum: 0x8212203C396064D4
fork-pid results: p1=4 p2=5 p3=6 p4=7 reaped=4,5,6
checks: 6 mismatches: 0
PASS: successive forks returned strictly increasing pids 4 < 5 < 6, and a post-reap fork advanced past them to 7
```

Everything from `$ forkpidorder` through `PASS` is byte-identical
across all three runs: same pids (4, 5, 6, 7), same checksum. The
full boot transcripts differ only in the SMP hart bring-up line
order, which is boot noise outside the test. The child prints
nothing, so no console interleaving is possible.

## Reading the numbers

- `check 1` (pids 4, 5, 6): pid 1 is `init`, pid 2 is `sh`, pid 3
  is the test itself; the first fork handed out the next counter
  value, 4.
- `check 2`/`check 3` (4 < 5 < 6): the parent's observations in
  strict program order follow the counter's advance, one per
  allocproc. This rules out a batch-ordered or reused-pid scheme,
  because each fork incremented the counter before returning.
- `check 4` (pairwise distinct): the counter never hands the same
  value to two live allocations.
- `check 5` (reaped 4, 5, 6): wait() returned exactly the pids the
  forks produced, confirming the parent reaped the children it
  created and no other process intervened.
- `check 6` (p4 = 7 > p3): the decisive control. All three
  children had already been reaped, yet the fourth fork still got a
  larger pid. Freeing a zombie does not rewind `nextpid`, so the
  allocator is monotonic across the whole process table, not
  per-batch.
- `checksum 0x8212203C396064D4`: FNV-1a 64-bit over the seven
  measured int values (p1..p4, w1..w3, little-endian), printed by
  the test and independently recomputed on the host with Python
  over the same bytes: 0x8212203c396064d4, match.

Checks: 6. Mismatches: 0 across all 3 runs.

## Scope (honest)

Verified: in this xv6 build, successive `fork()` calls return
strictly increasing pids to the parent, and the pid counter keeps
advancing past a fully reaped batch, consistent with `allocpid()`
drawing from and incrementing a single global `nextpid` under
`pid_lock`. Not verified: pid wraparound behavior (nextpid is a
32-bit int; reaching 2^31 would need billions of forks and is out
of scope for this test), concurrent forks racing for pids (3 CPUs
ran the boot, but the forks here are sequential in one parent), or
pid reuse after zombie reap under memory pressure (the control
shows the counter advanced, not that reaped pids are held back or
recycled).
