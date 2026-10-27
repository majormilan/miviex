# Minimal Viable Enterprise Unix (miviex)

## Overview
Minimal Viable Enterprise Unix (miviex) is a lightweight Unix-like operating system designed to be simple, minimalistic, and easy to understand. This project aims to provide a foundational OS that can be built upon for educational purposes or lightweight applications.

## Features
- Minimalistic design
- Basic Unix-like functionality
- Modular and easily extensible

## Prerequisites
To compile and build miviex, you need the following tools:
- `x86_64-elf` cross compiler for `gcc`
- `nasm` (Netwide Assembler)
- `grub-mkrescue` (with `xorriso`) and `cpio`, to build the boot ISO
- `musl-gcc` (Debian: `musl-tools`) and `yacc`, for `user/`, sbase and ubase
- `diet` (dietlibc), for shush
- `git` with network access on the first build

## Logging in
The init, simpled, runs `getty` on the VGA console (`/dev/tty1`) and on
the first serial port (`/dev/ttyS0`), as the units `getty@tty1` and
`serial-getty@ttyS0`. The image ships two accounts, both with the
password `miviex`: `root` and the user `miviex`. Passwords changed with
`passwd` last until the next boot, since the system runs from the
initramfs.

Kernel messages are kept for `dmesg`; add `debug` to the kernel command
line to also see debug messages on the console (or `quiet` for fewer).
`reboot` and `poweroff` stop every unit first (`-f` skips that) and work
on QEMU, Bochs and VirtualBox; elsewhere poweroff halts until ACPI
support lands.

## Services
simpled reads systemd-style unit files: miviex's own are in
`rootfs/lib/simpled/system`, and what is enabled is linked from
`rootfs/etc/simpled/system` (`default.target` is `multi-user.target`).
`simplectl` starts, stops and inspects them (`simplectl list`,
`simplectl status getty@tty1`) and enables and masks them.

## Userland
The initramfs carries a static Linux userland, pinned as git submodules
under `ext/` and fetched automatically by `make iso`:

- [shush](https://github.com/majormilan/shush) as `/bin/sh`
- [sbase](https://git.suckless.org/sbase) and
  [ubase](https://git.suckless.org/ubase) in `/bin`
- [simpled](https://github.com/majormilan/simpled) as `/init`, with
  `simplectl`
- [libfnt](https://github.com/majormilan/libfnt), compiled into the
  kernel: the console's fonts (unscii-16 and 10x20, public domain)
- `rootfs/`: the base system files (`/etc`), copied in as owned by root

Each project is built in a copy under `build/`, so `ext/` stays clean.
Local changes to a project live in `patches/<project>/*.patch` and are
applied to that copy; the ones in `patches/shush/` and `patches/ubase/`
should go upstream. To
build shush or simpled from a working copy instead, use
`make SHUSH_DIR=../shush iso` or `make SIMPLED_DIR=../simpled iso`.
To move a submodule to a newer upstream revision:
`git submodule update --remote ext/sbase` and commit the result.
- `qemu-system-x86_64`, for `make run`

