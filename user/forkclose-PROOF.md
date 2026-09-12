<!-- PROOF-HEADER
Checks: 6
Mismatches: 0
Checksum: 0xE79BD0145CA8EB94
Environment: QEMU 8.2.2
Verdict: PASS
-->

# PROOF: fork copies the descriptor table with independent close semantics (user-space test)

## What was built

A user-space test program, `user/forkclose.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Deletes any leftover `forkclose.out`, then opens it for writing
   (`O_CREATE|O_RDWR`); the open lands on fd 3.
2. Forks. Only the parent runs the checks; the child takes a silent
   branch first (no prints) so the two processes never interleave on
   the serial console and the transcript is deterministic. The child
   closes its inherited fd and exits with code 0 iff its `close`
   returned 0.
3. The parent asserts `fork` returned a positive pid, then asserts
   `wait()` reaps exactly that pid with status 0 (which is the
   evidence that the child's `close(fd)` returned 0, carried in the
   child's exit code).
4. The parent writes a fixed 32-byte known pattern through its own
   fd. If the child's close had freed the shared open file, this
   write would fail; the write returning 32 is the independence
   evidence.
5. The parent closes the fd, reopens the file read-only, and asserts
   the read returns exactly 32 bytes and matches the fixed pattern
   byte-exact.
6. Folds every measured value (child pid, write count, the 32 bytes
   read back) into an FNV-1a 64-bit checksum printed at the end.

No kernel code was changed; the test exercises xv6's existing
fork/filedup/close path from user space. This is distinct from
`user/forkoffset.c` (parent and child share one file offset through
inherited fds): here the measured property is that closing the
child's *copy* of a descriptor leaves the parent's *copy* fully
usable, which follows xv6's fork bumping each open file's reference
count (`kernel/proc.c`, `fork`) instead of moving ownership.

## Build (real log)

Built with the Ubuntu riscv64-unknown-elf 13.2.0 toolchain
(`~/workspace/toolchains/ubuntu-rv64/usr/bin` first on PATH), since
the xPack 15.2.0 `ld` mis-links xv6 user objects. Kernel was already
built; `make fs.img` rebuilt the new module and repacked the image,
exit 0 (full log in `bench-logs/forkclose-build.log`):

```
$ make fs.img
riscv64-unknown-elf-gcc -Wall -Werror ... -c -o user/forkclose.o user/forkclose.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld \
  -o user/_forkclose user/forkclose.o user/ulib.o user/usys.o \
  user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_forkclose > user/forkclose.asm
riscv64-unknown-elf-objdump -t user/_forkclose | sed ... > user/forkclose.sym
mkfs/mkfs fs.img README ... user/_waitorder user/_forkclose
nmeta 47 (boot, super, log blocks 31, inode blocks 13, bitmap blocks 1) blocks 2953 total 3000
balloc: first 2316 blocks have been allocated
```

No warnings, no errors, no mkfs complaint (2953 of 3000 blocks
used; FSSIZE 3000 still holds). The Makefile change is one line in
`UPROGS`: `$U/_forkclose`.

## Run (real QEMU console output)

QEMU emulator version 8.2.2, `-machine virt -bios none`, 128M, 3
CPUs. Three fresh boots, one run per boot, run 2026-09-11. Output
captured verbatim from the emulated serial console (raw transcripts
in `bench-logs/forkclose-run1.log` through `forkclose-run3.log`):

```
$ forkclose
check 1: open returned fd 3, as expected
check 2: fork returned child pid 4
check 3: wait returned 4 with status 0, child close returned 0
check 4: parent write through fd 3 returned 32, all 32 bytes
check 5: read back 32 bytes, matches 32 bytes written
check 6: all 32 bytes match the pattern, 0 mismatches
readback hex:
63 68 69 6c 64 2d 63 6c 6f 73 65 64 2d 62 75 74
2d 70 61 72 65 6e 74 2d 77 72 69 74 65 73 2d 79
checksum: 0xE79BD0145CA8EB94
child pid: 4, write returned: 32, bytes read: 32
checks: 6 mismatches: 0
PASS: child close did not affect the parent's descriptor, 32 bytes written and read back byte-exact
```

Everything from `$ forkclose` through `PASS` is byte-identical
across all three runs: same child pid (4), same write/read counts
(32/32), same checksum. The full boot transcripts differ only in the
SMP hart bring-up line order, which is boot noise outside the test.

Note: an earlier draft of the program had the child print its own
check 3. Two processes printing concurrently garbled one line on the
serial console in one run (interleaved characters, checks still
passed). The shipped version has the child print nothing and carry
its close result in its exit code, so the transcript is fully
deterministic.

## Reading the numbers

- `check 1` (fd 3): the first open lands on the lowest free
  descriptor past the console fds, as expected.
- `check 2` (child pid 4): pid 1 is `init`, pid 2 is `sh`, the test
  is pid 3, and the child got the next monotonic pid.
- `check 3` (wait returned 4, status 0): the parent reaped exactly
  the child it forked, and the child's exit code 0 certifies its
  `close(3)` returned 0.
- `check 4` (write returned 32): the decisive measurement. The
  child had already closed its copy of the descriptor and exited;
  the parent's write through its own copy still wrote all 32 bytes.
  Had fork moved ownership instead of bumping the reference count,
  this write would have failed.
- `check 5`/`check 6` (32 bytes read back byte-exact): the reopen
  read returned exactly the fixed pattern, so nothing about the
  file's contents was disturbed by the cross-process close.
- `checksum 0xE79BD0145CA8EB94`: FNV-1a 64-bit over the child pid
  (4, little-endian int), the write count (32), and the 32 readback
  bytes, printed by the test and independently recomputed on the
  host with Python over the same bytes: 0xe79bd0145ca8eb94, match.

Checks: 6. Mismatches: 0 across all 3 runs.

## Scope (honest)

Verified: closing a fork-inherited descriptor in the child leaves
the parent's copy of that descriptor fully usable (write succeeds,
contents intact on reopen readback), consistent with fork bumping
the open file's reference count per descriptor copy. Not verified:
close semantics for pipes or sockets (this xv6 has pipes but the
test used a regular file); descriptor-table slot reuse order after
close (the parent closes fd 3 last and the test ends); or what
happens when the *parent* closes first and the child writes (the
mirror case). One file, one fork, one fixed pattern: the whole
verification surface fits in this file.
