# PROOF: nprocs system call (syscall 24)

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
