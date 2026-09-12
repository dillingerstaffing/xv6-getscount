<!-- PROOF-HEADER
Checks: 7
Mismatches: 0
Checksum: 0x54D6734CAFB9C078
Environment: QEMU 8.2.2
Verdict: PASS
-->

# PROOF: a write past a full 512-byte xv6 pipe blocks until a reader drains it (user-space test)

## What was built

A user-space test program, `user/pipebuf.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Creates a pipe (check 1: two distinct fds) and writes a fixed
   512-byte pattern into it with one `write()` call (check 2: must
   return 512, so the buffer is exactly full; PIPESIZE is 512 in
   `kernel/pipe.c`).
2. Spins until the uptime tick counter advances, then captures t0
   microseconds after the fresh tick edge, and forks the reader
   child (check 3: positive child pid). t0 is captured BEFORE the
   fork and inherited through it, so both sides share one absolute
   deadline, t0 + 40. The parent's second write is issued in tick
   t0, microseconds after the fork.
3. The child closes the write end, sleeps until tick t0 + 40
   (guaranteeing the parent's second write is already asleep inside
   `pipewrite` when reading starts), then drains the whole 513-byte
   stream, comparing every byte against `pat()` at its absolute
   stream index. It folds the received bytes into an FNV-1a
   checksum, holds its console print until t0 + 55 (so its print can
   never interleave with the parent's check 4/5 prints and the
   transcript order is fixed), and exits 0 only if the stream was
   byte-exact.
4. The parent writes 1 more byte (check 4: must return 1), records
   uptime() immediately before and after that write, and asserts the
   elapsed ticks are >= 30 (check 5). The child did not touch the
   pipe until tick t0 + 40, so any elapsed under 30 would mean the
   write returned early instead of waiting for the drain.
5. The parent closes the write end, calls `wait()`, and asserts it
   returns the child's pid (check 6) with exit status 0 (check 7:
   the child's own byte-by-byte verification of all 513 bytes
   passed).
6. The parent folds `pat(0)..pat(512)` independently and prints the
   expected stream checksum next to the child's received-bytes
   checksum, and folds the structural values (child pid, both write
   returns, reaped pid, child status) into a second FNV-1a checksum.

The 513-byte pattern is a pure function of the index: printable
ASCII elsewhere, 0x00 at index 256 and 0xFF at index 512, so the
comparison must be length-driven. No kernel code was changed; the
test exercises xv6's existing pipe blocking path from user space.

## Build (real log)

Built with the Ubuntu riscv64-unknown-elf 13.2.0 toolchain
(`~/workspace/toolchains/ubuntu-rv64/usr/bin` first on PATH), since
the xPack 15.2.0 `ld` mis-links xv6 user objects. Kernel was already
built; `make fs.img` rebuilt the new module and repacked the image,
exit 0 (full log in `bench-logs/pipebuf-build.log`):

```
$ make fs.img
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O \
  -fno-omit-frame-pointer -ggdb -gdwarf-2 -ffile-prefix-map=.=. \
  -march=rv64gc -std=gnu99 -MD -mcmodel=medany -ffreestanding \
  -fno-common -nostdlib [builtin suppressions] -Wno-main \
  -fno-stack-protector -fno-pie -no-pie \
  -c -o user/pipebuf.o user/pipebuf.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld \
  -o user/_pipebuf user/pipebuf.o user/ulib.o user/usys.o \
  user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_pipebuf > user/pipebuf.asm
riscv64-unknown-elf-objdump -t user/_pipebuf | sed ... > user/pipebuf.sym
mkfs/mkfs fs.img README ... user/_sbrksubpage user/_pipebuf
nmeta 47 (boot, super, log blocks 31, inode blocks 13, bitmap blocks 1) blocks 2953 total 3000
balloc: first 2448 blocks have been allocated
balloc: write bitmap block at sector 46
```

No warnings, no errors. The Makefile change is one line in `UPROGS`:
`$U/_pipebuf`.

## Run (real QEMU console output)

QEMU emulator version 8.2.2, `-machine virt -bios none`, 128M, 3
CPUs. Three fresh boots, one run per boot, run 2026-09-12. Output
captured verbatim from the emulated serial console (raw transcripts
in `bench-logs/pipebuf-run1.log` through `pipebuf-run3.log`):

```
$ pipebuf
check 1: pipe() ok, read fd 3, write fd 4
check 2: first write() returned 512, pipe now exactly full
check 3: fork() returned child pid 4
check 4: second write() returned 1 after the drain
check 5: second write blocked 40 ticks (>= 30), child slept 40
child: read 513 bytes, checksum 0xE8AE63A269C5D907, byte-exact
check 6: wait() returned child pid 4
check 7: child exit status 0, its 513-byte readback verified byte-exact
expected stream checksum: 0xE8AE63A269C5D907
checksum: 0x54D6734CAFB9C078
pipebuf results: write1=512 write2=1 blocked_ticks=40 reap=4 status=0
checks: 7 mismatches: 0
PASS: write past a full 512-byte pipe blocked 40 ticks until the reader drained it
```

Everything from `$ pipebuf` onward is byte-identical across all
three runs: same pids (parent 3, child 4), same check lines, same
blocked tick count (40), same child and expected checksums, same
structural checksum. The full boot transcripts differ only in the
SMP hart bring-up line order (`hart 1 starting` vs `hart 2
starting`), which is boot noise outside the test.

## Reading the numbers

- `check 2` (write returned 512): one `write()` of 512 bytes into
  an empty pipe moved all 512 with no short write, so the buffer
  held exactly 512 of 512 bytes when the second write started.
- `check 4` (second write returned 1): the one byte that had
  nowhere to go was accepted after the drain and the call returned
  normally.
- `check 5` (blocked 40 ticks, gate >= 30): the child slept until
  tick t0 + 40 before its first read, and the second write was
  issued in tick t0, so the call could only return once the child
  drained the pipe. A write that returned early would show 0 ticks;
  40 ticks against the 30-tick gate proves the writer stalled for
  the reader. The count is exactly 40 in all three runs because t0
  is captured on a fresh tick edge before the fork (not after it,
  where a tick boundary between fork and write would make the count
  jitter by a tick; an early revision showed exactly that 39/40
  jitter and was reworked).
- `child: read 513 bytes, checksum 0xE8AE63A269C5D907,
  byte-exact`: the child compared all 513 received bytes against
  `pat()` at their absolute stream indices and found zero
  differences, including the 0x00 at index 256 and the 0xFF at
  index 512.
- `expected stream checksum: 0xE8AE63A269C5D907`: the parent's
  independent fold over `pat(0)..pat(512)` matches the child's fold
  over the received bytes exactly.
- `checksum 0x54D6734CAFB9C078`: FNV-1a 64-bit over the five
  measured structural ints in fixed order (child pid 4, write1 512,
  write2 1, reaped pid 4, status 0), printed by the test and
  independently recomputed on the host with Python over the
  little-endian bytes of (4, 512, 1, 4, 0) with the same offset
  basis and prime: 0x54d6734cafb9c078, match. The host also
  recomputed the stream checksum from the `pat()` definition:
  0xe8ae63a269c5d907, match.
- `check 6/7` (wait returned pid 4, status 0): the one forked child
  was reaped exactly once, and its status 0 is the child's own
  verdict that the 513-byte readback was byte-exact.

Checks: 7. Mismatches: 0 across all 3 runs.

## Scope (honest)

Verified: with the pipe buffer exactly full (512 of 512 bytes), a
further 1-byte `write()` blocks inside the kernel until a reader
drains the pipe (measured 40 ticks against a 30-tick gate, the
child provably asleep the whole time), returns 1, and the full
513-byte stream arrives byte-exact. Not verified: partial-write
behavior for writes larger than PIPESIZE (xv6 loops those in
PIPESIZE chunks); blocking when multiple writers contend; read-side
blocking on an empty pipe; or writer behavior after the read end
closes (SIGPIPE does not exist here; `pipewrite` returns -1). One
child, one fixed 513-byte pattern, fixed absolute deadlines: the
whole verification surface fits in this file.
