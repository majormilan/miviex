# Miviex roadmap

Goal: a POSIX, mostly Linux-compatible system written to the suckless
standard, running on all major hypervisors, with suckless sbase/ubase,
shush as the shell and simpled as the init.

The phases are the kernel's, in order; each ends with a check that runs
on Miviex itself. The shell, the init and the userland have their own
tracks below, tied to the kernel phase they wait for.

## Done

- Higher-half x86-64 kernel, PMM/VMM, heap, preemptive scheduler.
- Linux syscall ABI (`syscall`/`sysret`), static ELF loading.
- Processes: `fork`, `execve`, `wait4`, pipes, file descriptors, cwd.
- Signals, sessions and process groups.
- Terminals: VGA (VT100 subset) and serial consoles, termios.
- Userland from git submodules: shush as `/bin/sh`, sbase, ubase, and
  simpled as `/init`.
- Phase 1: wall clock and time syscalls; inode metadata (mode, owner,
  links, timestamps, device numbers); `chmod`/`chown`/`utimensat`,
  `mkdir`/`rmdir`/`unlink`/`rename`/`link`/`symlink`/`readlink`,
  `truncate`, `pread`/`pwrite`; symlinks in path resolution; stacks that
  grow on demand; `rootfs/` with `/etc` base files; `uname`.
- Phase 2: real/effective/saved uid and gid, supplementary groups and
  the set*id family; Linux permission checks on lookup, open, exec,
  create, unlink, rename (sticky directories), chmod, chown, utimensat,
  truncate, chdir and kill; access() on the real ids; setuid/setgid
  executables with AT_SECURE; poll/ppoll.
- Phase 3: accounts root and miviex (SHA-512 in /etc/shadow); init runs
  ubase getty on tty1 and ttyS0; login, su and passwd work; vhangup,
  utmp in /var/run; resource limits; a ChaCha20 kernel RNG with
  getrandom and /dev/{,u}random; /proc/self and /proc/<pid>/fd links;
  concatenated initramfs archives (per-user home directories).
- Phase 4: /proc in Linux's formats (listing, per-process stat, status,
  cmdline, comm, cwd, exe, fd; mounts, meminfo, uptime, loadavg,
  version); mount/umount with a mount table, tmpfs and read-only
  remounts; statfs; reboot/halt/poweroff and Ctrl+Alt+Del; the kernel
  log behind syslog(2); sysinfo, times, getrusage; #! scripts.
- Phase 5: ACPI tables (MADT, FADT, HPET, MCFG) with ACPI poweroff and
  reset; ioremap above the direct map; PCI enumeration with BARs and
  MSI/MSI-X capabilities; local APIC and I/O APIC routing (8259 as the
  fallback), shared IRQ lines, vectors for MSI; drivers register their
  /dev nodes; DMA memory. Tested on QEMU pc and q35; VirtualBox and
  VMware still to be tried.
- simpled phases 0-4: units, dependencies, restarts, clean shutdown,
  `simplectl` (see `ext/simpled/ROADMAP.md`).

## Where things stand (review, October 2026)

- Kernel: a sound single-CPU base. The weak points are a few security
  holes (a NULL dereference reachable from user space, `/dev/tty`
  falling back to the console), `readdir` that skips entries while
  files are removed, missing `pause`/`sigsuspend`/`clone`, and a VFS
  whose operations never see the open file (so no `O_NONBLOCK`) and
  whose sizes are 32-bit. The only locking is "interrupts off for the
  whole syscall", which disk I/O and SMP will break.
- shush: a good interactive command runner, not yet a POSIX sh. No
  shell variables, keywords, functions, `-c` or multi-line input, and
  memory bugs in expansion (`${#x}` and long words crash it). Its line
  editor flushes input when it switches terminal modes, which is why
  typing on the serial console loses characters. The front end needs
  a rewrite; the line editor, globbing and builtins can stay.
- simpled: phases 0-4 done and checked on Miviex.
- sbase/ubase: about 85 sbase tools and most of ubase work today.
  The rest wait on FIFOs and `mknod`, a vfork-style `clone` (every
  `system`/`popen`), block devices, and a few small syscalls. sbase's
  tests need a POSIX sh and a `diff`, which neither has.

## Phase 6: hardening and a test harness (next)

Kernel:
- `make check`: boot the ISO in QEMU with a test unit, run test
  scripts, read the results from the serial port, power off. Every
  later phase adds its checks here.
- Creating a file in a removed directory dereferences NULL and panics
  (`vfs.c` create/link/rename, `ramfs.c`): return `ENOENT`. Refuse
  `PT_LOAD` below 64 KiB, so user space cannot map page 0.
- `/dev/tty` without a controlling terminal must fail with `ENXIO`,
  not open the console (`tty.c` dev_tty); `TIOCSCTTY` steals a
  terminal only for root with arg 1.
- init without the console as its controlling terminal, as on Linux;
  `TIOCNOTTY`.
- `getdents` offsets that stay valid while entries are removed or
  renamed: today `rm -r` on a large directory leaves files behind.
- Orphans reparented to init send it SIGCHLD; children are reaped
  automatically when SIGCHLD is ignored or `SA_NOCLDWAIT`; `wait4(0)`
  and `wait4(-pgid)` match the process group.
- `pause` and `rt_sigsuspend`; `ppoll` applies its signal mask.
- Pipe writes up to `PIPE_BUF` are atomic, `writev` is one write.
- The idle loop reschedules when an interrupt wakes a process (now a
  keystroke waits up to the idle task's quantum: 34 ms median).
- `umount2` accepts `MNT_DETACH`/`MNT_FORCE`; `setuid(-1)` is
  `EINVAL`; `AT_RANDOM` from the RNG; zero-length reads return at once;
  `TIOCSWINSZ`/`TIOCGWINSZ` per terminal.
- Dead code and stale comments out of `kernel.c`, `scheduler.c`, `io.c`.

Shell (S1), init and userland: see the tracks.

Done when `make check` passes: the NULL-dereference case returns
`ENOENT`, `rm -r` of 300 files leaves none, `/dev/tty` fails without a
terminal, text typed at full speed on the serial console arrives
whole, and a stopped unit's orphans are reaped at once.

## Phase 7: syscalls the userland is missing

- `clone(CLONE_VM | CLONE_VFORK)`, the vfork-style subset musl's
  `posix_spawn` uses: `system` and `popen` work (ed and dc `!`, watch).
  Threads stay in phase 12.
- `mknod`/`mknodat` with FIFOs (a pipe on an inode) and device nodes:
  mkfifo, mknod, special files in `cp -a`, `mv` and `tar`, respawn `-l`.
- `getpriority`/`setpriority` (nice, renice), `chroot`, `flock`,
  `fallocate`, `readahead` (may do nothing).
- `/dev/rtc` with `RTC_RD_TIME`/`RTC_SET_TIME` (hwclock); the CMOS
  clock is already read.

Done when every tool kept in the image passes a smoke test in
`make check` (userland track).

## Phase 8: VFS rework

The VFS needs to change before a disk file system sits under it.

- File operations get the open file: `O_NONBLOCK` for pipes and
  terminals, which simpled's log collector needs.
- 64-bit offsets and sizes (`lseek` stops at 4 GiB today).
- Superblocks and shared operation tables; an inode cache keyed by
  (device, inode); dentries that can be evicted, with hashed lookup.
- `NAME_MAX` 255, `PATH_MAX` buffers instead of 128 and 256 byte
  arrays (execve, chdir, getcwd, /proc fd links).
- Path lookup as a loop, not recursion (kernel stacks are 16 KiB with
  no guard page; deep symlinks overflow them).
- Wait queues per object instead of scanning every process on
  `wakeup` and re-polling on every tick.
- `copy_from_user`/`copy_to_user` with fault fixup instead of check
  then dereference; a big kernel lock taken explicitly, so code that
  sleeps in disk I/O (phase 9) and SMP (phase 14) have something to
  build on.

Done when a service that floods its output cannot stall PID 1 (simpled
phase 5), names of 255 characters work, and a file grows past 4 GiB on
tmpfs (memory permitting) or sparse.

## Phase 9: storage and persistence

- Block layer: request interface, buffer cache, MBR/GPT partitions.
- Drivers: ATA/IDE (PIO), AHCI, virtio-blk (the first users of MSI).
- ext2, read and write; mounting a disk root (`root=`), `sync`.
- `MS_BIND` and `MS_MOVE` (switch_root, `mount --bind`); block ioctls
  (`BLKGETSIZE64`, `BLKDISCARD`, `FIFREEZE`).
- A clock source from the TSC or HPET instead of counting ticks: ticks
  are lost while syscalls run with interrupts off.

Done when `passwd` and files in `/home` survive a reboot, and shutdown
remounts the root read-only.

## Phase 10: musl as the system libc

- musl from a pinned submodule and an `x86_64-linux-musl` cross
  toolchain instead of the host's musl-tools.
- Headers (musl and Linux UAPI) and libraries installed in the image.
- shush built with musl (it already builds cleanly with musl-gcc).

## Phase 11: memory management

- File-backed `mmap`, demand paging, copy-on-write `fork` (fork copies
  everything now, with interrupts off).
- RAM above 1 GiB; NX for data pages; `mprotect`/`munmap` errors.
- A slab allocator and mapped (not physically contiguous) large
  allocations; kernel stacks with guard pages.
- `PT_INTERP`: dynamically linked programs with musl's `ld-musl`.
- Swap, if it is ever wanted (swapon, swapoff, mkswap).

## Phase 12: the missing POSIX pieces

- `select`, Unix domain sockets.
- Job control: stopping and continuing (`SIGTSTP`, `SIGCONT`,
  `WUNTRACED`); `SIGHUP` and the terminal released when a session
  leader exits.
- `alarm`/`setitimer`; `futex` and `clone` threads (on the user copy
  functions of phase 8).

Done when shush suspends and resumes a job with `^Z`/`fg`, and
`logger` reaches simpled's log.

## Phase 13: every tool, a smaller image

- Run every tool and sbase's tests on Miviex until no syscall is
  reported as unimplemented.
- Multi-call `sbase-box`/`ubase-box` with symlinks to shrink the image.

Done when sbase's test suite passes on Miviex.

## Phase 14: portability and networking

- UEFI boot and a framebuffer console; SMP (real locks in place of the
  big kernel lock); HPET or APIC timer instead of the PIT.
- NVMe; Hyper-V VMBus (storvsc, netvsc, keyboard).
- A TCP/IP stack with e1000, virtio-net and vmxnet3; virtio-rng (tftp).
- USB (xHCI, keyboards); a small sysfs for lsusb.

## Shell track: shush as a POSIX sh

shush is `/bin/sh` and the login shell. It is developed in its own
repository and tested on Linux against dash.

- S1, with phase 6: the crashes (`${#x}`, `${?}` and `${` overflow the
  stack; any expansion followed by a long word overflows the heap;
  `fg`/`bg` without an argument free a string literal; more than 1024
  words; parse errors inside `$(...)` leave a second shell running).
  `TCSANOW` instead of `TCSAFLUSH` and no keystrokes eaten while asking
  for the cursor position (the serial console bug). `-c` and `-s`;
  `$?` after every command, not every line; exit status 127, 126 and
  128+n. An ASan build and a test runner that compares with dash.
- S2, from phase 7 on: a new front end. A reader that asks for more
  lines, a tokenizer that builds words from quoted and unquoted parts
  (backslashes, `"$(...)"`), a recursive-descent parser for the POSIX
  grammar, shell variables with `export` and `readonly`, assignments
  and `VAR=x cmd`, `if`/`while`/`for`/`until`/`case`, `{ }`, `( )` in a
  subshell, functions.
- S3, before phase 13: all of `${...}`, `$((...))`, `$$ $! $# $@ "$@"`,
  field splitting, globbing that respects quotes, here-documents; the
  builtins `.`, `exec`, `eval`, `set -e -u -x --`, `shift`, `read`,
  `trap`, `wait`, `command`, `:`, `true`, `false`, `test`/`[`,
  `return`, `local`. Login shells read `/etc/profile` and
  `~/.profile`; the `-l` patch goes upstream. Done when sbase's tests
  run under shush.
- S4, with phase 12: job control with process groups and `tcsetpgrp`,
  `&` anywhere, `jobs`/`fg`/`bg`.

Until S3, units and system scripts avoid `sh -c` beyond simple
commands.

## Init track: simpled

Its roadmap is `ext/simpled/ROADMAP.md`; how it lines up:

- With phase 6: read `/proc/cmdline` for `emergency` and
  `simpled.unit=` (the kernel does not pass arguments to init); freeze
  in `sigsuspend` instead of a spinning `pause`; drop its workarounds
  once `ppoll` and init's terminal are fixed.
- With phase 8: simpled phase 5, logging: a pipe per service, read by
  PID 1, timestamps, rotation, `simplectl log`.
- With phase 9: a disk root, `fsck` and remount units, logs that
  survive a reboot; wtmp and lastlog written at login (for last and
  lastlog).
- With phase 12: `simplectl` over a socket, `Type=notify`, and the log
  collector listening on `/dev/log` (for logger and cron).

## Userland track: sbase and ubase

- With phase 6: smoke tests for the tools in `make check`.
- With phase 7: a `diff` (neither sbase nor ubase has one, and 45 of
  sbase's 55 tests need it). Stop shipping tools that cannot work
  here: insmod, rmmod, lsmod, unshare, pivot_root, eject, freeramdisk;
  and, until their hardware or kernel support exists, chvt, vtallow,
  lsusb, swap tools, blkdiscard, fsfreeze, tftp. Do not build ubase
  halt, dd and mknod, which are replaced anyway.
- With phase 13: sbase's test suite as a gate; the multi-call binaries.
- Patches go upstream when they are general (serial tty names in ps).

## Not planned

Loadable modules, namespaces and `pivot_root`, `/proc/sys` beyond what
a real need brings, CD-ROM and RAM-disk ioctls.

Until phase 9 the system lives in the initramfs built from `rootfs/`:
changes made at run time, `passwd` included, are lost on reboot.
