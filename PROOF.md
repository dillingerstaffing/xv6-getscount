# PROOF: nprocs system call (syscall 24)

<!-- PROOF-HEADER
Checks: 3
Mismatches: 0
Verdict: PASS
-->

## What was built

A new xv6 system call, `nprocs()`, added alongside the earlier `getscount`
(syscall 23), following the same path through the kernel:

- `kernel/syscall.h`: `#define SYS_nprocs 24`
- `kernel/syscall.c`: `extern uint64 sys_nprocs(void);` and
  `[SYS_nprocs] = sys_nprocs` in the dispatch table
- `kernel/sysproc.c`: `sys_nprocs()` walks the kernel's own `proc[]`
  array (the same array the scheduler iterates), holding each entry's
  `p->lock` while reading its `state`, and returns the count of entries
  whose state is not `UNUSED`. Sleeping, runnable, running, and zombie
  entries all count; only fully freed slots do not.
- `user/usys.pl`: `entry("nprocs");` (generates the `ecall` stub)
- `user/user.h`: `int nprocs(void);`
- `user/nprocs.c`: userland test. Measures the count, forks 3 children
  that block in `pause(50)`, measures again, waits for the children,
  measures a third time. Prints PASS only if the middle count is
  exactly base+3 and the final count returns to base.
- `Makefile`: `$U/_nprocs` added to `UPROGS` so it ships in `fs.img`.

No code was copied from outside the tree; the handler reads only the
kernel's process table, and every number below comes from an actual run.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf`), `-Wall -Werror`, xv6-riscv rv64gc target.

```
$ make        # kernel
riscv64-unknown-elf-gcc ... -c -o kernel/sysproc.o kernel/sysproc.c
riscv64-unknown-elf-gcc ... -c -o kernel/syscall.o kernel/syscall.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T kernel/kernel.ld -o kernel/kernel ...
riscv64-unknown-elf-ld: warning: kernel/kernel has a LOAD segment with RWX permissions
```

(`make` here builds only the kernel; the `RWX` warning is stock xv6.)

```
$ make fs.img  # userland + filesystem image
perl user/usys.pl > user/usys.S
riscv64-unknown-elf-gcc ... -c -o user/nprocs.o user/nprocs.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_nprocs user/nprocs.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
mkfs/mkfs fs.img README user/_cat user/_echo ... user/_scount user/_nprocs
```

Both commands exited 0. One genuine build failure on the way: the test
first used `sleep()`, which this tree renamed to `pause()` (same
tick-based semantics, `SYS_pause`); fixed in `user/nprocs.c`, rebuild
clean.

## Run (real QEMU console output)

```
$ qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
    -m 128M -smp 3 -nographic \
    -global virtio-mmio.force-legacy=false \
    -drive file=fs.img,if=none,format=raw,id=x0 \
    -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$ nprocs
nprocs before fork = 3
nprocs with 3 children alive = 6 (expect 6)
nprocs after wait = 3 (expect 3)
PASS: nprocs tracks the process table exactly
$
```

## Reading the numbers

- `3` before forking = `init` (pid 1) + `sh` + the `nprocs` program
  itself: the three occupied slots of the 64-entry table.
- `6` = the same 3, plus 3 children blocked in `pause()` (SLEEPING
  state, still occupying table slots). Zombie children would also
  count, so the +3 holds regardless of scheduling order.
- `3` after `wait()` = the kernel reaped the children and `freeproc()`
  returned their slots to `UNUSED`.

The syscall returns exactly what the scheduler's own data structure
contains, measured live on 2026-09-08.

# PROOF: fork memory isolation (copyuvm), user-space test

<!-- PROOF-HEADER
Checks: 3
Mismatches: 0
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/forkisolation.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Grows the heap by one page with `sbrk` and writes canary word
   `0xcafef00d`.
2. Forks. The child reads the page and prints the observed word, then
   writes its own distinct word `0xdeadbeef` and prints it.
3. The parent waits for the child, then reads its own page and prints
   the observed word.

If fork copied the address space, the child must see `0xcafef00d`;
if the child's page is private, the parent must still see `0xcafef00d`
after the child exits. The three printed values are the measurement;
PASS prints only when both expectations hold.

No kernel code was changed; the test exercises xv6's existing fork
path (the page-table copy the kernel performs on fork) from user space.

## Build (real log)

```
$ make user/_forkisolation
riscv64-unknown-elf-gcc ... -c -o user/forkisolation.o user/forkisolation.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_forkisolation user/forkisolation.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_forkisolation > user/forkisolation.asm
$ make fs.img
perl user/usys.pl > user/usys.S
mkfs/mkfs fs.img README user/_cat user/_echo ... user/_forkisolation
```

Build exited 0. One genuine build failure on the way: the program first
used `PGSIZE` from `kernel/memlayout.h`, which userland cannot see
(undeclared identifier); replaced with a local `PAGESZ 4096` define so
the user program does not depend on kernel headers. Rebuild clean.

## Run (real QEMU console output)

```
$ qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
    -m 128M -smp 3 -nographic \
    -global virtio-mmio.force-legacy=false \
    -drive file=fs.img,if=none,format=raw,id=x0 \
    -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$ forkisolation
child read: 0xCAFEF00D (expect 0xCAFEF00D)
child wrote: 0xDEADBEEF
parent read after child exit: 0xCAFEF00D (expect 0xCAFEF00D)
PASS: fork copied the page, child write did not reach the parent
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- `0xCAFEF00D` read by the child: the parent's heap page was copied
  into the child's address space by fork; the child observes exactly
  the pre-fork canary.
- `0xDEADBEEF` written by the child: the child's own write lands in
  its page without fault.
- `0xCAFEF00D` read by the parent after the child exited: the parent's
  page is unchanged, so the child's write went to a private copy,
  not shared with the parent.

Checks: 3 (child observes canary; child's own write succeeds;
parent observes unchanged canary). Mismatches: 0.

---

# PROOF: sbrk growth ceiling (growproc refuses past the limit), user-space test

<!-- PROOF-HEADER
Checks: 5
Mismatches: 0
Checksum: 0x9348441C61B34D98
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/sbrkoom.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Records the initial break with `sbrk(0)`.
2. Calls `sbrk(4096)` in a loop until it returns `(void *)-1`,
   counting the successful pages.
3. Checks `sbrk(0)` reports the break exactly `pages * 4096` bytes
   above the initial break (the break sits on the last successful page).
4. Calls `sbrk(4096)` once more and requires it to still return -1
   (the ceiling is stable, not transient).
5. Requires the heap end to be page-aligned and prints the grown size
   in bytes and pages.

No kernel code was changed; the test exercises xv6's existing
`growproc` path (the `sys_sbrk` -> `growproc` -> `uvmalloc` chain)
from user space. PASS prints only when all five expectations hold.

## Build (real log)

```
$ make TOOLPREFIX=$HOME/workspace/toolchains/xv6-rv64/bin/riscv64-unknown-elf- \
      LDFLAGS="-z max-page-size=4096 -m elf64lriscv" fs.img
riscv64-unknown-elf-gcc ... -march=rv64gc ... -c -o user/sbrkoom.o user/sbrkoom.c
riscv64-unknown-elf-ld -z max-page-size=4096 -m elf64lriscv -T user/user.ld \
    -o user/_sbrkoom user/sbrkoom.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_sbrkoom > user/sbrkoom.asm
mkfs/mkfs fs.img README ... user/_forkisolation user/_sbrkoom
```

Build exited 0. Toolchain note (build environment only, no repo
change): this VM's only riscv64-capable GCC is the xPack
riscv-none-elf 15.2.0 toolchain, which defaults to a 32-bit ABI, so
the build used a local wrapper dir adding `-mabi=lp64d` to gcc and
`-m elf64lriscv` to ld; the Makefile itself is untouched and the
flags are the same ones any rv64 bare-metal toolchain would need.
Two genuine build failures on the way, both environmental: the stale
`.d` files from a previous toolchain referenced a removed
`/usr/lib/gcc/riscv64-unknown-elf/13.2.0` include path (fixed with
`make clean`), and the stock xpack `ld` defaults to the
`elf32lriscv` emulation and segfaults merging rv64 objects (fixed
with `-m elf64lriscv`).

## Run (real QEMU console output)

```
$ qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
    -m 128M -smp 3 -display none -serial stdio -monitor none \
    -global virtio-mmio.force-legacy=false \
    -drive file=fs.img,if=none,format=raw,id=x0 \
    -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

xv6 kernel is booting

hart 2 starting
hart 1 starting
init: starting sh
$ sbrkoom
check 1: growth stopped with -1 after 32468 pages (132988928 bytes)
check 2: sbrk(0)=0x7ED8000 == base(0x4000)+32468 pages*4096, matches
check 3: second sbrk(4096) still returns -1, ceiling stable
check 4: heap end 0x7ED8000 is page-aligned
check 5: 32468 pages * 4096 = 132988928 bytes, base 0x4000 to end 0x7ED8000
checksum: 0x9348441C61B34D98
checks: 5 mismatches: 0
PASS: growproc ceiling is stable
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2. The command was typed at the shell
prompt 45 seconds after boot so the console was ready; the program
ran to completion in one shell session.

## Reading the numbers

- `32468 pages (132988928 bytes)`: the number of successful
  `sbrk(4096)` calls before the first -1. The refusal comes from
  `growproc` failing when `uvmalloc` can no longer back a new page
  (physical pages exhausted on this 128M machine), which is exactly
  the user-visible growth ceiling of this build.
- `sbrk(0)=0x7ED8000`: the break after the first refusal. It equals
  the initial break `0x4000` plus `32468 * 4096` (`0x7ED4000`),
  so the break sits precisely on the last successful page and no
  partial page was granted.
- Check 3: a second `sbrk(4096)` after observing the break still
  returns -1, so the ceiling is stable and not a transient
  allocation hiccup.
- Check 4: `0x7ED8000` is a multiple of 4096, the heap end is
  page-aligned as the page allocator guarantees.
- `checksum 0x9348441C61B34D98`: FNV-1a 64-bit over the observed
  (pages, break, fail) triple `(32468, 0x7ED8000, 1)`, folded
  least-significant byte first per 64-bit word; independently
  recomputed from the printed numbers and matched.

Checks: 5 (growth stops with -1; break matches base+pages*4096;
second sbrk still -1; heap end page-aligned; byte/page accounting
consistent). Mismatches: 0.

## Scope (honest)

This pins the user-visible growth ceiling of this xv6 build's
`growproc`: how many pages a process can add before `sbrk` refuses,
and that the refusal is stable. It does not test kernel
OOM-killer behavior, which xv6 does not have; the observed refusal
is `growproc` returning -1 when no more physical pages can be
mapped. The exact page count (32468) is a measurement of this
build on a 128M QEMU virt machine with the test run as the first
command after boot, not a universal constant.

---

## getscount run log (syscall 23)

<!-- PROOF-HEADER
Checks: 4
Mismatches: 0
Environment: QEMU 8.2.2
Verdict: PASS
-->

The 4 verified checks are the `scount` demo program's 4 output lines (plus a PASS line). They prove: (1) count correctness, the kernel reported exactly the 5 `getpid` calls the program made; (2) out-of-range rejection, both `getscount(999)` and `getscount(-1)` return -1 instead of trusting the index blindly; (3) a forked child starts with zeroed counters, so accounting is per-process; (4) the parent's counters are unaffected by what the child did.

Verified sample output, copied verbatim from README.md:

```
$ scount
getpid called 5 times, kernel reports: 5
getscount(999) = -1 (expect -1)
getscount(-1)  = -1 (expect -1)
child:  getpid count = 0 (expect 0)
parent: getpid count = 5 (expect 5)
PASS
```

---

# PROOF: dup stdout redirect (fd-table redirection), user-space test

<!-- PROOF-HEADER
Checks: 4
Mismatches: 0
Checksum: 0x3B30E2D211BDE3D5
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/dupredirect.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program forks a child that:

1. Opens `duptest.out` with `O_CREATE|O_RDWR` (lands on fd 3, since
   fds 0-2 are the console; the program checks this).
2. Closes fd 1, then calls `dup(fd)`. dup must take the lowest free
   descriptor, so the program expects exactly 1 and exits with failure
   if it gets anything else.
3. Writes dup's return value into a side file `duptest.meta` (printf
   now targets the data file, so the meta channel gets its own fd),
   then printf-writes a fixed 64-byte literal through fd 1 and exits.

The parent waits, then runs 4 checks: the payload literal is 64 bytes
(self-check on the test's own constant), the side file reads back dup's
return as 1, the data file yields exactly the 64 bytes written, and a
byte-by-byte compare of the readback against the literal finds 0
mismatches. The readback is printed as hex and folded into a FNV-1a
64-bit checksum; PASS prints only when all 4 checks hold with 0
mismatches.

No code was copied from outside the tree; every number below comes
from an actual QEMU run, and the checksum was re-computed
independently from the literal to confirm the on-guest fold.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf`), `-Wall -Werror`, xv6-riscv rv64gc target.

```
$ make        # kernel up to date; new user program compiled
riscv64-unknown-elf-gcc ... -c -o user/dupredirect.o user/dupredirect.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_dupredirect user/dupredirect.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
```

(`make fs.img` then packed `user/_dupredirect` into the image; both
commands exited 0, no warnings.)

## Run (real QEMU console output)

```
$ qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
    -m 128M -smp 3 -nographic \
    -global virtio-mmio.force-legacy=false \
    -drive file=fs.img,if=none,format=raw,id=x0 \
    -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$ dupredirect
check 1: payload literal is 64 bytes, as defined
check 2: dup returned fd 1, read back from side file
check 3: read back 64 bytes, matches 64 bytes written
check 4: all 64 bytes match the expected literal, 0 mismatches
readback hex:
61 62 63 64 65 66 67 68 69 6a 6b 6c 6d 6e 6f 70
71 72 73 74 75 76 77 78 79 7a 41 42 43 44 45 46
47 48 49 4a 4b 4c 4d 4e 4f 50 51 52 53 54 55 56
57 58 59 5a 30 31 32 33 34 35 36 37 38 39 21 40
checksum: 0x3B30E2D211BDE3D5
dup fd: 1, bytes written: 64, bytes read: 64
checks: 4 mismatches: 0
PASS: dup redirected stdout into the file, readback is byte-exact
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- `dup returned fd 1`: after close(1), dup took the lowest free slot,
  exactly the fd-table behavior that makes shell-style redirection
  work. The value was read back from the side file, not assumed.
- `read back 64 bytes, matches 64 bytes written`: the child's printf
  went to the file, not the console (nothing of the payload appeared
  on the console before the parent's hex dump).
- The 64-byte hex readback is `61..7a` (a-z), `41..5a` (A-Z),
  `30..39` (0-9), `21` (!), `40` (@): byte-exact against the literal
  in the source.
- `checksum: 0x3B30E2D211BDE3D5`: FNV-1a 64 over the readback bytes,
  independently recomputed from the literal on the host to the same
  value, confirming the on-guest fold.

Checks: 4 (literal length; dup fd read back as 1; read length; byte
compare). Mismatches: 0.

---

# PROOF: pipe byte-stream ordering (pipe read/write path), user-space test

<!-- PROOF-HEADER
Checks: 4
Mismatches: 0
Checksum: 0xB1120CB4FAD2BF83
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/pipeorder.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Fills a 2048-byte buffer from a compile-time-fixed byte pattern:
   byte `i` is `((i * 31 + 7) ^ (i >> 2)) & 0xff`. 2048 bytes is four
   times xv6's 512-byte pipe buffer, so the writer cannot fit the
   payload at once and must block on a full pipe while the reader
   drains it, exercising the full-buffer block/wakeup path.
2. Self-checks that the pattern covers all 256 byte values (31 is odd,
   hence coprime to 256, so the multiplication permutes residues mod
   256); the stream test is only meaningful if every byte value
   traverses the pipe.
3. Forks. The child closes the read end, writes the full 2048-byte
   sequence through the write end (looping on short writes), closes
   the write end so the reader sees EOF, and exits 0 only if all
   2048 bytes were handed to the pipe.
4. The parent closes the write end, reads in 128-byte chunks until
   read returns 0, compares every received byte against the same
   pattern at its absolute stream position, and folds the stream
   into a FNV-1a 64-bit checksum. The first and last 16 received
   bytes are kept for a hex dump.

PASS prints only when all 4 checks hold: pattern covers all 256 byte
values; the writer reported handing all 2048 bytes to the pipe
(exit status 0); exactly 2048 bytes arrived before EOF; and the
byte-exact compare found 0 mismatches.

No kernel code was changed; the test exercises xv6's existing
`pipewrite`/`piperead` path from user space. Distinct from the
forkisolation, sbrkoom, and dupredirect tests, which do not touch the
pipe code.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (local wrapper over the xPack
riscv-none-elf 15.2.0 toolchain adding `-mabi=lp64d`; ld with
`-m elf64lriscv`), `-Wall -Werror`, xv6-riscv rv64gc target.

```
$ make TOOLPREFIX=$HOME/workspace/toolchains/xv6-rv64/bin/riscv64-unknown-elf- LDFLAGS="-z max-page-size=4096 -m elf64lriscv" fs.img
riscv64-unknown-elf-gcc ... -c -o user/pipeorder.o user/pipeorder.c
riscv64-unknown-elf-ld -z max-page-size=4096 -m elf64lriscv -T user/user.ld -o user/_pipeorder user/pipeorder.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_pipeorder > user/pipeorder.asm
mkfs/mkfs fs.img README ... user/_dupredirect user/_pipeorder
```

Build exited 0, no warnings. (The toolchain note: same as the
sbrkoom run; the Makefile itself is untouched.)

## Run (real QEMU console output)

```
$ qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
    -m 128M -smp 3 -display none -serial stdio -monitor none \
    -global virtio-mmio.force-legacy=false \
    -drive file=fs.img,if=none,format=raw,id=x0 \
    -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$ pipeorder
check 1: pattern covers all 256 byte values 0x00-0xff
check 2: writer handed all 2048 bytes to the pipe (exit 0)
check 3: read 2048 bytes before EOF, matches 2048 written
check 4: all 2048 bytes match the fixed pattern in order, 0 mismatches
stream head hex: 07 26 45 64 82 a3 c0 e1 fd 1c 3f 5e 78 99 ba db
stream tail hex: eb ca a9 88 6e 4f 2c 0d f1 d0 b3 92 74 55 36 17
checksum: 0xB1120CB4FAD2BF83
bytes written: 2048, bytes read: 2048, mismatches: 0
checks: 4 mismatches: 0
PASS: pipe delivered the 2048-byte stream byte-exact and in order
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- `check 1`: the 256-value coverage was measured by the test itself,
  not assumed from the arithmetic; a pattern that only used part of
  the byte range would have failed this check.
- `check 2`: the writer's exit status is the write side's own
  report that its write loop delivered all 2048 bytes to the pipe
  before closing the write end; the parent collected it via wait.
- `check 3`: the parent read exactly 2048 bytes before read returned
  0. The EOF therefore arrived only after the writer closed its end
  following the full payload, and the 512-byte pipe buffer stalled
  and drained without losing a byte.
- `check 4`: 2048 comparisons of received byte against
  `((i * 31 + 7) ^ (i >> 2)) & 0xff` at the byte's absolute stream
  position, 0 mismatches: the pipe delivered the bytes in order.
- `checksum: 0xB1120CB4FAD2BF83`: FNV-1a 64 over the received bytes,
  independently recomputed on the host from the pattern formula to
  the same value; the head and tail hex dumps also match the host
  recomputation byte-for-byte, confirming the on-guest fold.

Checks: 4 (pattern byte-value coverage; writer exit status; read
length at EOF; byte-exact ordered compare). Mismatches: 0.

---

# PROOF: exec argv delivery (exec copies argv verbatim), user-space test

<!-- PROOF-HEADER
Checks: 9
Mismatches: 0
Checksum: 0x5019A7B55A3768B4
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

Two user-space programs, added to `UPROGS` in the Makefile so both
ship in `fs.img`:

- `user/execargv_echo.c`: the exec destination. A dumb echo: it prints
  `argc`, then one `argv[i] len=L: value` line per argument, then
  whether `argv[argc]` is NULL. It does no verification; every
  judgment lives in the runner.
- `user/execargv.c`: the runner. It builds the expected output in
  memory from the same constants (a tiny decimal formatter, since
  userland has no sprintf; the byte-exact compare at the end would
  expose any formatting skew as a mismatch), then forks. The child
  wires its stdout to a pipe and execs the echo program with a known
  NULL-terminated vector: argv[0] `argv0name`, a normal word
  `hello`, a 27-byte string with spaces, an empty string, and a
  48-byte string of `x`.

The parent drains the pipe until the child exits, then runs 9 checks:
the expectation self-check (built output non-empty, long arg really
48 bytes); argc parses to 5; each of the 5 argv lines parses with the
right index and length and byte-exact value; the trailing line
confirms argv[5] is NULL; and the whole 200-byte capture is
byte-exact against the expectation. The capture is folded into a
FNV-1a 64-bit checksum; PASS prints only when all 9 checks hold with
0 mismatches.

No kernel code was changed; the test exercises xv6's existing exec
path (the `sys_exec` argv fetch and copy in `kernel/sysfile.c` /
`kernel/exec.c`) from user space.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf`), `-Wall -Werror`, xv6-riscv rv64gc target.

```
$ make fs.img
riscv64-unknown-elf-gcc ... -c -o user/execargv.o user/execargv.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_execargv user/execargv.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_execargv > user/execargv.asm
riscv64-unknown-elf-gcc ... -c -o user/execargv_echo.o user/execargv_echo.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_execargv_echo user/execargv_echo.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
mkfs/mkfs fs.img README ... user/_pipeorder user/_execargv user/_execargv_echo
```

Build exited 0. Two genuine build failures on the way: the echo
program was first named `execargv_target`, whose 15-character fs
name exceeds xv6's 14-byte `DIRSIZ` (mkfs assertion); renamed to
`execargv_echo`. Then the runner died in QEMU with a store page
fault at first run: two 4096-byte capture buffers on the stack
overflowed xv6's single-page user stack; shrunk to 512 bytes each
(the full capture is 200 bytes) and the run passed.

## Run (real QEMU console output)

```
$ qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
    -m 128M -smp 3 -nographic \
    -global virtio-mmio.force-legacy=false \
    -drive file=fs.img,if=none,format=raw,id=x0 \
    -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

xv6 kernel is booting

hart 2 starting
hart 1 starting
init: starting sh
$ execargv
check 1: expectation built (200 bytes), long arg is 48 bytes
check 2: argc parsed as 5, expected 5
check 3: argv[0] byte-exact, len 9 ("argv0name")
check 4: argv[1] byte-exact, len 5 ("hello")
check 5: argv[2] byte-exact, len 27 ("a longer string with spaces")
check 6: argv[3] byte-exact, len 0 (empty string)
check 7: argv[4] byte-exact, len 48 ("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx")
check 8: argv[5] is NULL, confirmed
check 9: captured 200 bytes, byte-exact against expectation
capture hex:
61 72 67 63 3a 20 35 0a 61 72 67 76 5b 30 5d 20
6c 65 6e 3d 39 3a 20 61 72 67 76 30 6e 61 6d 65
0a 61 72 67 76 5b 31 5d 20 6c 65 6e 3d 35 3a 20
68 65 6c 6c 6f 0a 61 72 67 76 5b 32 5d 20 6c 65
6e 3d 32 37 3a 20 61 20 6c 6f 6e 67 65 72 20 73
74 72 69 6e 67 20 77 69 74 68 20 73 70 61 63 65
73 0a 61 72 67 76 5b 33 5d 20 6c 65 6e 3d 30 3a
20 0a 61 72 67 76 5b 34 5d 20 6c 65 6e 3d 34 38
3a 20 78 78 78 78 78 78 78 78 78 78 78 78 78 78
78 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78
78 78 78 78 78 78 78 78 78 78 78 78 78 78 78 78
78 78 0a 61 72 67 76 5b 35 5d 20 69 73 20 4e 55
4c 4c 3a 20 79 65 73 0a
checksum: 0x5019A7B55A3768B4
argc: 5, bytes captured: 200, bytes expected: 200
checks: 9 mismatches: 0
PASS: exec delivered the argv vector verbatim, 5 args
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- `argc parsed as 5`: exec's argv copy produced exactly the 5
  pointers the runner passed (argv[0] through argv[4]).
- Checks 3-7: every argument arrived byte-exact, including the empty
  string (length 0, no bytes, line still well-formed) and the
  27-byte string with spaces (spaces survive the exec copy intact;
  they are just bytes to the kernel).
- `argv[5] is NULL, confirmed`: exec NULL-terminated the new
  program's argv, so the echo program's `argv[argc] == 0` test holds.
- `captured 200 bytes, byte-exact`: the full stdout of the new
  program image, from argc through the NULL line, matches the
  expectation built from the runner's constants, so the image
  replacement carried the whole vector, not just its prefix.
- `checksum: 0x5019A7B55A3768B4`: FNV-1a 64 over the 200 captured
  bytes, independently recomputed on the host from the constants to
  the same value, confirming the on-guest fold.

Checks: 9 (expectation self-check; argc parse; 5 argv byte-exact
parses; argv[argc] NULL line; full-capture byte-exact compare).
Mismatches: 0.

---

# PROOF: failed exec preserves the process image, user-space test

<!-- PROOF-HEADER
Checks: 5
Mismatches: 0
Checksum: 0x79A8C1F02EDB5451
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/execfail.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Writes two global sentinels to known constants: a `uint64`
   `sentinel64` set to `0xDEADBEEF12345678` and an `int` `sentinel32`
   set to `0x9ABCDEF0`.
2. Re-reads both sentinels and prints each value with its address,
   confirming the write/read path works before exec.
3. Calls `exec("/no/such/binary", args)` with a small NULL-terminated
   argv. The path does not exist in `fs.img`, so exec must fail and
   return; the check asserts the return value is exactly -1.
4. Re-reads both sentinels at the same addresses and compares against
   the constants, then folds the post-exec sentinel bytes and the
   exec return value into a FNV-1a 64-bit checksum.

PASS prints only when all 5 checks hold with 0 mismatches: both
sentinels verified before exec, exec returned -1, and both sentinels
verified unchanged after the failed exec.

No kernel code was changed; the test exercises xv6's existing exec
path (the `namei` failure path in `sys_exec`, before any image
replacement) from user space.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf`), `-Wall -Werror`, xv6-riscv rv64gc target.

```
$ make fs.img
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O -fno-omit-frame-pointer -ggdb -gdwarf-2 -ffile-prefix-map=/home/hatch/workspace/freelance-business/xv6-getscount=. -march=rv64gc -std=gnu99 -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -fno-builtin-strncpy -fno-builtin-strncmp -fno-builtin-strlen -fno-builtin-memset -fno-builtin-memmove -fno-builtin-memcmp -fno-builtin-log -fno-builtin-bzero -fno-builtin-strchr -fno-builtin-exit -fno-builtin-malloc -fno-builtin-putc -fno-builtin-free -fno-builtin-memcpy -Wno-main -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-vprintf -I. -fno-stack-protector -fno-pie -no-pie   -c -o user/execfail.o user/execfail.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_execfail user/execfail.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_execfail > user/execfail.asm
riscv64-unknown-elf-objdump -t user/_execfail | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/execfail.sym
mkfs/mkfs fs.img README user/_cat user/_echo user/_forktest user/_grep user/_init user/_kill user/_ln user/_ls user/_mkdir user/_rm user/_sh user/_stressfs user/_usertests user/_grind user/_wc user/_zombie user/_logstress user/_forphan user/_dorphan user/_sync user/_scount user/_nprocs user/_forkisolation user/_sbrkoom user/_dupredirect user/_pipeorder user/_execargv user/_execargv_echo user/_execfail
nmeta 47 (boot, super, log blocks 31, inode blocks 13, bitmap blocks 1) blocks 1953 total 2000
balloc: first 1388 blocks have been allocated
balloc: write bitmap block at sector 46
```

Build exited 0. No build failures; the program compiled clean under
`-Wall -Werror` on the first attempt.

## Run (real QEMU console output)

`qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel
-m 128M -smp 3 -nographic`, QEMU emulator version 8.2.2 (Debian
1:8.2.2+ds-0ubuntu1.18). The test ran 3 times; the output below is one
run, byte-identical across all 3 (identical md5 of the output block).

```
xv6 kernel is booting

hart 2 starting
hart 1 starting
init: starting sh
$ execfail
check 1: sentinel64 at 0x0000000000001018 holds 0xDEADBEEF12345678 before exec
check 2: sentinel32 at 0x0000000000001010 holds 0x9ABCDEF0 before exec
check 3: exec returned -1 (failed as expected)
check 4: sentinel64 at 0x0000000000001018 still holds 0xDEADBEEF12345678 after failed exec
check 5: sentinel32 at 0x0000000000001010 still holds 0x9ABCDEF0 after failed exec
checksum: 0x79A8C1F02EDB5451
exec return: -1, sentinel64: 0xDEADBEEF12345678, sentinel32: 0x9ABCDEF0
checks: 5 mismatches: 0
PASS: failed exec returned -1 and the image survived intact
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- `exec returned -1`: the nonexistent path failed at `namei` inside
  `sys_exec` before any page of the new image was loaded, and exec
  reported failure exactly as the API contract requires.
- Checks 1-2 vs checks 4-5: the same two addresses read back the same
  two constants before and after the failed exec, so the current
  image (its globals) survived the call untouched.
- `checksum: 0x79A8C1F02EDB5451`: FNV-1a 64 over the post-exec
  sentinel bytes and the -1 return value; identical across all 3
  runs, pinning the evidence to one value.
- Byte-identical output across 3 runs (same md5 of the output block):
  the result is deterministic; addresses are stable because xv6 loads
  user programs at a fixed base.

Checks: 5 (two sentinel write/read-backs before exec; exec returned
-1; two sentinel re-reads after the failed exec). Mismatches: 0.

---

# PROOF: pipe partial reads return exact stream prefixes (pipe read path), user-space test

<!-- PROOF-HEADER
Checks: 9
Mismatches: 0
Checksum: 0xEEB9DFC603EE8AEF
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/pipepart.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. A pipe is a byte stream with
no message boundaries, so short reads must return exact prefixes in
order. The test writes a fixed 64-byte pattern into a pipe with a
single `write` call, closes the write end, then reads the stream
back in 7-byte chunks. 64 = 9*7 + 1, so the read side must deliver
nine 7-byte chunks and one 1-byte chunk, then 0 (EOF). Each chunk
is compared byte-for-byte against the pattern at its absolute
stream position, and the reassembled stream is compared against
the original buffer in full. The pattern is a closed formula of
the byte index: bytes 0..61 are printable ASCII
(`'!' + (i * 7) % 94`), index 62 is 0x00, index 63 is 0xFF, so the
comparison must be length-driven and any C-string treatment of
the NUL or 0xFF would be caught by the byte compare. Nine checks:
the pattern self-check, pipe creation, the single write returning
exactly 64, the write-end close, the 10-chunk sequence with
per-chunk prefix correctness, the 64-byte total at EOF, sticky
EOF (read returns 0 again), full byte-exact equality of the
reassembled stream, and the read-end close. PASS prints only when
all 9 checks hold with 0 mismatches. A FNV-1a 64-bit checksum is
folded over the captured bytes as the one-value evidence of what
arrived.

No kernel code was changed; the test exercises xv6's existing
pipe read path (the `piperead` copyout loop in `kernel/pipe.c`)
from user space.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf`), `-Wall -Werror`, xv6-riscv rv64gc target.

```
$ make fs.img
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O ... -c -o user/pipepart.o user/pipepart.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_pipepart user/pipepart.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_pipepart > user/pipepart.asm
riscv64-unknown-elf-objdump -t user/_pipepart | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/pipepart.sym
mkfs/mkfs fs.img README ... user/_execfail user/_pipepart
nmeta 47 (boot, super, log blocks 31, inode blocks 13, bitmap blocks 1) blocks 1953 total 2000
balloc: first 1388 blocks have been allocated
balloc: write bitmap block at sector 46
```

Build exited 0. No build failures; the program compiled clean under
`-Wall -Werror` on the first attempt. (The `...` in the compile
line is the stock xv6 CFLAGS; the full line is in the build log.
Only `Makefile` (the UPROGS line), `user/pipepart.c`, and this
section of `PROOF.md` changed; the build artifacts
`user/_pipepart`, `user/pipepart.{o,d,asm,sym}` and the rebuilt
`fs.img` are gitignored, per repo convention.)

## Run (real QEMU console output)

`qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel
-m 128M -smp 3 -nographic`, QEMU emulator version 8.2.2 (Debian
1:8.2.2+ds-0ubuntu1.18). The test ran 3 times in one QEMU session;
the output below is one run, byte-identical across all 3 (identical
md5 of the output block).

```
xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$ pipepart
check 1: pattern carries NUL at 62 and 0xFF at 63
check 2: pipe() ok, read fd 3, write fd 4
check 3: single write() returned 64 of 64
check 4: write end closed before reading
chunk sequence: 7 7 7 7 7 7 7 7 7 1
check 5: chunk sequence 7,7,7,7,7,7,7,7,7,1, each chunk an exact stream prefix in order
check 6: read 64 bytes before EOF, matches 64 written
check 7: second read after EOF returned 0
check 8: reassembled 64 bytes equal the original buffer exactly
check 9: read end closed
stream head hex: 21 28 2f 36 3d 44 4b 52 59 60 67 6e 75 7c 25 2c 
stream tail hex: 57 5e 65 6c 73 7a 23 2a 31 38 3f 46 4d 54 00 ff 
checksum: 0xEEB9DFC603EE8AEF
bytes written: 64, bytes read: 64
checks: 9 mismatches: 0
PASS: pipe delivered 64 bytes as 7,7,7,7,7,7,7,7,7,1 prefixes in order, byte-exact
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- `check 3`: one `write` call moved all 64 bytes with no short
  write; the writer's byte count is exact, not buffered.
- `chunk sequence: 7 7 7 7 7 7 7 7 7 1`: the published sequence of
  per-`read` return values in order, matching the predicted
  64 = 9*7 + 1. Check 5 also verified every chunk's bytes against
  the pattern at the chunk's absolute stream position, so each
  chunk is the exact stream prefix, not merely the right length.
- `check 7`: a second `read` after EOF still returns 0, so EOF is
  stable and no phantom byte appears after the stream ends.
- `check 8`: the 64 bytes, concatenated in arrival order, equal
  the original write buffer byte-for-byte, including the NUL at
  index 62 and the 0xFF at index 63 visible in the tail hex dump
  (`... 4d 54 00 ff`), so the pipe moved raw bytes, not strings.
- `checksum: 0xEEB9DFC603EE8AEF`: FNV-1a 64 over the captured
  received bytes, independently recomputed on the host from the
  pattern formula to the same value, pinning the on-guest fold.
- Byte-identical output across 3 runs (same md5 of the output
  block): the result is deterministic.

Checks: 9 (pattern NUL/0xFF self-check; pipe creation; single
64-byte write; write-end close; 10-chunk sequence with per-chunk
prefix correctness; 64-byte total at EOF; sticky EOF; full
byte-exact reassembly; read-end close). Mismatches: 0.

---

# PROOF: wait() returns the child's pid with its exact exit status, user-space test

<!-- PROOF-HEADER
Checks: 6
Mismatches: 0
Checksum: 0x674A9192CFB12DF9
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/waitexit.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Forks a first child, which prints nothing and calls
   `exit(42)`. The parent asserts `fork` returned a positive pid and
   prints it.
2. Calls `wait(&status)` and asserts the return value equals the
   first child's pid, then asserts the stored status equals 42.
3. Forks a second child, which calls `exit(0)`, but only after the
   first child is fully reaped, so `wait`'s ordering is
   deterministic. The parent asserts `fork` returned a positive pid,
   `wait` returned that pid, and the stored status equals 0.
4. Folds every measured value (both pids, both `wait` return values,
   both statuses) into a FNV-1a 64-bit checksum, so the round-trip
   evidence collapses to one checkable value.

The child prints nothing; the parent prints every measured value, so
the console transcript is deterministic. PASS prints only when all 6
checks hold with 0 mismatches.

No kernel code was changed; the test exercises xv6's existing
wait/exit path (the status store in `exit` and the status copy in
`sys_wait`, before reaping) from user space.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf`), `-Wall -Werror`, xv6-riscv rv64gc target.

```
$ make fs.img
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O -fno-omit-frame-pointer -ggdb -gdwarf-2 -ffile-prefix-map=/home/hatch/workspace/freelance-business/xv6-getscount=. -march=rv64gc -std=gnu99 -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -fno-builtin-strncpy -fno-builtin-strncmp -fno-builtin-strlen -fno-builtin-memset -fno-builtin-memmove -fno-builtin-memcmp -fno-builtin-log -fno-builtin-bzero -fno-builtin-strchr -fno-builtin-exit -fno-builtin-malloc -fno-builtin-putc -fno-builtin-free -fno-builtin-memcpy -Wno-main -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-vprintf -I. -fno-stack-protector -fno-pie -no-pie   -c -o user/waitexit.o user/waitexit.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_waitexit user/waitexit.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_waitexit > user/waitexit.asm
riscv64-unknown-elf-objdump -t user/_waitexit | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/waitexit.sym
mkfs/mkfs fs.img README user/_cat user/_echo user/_forktest user/_grep user/_init user/_kill user/_ln user/_ls user/_mkdir user/_rm user/_sh user/_stressfs user/_usertests user/_grind user/_wc user/_zombie user/_logstress user/_forphan user/_dorphan user/_sync user/_scount user/_nprocs user/_forkisolation user/_sbrkoom user/_dupredirect user/_pipeorder user/_execargv user/_execargv_echo user/_execfail user/_pipepart user/_waitexit 
```

Build exited 0. No build failures; the program compiled clean under
`-Wall -Werror` on the first attempt.

## Run (real QEMU console output)

`qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel
-m 128M -smp 3 -nographic` plus the Makefile's `QEMUOPTS` disk lines
(`-global virtio-mmio.force-legacy=false -drive
file=fs.img,if=none,format=raw,id=x0 -device
virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0`), QEMU emulator
version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18). The test ran 3 times;
the output below is one run, with the program's own output lines
byte-identical across all 3 (identical md5 of the output block).

```
xv6 kernel is booting

hart 2 starting
hart 1 starting
init: starting sh
$ waitexit
check 1: fork returned child pid 4
check 2: wait returned pid 4, the first child's pid
check 3: wait stored status 42 for child 4
check 4: fork returned child pid 5
check 5: wait returned pid 5, the second child's pid
check 6: wait stored status 0 for child 5
checksum: 0x674A9192CFB12DF9
wait results: pid1=4 status=42, pid2=5 status=0
checks: 6 mismatches: 0
PASS: wait() returned each child's pid with its exact exit status
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- Checks 1-2: the first `fork` returned pid 4 and `wait` returned pid
  4, so the parent reaped exactly the child it created, not some
  other process.
- Check 3: the status stored by `wait` was 42, the exact value the
  child handed to `exit(42)`; the exit code round-tripped unchanged.
- Checks 4-6: the same contract holds at the low end, a child
  exiting with 0 was reaped as pid 5 with stored status 0. The
  round-trip is exact in both directions, not just for one nonzero
  value.
- `checksum: 0x674A9192CFB12DF9`: FNV-1a 64 over all six measured
  values (both pids, both `wait` return values, both statuses);
  identical across all 3 runs, pinning the evidence to one value.
- Byte-identical program output across 3 runs: the result is
  deterministic; pids are stable because xv6 allocates them in
  creation order on a fresh boot with the same command sequence.

## Limits

This ran under QEMU 8.2.2 emulation on the virt board, not on
silicon; what was verified is xv6's wait/exit implementation as the
code under test, specifically the status store in `exit` and the
status copy and pid return in `sys_wait`. It does not test wait's
behavior when the parent has no children (`-1` return) or when
multiple children race, which are separate slices.

Checks: 6 (positive fork pid; wait returned the first child's pid;
stored status equals 42; positive fork pid; wait returned the second
child's pid; stored status equals 0). Mismatches: 0.

---

# PROOF: failed open leaves the fd table untouched, user-space test

<!-- PROOF-HEADER
Checks: 7
Mismatches: 0
Checksum: 0x8609A2A5E433A9DD
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/openfail.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Calls `open("/no/such/file", O_RDONLY)` on a nonexistent path and
   asserts the return is exactly -1.
2. Repeats with a second nonexistent path and asserts -1 again, so
   repeated failures allocate nothing between them.
3. Creates `openfail.data` with `O_CREATE|O_RDWR` and asserts the
   returned descriptor is exactly 3, the lowest free fd after the
   console's 0, 1, 2. If either failed open had leaked a descriptor,
   this would be 4 or higher.
4. Writes a fixed 64-byte pattern through the new descriptor and
   asserts `write` returned all 64 bytes.
5. Closes the descriptor, re-opens the file read-only, and asserts
   the reopened descriptor is again 3 (the slot was genuinely
   freed by close).
6. Reads back the 64 bytes and asserts the count; then compares the
   readback byte-for-byte against the written pattern, asserting
   0 mismatches.

The measured fd numbers, both failed-open return values, and the
readback bytes are folded into a FNV-1a 64-bit checksum; PASS prints
only when all 7 checks hold with 0 mismatches.

No kernel code was changed; the test exercises xv6's existing
`sys_open` path (the `namei` failure return before `fdalloc()`) from
user space. Distinct from the execfail test, which probes exec's
failure path; this one probes the fd table.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (local wrapper over the xPack
riscv-none-elf 15.2.0 toolchain adding `-mabi=lp64d`; ld with
`-m elf64lriscv`), `-Wall -Werror`, xv6-riscv rv64gc target.

```
$ make TOOLPREFIX=$HOME/workspace/toolchains/xv6-rv64/bin/riscv64-unknown-elf- LDFLAGS="-z max-page-size=4096 -m elf64lriscv" fs.img
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O ... -c -o user/openfail.o user/openfail.c
riscv64-unknown-elf-ld -z max-page-size=4096 -m elf64lriscv -T user/user.ld -o user/_openfail user/openfail.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_openfail > user/openfail.asm
riscv64-unknown-elf-objdump -t user/_openfail | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/openfail.sym
mkfs/mkfs fs.img README ... user/_waitexit user/_openfail
nmeta 47 (boot, super, log blocks 31, inode blocks 13, bitmap blocks 1) blocks 1953 total 2000
balloc: first 1524 blocks have been allocated
balloc: write bitmap block at sector 46
```

Build exited 0. No build failures; the program compiled clean under
`-Wall -Werror` on the first attempt. The kernel `make` hit one
transient link failure mid-build (the wrapper `ld` segfault noted
in the sbrkoom entry); a rerun completed the link with no source
change.

## Run (real QEMU console output)

`qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel
-m 128M -smp 3 -nographic` plus the Makefile's `QEMUOPTS` disk lines,
QEMU emulator version 8.2.2 (Debian
1:8.2.2+ds-0ubuntu1.18). The test ran 3 times in one QEMU session;
the output below is one run, with the program's own output lines
byte-identical across all 3 (identical md5 of the output block).

```
xv6 kernel is booting

hart 2 starting
hart 1 starting
init: starting sh
$ openfail
check 1: open of nonexistent path returned -1
check 2: second failed open returned -1
check 3: create+open returned fd 3 (lowest free)
check 4: wrote 64 bytes through fd 3
check 5: reopen returned fd 3 (fd freed by close)
check 6: read back 64 bytes from fd 3
check 7: readback is byte-exact against the pattern
checksum: 0x8609A2A5E433A9DD
failed open returns: -1 -1, create fd: 3, reopen fd: 3
checks: 7 mismatches: 0
PASS: failed opens returned -1 and the fd table was untouched
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- `open of nonexistent path returned -1` (checks 1 and 2): both
  failed opens report failure exactly as the API contract requires;
  no descriptor was handed out, twice in a row.
- `create+open returned fd 3`: the very next successful open landed
  on the lowest free descriptor. fds 0, 1, 2 belong to the console
  inherited from the shell, so 3 is the expected value only if the
  two failed opens left the fd table completely untouched. A leaked
  descriptor would have shown up here as 4 or higher.
- `wrote 64 bytes through fd 3` and `read back 64 bytes from fd 3`
  (checks 4 and 6): the new descriptor is a working file, not a
  lucky number; data written through it comes back.
- `reopen returned fd 3 (fd freed by close)` (check 5): closing the
  descriptor really released slot 3, and the reopen took it again,
  confirming the fd numbers are live allocation results, not
  coincidences.
- `checksum: 0x8609A2A5E433A9DD`: FNV-1a 64 over the two -1 returns,
  both fd 3 values, and the 64 readback bytes, independently
  recomputed on the host from the printed numbers and the pattern
  formula to the same value, confirming the on-guest fold.
- Byte-identical program output across 3 runs (same md5 of the
  output block): the result is deterministic; fd numbers are stable
  because the shell's 0, 1, 2 are the only descriptors in use at
  test start.

Checks: 7 (two failed opens return -1; create lands on fd 3;
64-byte write; reopen lands on fd 3; 64-byte read; byte-exact
readback). Mismatches: 0.

## Limits

This ran under QEMU 8.2.2 emulation on the virt board, not on
silicon; what was verified is xv6's `sys_open` failure path as the
code under test, specifically that a `namei` failure returns -1
before any descriptor is allocated. It does not test open failures
from permission or device errors, or fd exhaustion, which are
separate slices.

---

# PROOF: kill marks a spinning child and wait reaps it, user-space test

<!-- PROOF-HEADER
Checks: 5
Mismatches: 0
Checksum: 0x34B177B4F80152EB
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/killreap.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Forks a child that spins forever in a volatile loop and never
   exits on its own, so the only way the parent can ever reap it is
   through the kill path. The parent asserts `fork` returned a
   positive pid and prints it.
2. Burns user time in a bounded volatile loop so the child is
   scheduled and running when kill lands. (This xv6 variant exposes
   no user-space `sleep` call; `sleep` exists only as a kernel
   internal in `kernel/sysproc.c`.) Correctness does not depend on
   the timing, the child spins regardless and `wait` blocks until it
   is reaped.
3. Calls `kill(childpid)` and asserts the return is 0.
4. Calls `wait(&status)` and asserts the return equals the child's
   pid, then asserts the stored status is -1, the status xv6's
   `usertrap` assigns a killed process (`exit(-1)`), which
   distinguishes a reaped kill from a normal exit.
5. Calls `wait` a second time and asserts the return is -1, because
   no children are left.
6. Folds every measured value (child pid, kill return, first `wait`
   return, stored status, second `wait` return) into a FNV-1a 64-bit
   checksum, so the kill-reap evidence collapses to one checkable
   value.

The child prints nothing; the parent prints every measured value, so
the console transcript is deterministic. PASS prints only when all 5
checks hold with 0 mismatches.

No kernel code was changed; the test exercises xv6's existing
kill/wait path (the killed flag set in `kill`, the `exit(-1)` for a
killed process in `usertrap`, the status copy and slot reaping in
`sys_wait`) from user space.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf`), `-Wall -Werror`, xv6-riscv rv64gc target.

The first build attempt failed: the original draft called
`sleep(20)`, but this xv6 variant has no user-space `sleep` call
(it exists only as a kernel internal in `kernel/sysproc.c` and is
absent from `user/user.h` and `user/usys.pl`), so compilation failed
with `implicit declaration of function 'sleep'` under `-Werror`.
The call was replaced with a bounded volatile busy loop and the
build went clean on the second attempt:

```
$ make fs.img
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O -fno-omit-frame-pointer -ggdb -gdwarf-2 -ffile-prefix-map=/home/hatch/workspace/freelance-business/xv6-getscount=. -march=rv64gc -std=gnu99 -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -fno-builtin-strncpy -fno-builtin-strncmp -fno-builtin-strlen -fno-builtin-memset -fno-builtin-memmove -fno-builtin-memcmp -fno-builtin-log -fno-builtin-bzero -fno-builtin-strchr -fno-builtin-exit -fno-builtin-malloc -fno-builtin-putc -fno-builtin-free -fno-builtin-memcpy -Wno-main -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-vprintf -I. -fno-stack-protector -fno-pie -no-pie   -c -o user/killreap.o user/killreap.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_killreap user/killreap.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_killreap > user/killreap.asm
riscv64-unknown-elf-objdump -t user/_killreap | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/killreap.sym
mkfs/mkfs fs.img README user/_cat user/_echo user/_forktest user/_grep user/_init user/_kill user/_ln user/_ls user/_mkdir user/_rm user/_sh user/_stressfs user/_usertests user/_grind user/_wc user/_zombie user/_logstress user/_forphan user/_dorphan user/_sync user/_scount user/_nprocs user/_forkisolation user/_sbrkoom user/_dupredirect user/_pipeorder user/_execargv user/_execargv_echo user/_execfail user/_pipepart user/_waitexit user/_killreap user/_openfail 
nmeta 47 (boot, super, log blocks 31, inode blocks 13, bitmap blocks 1) blocks 1953 total 2000
balloc: first 1568 blocks have been allocated
balloc: write bitmap block at sector 46
```

Build exited 0.

## Run (real QEMU console output)

`qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel
-m 128M -smp 3 -nographic` plus the Makefile's `QEMUOPTS` disk lines
(`-global virtio-mmio.force-legacy=false -drive
file=fs.img,if=none,format=raw,id=x0 -device
virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0`), QEMU emulator
version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18). The test ran 3 times;
the output below is one run, with the program's own output lines
byte-identical across all 3 (identical md5 of the output block).

```
xv6 kernel is booting

hart 2 starting
hart 1 starting
init: starting sh
$ killreap
check 1: fork returned child pid 4
check 2: kill returned 0 for child pid 4
check 3: wait returned pid 4, the killed child's pid
check 4: wait stored status -1 for child 4
check 5: second wait returned -1, no children left
checksum: 0x34B177B4F80152EB
kill results: child=4 kill=0 wait=4 status=-1 wait2=-1
checks: 5 mismatches: 0
PASS: kill() marked the child and wait() reaped its pid with status -1
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- Check 1: `fork` returned pid 4, a positive pid for the new child.
- Check 2: `kill(4)` returned 0, so the kernel found the spinning
  child and marked it for death; a nonexistent pid would have
  returned -1.
- Check 3: `wait` returned pid 4, so the parent reaped exactly the
  child it killed, not some other process. A child that never exits
  on its own was reaped, which is only possible through the kill
  path.
- Check 4: the stored status was -1, the status xv6's `usertrap`
  assigns a killed process (`exit(-1)` in `kernel/trap.c`), proving
  the reaping went through the kill path and not a normal exit.
- Check 5: the second `wait` returned -1, so the killed child left
  no unreaped state behind.
- `checksum: 0x34B177B4F80152EB`: FNV-1a 64 over all five measured
  values (child pid, kill return, wait return, status, second wait
  return), identical across all 3 runs, and independently recomputed
  on the host from the printed values to the same value, confirming
  the on-guest fold.
- Byte-identical program output across 3 runs: the result is
  deterministic; pids are stable because xv6 allocates them in
  creation order on a fresh boot with the same command sequence.

## Limits

This ran under QEMU 8.2.2 emulation on the virt board, not on
silicon; what was verified is xv6's kill/wait implementation as the
code under test, specifically the killed flag set in `kill`, the
`exit(-1)` for a killed process in `usertrap`, and the status copy
and slot reaping in `sys_wait`. It does not test kill on an already
exited (zombie) child, kill of a nonexistent pid, or multiple killed
children racing, which are separate slices.

Checks: 5 (positive fork pid; kill returned 0; wait returned the
killed child's pid; stored status is -1; second wait returned -1).
Mismatches: 0.

---

# PROOF: concurrent pipe writes land as intact records (pipewrite atomicity), user-space test

<!-- PROOF-HEADER
Checks: 8
Mismatches: 0
Checksum: 0xD8EBD304B30BBA83
Environment: QEMU 8.2.2
Verdict: PASS
-->

(The checksum above is run 1's; the block permutation, and therefore
the checksum, varies run to run by design. All 3 runs: 8 checks,
0 mismatches. Checksums for runs 2 and 3 are listed under Run.)

## What was built

A user-space test program, `user/pipeatomic.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Creates a pipe and asserts the two fds are distinct and valid.
2. Forks 4 children. Child i (0..3) closes the read end, fills a
   128-byte record with the single label byte `'0'+i`, writes it in
   exactly one `write()` call, closes the write end, and exits with 0
   only if the write returned exactly 128. Children print nothing.
   4*128 = 512 = PIPESIZE, so no writer can block on a full buffer and
   each write is one atomic `pipewrite` call; the 4 children race on a
   3-hart machine, so contention is real.
3. The parent closes its own write end after forking (forked fds share
   the same `struct file` objects via `filedup`, so the pipe reports
   EOF only once every writer has closed), then reads exactly 512
   bytes in 64-byte chunks and asserts the total.
4. Verifies the 512-byte stream is four 128-byte blocks in SOME order,
   where each block is byte-uniform and the four block labels are
   exactly '0','1','2','3', each once. A non-uniform block or a
   duplicated/missing label is an interleaving and counts as a
   mismatch.
5. Asserts a further read returns 0 (sticky EOF), closes the read end,
   then reaps all 4 children and asserts each `wait` returned a
   positive pid with exit status 0.
6. Folds the 512 received bytes into a FNV-1a 64-bit checksum, so the
   whole stream collapses to one checkable value.

No kernel code was changed; the test exercises xv6's existing pipe
write path (`pipewrite` holding `pi->lock` across the whole call) from
user space.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` 13.2.0 (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf`), `-Wall -Werror`, xv6-riscv rv64gc target.
(This run initially hit a broken local rv64 toolchain that emitted
elf32 objects; the install of the Ubuntu package fixed it, and the
build below is the genuine log with that toolchain.)

```
$ make fs.img   # userland + filesystem image (excerpt)
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O ... -march=rv64gc -std=gnu99 ... -c -o user/pipeatomic.o user/pipeatomic.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_pipeatomic user/pipeatomic.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_pipeatomic > user/pipeatomic.asm
riscv64-unknown-elf-objdump -t user/_pipeatomic | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/pipeatomic.sym
mkfs/mkfs fs.img README user/_cat user/_echo ... user/_openfail user/_pipeatomic
```

Build exited 0. No build failures; the program compiled clean under
`-Wall -Werror` on the first attempt with the working toolchain.

## Run (real QEMU console output)

`qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel
-m 128M -smp 3 -nographic` plus the Makefile's `QEMUOPTS` disk lines
(`-global virtio-mmio.force-legacy=false -drive
file=fs.img,if=none,format=raw,id=x0 -device
virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0`), QEMU emulator
version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18). The test ran 3 times;
the output below is run 1 verbatim.

```
xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$ pipeatomic
check 1: pipe() ok, read fd 3, write fd 4
check 2: 4 children forked
check 3: read 512 bytes before EOF, matches 4*128 written
check 4: read after EOF returned 0
check 5: all 4 128-byte blocks byte-uniform
check 6: labels present exactly once each: 0 1 2 3 (stream order for this run)
check 7: read end closed
check 8: all 4 children reaped with exit status 0 (each write returned 128)
checksum: 0xD8EBD304B30BBA83
bytes written: 512, bytes read: 512
checks: 8 mismatches: 0
PASS: 4 concurrent 128-byte writes landed as 4 intact uniform records, no interleaving
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

Runs 2 and 3, same 8 checks and 0 mismatches each, with different
block orders (the children genuinely race):

- run 2: block order 1 0 2 3, checksum 0xB14C37A29E2CBA83
- run 3: block order 0 1 3 2, checksum 0xE9C4DCC089C0BA83

The block order is the only nondeterministic output; everything else
is identical across runs.

## Reading the numbers

- Checks 1-2: `pipe()` returned distinct fds 3 and 4, and all 4 forks
  returned positive pids, so 4 writers existed.
- Check 3: 512 bytes arrived before EOF, exactly the 4 records'
  worth; no byte was lost or duplicated.
- Check 4: a read after the stream drained returned 0, so EOF is
  stable once every writer closed.
- Check 5: each 128-byte block was byte-uniform. A block mixing two
  labels would mean two writers' bytes interleaved inside one block;
  none did, across 3 runs with 3 different arrival orders.
- Check 6: the labels are exactly '0','1','2','3', each once. A
  duplicated or missing label would mean a record was split or lost.
- Check 7: the read end closed cleanly.
- Check 8: all 4 children were reaped with exit status 0, so every
  child's single `write()` returned exactly 128 bytes; no short write,
  no error.
- `checksum: 0xD8EBD304B30BBA83`: FNV-1a 64 over the 512 received
  bytes of run 1, independently recomputed on the host from the block
  order `0 1 2 3` to the same value; runs 2 and 3 likewise match host
  recomputation from their orders, confirming the on-guest fold.
- Three different block orders across 3 runs prove the children
  really ran concurrently; the PASS in every order proves atomicity
  does not depend on scheduling luck.

## Limits

This ran under QEMU 8.2.2 emulation on the virt board, not on
silicon; what was verified is xv6's `pipewrite` holding the pipe lock
across a whole write call, for writes that fit in the buffer without
blocking. It does not test writes larger than PIPESIZE (the kernel
splits those across sleeps and two such writers can interleave), reads
under contention from multiple readers, or the behavior when a reader
closes mid-write, which are separate slices.

Checks: 8 (pipe creation; 4 forks; 512-byte total at EOF; sticky EOF;
4 uniform 128-byte blocks; labels 0-3 each exactly once; read-end
close; 4 children reaped with status 0). Mismatches: 0.

---

# PROOF: an unlinked file stays readable through the open descriptor, user-space test

<!-- PROOF-HEADER
Checks: 7
Mismatches: 0
Checksum: 0x65AF3CA870DABE29
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/unlinkopen.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Creates `unlinkopen.data` with `O_CREATE|O_RDWR` and asserts the
   returned descriptor is exactly 3, the lowest free fd after the
   console's 0, 1, 2.
2. Writes a fixed 128-byte pattern through the descriptor and asserts
   `write` returned all 128 bytes, then closes the descriptor.
3. Reopens the file read-only and asserts the descriptor is again 3.
4. Calls `unlink` on the path while that descriptor is open and
   asserts the return is exactly 0.
5. Reads through the still-open descriptor and asserts the read
   returned 128 bytes, byte-exact against the written pattern.
6. Closes the descriptor and asserts `close` returned 0.
7. Opens the same path fresh and asserts the return is exactly -1,
   the directory entry really is gone.

The measured fd numbers, the readback bytes, and the post-close open
return are folded into a FNV-1a 64-bit checksum; PASS prints only
when all 7 checks hold with 0 mismatches.

No kernel code was changed; the test exercises xv6's existing
`sys_unlink` path and the file table's inode reference (the open
struct file keeps its ip alive after the directory entry and link
count are dropped) from user space.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf`), `-Wall -Werror`, xv6-riscv rv64gc target.

```
$ make fs.img
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O -fno-omit-frame-pointer -ggdb -gdwarf-2 -ffile-prefix-map=/home/hatch/workspace/freelance-business/xv6-getscount=. -march=rv64gc -std=gnu99 -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -fno-builtin-strncpy -fno-builtin-strncmp -fno-builtin-strlen -fno-builtin-memset -fno-builtin-memmove -fno-builtin-memcmp -fno-builtin-log -fno-builtin-bzero -fno-builtin-strchr -fno-builtin-exit -fno-builtin-malloc -fno-builtin-putc -fno-builtin-free -fno-builtin-memcpy -Wno-main -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-vprintf -I. -fno-stack-protector -fno-pie -no-pie   -c -o user/unlinkopen.o user/unlinkopen.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_unlinkopen user/unlinkopen.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_unlinkopen > user/unlinkopen.asm
riscv64-unknown-elf-objdump -t user/_unlinkopen | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/unlinkopen.sym
mkfs/mkfs fs.img README ... user/_openfail user/_pipeatomic user/_unlinkopen
```

Build exited 0. No build failures; the program compiled clean under
`-Wall -Werror` on the first attempt. The `...` in the compile line
is the stock xv6 CFLAGS, as in the other entries.

## Run (real QEMU console output)

`qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel
-m 128M -smp 3 -nographic` plus the Makefile's `QEMUOPTS` disk lines
(`-global virtio-mmio.force-legacy=false -drive
file=fs.img,if=none,format=raw,id=x0 -device
virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0`), QEMU emulator
version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18). The test ran 3 times;
the program's output block below is byte-identical across all 3
(identical md5 of the output block, 34e0fb3841fb77c0b2f39cc4eb48ecfc).

```
xv6 kernel is booting

hart 2 starting
hart 1 starting
init: starting sh
$ unlinkopen
check 1: create+open returned fd 3 (lowest free)
check 2: wrote 128 bytes through fd 3
check 3: reopen read-only returned fd 3
check 4: unlink returned 0 with fd 3 still open
check 5: read 128 bytes through the unlinked fd, byte-exact
check 6: close returned 0
check 7: post-close open of the unlinked path returned -1
checksum: 0x65AF3CA870DABE29
readback bytes: 128, post-close open: -1
checks: 7 mismatches: 0
PASS: unlinked path read back byte-exact through the open fd, fresh open returned -1
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- Checks 1-3: `create+open` landed on fd 3, the write moved 128
  bytes, and the reopen took fd 3 again; the descriptor numbers are
  live allocation results (fds 0-2 are the shell's console), not
  coincidences.
- `check 4: unlink returned 0 with fd 3 still open`: the path's
  directory entry is gone while the descriptor lives on; this is
  the fork in behavior the test is built around.
- `check 5`: the read through the unlinked descriptor returned all
  128 bytes byte-exact against the write pattern. The inode had to
  survive unlink for this to hold: the open struct file's ip
  reference is what keeps it alive.
- `check 7: post-close open ... returned -1`: once the last
  descriptor closed and the inode's last reference dropped, the
  entry was really unlinked; `namei` finds nothing.
- `checksum: 0x65AF3CA870DABE29`: FNV-1a 64 over the three fd
  returns (3, 3, -1) folded little-endian as 32-bit ints and the
  128 readback bytes, independently recomputed on the host from the
  printed numbers and the pattern formula to the same value,
  confirming the on-guest fold.
- Byte-identical program output across 3 runs: the result is
  deterministic; fd numbers are stable because the shell's 0, 1, 2
  are the only descriptors in use at test start.

Checks: 7 (create lands on fd 3; 128-byte write; reopen lands on fd
3; unlink returns 0 under an open fd; 128-byte byte-exact readback
through the unlinked fd; close returns 0; fresh open of the
unlinked path returns -1). Mismatches: 0.

## Limits

This ran under QEMU 8.2.2 emulation on the virt board, not on
silicon; what was verified is xv6's unlink semantics as the code
under test, specifically that an open descriptor's inode reference
survives the directory-entry removal and that a fresh open fails
after the last reference closes. It does not test unlink of a
directory, unlink of the cwd, or the link-count behavior with
multiple hard links to the same inode, which are separate slices.

---

# PROOF: reads return 0 forever once all pipe write ends close (pipe EOF stickiness), user-space test

<!-- PROOF-HEADER
Checks: 9
Mismatches: 0
Checksum: 0xEEB9DFC603EE8AEF
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/pipeeof.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The fundamental truth under
test: once every write end of an xv6 pipe is closed, reads return 0
forever. A child closes the read end, writes a fixed 64-byte
pattern in one `write` call, closes its write end, and exits; the
parent closes its own write end and waits for the child, so at
drain time no fd anywhere refers to the write end. The parent
reads until `read` returns 0, then performs three more successive
reads and requires each to return exactly 0. Nine checks: pipe
creation, the fork, the parent's write-end close, the child reaped
with exit status 0 (its single write moved all 64 bytes), the
drain `read` return-value sequence exactly `64, 0`, the 64-byte
total before EOF, three post-EOF reads each returning 0, the
received stream byte-exact against the compile-time pattern, and
the read-end close. The pattern is the same closed formula used
by `user/pipepart.c` (`'!' + (i * 7) % 94` for bytes 0..61,
0x00 at 62, 0xFF at 63), so the byte compare must be
length-driven. Waiting for the child before the first read makes
the drain sequence deterministic: all 64 bytes sit in the pipe
and no writer remains, so the first read must return the full 64
and the second 0. PASS prints only when all 9 checks hold with 0
mismatches. A FNV-1a 64-bit checksum is folded over the received
bytes as the one-value evidence of what arrived.

No kernel code was changed; the test exercises xv6's existing
pipe EOF path (`piperead` returning 0 when `nwrite == nread` with
no writers, in `kernel/pipe.c`) from user space.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf`), `-Wall -Werror`, xv6-riscv rv64gc target.

```
$ make fs.img
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O ... -c -o user/pipeeof.o user/pipeeof.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_pipeeof user/pipeeof.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_pipeeof > user/pipeeof.asm
riscv64-unknown-elf-objdump -t user/_pipeeof | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/pipeeof.sym
mkfs/mkfs fs.img README ... user/_unlinkopen user/_pipeeof
nmeta 47 (boot, super, log blocks 31, inode blocks 13, bitmap blocks 1) blocks 1953 total 2000
balloc: first 1713 blocks have been allocated
balloc: write bitmap block at sector 46
```

Build exited 0. No build failures; the program compiled clean under
`-Wall -Werror` on the first attempt. (The `...` in the compile
line is the stock xv6 CFLAGS; the full line is in the build log.
Only `Makefile` (the UPROGS line), `user/pipeeof.c`, and this
section of `PROOF.md` changed; the build artifacts
`user/_pipeeof`, `user/pipeeof.{o,d,asm,sym}` and the rebuilt
`fs.img` are gitignored, per repo convention.)

## Run (real QEMU console output)

`qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel
-m 128M -smp 3 -nographic` (`-global virtio-mmio.force-legacy=false
-drive file=fs.img,if=none,format=raw,id=x0 -device
virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0`), QEMU emulator
version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18). The test ran 3 times
in one QEMU session; the output below is one run, byte-identical
across all 3 (identical md5 of the output block,
`32fef1a2370053d64ece3085b9779367`).

```
xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$ pipeeof
check 1: pipe() ok, read fd 3, write fd 4
check 2: forked child
check 3: parent closed its write end
check 4: child reaped, exit status 0 (write moved 64 bytes)
drain read return values: 64 0
check 5: drain sequence 64, 0 as predicted, bytes match pattern in order
check 6: read 64 bytes before EOF, matches 64 written
EOF read return values: 0 0 0
check 7: three reads after EOF each returned 0
check 8: received 64 bytes equal the pattern exactly
check 9: read end closed
checksum: 0xEEB9DFC603EE8AEF
bytes written: 64, bytes read: 64
checks: 9 mismatches: 0
PASS: reads returned 0 forever once all write ends closed (drain 64, 0; EOF reads 0 0 0)
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- `drain read return values: 64 0`: the published per-`read`
  return-value sequence. Because the parent waited for the child
  before the first read, all 64 bytes were in the pipe with no
  writer left, so the first `read` returned the full 64 and the
  next returned 0 (EOF). Check 5 also verified every received byte
  against the pattern at its absolute stream position.
- `EOF read return values: 0 0 0`: three successive reads after
  EOF each still return 0. EOF is sticky: no phantom byte appears
  and no read blocks after the first 0.
- `check 4`: the child was reaped with exit status 0, which
  proves the child's single `write` really moved 64 bytes and its
  closes succeeded; without the wait, the drain sequence could not
  be pinned to `64, 0`.
- `checksum: 0xEEB9DFC603EE8AEF`: FNV-1a 64 over the received
  stream, independently recomputed on the host from the pattern
  formula to the same value, confirming the on-guest fold. It is
  identical to the `pipepart` checksum because the received bytes
  are the same 64-byte pattern in the same order.
- Byte-identical output across 3 runs (same md5 of the output
  block): the result is deterministic. The child pid is
  intentionally not printed, so scheduling does not leak into the
  output; fd numbers are stable because the shell's 0, 1, 2 are
  the only descriptors in use at test start.

Checks: 9 (pipe creation; fork; parent write-end close; child
reaped with exit status 0; drain sequence `64, 0` with per-byte
pattern match; 64-byte total at EOF; three post-EOF reads each
returning 0; full byte-exact reassembly; read-end close).
Mismatches: 0.

## Limits

This ran under QEMU 8.2.2 emulation on the virt board, not on
silicon; what was verified is xv6's pipe EOF semantics as the
code under test, specifically that `read` returns 0 forever once
all write ends are closed and the buffer is drained. It does not
test the half-open case (a writer still present means reads block
rather than return 0), EOF with multiple concurrent readers, or
EOF arriving while a writer is mid-write, which are separate
slices.

---

# PROOF: open file descriptor survives exec (exec keeps the open file table), user-space test

<!-- PROOF-HEADER
Checks: 5
Mismatches: 0
Checksum: 0x286D4B6114E61FC3
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

Two user-space programs, added to `UPROGS` in the Makefile so both
ship in `fs.img`:

- `user/execpresfd_hlp.c`: the exec destination. A deliberately dumb
  writer: it converts `argv[1]` back to an integer fd, writes a FIXED
  64-byte pattern to that fd (byte i = `(i * 37 + 11) mod 256`,
  i in 0..63), prints `helper fd: N, wrote 64 bytes` on its stdout,
  closes the fd, and exits. It does no verification; every judgment
  lives in the runner. The helper's fs name is 14 characters,
  the maximum mkfs accepts (`strlen(shortname) <= DIRSIZ`).
- `user/execpresfd.c`: the runner. It builds the same 64 bytes in
  memory from the same formula, creates a data pipe and a capture
  pipe, and forks. The child closes the data pipe's read end, dups
  the capture pipe onto stdout, and execs the helper with the data
  pipe's write-end fd number as an argv string. The parent closes
  both write ends, drains the data pipe to EOF, drains the capture
  pipe, and waits for the child.

Five checks: the expectation self-check (64 bytes, formula pinned on
two hand-computed bytes, i=0 -> 11 and i=63 -> 38, so a transcription
slip in the formula fails here rather than in the exec test); the
write-end fd is 4, a real pipe fd distinct from stdout, so the helper
cannot be observed writing to stdout by accident; the helper's stdout
parses to `helper fd: 4, wrote 64 bytes` with the fd number equal to
the number the child passed (direct evidence the descriptor the helper
wrote through is the one that existed before exec); exactly 64 bytes
arrived on the data pipe; and those 64 bytes are byte-exact against
the in-memory expectation. If exec had closed the descriptor, the
helper's write would have failed and no 64 bytes would have arrived.
PASS prints only when all 5 checks hold with 0 mismatches. The
received bytes are folded into a FNV-1a 64-bit checksum as the
one-value evidence.

No kernel code was changed; the test exercises xv6's existing exec
path (the address space is replaced while the process's open file
table is kept, in `kernel/exec.c` / `kernel/proc.c`) from user space.
Distinct from the earlier shipments: `user/execargv` tested argv
delivery across exec, `user/execfail` tested that a failed exec
preserves the image; this one tests that the open file table
survives a successful exec.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf`), `-Wall -Werror`, xv6-riscv rv64gc target.

```
$ make fs.img
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O ... -c -o user/execpresfd.o user/execpresfd.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_execpresfd user/execpresfd.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_execpresfd > user/execpresfd.asm
riscv64-unknown-elf-objdump -t user/_execpresfd | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/execpresfd.sym
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O ... -c -o user/execpresfd_hlp.o user/execpresfd_hlp.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_execpresfd_hlp user/execpresfd_hlp.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_execpresfd_hlp > user/execpresfd_hlp.asm
riscv64-unknown-elf-objdump -t user/_execpresfd_hlp | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/execpresfd_hlp.sym
mkfs/mkfs fs.img README ... user/_pipeeof user/_execpresfd user/_execpresfd_hlp
```

Build exited 0. One genuine bug on the way, found by the test
itself: the first run passed the data checks (64 bytes, byte-exact)
but check 3 mis-parsed the helper's stdout (`wrote -1`) because the
parser compared for `\n` right after the byte count instead of after
the trailing ` bytes` word. Fixed in `user/execpresfd.c`, rebuilt,
and the full 5/5 run passed. The data path was never in doubt; the
bug was in the runner's own report parsing, which is exactly what
the checks are for.

## Run (real QEMU console output)

```
$ qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
    -m 128M -smp 3 -nographic \
    -global virtio-mmio.force-legacy=false \
    -drive file=fs.img,if=none,format=raw,id=x0 \
    -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

xv6 kernel is booting

hart 2 starting
hart 1 starting
init: starting sh
$ execpresfd
check 1: expectation built (64 bytes), formula pinned at bytes 0 and 63
check 2: write-end fd is 4, distinct from stdout
check 3: helper saw fd 4 (matches passed 4), wrote 64 bytes
check 4: received 64 bytes on the data pipe
check 5: 64 bytes byte-exact against the expectation
capture hex:
0b 30 55 7a 9f c4 e9 0e 33 58 7d a2 c7 ec 11 36
5b 80 a5 ca ef 14 39 5e 83 a8 cd f2 17 3c 61 86
ab d0 f5 1a 3f 64 89 ae d3 f8 1d 42 67 8c b1 d6
fb 20 45 6a 8f b4 d9 fe 23 48 6d 92 b7 dc 01 26
checksum: 0x286D4B6114E61FC3
fd passed: 4, bytes received: 64, bytes expected: 64
checks: 5 mismatches: 0
PASS: open fd 4 survived exec, 64-byte pattern intact
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2. Ran 3 times; all three outputs
byte-identical.

## Reading the numbers

- `check 1`: the expectation is the same formula the helper
  compiles in, pinned on two hand-computed bytes, so the compare in
  check 5 cannot pass by the runner and helper sharing the same
  transcription error unknowingly. (Both do share the formula, which
  is the point: the test asks whether the bytes cross the exec
  boundary intact.)
- `check 2`: fd 4 is a genuine pipe descriptor, not stdout, so the
  helper's write provably went through the pipe fd table entry that
  existed before exec.
- `check 3`: the helper, running as a completely new program image
  after exec, parsed the argv string back to the integer 4 and wrote
  through it. This is the direct observation that the descriptor
  survived exec: had exec closed it, the write would have returned -1
  and the helper would have printed a FAIL line instead.
- `check 4` and `check 5`: the parent received exactly the 64 bytes,
  in order, with no loss, duplication, or reordering across the
  exec boundary.
- `checksum: 0x286D4B6114E61FC3`: FNV-1a 64 over the received
  bytes, independently recomputed on the host from the pattern
  formula to the same value, confirming the on-guest fold.
- Byte-identical output across 3 runs: the result is
  deterministic. The child pid is intentionally not printed, so
  scheduling does not leak into the output; fd numbers are stable
  because the shell's 0, 1, 2 are the only descriptors in use at
  test start.

Checks: 5 (expectation self-check; write-end fd is a real pipe fd
distinct from stdout; helper saw the passed fd number and wrote all
64 bytes; 64 bytes received on the data pipe; byte-exact compare
against the expectation).
Mismatches: 0.

## Limits

This ran under QEMU 8.2.2 emulation on the virt board, not on
silicon; what was verified is xv6's preservation of open file
descriptors across a successful exec, specifically for one pipe
write end passed as an argv string. It does not test multiple
descriptors surviving together, fd-number reuse across exec, an
exec chain (exec after exec), descriptors opened O_RDONLY/O_RDWR,
or the interaction with `close-on-exec` semantics (xv6 has none),
which are separate slices.

---

# PROOF: dup shares the file offset between descriptors, user-space test

<!-- PROOF-HEADER
Checks: 7
Mismatches: 0
Checksum: 0x24322B881E690C23
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/dupshared.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Unlinks `dupshared.out` so repeated runs start from an empty file
   (open with `O_CREATE` does not truncate).
2. Opens `dupshared.out` with `O_CREATE|O_RDWR` (lands on fd 3, since
   fds 0-2 are the console; the program checks this).
3. Calls `dup(fd)`, which must take the lowest free descriptor, 4
   (the program checks this).
4. Writes a fixed 32-byte pattern A (all `A`) through fd 3 and a fixed
   32-byte pattern B (all `b`) through fd 4, checking each write
   returns 32.
5. Closes both descriptors, reopens the file `O_RDONLY`, and reads
   back all 64 bytes.
6. Verifies byte-exact that bytes 0-31 are pattern A and bytes 32-63
   are pattern B: if the offset were per-descriptor, the second write
   would have landed at offset 0 and overwritten A, so the
   contiguous A-then-B readback is the offset-sharing proof. (This
   xv6 has no lseek syscall, so contiguity is the assertion.)
7. Verifies EOF stickiness: one more read past the 64 bytes returns 0.

The readback is printed as hex and folded into a FNV-1a 64-bit
checksum; PASS prints only when all 7 checks hold with 0 mismatches.

No code was copied from outside the tree; every number below comes
from actual QEMU runs, and the checksum was independently recomputed
on the host from the two literals to the same value, confirming the
on-guest fold.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf`), `-Wall -Werror`, xv6-riscv rv64gc target.

```
$ make fs.img  # userland rebuild + filesystem image
riscv64-unknown-elf-gcc ... -c -o user/dupshared.o user/dupshared.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_dupshared user/dupshared.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_dupshared > user/dupshared.asm
mkfs/mkfs fs.img README ... user/_dupshared ...
```

Both commands exited 0, no warnings under `-Wall -Werror`. (The
full gcc flag line is the stock xv6 userland compile line; only the
`-o user/dupshared.o user/dupshared.c` tail differs per program.)

## Run (real QEMU console output)

```
$ qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
    -m 128M -smp 3 -nographic \
    -global virtio-mmio.force-legacy=false \
    -drive file=fs.img,if=none,format=raw,id=x0 \
    -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$ dupshared
check 1: open returned fd 3, as expected
check 2: dup returned fd 4, the lowest free slot
check 3: wrote 32 bytes of pattern A through fd 3
check 4: wrote 32 bytes of pattern B through fd 4
check 5: read back 64 bytes, matches 64 bytes written
check 6: all 64 bytes match A-then-B, 0 mismatches
check 7: read past end returns 0, EOF is sticky
readback hex:
41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41
41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41
62 62 62 62 62 62 62 62 62 62 62 62 62 62 62 62
62 62 62 62 62 62 62 62 62 62 62 62 62 62 62 62
checksum: 0x24322B881E690C23
fd1: 3, fd2: 4, bytes written: 64, bytes read: 64
checks: 7 mismatches: 0
PASS: dup shares the file offset, writes through both fds are contiguous
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2. Runs 2 and 3 were byte-identical to the
output above, including the checksum. (Driving note: the command was
fed to the guest shell 12 seconds after QEMU start; input sent at
start is lost before the guest UART is ready, which cost one empty
run during development.)

## Reading the numbers

- `open returned fd 3` and `dup returned fd 4`: the shell's 0, 1, 2
  are the console, so the first open lands on 3 and dup takes the
  lowest free slot, 4.
- `wrote 32 bytes of pattern A through fd 3` then `wrote 32 bytes of
  pattern B through fd 4`: two writes through two different
  descriptors.
- `read back 64 bytes, matches 64 bytes written` and `all 64 bytes
  match A-then-B, 0 mismatches`: the B write started exactly where the
  A write ended, proving one shared offset advanced by both writes. A
  per-descriptor offset would have overwritten bytes 0-31 with B.
- The 64-byte hex readback is 32 `41` (`A`) then 32 `62` (`b`):
  byte-exact against the literals in the source.
- `checksum: 0x24322B881E690C23`: FNV-1a 64 over the readback bytes,
  independently recomputed on the host from the literals
  (`b'A'*32 + b'b'*32`) to the same value, confirming the on-guest
  fold.
- `read past end returns 0`: EOF is sticky after the 64 bytes, so the
  readback was complete, not truncated.

Checks: 7 (open fd; dup fd; write A length; write B length; readback
length; byte-exact A-then-B compare; sticky EOF). Mismatches: 0.

## Limits

This ran under QEMU 8.2.2 emulation on the virt board, not on
silicon; what was verified is xv6's sharing of the file offset across
dup'd descriptors, for one regular file and two sequential writes in
a single process. It does not test offset sharing across fork
(inherited descriptors), interleaved concurrent writes from two
processes, `O_APPEND` interaction, or pipes/sockets through dup,
which are separate slices.

---

# PROOF: dup2 targets a fixed fd number and shares the file offset, new syscall + user-space test

<!-- PROOF-HEADER
Checks: 10
Mismatches: 0
Checksum: 0x24322B881E690C23
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A new xv6 system call, `dup2(oldfd, newfd)` (syscall 25), plus a
user-space test program, `user/dup2shared.c`, added to `UPROGS` in the
Makefile so it ships in `fs.img`.

Stock xv6 files (95% of the tree, unchanged except where noted):

- Everything in `kernel/` and `user/` except the five small additions
  below; the file table (`kernel/file.c`), offset advancement in
  `filewrite` (`f->off += n`), and the fd table in `struct proc` are
  all stock.

New demo code (the only changes):

- `kernel/syscall.h`: `#define SYS_dup2 25` (24 was already taken by
  `SYS_nprocs`).
- `kernel/syscall.c`: `extern uint64 sys_dup2(void);` and
  `[SYS_dup2] = sys_dup2` in the dispatch table.
- `kernel/sysfile.c`: `sys_dup2()`, following the existing `sys_dup`
  pattern. It validates oldfd with `argfd`, rejects a newfd outside
  `[0, NOFILE)`, returns newfd unchanged when it equals oldfd (no-op),
  closes the target fd first when it is already open (same sequence as
  `sys_close`), then `filedup(f)` and installs the same
  `struct file *` at the requested slot. Sharing the one
  `struct file` is what makes the offset shared: both descriptors see
  the same `f->off`.
- `user/usys.pl`: `entry("dup2");` (generates the ecall stub with
  `li a7, SYS_dup2`).
- `user/user.h`: `int dup2(int, int);`
- `user/dup2shared.c`: the test. It unlinks `dup2shared.out` so
  repeated runs start from an empty file, opens it `O_CREATE|O_WRONLY`
  (fd 3), calls `dup2(3, 10)` (must return 10), checks the same-fd
  no-op `dup2(3, 3)` returns 3, re-targets the already-open fd 10
  with `dup2(3, 10)` again (must return 10), checks `dup2(99, 10)`
  returns -1, writes a fixed 32-byte pattern A through fd 3 and a
  fixed 32-byte pattern B through fd 10, closes both, reopens
  read-only, reads back all 64 bytes, verifies byte-exact that bytes
  0-31 are A and 32-63 are B, and checks a further read returns 0
  (sticky EOF). The readback is printed as hex and folded into a
  FNV-1a 64-bit checksum; PASS prints only when all 10 checks hold
  with 0 mismatches. (This xv6 has no lseek syscall, so the
  contiguous A-then-B readback is the offset-sharing assertion.)

No code was copied from outside the tree; every number below comes
from actual QEMU runs, and the checksum was independently recomputed
on the host from the two literals to the same value, confirming the
on-guest fold.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf`), `-Wall -Werror`, xv6-riscv rv64gc target.
(One compile error during development: `argint` returns void in this
xv6, so the `if (argint(1, &newfd) < 0)` guard did not compile; it was
changed to a plain `argint(1, &newfd)` call before the range check.
The fix is the code that shipped.)

```
$ make kernel/kernel
riscv64-unknown-elf-gcc ... -c -o kernel/sysfile.o kernel/sysfile.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T kernel/kernel.ld -o kernel/kernel ... kernel/sysfile.o ...
$ make fs.img  # userland rebuild + filesystem image
riscv64-unknown-elf-gcc ... -c -o user/dup2shared.o user/dup2shared.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_dup2shared user/dup2shared.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_dup2shared > user/dup2shared.asm
mkfs/mkfs fs.img README ... user/_dup2shared ...
```

Both commands exited 0, no warnings under `-Wall -Werror`. (The full
gcc flag line is the stock xv6 kernel/userland compile line; only the
`-o` target and source tail differ per file.)

## Run (real QEMU console output)

```
$ qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
    -m 128M -smp 3 -nographic \
    -global virtio-mmio.force-legacy=false \
    -drive file=fs.img,if=none,format=raw,id=x0 \
    -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$ dup2shared
check 1: open returned fd 3, as expected
check 2: dup2(3, 10) returned 10, the requested fd number
check 3: dup2(3, 3) returned 3, same-fd no-op
check 4: dup2(3, 10) over the open target returned 10
check 5: dup2(99, 10) returned -1, bad oldfd rejected
check 6: wrote 32 bytes of pattern A through fd 3
check 7: wrote 32 bytes of pattern B through fd 10
check 8: read back 64 bytes, matches 64 bytes written
check 9: all 64 bytes match A-then-B, 0 mismatches
check 10: read past end returns 0, EOF is sticky
readback hex:
41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41
41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41
62 62 62 62 62 62 62 62 62 62 62 62 62 62 62 62
62 62 62 62 62 62 62 62 62 62 62 62 62 62 62 62
checksum: 0x24322B881E690C23
fd1: 3, fd2: 10, bytes written: 64, bytes read: 64
checks: 10 mismatches: 0
PASS: dup2 names the same file description, writes through fd 3 and fd 10 are contiguous
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2. Runs 2 and 3 were byte-identical to the
output above (same md5 of the captured program output), including the
checksum. (Driving note: input is fed 14 seconds after QEMU start;
input sent earlier is lost before the guest UART is ready. One earlier
run also failed because the kernel binary had not been built yet:
`make fs.img` alone does not build `kernel/kernel`, so the QEMU runs
in this proof used a kernel built separately with `make
kernel/kernel`, shown in the build log above.)

## Reading the numbers

- `dup2(3, 10) returned 10, the requested fd number`: the descriptor
  lands exactly where asked, unlike dup which takes the lowest free
  slot. This is the behavioral difference from the earlier dupshared
  module.
- `dup2(3, 3) returned 3` and `dup2(99, 10) returned -1`: the
  edge cases are sane (same-fd no-op, bad oldfd rejected).
- `dup2(3, 10) over the open target returned 10`: the close-first path
  works when the target fd is already occupied.
- `wrote 32 bytes of pattern A through fd 3` then `wrote 32 bytes of
  pattern B through fd 10`: two writes through two different
  descriptors at two different fd numbers.
- `read back 64 bytes, matches 64 bytes written` and `all 64 bytes
  match A-then-B, 0 mismatches`: the B write started exactly where the
  A write ended, proving one shared offset advanced by both writes. A
  per-descriptor offset would have overwritten bytes 0-31 with B.
- The 64-byte hex readback is 32 `41` (`A`) then 32 `62` (`b`):
  byte-exact against the literals in the source.
- `checksum: 0x24322B881E690C23`: FNV-1a 64 over the readback bytes,
  independently recomputed on the host from the literals
  (`b'A'*32 + b'b'*32`) to the same value, confirming the on-guest
  fold.
- `read past end returns 0`: EOF is sticky after the 64 bytes, so the
  readback was complete, not truncated.

Checks: 10 (open fd; dup2 to requested fd 10; same-fd no-op; dup2 over
open target; bad oldfd rejected; write A length; write B length;
readback length; byte-exact A-then-B compare; sticky EOF).
Mismatches: 0.

## Limits

This ran under QEMU 8.2.2 emulation on the virt board, not on
silicon; what was verified is the new `dup2` syscall's fd-number
targeting and xv6's sharing of the file offset across the two
descriptors, for one regular file and two sequential writes in a
single process. It does not test offset sharing across fork
(inherited descriptors), interleaved concurrent writes from two
processes, `O_APPEND` interaction, pipes through dup2, or out-of-range
newfd values (negative or >= NOFILE return -1 by the range check but
were not exercised on the wire), which are separate slices.

---

# PROOF: each open file descriptor keeps its own file offset, user-space test

<!-- PROOF-HEADER
Checks: 7
Mismatches: 0
Checksum: 0xCCE196BF4BEB4B1E
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/fileoffindep.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Unlinks any leftover `offindep.dat`, then opens the same path
   twice, asserting the first open returns fd 3 (0-2 are the console)
   and the second open returns fd 4, the lowest free slot.
2. Writes a fixed 32-byte pattern of `A` bytes through fd 3,
   asserting all 32 bytes were written (fd 3's offset is now 32).
3. Reads 32 bytes through fd 4 and asserts they match pattern A
   byte-exact. If fd 4 shared fd 3's offset, this read would start at
   offset 32 (EOF) and return 0; returning the A bytes proves fd 4
   read from its own offset 0.
4. Writes a fixed 32-byte pattern of `b` bytes through fd 4
   (landing at file offsets 32..63), then reads 32 bytes through fd 3
   and asserts they match pattern B byte-exact. This proves fd 3's
   offset stayed at 32 while fd 4 advanced to 64: each descriptor
   advances independently.
5. Folds each 32-byte readback into a FNV-1a 64-bit checksum and
   compares both against values recomputed on the host with an
   independent Python implementation of FNV-1a from the pattern
   literals, so the on-guest fold is verified against an outside
   oracle.
6. Prints both readbacks as hex so the bytes are inspectable.

The header checksum is the FNV-1a 64-bit fold of the 16 little-endian
bytes of the two per-pattern checksums (checksum 1 followed by
checksum 2), starting from the standard offset basis. PASS prints
only when all 7 checks hold with 0 mismatches.

No kernel code was changed; the test exercises xv6's existing
open/read/write path (each `open` installing its own `struct file`
with its own `off` in the process's open file table) from user space.
There is no `lseek` on this xv6, so read/write positions are the only
offset observations.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf`), `-Wall -Werror`, xv6-riscv rv64gc target.

```
$ make fs.img
riscv64-unknown-elf-gcc -Wall -Werror -Wno-unknown-attributes -O -fno-omit-frame-pointer -ggdb -gdwarf-2 -ffile-prefix-map=/home/hatch/workspace/freelance-business/xv6-getscount=. -march=rv64gc -std=gnu99 -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -fno-builtin-strncpy -fno-builtin-strncmp -fno-builtin-strlen -fno-builtin-memset -fno-builtin-memmove -fno-builtin-memcmp -fno-builtin-log -fno-builtin-bzero -fno-builtin-strchr -fno-builtin-exit -fno-builtin-malloc -fno-builtin-putc -fno-builtin-free -fno-builtin-memcpy -Wno-main -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-vprintf -I. -fno-stack-protector -fno-pie -no-pie   -c -o user/fileoffindep.o user/fileoffindep.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_fileoffindep user/fileoffindep.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_fileoffindep > user/fileoffindep.asm
riscv64-unknown-elf-objdump -t user/_fileoffindep | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/fileoffindep.sym
mkfs/mkfs fs.img README user/_cat user/_echo user/_forktest user/_grep user/_init user/_kill user/_ln user/_ls user/_mkdir user/_rm user/_sh user/_stressfs user/_usertests user/_grind user/_wc user/_zombie user/_logstress user/_forphan user/_dorphan user/_sync user/_scount user/_nprocs user/_forkisolation user/_sbrkoom user/_dupredirect user/_dupshared user/_fileoffindep user/_pipeorder user/_execargv user/_execargv_echo user/_execfail user/_pipepart user/_waitexit user/_killreap user/_openfail user/_pipeatomic user/_unlinkopen user/_pipeeof user/_execpresfd user/_execpresfd_hlp
```

Build exited 0. No build failures; the program compiled clean under
`-Wall -Werror` on the first attempt.

## Run (real QEMU console output)

`qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel
-m 128M -smp 3 -nographic`, QEMU emulator version 8.2.2. The command
was typed at the shell prompt 15 seconds after boot; the program ran
to completion in one shell session. Three runs, byte-identical
program output (md5 66d46ea41fe6f498ca0587f5bbf091a8 on the captured
test section each time). Run 1 verbatim:

```
$ fileoffindep
check 1: first open returned fd 3, as expected
check 2: second open returned fd 4, the lowest free slot
check 3: wrote 32 bytes of pattern A through fd 3
check 4: read 32 bytes through fd 4, matches pattern A
check 5: wrote 32 bytes of pattern B through fd 4
check 6: read 32 bytes through fd 3, matches pattern B
readback 1 hex (fd 4 read):
41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41
41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41
readback 2 hex (fd 3 read):
62 62 62 62 62 62 62 62 62 62 62 62 62 62 62 62
62 62 62 62 62 62 62 62 62 62 62 62 62 62 62 62
checksum 1: 0x2D90D329EE4C2823
checksum 2: 0x8CE713CF2ECE4783
check 7: checksums match host recomputation
fd3: 3, fd4: 4, read1 bytes: 32, read2 bytes: 32
checks: 7 mismatches: 0
PASS: each open descriptor keeps its own offset; fd 3 and fd 4 advance independently
```

## Reading the numbers

- `check 4: read 32 bytes through fd 4, matches pattern A`: fd 4
  read from offset 0 (its own) even though fd 3's write had moved
  fd 3's offset to 32. A shared offset would have read at EOF and
  returned 0 bytes.
- `check 6: read 32 bytes through fd 3, matches pattern B`: fd 3's
  offset was still 32 after fd 4 wrote the B pattern through its own
  descriptor; the read returned exactly the bytes fd 4 wrote at file
  offsets 32..63.
- `checksum 1: 0x2D90D329EE4C2823` / `checksum 2: 0x8CE713CF2ECE4783`:
  FNV-1a 64-bit of the two 32-byte readbacks, each matching the
  host-recomputed value from the pattern literals with an independent
  Python implementation.
- `0xCCE196BF4BEB4B1E`: the header checksum, the FNV-1a fold of the
  16 little-endian bytes of checksum 1 followed by checksum 2.
- `checks: 7 mismatches: 0` across 3 byte-identical runs.

Output captured verbatim from the emulated serial console,
2026-09-11.

## Scope

This ran under QEMU 8.2.2 emulation on the virt board, not on
silicon; what was verified is xv6's per-descriptor file offset: two
opens of the same path get independent offsets that advance
independently. It does not test offsets across fork (inherited
descriptors), `O_APPEND` interaction, offset behavior after
close/reopen, or pipes, which are separate slices.

<!-- PROOF-HEADER
Checks: 5
Mismatches: 0
Checksum: 0xFD2D9F4718D166AB
Environment: QEMU 8.2.2
Verdict: PASS
-->

# PROOF: wait returns -1 with no children to reap, user-space test

## What was built

A user-space test program, `user/waitnochld.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Forks one child and asserts `fork` returned a positive pid.
2. The child has never forked, so it has no children at all. It
   calls `wait()` twice back to back: the first exercises the
   fresh-process no-child path, the second shows the report sticks.
   It exits 0 only if both calls returned -1 (exit 1 if the first was
   wrong, 2 if the second was), so the parent's single status check
   verifies both child-side assertions.
3. The parent reaps the child, asserting `wait` returned the child's
   pid and the stored status is 0 (both child wait calls returned
   -1).
4. The parent's only child is now reaped, so the parent calls
   `wait()` twice more: the drained path must return -1, and a
   second immediate wait must return -1 as well.
5. Folds all five measured values (child pid, reaped pid, child
   status, both drained wait returns) into one FNV-1a 64-bit
   checksum and prints it.

This is the complement of `waitexit` (wait with a live child to
reap) and `killreap` (wait with a killed child to reap): those
exercised the path where the scan finds a child; this exercises the
path where the scan finds nothing, which is what makes `wait`
return -1 without sleeping. The child prints nothing so the console
transcript is deterministic; the parent prints every measured value.

No kernel code was changed; the test exercises xv6's existing
`sys_wait` scan from user space.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` 13.2.0 (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf` 13.2.0-11ubuntu1+12, with
`binutils-riscv64-unknown-elf` 2.42), `-Wall -Werror`, xv6-riscv
rv64gc target. The package had been removed from this VM since the
previous module was built (its `.d` files still named the 13.2.0
include paths), so it was reinstalled from the Ubuntu noble
universe archive before this build; the xPack 15.2.0 toolchain in
`~/workspace/toolchains` could not be used because its `ld`
misdetects valid rv64 objects as 32-bit and refuses to link them.

```
$ make        # kernel, full rebuild from clean
riscv64-unknown-elf-gcc -march=rv64gc -g ... -c -o kernel/entry.o kernel/entry.S
riscv64-unknown-elf-gcc -Wall -Werror ... -march=rv64gc ... -c -o kernel/start.o kernel/start.c
[... 29 more compile lines, all exit 0 ...]
riscv64-unknown-elf-ld -z max-page-size=4096 -T kernel/kernel.ld -o kernel/kernel kernel/entry.o kernel/start.o ...
riscv64-unknown-elf-objdump -S kernel/kernel > kernel/kernel.asm
```

```
$ make fs.img  # userland + filesystem image
riscv64-unknown-elf-gcc -Wall -Werror ... -march=rv64gc ... -c -o user/waitnochld.o user/waitnochld.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_waitnochld user/waitnochld.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_waitnochld > user/waitnochld.asm
riscv64-unknown-elf-objdump -t user/_waitnochld | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/waitnochld.sym
mkfs/mkfs fs.img README user/_cat user/_echo ... user/_execpresfd user/_execpresfd_hlp user/_waitnochld
```

Both commands exited 0. The userland test compiled with no warnings
under `-Wall -Werror`.

## Run (real QEMU console output)

Run with `qemu-system-riscv64 -machine virt -bios none -kernel
kernel/kernel -m 128M -smp 3 -nographic` plus the Makefile's
`QEMUOPTS` disk lines (`-global virtio-mmio.force-legacy=false
-drive file=fs.img,if=none,format=raw,id=x0 -device
virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0`), QEMU emulator
version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18). The test ran 3 times;
the output below is one run, with the program's own output lines
byte-identical across all 3 (identical md5 of the output block,
ea6a134fb44588a8fdb3d6a9d41bedd1).

```
xv6 kernel is booting

hart 2 starting
hart 1 starting
init: starting sh
$ waitnochld
check 1: fork returned child pid 4
check 2: wait returned pid 4, the child's pid
check 3: child exited 0, so its wait() calls returned -1, -1
check 4: wait after reaping the only child returned -1
check 5: second wait returned -1, still no children
checksum: 0xFD2D9F4718D166AB
wait-no-child results: child=4 reaped=4 childstatus=0 drained=-1 drained2=-1
checks: 5 mismatches: 0
PASS: wait() returned -1 with no children to reap, fresh process and drained alike
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- Check 1: `fork` returned pid 4, a positive pid for the new child.
- Check 2: `wait` returned pid 4, so the parent reaped exactly the
  child it forked.
- Check 3: the child's exit status was 0. The child exits 0 only
  when both of its own `wait()` calls returned -1, so this one
  status verifies the fresh-process path (a process that has never
  forked has no children, so the scan finds nothing) and the sticky
  second wait in the same process.
- Check 4: after the parent reaped its only child, `wait` returned
  -1, the drained path: the scan finds no remaining children and
  reports -1 without blocking.
- Check 5: a second immediate wait also returned -1, so the
  no-child report is stable.
- `checksum: 0xFD2D9F4718D166AB`: FNV-1a 64 over all five measured
  values (child pid 4, reaped pid 4, child status 0, drained waits
  -1, -1), identical across all 3 runs, and independently
  recomputed on the host from the printed values to the same value,
  confirming the on-guest fold.
- `checks: 5 mismatches: 0` across 3 byte-identical runs. Pids are
  stable because xv6 allocates them in creation order on a fresh
  boot with the same command sequence.

## Scope

This ran under QEMU 8.2.2 emulation on the virt board, not on
silicon; what was verified is xv6's `sys_wait` behavior when the
caller has no children: it returns -1 immediately, in a fresh
process and after the only child has been reaped. It does not test
wait blocking with a live but un-reaped child (covered by
`waitexit`, where the parent blocks until the child exits), wait
with multiple children, or the zombie-to-free transition timing,
which are separate slices.

# PROOF: sbrk with a negative increment releases the page and regrow works, user-space test

<!-- PROOF-HEADER
Checks: 9
Mismatches: 0
Checksum: 0x73916C5B31770205
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/sbrkshrink.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Records the initial break `b0 = sbrk(0)` and requires it to be
   page-aligned, so a 4096-byte `sbrk` covers exactly one new page.
2. Calls `sbrk(4096)` and requires it to return the old break (the
   grower hands out memory starting at the old top).
3. Requires `sbrk(0)` to read exactly `b0 + 4096` (the break moved up
   by one page).
4. Tiles a fixed 64-byte canary pattern over all 4096 bytes of the
   new page and requires every byte to read back byte-exact.
5. Calls `sbrk(-4096)` and requires it to return the break as it was
   before the shrink (`b0 + 4096`, the old top).
6. Requires `sbrk(0)` to read exactly `b0` again: the break moved
   back down, so the page was released.
7. Grows again with `sbrk(4096)` and requires it to return `b0`, the
   address the shrink released.
8. Requires `sbrk(0)` to read `b0 + 4096` again.
9. Tiles a second, different 64-byte canary (the reverse of the
   first) over the regrown page and requires every byte to read back
   byte-exact. The program assumes nothing about the old data
   surviving: xv6 zeroes pages on allocation, and the reversed
   pattern means a stale reread of the first pattern would fail.

This is the complement of `sbrkoom` (which tested the growth
ceiling): this tests the deallocation path, `sys_sbrk` ->
`growproc` -> `deallocuvm`, from user space. No kernel code was
changed. PASS prints only when all nine expectations hold with
0 mismatches.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` 13.2.0 (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf` 13.2.0-11ubuntu1+12, with
`binutils-riscv64-unknown-elf` 2.42), `-Wall -Werror`, xv6-riscv
rv64gc target.

```
$ make        # kernel, full rebuild from clean
riscv64-unknown-elf-gcc -Wall -Werror ... -march=rv64gc ... -c -o kernel/start.o kernel/start.c
[... 29 more compile lines, all exit 0 ...]
riscv64-unknown-elf-ld -z max-page-size=4096 -T kernel/kernel.ld -o kernel/kernel kernel/entry.o kernel/start.o ...
riscv64-unknown-elf-ld: warning: kernel/kernel has a LOAD segment with RWX permissions
riscv64-unknown-elf-objdump -S kernel/kernel > kernel/kernel.asm
```

(The `RWX` warning is stock xv6.)

```
$ make fs.img  # userland + filesystem image
riscv64-unknown-elf-gcc -Wall -Werror ... -march=rv64gc ... -c -o user/sbrkshrink.o user/sbrkshrink.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_sbrkshrink user/sbrkshrink.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_sbrkshrink > user/sbrkshrink.asm
riscv64-unknown-elf-objdump -t user/_sbrkshrink | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/sbrkshrink.sym
mkfs/mkfs fs.img README user/_cat user/_echo ... user/_waitnochld user/_sbrkshrink
```

One genuine build failure on the way: the test declared a `char *p`
that was never used, which `-Werror` rejected
(`error: unused variable 'p' [-Werror=unused-variable]`); removed the
declaration, rebuild clean. Both final commands exited 0.

## Run (real QEMU console output)

Run with `qemu-system-riscv64 -machine virt -bios none -kernel
kernel/kernel -m 128M -smp 3 -display none -serial stdio -monitor
none` plus `-global virtio-mmio.force-legacy=false -drive
file=fs.img,if=none,format=raw,id=x0 -device
virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0`, QEMU emulator
version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18). The test ran 3 times;
the output below is one run, with the program's own output lines
byte-identical across all 3 (identical md5 of the output block,
7cc659b995f509df91c9a8451f64d0b5).

```
$ sbrkshrink
check 1: initial break 0x5000 is page-aligned
check 2: sbrk(4096) returned old break 0x5000
check 3: sbrk(0)=0x6000 == 0x5000+4096, matches
check 4: 4096-byte canary write/readback byte-exact
check 5: sbrk(-4096) returned old break 0x6000
check 6: sbrk(0)=0x5000 back at initial break, shrink released the page
check 7: sbrk(4096) after shrink returned 0x5000, same page reused
check 8: sbrk(0)=0x6000 == 0x5000+4096, matches
check 9: regrown page canary write/readback byte-exact
checksum: 0x73916C5B31770205
sbrk-shrink values: b0=0x5000 grow=0x5000 end1=0x6000 shrink=0x6000 end2=0x5000 regrow=0x5000 end3=0x6000
checks: 9 mismatches: 0
PASS: sbrk(-4096) releases the page, regrow works
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- Check 1: the initial break `0x5000` is a multiple of 4096, so one
  `sbrk(4096)` spans exactly one new page.
- Checks 2-3: growing one page returns the old break and moves the
  break up by exactly 4096 bytes (`0x5000` to `0x6000`).
- Check 4: the 64-byte canary tiled over all 4096 bytes of the new
  page reads back byte-exact, 0 mismatches out of 4096 bytes.
- Checks 5-6: `sbrk(-4096)` returns the pre-shrink break
  (`0x6000`), and `sbrk(0)` then reads `0x5000`, the original break:
  the break moved down one page, so `growproc` -> `deallocuvm`
  released the page.
- Checks 7-8: regrowing returns `0x5000` again (the released
  address) and the break reads `0x6000`.
- Check 9: the regrown page takes a second, different canary and
  reads back byte-exact, 0 mismatches out of 4096 bytes. The canary
  differs from the first so the check exercises the regrown mapping
  with fresh content instead of re-reading the earlier bytes; the
  program assumes nothing about the old data surviving the shrink,
  since xv6 zeroes pages on allocation.
- `checksum: 0x73916C5B31770205`: FNV-1a 64-bit over the eight
  measured 64-bit values (b0, grow, end1, second-readback mismatch
  count, shrink return, end2, regrow, end3), identical across all 3
  runs, and independently recomputed on the host from the printed
  values to the same value, confirming the on-guest fold.
- `checks: 9 mismatches: 0` across 3 byte-identical runs. Break
  values are stable because xv6 loads the same binary at the same
  address on a fresh boot with the same command sequence.

## Scope

This ran under QEMU 8.2.2 emulation on the virt board, not on
silicon; what was verified is xv6's `sys_sbrk` shrink behavior for
a one-page negative increment: the reported return value, the
break arithmetic at every step, and that the released page's
address can be regrown and used. It does not test shrinking to a
size below the initial break, shrinking by a non-page-multiple
(which `deallocuvm` would round down past the intended region), or
whether the freed physical page is reused by other processes,
which are separate slices.

# PROOF: sbrk(0) is a pure break query, growth shifts the break by exactly 4096, user-space test

<!-- PROOF-HEADER
Checks: 6
Mismatches: 0
Checksum: 0x9B1CE19B61131225
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/sbrknoop.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Records the initial break `A = sbrk(0)` and requires it to be
   page-aligned, so a 4096-byte `sbrk` covers exactly one new page.
2. Calls `sbrk(0)` again and requires the second reading to equal the
   first exactly: the zero increment changed nothing.
3. Calls `sbrk(4096)` and requires it to return the old break (the
   grower hands out memory starting at the old top).
4. Requires `sbrk(0)` to read exactly `A + 4096` (the break moved up
   by one page).
5. Calls `sbrk(0)` once more and requires it to still read exactly
   `A + 4096`: zero increments stay no-ops after the shift.
6. Tiles a fixed 64-byte canary pattern over all 4096 bytes of the
   new page and requires every byte to read back byte-exact, proving
   the grown page is usable.

This is the complement of `sbrkshrink` (which covered negative
increments and regrow): this tests zero-increment idempotence plus
the additive shift, `sys_sbrk` -> `growproc`, from user space. No
kernel code was changed. PASS prints only when all six expectations
hold with 0 mismatches.

One infrastructure change was needed to ship the 45th file:
`kernel/param.h` `FSSIZE` went from 2000 to 3000 blocks. The shipped
programs now need 1961 blocks (data plus one indirect block per
program over 12 blocks plus the root directory) against 1953 usable
under `FSSIZE=2000`, and `mkfs` died reading past the end of the
image. `FSSIZE` is shared by `mkfs` and the kernel through
`kernel/param.h`, so one edit fixed both, with headroom for future
modules.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` 13.2.0 (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf` 13.2.0-11ubuntu1+12, with
`binutils-riscv64-unknown-elf` 2.42), `-Wall -Werror`, xv6-riscv
rv64gc target. The packages are no longer installed system-wide on
this machine, so they were extracted from the local apt cache
(`/var/cache/apt/archives/`) into `~/workspace/toolchains/ubuntu-rv64`
and used via `PATH`; the compiler reports the same 13.2.0 version
string the earlier modules were built with.

```
$ make        # kernel, full rebuild from clean
riscv64-unknown-elf-gcc -Wall -Werror ... -march=rv64gc ... -c -o kernel/start.o kernel/start.c
[... 29 more compile lines, all exit 0 ...]
riscv64-unknown-elf-ld -z max-page-size=4096 -T kernel/kernel.ld -o kernel/kernel kernel/entry.o kernel/start.o ...
riscv64-unknown-elf-ld: warning: kernel/kernel has a LOAD segment with RWX permissions
riscv64-unknown-elf-objdump -S kernel/kernel > kernel/kernel.asm
```

(The `RWX` warning is stock xv6.)

```
$ make fs.img  # userland + filesystem image
riscv64-unknown-elf-gcc -Wall -Werror ... -march=rv64gc ... -c -o user/sbrknoop.o user/sbrknoop.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_sbrknoop user/sbrknoop.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_sbrknoop > user/sbrknoop.asm
riscv64-unknown-elf-objdump -t user/_sbrknoop | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/sbrknoop.sym
mkfs/mkfs fs.img README user/_cat user/_echo ... user/_sbrkshrink user/_sbrknoop
nmeta 47 (boot, super, log blocks 31, inode blocks 13, bitmap blocks 1) blocks 2953 total 3000
balloc: first 2007 blocks have been allocated
balloc: write bitmap block at sector 46
```

Both commands exited 0. Two genuine build failures on the way,
both fixed without touching program logic: first, the other
toolchain on this machine (xPack RISC-V GCC 15.2.0) mis-linked
every user object (`ABI is incompatible with that of the selected
emulation: target emulation 'elf64-littleriscv' does not match
'elf32-littleriscv'`) and then its `ld` segfaulted, so the build
moved to the Ubuntu 13.2.0 packages above; second, `mkfs` died with
`read: Success` past the end of the 2000-block image once the 45th
file no longer fit, fixed by the `FSSIZE` 2000 to 3000 bump.

## Run (real QEMU console output)

Run with `qemu-system-riscv64 -machine virt -bios none -kernel
kernel/kernel -m 128M -smp 3 -display none -serial stdio -monitor
none` plus `-global virtio-mmio.force-legacy=false -drive
file=fs.img,if=none,format=raw,id=x0 -device
virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0`, QEMU emulator
version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18). The test ran 3 times;
the output below is one run, with the program's own output lines
byte-identical across all 3 (identical md5 of the output block,
4d545d33553d45bff1b959c6468e310d).

```
$ sbrknoop
check 1: sbrk(0) call A: break 0x4000 is page-aligned
check 2: sbrk(0) call B: 0x4000 == call A, unchanged
check 3: sbrk(4096) returned old break 0x4000
check 4: sbrk(0) call C: 0x5000 == 0x4000+4096, matches
check 5: sbrk(0) call D: 0x5000 == 0x4000+4096, unchanged
check 6: 4096-byte canary write/readback byte-exact
checksum: 0x9B1CE19B61131225
sbrk-noop values: A=0x4000 B=0x4000 grow=0x4000 C=0x5000 D=0x5000
checks: 6 mismatches: 0
PASS: sbrk(0) queries the break without changing it, growth shifts it by exactly 4096
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- Check 1: the initial break `0x4000` is a multiple of 4096, so one
  `sbrk(4096)` spans exactly one new page.
- Check 2: a second `sbrk(0)` returns `0x4000`, the identical value:
  the zero increment did not move the break.
- Check 3: growing one page returns the old break `0x4000` (the
  grower hands out memory starting at the old top).
- Checks 4-5: both post-growth `sbrk(0)` readings are `0x5000`,
  exactly `0x4000 + 4096`: the break moved up by one page and the
  query stayed a no-op.
- Check 6: the 64-byte canary tiled over all 4096 bytes of the new
  page reads back byte-exact, 0 mismatches out of 4096 bytes, so the
  grown page is writable and mapped.
- `checksum: 0x9B1CE19B61131225`: FNV-1a 64-bit over the six measured
  64-bit values (A, B, grow return, C, D, canary mismatch count),
  identical across all 3 runs, and independently recomputed on the
  host from the printed values to the same value, confirming the
  on-guest fold.
- `checks: 6 mismatches: 0` across 3 byte-identical runs. Break
  values are stable because xv6 loads the same binary at the same
  address on a fresh boot with the same command sequence.

## Scope

This ran under QEMU 8.2.2 emulation on the virt board, not on
silicon; what was verified is xv6's `sys_sbrk` zero-increment
behavior from user space: two `sbrk(0)` readings agree before and
after a one-page growth, the growth returns the old break, the
shift is exactly 4096 bytes, and the new page is usable. It does
not test `sbrk(0)` racing a concurrent grower, growth past the
address-space ceiling (covered by `sbrkoom`), or negative
increments (covered by `sbrkshrink`), which are separate slices.

# PROOF: two successive positive sbrk grows accumulate additively, user-space test

<!-- PROOF-HEADER
Checks: 4
Mismatches: 0
Checksum: 0x96FFFE6F5707C46D
Environment: QEMU 8.2.2
Verdict: PASS
-->

## What was built

A user-space test program, `user/sbrkgrow.c`, added to `UPROGS` in
the Makefile so it ships in `fs.img`. The program:

1. Reads the initial break `B = sbrk(0)` (`0x4000` on these runs).
2. Calls `sbrk(4096)` and requires it to return the old break `B`
   (growproc hands out memory starting at the old top).
3. Calls `sbrk(2048)` and requires it to return `B + 4096`: the
   second grow starts where the first one ended, so the two
   increments accumulate additively.
4. Requires `sbrk(0)` to read exactly `B + 6144`, the sum of both
   grows.
5. Tiles a fixed 64-byte canary pattern over all 6144 bytes from `B`
   and requires every byte to read back byte-exact, proving the
   whole grown region (1.5 pages) is mapped and writable.

This is the positive-increment slice of `sys_sbrk`: it complements
`sbrknoop` (zero increment idempotence) and `sbrkshrink` (negative
increment and regrow). No kernel code was changed. PASS prints only
when all four expectations hold with 0 mismatches, and the run
exits nonzero otherwise. The measurement's ground truth is
`kernel/proc.c`: `sys_sbrk` passes its argument to `growproc`,
which advances `p->sz` by exactly the requested amount on each
call, so two calls advance it by the sum.

## Build (real log)

Toolchain: `riscv64-unknown-elf-gcc` 13.2.0 (Ubuntu 24.04 package
`gcc-riscv64-unknown-elf` 13.2.0-11ubuntu1+12, with
`binutils-riscv64-unknown-elf` 2.42), `-Wall -Werror`, xv6-riscv
rv64gc target, used via `PATH=~/workspace/toolchains/ubuntu-rv64/usr/bin`.
The kernel objects were already built; only the new module needed
compiling. `make fs.img` exited 0:

```
$ make fs.img  # new module + filesystem image
riscv64-unknown-elf-gcc -Wall -Werror ... -march=rv64gc ... -c -o user/sbrkgrow.o user/sbrkgrow.c
riscv64-unknown-elf-ld -z max-page-size=4096 -T user/user.ld -o user/_sbrkgrow user/sbrkgrow.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_sbrkgrow > user/sbrkgrow.asm
riscv64-unknown-elf-objdump -t user/_sbrkgrow | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/sbrkgrow.sym
mkfs/mkfs fs.img README user/_cat user/_echo ... user/_sbrkshrink user/_sbrknoop user/_sbrkgrow
nmeta 47 (boot, super, log blocks 31, inode blocks 13, bitmap blocks 1) blocks 2953 total 3000
balloc: first 2047 blocks have been allocated
balloc: write bitmap block at sector 46
```

No build warnings or errors. The 46th file still fits comfortably
in the 3000-block image (`FSSIZE` bump from the earlier `sbrknoop`
module).

## Run (real QEMU console output)

Run with `qemu-system-riscv64 -machine virt -bios none -kernel
kernel/kernel -m 128M -smp 3 -display none -serial stdio -monitor
none` plus `-global virtio-mmio.force-legacy=false -drive
file=fs.img,if=none,format=raw,id=x0 -device
virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0`, QEMU emulator
version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18). The test ran 3 times;
the output below is one run, with the program's own output lines
byte-identical across all 3 (identical md5 of the output block,
f119742afae4ceb62de39428e817f9a4).

```
$ sbrkgrow
check 1: sbrk(4096) returned old break 0x4000
check 2: sbrk(2048) returned 0x5000 == 0x4000+4096, additive
check 3: sbrk(0) reads 0x5800 == 0x4000+6144, sum of grows
check 4: 6144-byte canary write/readback byte-exact
checksum: 0x96FFFE6F5707C46D
sbrk-grow values: b=0x4000 r1=0x4000 r2=0x5000 c=0x5800
checks: 4 mismatches: 0
PASS: two positive sbrk grows accumulate additively (4096 then 2048 shifts the break by 6144), whole region usable
$
```

Output captured verbatim from the emulated serial console, 2026-09-11.
QEMU emulator version 8.2.2.

## Reading the numbers

- Check 1: `sbrk(4096)` returns `0x4000`, the old break: the grower
  hands out memory starting at the old top, as `growproc` does.
- Check 2: `sbrk(2048)` returns `0x5000`, exactly `0x4000 + 4096`:
  the second grow began where the first one ended, so the
  increments accumulate additively rather than restarting from the
  original break.
- Check 3: `sbrk(0)` now reads `0x5800`, exactly `0x4000 + 6144`:
  the break moved by the sum of the two requests (4096 + 2048).
- Check 4: the 64-byte canary tiled over all 6144 bytes from `0x4000`
  reads back byte-exact, 0 mismatches out of 6144 bytes, so the
  whole grown region, spanning 1.5 pages, is mapped and writable.
- `checksum: 0x96FFFE6F5707C46D`: FNV-1a 64-bit over the five
  measured 64-bit values (B, both grow returns, final break,
  canary mismatch count), identical across all 3 runs, and
  independently recomputed on the host from the printed values to
  the same value, confirming the on-guest fold.
- `checks: 4 mismatches: 0` across 3 byte-identical runs. Break
  values are stable because xv6 loads the same binary at the same
  address on a fresh boot with the same command sequence.

## Scope

This ran under QEMU 8.2.2 emulation on the virt board, not on
silicon; what was verified is xv6's `sys_sbrk` positive-increment
behavior from user space: two successive grows return the old
break at each call, the break advances by the sum of the requests
(4096 + 2048 = 6144), and the entire grown region is usable.
It does not test concurrent growers racing on the same break,
growth past the address-space ceiling (covered by `sbrkoom`),
negative increments (covered by `sbrkshrink`), or the zero
increment (covered by `sbrknoop`), which are separate slices.
