<!-- PROOF-HEADER
Checks: 7
Mismatches: 0
Checksum: 0xA484B136CCE206F5
Environment: QEMU 8.2.2
Verdict: PASS
-->

# PROOF: newly grown sbrk pages read all zero, and a later grow leaves the earlier page intact (user-space test)

## What was built

A user-space test program, `user/sbrkzfill.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Reads the current break (0x5000 in every run) and grows by one
   page: `sbrk(4096)` (check 1: returns the old break).
2. Scans all 4096 bytes of the fresh page (check 2: nonzero count
   0 and byte sum 0; both are printed so a scan that silently
   skipped bytes would have to fake both).
3. Tiles a 32-byte canary
   (`0123456789ABCDEFabcdef!@#$%^&*()`, no zero bytes, no 0xFF)
   over the page and requires a byte-exact readback (check 3:
   proves the zero scan ran over real, mapped, usable memory).
4. Grows a second page: `sbrk(4096)` (check 4: returns old+4096).
5. Scans all 4096 bytes of the second page (check 5: nonzero count
   0, byte sum 0).
6. Re-reads the first page (check 6: the canary is still
   byte-exact, proving the second grow mapped new pages instead of
   disturbing the first).
7. Reads `sbrk(0)` (check 7: exactly 0x7000, two pages above the
   start).

The ground truth is `kalloc` in `kernel/kalloc.c`: every physical
page it hands out is zeroed with `memset`, and `growproc` (via
`uvmalloc`) builds fresh grows out of those pages. No kernel code
was changed; the test exercises xv6's existing zero-fill path from
user space.

## Build (real log)

Built with the Ubuntu riscv64-unknown-elf 13.2.0 toolchain
(`~/workspace/toolchains/ubuntu-rv64/usr/bin` first on PATH), since
the xPack 15.2.0 `ld` mis-links xv6 user objects. Kernel was already
built; `make fs.img` compiled the new module and repacked the image,
exit 0 (full log in `bench-logs/sbrkzfill-build.log`):

```
$ make fs.img
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O \
  -fno-omit-frame-pointer -ggdb -gdwarf-2 -ffile-prefix-map=.=. \
  -march=rv64gc -std=gnu99 -MD -mcmodel=medany -ffreestanding \
  -fno-common -nostdlib [builtin suppressions] -Wno-main \
  -fno-stack-protector -fno-pie -no-pie \
  -c -o user/sbrkzfill.o user/sbrkzfill.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld \
  -o user/_sbrkzfill user/sbrkzfill.o user/ulib.o user/usys.o \
  user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_sbrkzfill > user/sbrkzfill.asm
riscv64-unknown-elf-objdump -t user/_sbrkzfill | sed ... > user/sbrkzfill.sym
mkfs/mkfs fs.img README ... user/_sbrksubpage user/_pipebuf user/_sbrkzfill
nmeta 47 (boot, super, log blocks 31, inode blocks 13, bitmap blocks 1) blocks 2953 total 3000
balloc: first 2495 blocks have been allocated
balloc: write bitmap block at sector 46
```

No warnings, no errors. The Makefile change is one line in `UPROGS`:
`$U/_sbrkzfill`.

## Run (real QEMU console output)

QEMU emulator version 8.2.2, `-machine virt -bios none`, 128M, 3
CPUs. Three fresh boots, one run per boot, run 2026-09-12. Input
driven by an expect script that waits for the shell prompt before
sending the command (an early attempt piping the command at boot
lost the first character to the UART race, so the prompt-wait was
added). Output captured verbatim from the emulated serial console
(raw transcripts in `bench-logs/sbrkzfill-run1.log` through
`sbrkzfill-run3.log`):

```
$ sbrkzfill
check 1: sbrk(4096) returned old break 0x5000
check 2: first page (4096 bytes) scanned: nonzero=0 sum=0, all zero
check 3: 4096-byte canary tiled over first page, readback byte-exact
check 4: sbrk(4096) returned 0x6000 == 0x5000+4096
check 5: second page (4096 bytes) scanned: nonzero=0 sum=0, all zero
check 6: first-page canary intact after second sbrk, 4096 bytes re-verified
check 7: sbrk(0) reads 0x7000 == 0x5000+8192, sum of grows
checksum: 0xA484B136CCE206F5
sbrkzfill values: b=0x5000 r1=0x5000 r2=0x6000 c=0x7000 nz1=0 nz2=0
checks: 7 mismatches: 0
PASS: two fresh sbrk pages read all zero (8192 bytes scanned, 0 nonzero), first-page canary intact after the second grow
```

Everything from `$ sbrkzfill` onward is byte-identical across all
three runs: same break addresses, same scan results, same checksum.
The full boot transcripts differ only in the SMP hart bring-up line
order (`hart 1 starting` vs `hart 2 starting`), which is boot noise
outside the test.

## Reading the numbers

- `check 2` (nonzero=0, sum=0 over 4096 bytes): the page handed
  out by the first grow contained no nonzero byte at all. A stale
  page recycled from a previous process would carry leftover
  contents; 0 of 4096 nonzero proves `kalloc` zeroed it.
- `check 3` (4096-byte canary readback byte-exact): the page is
  real, writable, mapped memory, not a faulting hole; it also
  establishes the canary baseline for check 6.
- `check 5` (nonzero=0, sum=0 over the second page): the zero-fill
  holds for a second consecutive grow, not just the first.
- `check 6` (4096 bytes re-verified byte-exact): after the second
  `sbrk(4096)`, every byte of the first page still matches the
  canary, so the second grow allocated new pages instead of
  remapping or clobbering the first.
- `checksum 0xA484B136CCE206F5`: FNV-1a 64-bit over the ten
  measured values in fixed order (breaks 0x5000/0x5000/0x6000/
  0x7000, nonzero counts 0/0, canary mismatches 0/0, byte sums 0/0),
  printed by the test and independently recomputed on the host with
  Python over the little-endian bytes with the same offset basis
  and prime: 0xa484b136cce206f5, match.
- `checks: 7 mismatches: 0`: every expectation held in all three
  runs.

Checks: 7. Mismatches: 0 across all 3 runs.

## Scope (honest)

Verified: two successive one-page `sbrk` grows hand out
zero-filled pages (8192 bytes scanned, 0 nonzero, byte sums 0),
each grow returns the old break, the break advances by exactly
8192, and the second grow leaves the first page's contents
untouched. Not verified: zero-fill of pages grown by a single
multi-page sbrk; grows racing with fork (copy-on-write is not part
of xv6); whether a shrink followed by a re-grow re-zeroes (the
freed pages are a separate path); or zero-fill under memory
pressure near OOM (see `user/sbrkoom.c` for the OOM slice). One
process, two fixed one-page grows, fixed addresses: the whole
verification surface fits in this file.
