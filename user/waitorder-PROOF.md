<!-- PROOF-HEADER
Checks: 7
Mismatches: 0
Checksum: 0x5BBF2DC7084738FF
Environment: QEMU 8.2.2
Verdict: PASS
-->

# PROOF: wait() reaps several already-zombied children in pid order, not exit order (user-space test)

## What was built

A user-space test program, `user/waitorder.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Reads the `uptime()` tick counter before any fork; the value is
   inherited by the children and used as the base for absolute exit
   deadlines. (This xv6 variant exposes no user-space `sleep` call,
   so ordering is driven by the tick counter, the same technique
   `user/killreap.c` documents.)
2. Forks three children in order, recording the fork-return pids
   p1, p2, p3.
3. Child 1 spins until tick base+30, prints `exit <pid>`, exits with
   code 11. Child 2 spins until base+20, prints, exits with code 22.
   Child 3 spins until base+10, prints, exits with code 33. The
   absolute deadlines force the exit order p3, p2, p1, the exact
   reverse of the fork order, no matter when each child was forked.
4. The parent prints the fork order, spins until base+40 (all three
   children are zombies when the first `wait()` runs), then calls
   `wait()` three times.
5. Asserts the three `wait()` returns are exactly (p1, 11),
   (p2, 22), (p3, 33) in that order: each reaped pid must equal the
   corresponding fork-return pid, and each reaped status must equal
   that child's own exit code.
6. Folds every measured value (three fork pids, three reaped pids,
   three statuses) into an FNV-1a 64-bit checksum printed at the end.

The child exit prints are the exit-order evidence; the parent's
checks are the reap-order evidence. No kernel code was changed; the
test exercises xv6's existing exit/wait path from user space. This
is distinct from `user/waitreaps.c` (one child reaped exactly once,
then -1): here the measured property is the reap *order* for
several zombies, which follows xv6's scan of the process table from
its start, returning the first ZOMBIE child it finds
(`kernel/proc.c`, `wait`).

## Build (real log)

Built with the Ubuntu riscv64-unknown-elf 13.2.0 toolchain
(`~/workspace/toolchains/ubuntu-rv64/usr/bin` first on PATH), since
the xPack 15.2.0 `ld` mis-links xv6 user objects. Kernel was already
built; `make fs.img` rebuilt the new module and repacked the image,
exit 0 (full log in `bench-logs/waitorder-build.log`):

```
$ make fs.img
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O \
  -fno-omit-frame-pointer -ggdb -gdwarf-2 -ffile-prefix-map=.=. \
  -march=rv64gc -std=gnu99 -MD -mcmodel=medany -ffreestanding \
  -fno-common -nostdlib [builtin suppressions] -Wno-main \
  -fno-stack-protector -fno-pie -no-pie \
  -c -o user/waitorder.o user/waitorder.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld \
  -o user/_waitorder user/waitorder.o user/ulib.o user/usys.o \
  user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_waitorder > user/waitorder.asm
riscv64-unknown-elf-objdump -t user/_waitorder | sed ... > user/waitorder.sym
mkfs/mkfs fs.img README ... user/_getpidunique user/_waitorder
nmeta 47 (boot, super, log blocks 31, inode blocks 13, bitmap blocks 1) blocks 2953 total 3000
balloc: first 2270 blocks have been allocated
balloc: write bitmap block at sector 46
```

No warnings, no errors. The Makefile change is one line in `UPROGS`:
`$U/_waitorder`.

## Run (real QEMU console output)

QEMU emulator version 8.2.2, `-machine virt -bios none`, 128M, 3
CPUs. Three fresh boots, one run per boot, run 2026-09-11. Output
captured verbatim from the emulated serial console (raw transcripts
in `bench-logs/waitorder-run1.log` through `waitorder-run3.log`):

```
$ waitorder
fork order: p1=4 p2=5 p3=6
check 1: three forks returned positive pids 4 5 6
exit 6
exit 5
exit 4
check 2: first wait() returned 4, the first forked child
check 3: first reaped status 11, the first child's exit code
check 4: second wait() returned 5, the second forked child
check 5: second reaped status 22, the second child's exit code
check 6: third wait() returned 6, the third forked child
check 7: third reaped status 33, the third child's exit code
checksum: 0x5BBF2DC7084738FF
wait-order results: fork=4,5,6 reap=4,5,6 status=11,22,33
checks: 7 mismatches: 0
PASS: wait() reaped three zombies in pid order, not exit order
```

Everything from `$ waitorder` onward is byte-identical across all
three runs: same parent pid (3), same fork pids (4, 5, 6), same exit
print order (6, 5, 4), same reap sequence (4, 5, 6) with statuses
(11, 22, 33), same checksum. The full boot transcripts differ only
in the SMP hart bring-up lines (`hart 1 starting` vs `hart 2
starting` order), which is boot noise outside the test.

## Reading the numbers

- `fork order: p1=4 p2=5 p3=6`: pid 1 is `init`, pid 2 is `sh`; the
  test is pid 3 and the allocator handed out the next three
  monotonic pids in fork order.
- `exit 6`, `exit 5`, `exit 4`: the children exited in reverse fork
  order, forced by the absolute uptime deadlines (base+10, base+20,
  base+30). This is the exit-order evidence; if reap order followed
  exit order, the first `wait()` would have returned 6.
- `check 2/4/6` (`wait()` returned 4, 5, 6): with all three children
  already zombies, the three `wait()` calls returned the children in
  ascending pid order, the fork order, not the exit order. This is
  what xv6's scan-from-the-start of the process table predicts.
- `check 3/5/7` (statuses 11, 22, 33): each reaped zombie carried
  its own child's exit code, so the reap sequence also paired each
  pid with its correct status, not just the pid set.
- `checksum 0x5BBF2DC7084738FF`: FNV-1a 64-bit over the nine
  measured ints in fixed order (p1, p2, p3, w1, w2, w3, s1, s2, s3),
  printed by the test and independently recomputed on the host with
  Python over the little-endian bytes of (4, 5, 6, 4, 5, 6, 11, 22,
  33) with the same offset basis and prime: 0x5bbf2dc7084738ff,
  match.

Checks: 7. Mismatches: 0 across all 3 runs.

## Scope (honest)

Verified: when several children are already zombies, xv6's `wait()`
reaps them in ascending pid (process-table) order, not in the order
they exited, and each reap carries that child's own exit status. Not
verified: reap order when children exit at different times while the
parent waits between exits (the parent here waits only after all
three are zombies); `waitpid`-style selective reaping (this xv6
exposes none); or pid reuse order after pids wrap. Three children,
one fixed fork pattern, fixed absolute deadlines: the whole
verification surface fits in this file.
