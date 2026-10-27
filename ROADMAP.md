# Miviex roadmap

Goal: a POSIX, mostly Linux-compatible system written to the suckless
standard, running on all major hypervisors, with suckless sbase/ubase,
shush as the shell and its own init.

Each phase depends on the ones before it and ends with a check that can
be run on Miviex itself.

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

## Phase 6: storage and persistence (next)

- Block layer: request interface, buffer cache, MBR/GPT partitions.
- Drivers: ATA/IDE (PIO), AHCI, virtio-blk (the first users of MSI).
- ext2, read and write; mounting a disk root (`root=`), `sync`.

Done when `passwd` and files in `/home` survive a reboot.

## Phase 7: musl as the system libc

- musl from a pinned submodule and an `x86_64-linux-musl` cross
  toolchain instead of the host's musl-tools.
- Headers (musl and Linux UAPI) and libraries installed in the image.
- shush built with musl (it already builds cleanly with musl-gcc).

## Phase 8: memory management

- File-backed `mmap`, demand paging, copy-on-write `fork`.
- RAM above 1 GiB.
- `PT_INTERP`: dynamically linked programs with musl's `ld-musl`.

## Phase 9: the missing POSIX pieces

- `select`, Unix domain sockets, FIFOs.
- Job control: stopping and continuing (`SIGTSTP`, `SIGCONT`,
  `WUNTRACED`).
- `alarm`/`setitimer`; `futex` and `clone` threads.

## Phase 10: streamlining sbase and ubase

- Run every tool (and sbase's tests) on Miviex until no syscall is
  reported as unimplemented.
- Multi-call `sbase-box`/`ubase-box` with symlinks to shrink the image.

## Phase 11: portability and networking

- UEFI boot and a framebuffer console; SMP; HPET or APIC timer instead
  of the PIT.
- NVMe; Hyper-V VMBus (storvsc, netvsc, keyboard).
- A TCP/IP stack with e1000, virtio-net and vmxnet3; virtio-rng.
- USB (xHCI, keyboards).

## In parallel: the init system

simpled (`ext/simpled`) is `/init`: it reads systemd-style units, runs
the gettys and `fstab.service`, restarts what dies and powers off
cleanly. Its own roadmap is in `ext/simpled/ROADMAP.md`. It is developed
and tested on Linux first, since Miviex speaks the Linux ABI.

What it would like from the kernel, worked around for now: an atomic
`ppoll` signal mask, `O_NONBLOCK` on pipes (for its log collector), and
init without the console as its controlling terminal (until a getty
takes the console, `^C` there reaches init as SIGINT, which reboots).

Until phase 6 the system lives in the initramfs built from `rootfs/`:
changes made at run time, `passwd` included, are lost on reboot.
