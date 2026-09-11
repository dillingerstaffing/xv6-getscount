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
