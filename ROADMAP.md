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

## Phase 4: system interfaces for the admin tools (next)

- `/proc`: listing, per-process `stat`/`status`/`cmdline`, `mounts`,
  `meminfo`, `uptime` (`self` and `<pid>/fd` links exist already).
- `mount`/`umount` for ramfs, proc and devfs; `reboot`/`halt`; a kernel
  log buffer for `dmesg`; `sysinfo`, `times`/`getrusage`.

Done when ubase `ps`, `mount`, `free`, `uptime`, `dmesg`, `halt` and
`reboot` work.

## Phase 5: the real init system

Replace the placeholder `/init`: mounts, getty respawning, reaping,
signal handling, orderly shutdown.

## Phase 6: streamlining sbase and ubase

- Run every tool (and sbase's tests) on Miviex until no syscall is
  reported as unimplemented.
- Multi-call `sbase-box`/`ubase-box` with symlinks to shrink the image.

## Phase 7: persistence and portability

- Disk drivers (IDE/AHCI, then virtio-blk) and ext2, so `/etc` and home
  directories survive a reboot.
- UEFI boot, ACPI, APIC/HPET timers, SMP.
- Demand paging and copy-on-write `fork`.

Until Phase 7 the system lives in the initramfs built from `rootfs/`:
changes made at run time, `passwd` included, are lost on reboot.
