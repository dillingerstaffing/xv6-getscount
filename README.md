# xv6 Extension: Per-Process Syscall Accounting (`getscount`)

A clean, single-feature extension to MIT's xv6 operating system (RISC-V):
a new system call that reports how many times the calling process has
invoked any given syscall. Built as portfolio proof of OS-internals skill:
kernel data structures, the syscall dispatch path, user/kernel interface
wiring, and a user-space test program.

## What it does

Each process now carries a small table of per-syscall counters. Every time
a process traps into the kernel through the syscall dispatcher, the
dispatcher increments that process's counter for the invoked call. A new
system call, `getscount(n)`, returns the calling process's count for
syscall number `n` (see `kernel/syscall.h` for the numbers), or -1 if `n`
is not a valid syscall number.

Why this is useful: it is the same accounting idea behind tools like
`strace -c`, implemented at the point where xv6 has the most information
(the trap dispatcher), with zero per-call overhead beyond one increment.

## Design notes

* **Counting point:** `syscall()` in `kernel/syscall.c`, the single funnel
  every system call passes through. The counter is bumped before dispatch,
  so the count includes the in-flight call (this is observable when a
  program calls `getscount` on itself, and the demo relies on it).
* **Storage:** `uint64 scounts[MAXSYSCALLS]` in `struct proc`
  (`kernel/proc.h`), indexed directly by syscall number, so lookup is O(1).
  `MAXSYSCALLS` is 32, comfortably above the 23-entry dispatch table.
* **Correctness guard:** a `_Static_assert` in `kernel/syscall.c` fails the
  build if the dispatch table ever outgrows the counter array, instead of
  silently overflowing at runtime.
* **Lifecycle:** counters are zeroed by `allocproc` (which memsets the whole
  `struct proc`), so every new process starts at 0 and forked children do
  not inherit their parent's counts. The demo program verifies both facts.

## Files changed

| File | Change |
|---|---|
| `kernel/syscall.h` | Added `#define SYS_getscount 23` |
| `kernel/proc.h` | Added `MAXSYSCALLS` and `scounts[]` to `struct proc`, with comments |
| `kernel/syscall.c` | Dispatch-table entry, per-call increment in `syscall()`, compile-time size check |
| `kernel/sysproc.c` | Implemented `sys_getscount` (argument validation, returns count or -1) |
| `user/usys.pl` | Added the `getscount` assembly stub entry |
| `user/user.h` | Added `int getscount(int);` declaration |
| `user/scount.c` | New demo/test program (see below) |
| `Makefile` | Added `scount` to `UPROGS` so it ships in the filesystem image |

## How to build and test in QEMU

Prerequisites: a RISC-V cross toolchain and QEMU, e.g. on Debian/Ubuntu:

```
sudo apt-get install gcc-riscv64-linux-gnu qemu-system-misc
```

Then, from this directory:

```
make            # builds the kernel and user programs
make qemu       # boots xv6 in QEMU (serial console on stdio)
```

At the xv6 shell prompt, run the demo:

```
$ scount
```

## Sample output (verified)

```
$ scount
getpid called 5 times, kernel reports: 5
getscount(999) = -1 (expect -1)
getscount(-1)  = -1 (expect -1)
child:  getpid count = 0 (expect 0)
parent: getpid count = 5 (expect 5)
PASS
```

What each line proves:

* The kernel counted exactly the 5 `getpid` calls the program made.
* Out-of-range syscall numbers are rejected with -1, not trusted blindly.
* A forked child starts with zeroed counters: accounting is per-process.
* The parent's counters are unaffected by what the child did.

## Limitations (honest scope)

* Counters are per-process only; there is no system-wide aggregation.
* The table is sized for xv6's small syscall set (32 slots); a production
  version would size it from the table itself or use a resizable structure.
* No locking is needed today because a process's counters are only touched
  while that process is running on its own CPU, but a version that let one
  process read another's counters would need `p->lock` discipline.

Upstream base: MIT xv6-riscv (https://github.com/mit-pdos/xv6-riscv),
commit `9e3161a`.
