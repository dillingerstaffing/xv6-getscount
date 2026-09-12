<!-- PROOF-HEADER
Checks: 9
Mismatches: 0
Checksum: 0x24322B881E690C23
Environment: QEMU 8.2.2
Verdict: PASS
-->

# PROOF: fork shares the file offset (fd table entry copy), user-space test

## What was built

A user-space test program, `user/forkoffset.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Opens a fresh file `forkoffset.out` (unlinked first so repeats start
   empty); asserts the open lands on fd 3.
2. Forks. The child writes a fixed 32-byte pattern A (32 `A` bytes)
   through its inherited fd 3 and exits with status 42 only when all
   32 bytes went out (status 7 otherwise).
3. The parent asserts `wait` returns the child's pid and the status
   round-trips as 42 (which also proves the child's write in step 2
   succeeded), then writes a fixed 32-byte pattern B (32 `b` bytes)
   through its own fd 3, closes, reopens read-only, and reads back
   all 64 bytes.
4. Asserts the readback is byte-exact A-then-B (contiguity proves one
   shared offset, since this xv6 has no lseek), and asserts a further
   read past the 64 bytes returns 0 (EOF sticks).

No kernel code was changed; the test exercises xv6's existing fork
path (fd table entry copy) from user space. This is distinct from
`user/forkisolation.c`, which tested that fork copies memory pages;
here the shared object is the file offset.

## Build (real log)

Built with the Ubuntu riscv64-unknown-elf 13.2.0 toolchain
(`~/workspace/toolchains/ubuntu-rv64/usr/bin` first on PATH), since
the xPack 15.2.0 `ld` mis-links xv6 user objects. Full kernel plus
`make fs.img` built clean (exit 0); targeted rebuild of the new
module:

```
$ make user/_forkoffset
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O \
  -fno-omit-frame-pointer -ggdb -gdwarf-2 -ffile-prefix-map=.=. \
  -march=rv64gc -std=gnu99 -MD -mcmodel=medany -ffreestanding \
  -fno-common -nostdlib [builtin suppressions] -Wno-main \
  -fno-stack-protector -fno-pie -no-pie \
  -c -o user/forkoffset.o user/forkoffset.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld \
  -o user/_forkoffset user/forkoffset.o user/ulib.o user/usys.o \
  user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_forkoffset > user/forkoffset.asm
```

No warnings, no errors. `mkfs/mkfs fs.img ... user/_forkoffset`
packed the program into the image.

## Run (real QEMU console output)

QEMU emulator version 8.2.2, `-machine virt -bios none`, 128M, one
fresh boot per run (fresh `fs.img` each time so `forkoffset.out`
starts empty). Run 2026-09-11. Output captured verbatim from the
emulated serial console:

```
$ forkoffset
check 1: open returned fd 3, as expected
check 2: fork returned child pid 4
check 3: child wrote 32 bytes of pattern A through inherited fd 3
check 4: wait returned the child's pid 4
check 5: child exit status 42 round-tripped through wait
check 6: parent wrote 32 bytes of pattern B through fd 3
check 7: read back 64 bytes, matches 64 bytes written
check 8: all 64 bytes match A-then-B, 0 mismatches
check 9: read past end returns 0, EOF is sticky
readback hex:
41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41
41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41
62 62 62 62 62 62 62 62 62 62 62 62 62 62 62 62
62 62 62 62 62 62 62 62 62 62 62 62 62 62 62 62
checksum: 0x24322B881E690C23
child pid: 4, status: 42, bytes written: 64, bytes read: 64
checks: 9 mismatches: 0
PASS: fork shares the file offset, child and parent writes are contiguous
```

The same console section was captured for two more fresh boots
(runs 2 and 3); all three runs are byte-identical, including the
child pid (4), the status (42), the hex dump, and the checksum.

## Reading the numbers

- `check 1` (fd 3): descriptors 0-2 are the console, so the first
  open takes 3; this pins the descriptor numbering the rest of the
  test assumes.
- `check 2` (child pid 4): fork succeeded and handed the parent a
  positive pid.
- `check 3` (child wrote 32 of A through fd 3): the inherited
  descriptor names the same open file description, so the write
  lands at offset 0 and advances the shared offset to 32.
- `check 4` (wait returned pid 4): wait reaped exactly the forked
  child.
- `check 5` (status 42 round-tripped): exit stored the status in the
  child's control block and wait copied it back unchanged; status 42
  additionally proves check 3's write completed in the child.
- `check 6` (parent wrote 32 of B through fd 3): lands at offset 32
  because the child's write already moved the shared offset there.
  A per-process offset would have landed it at 0, overwriting A.
- `check 7` (read 64): both writes are in the file, 64 bytes total.
- `check 8` (A-then-B, 0 mismatches): the 64 bytes are exactly the
  child's 32 `A` bytes followed by the parent's 32 `b` bytes, which
  is only possible if both writes went through one shared offset.
- `check 9` (read past end returns 0): EOF sticks, as xv6's file
  read specifies.
- `checksum 0x24322B881E690C23`: FNV-1a 64-bit over the 64 readback
  bytes, printed by the test and independently recomputed on the
  host with Python (same offset basis and prime): 0x24322b881e690c23,
  match.

Checks: 9 (the parent's 8 plus the child's write, verified through
the exit status). Mismatches: 0 across all 3 runs.

## Scope (honest)

Verified: fork copies fd table entries naming the same open file
description, so parent and child share one file offset; wait returns
the child pid with the exact exit status; EOF sticks on files. Not
verified: behavior of `dup` (covered separately by `dupshared`) or
of two independent opens of the same path (covered by
`fileoffindep`). One file, one fork, one fixed 32-byte pair: the
whole verification surface fits in this file.
