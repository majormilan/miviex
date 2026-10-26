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
- Userland from git submodules: shush as `/bin/sh`, sbase, ubase, a
  placeholder `/init`.
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

## Phase 5: platform foundation and the driver model (next)

- ACPI tables (MADT, FADT, HPET, MCFG); ACPI poweroff and reset.
- PCI/PCIe enumeration; mapping device memory (MMIO) above the 1 GiB
  direct map.
- Local APIC and I/O APIC instead of the 8259; MSI/MSI-X; HPET or APIC
  timer.
- A small driver model: devices registered by major/minor, /dev filled
  from it, shared IRQs, DMA-able memory.

Done when the boot log lists the PCI devices and interrupts are routed
through the APIC on QEMU, VirtualBox and VMware.

## Phase 6: storage and persistence

- Block layer: request interface, buffer cache, MBR/GPT partitions.
- Drivers: ATA/IDE (PIO), AHCI, virtio-blk.
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

- UEFI boot and a framebuffer console; SMP.
- NVMe; Hyper-V VMBus (storvsc, netvsc, keyboard).
- A TCP/IP stack with e1000, virtio-net and vmxnet3; virtio-rng.
- USB (xHCI, keyboards).

## In parallel: the init system

The real init replaces the placeholder `/init` whenever it is ready.
Miviex speaks the Linux ABI, so it can be developed and tested on Linux.
What it needs from the kernel (`mount`, `reboot`, `/proc`, the kernel
log) is there; ACPI poweroff on all hypervisors arrives in phase 5.

Until phase 6 the system lives in the initramfs built from `rootfs/`:
changes made at run time, `passwd` included, are lost on reboot.
