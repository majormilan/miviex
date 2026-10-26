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
- `musl-gcc` (Debian: `musl-tools`), for the userland in `user/`
- `diet` (dietlibc) and a checkout of [shush](https://github.com/majormilan/shush)
  next to this repository (override with `make SHUSH_DIR=...`), used as `/bin/sh`
- `qemu-system-x86_64`, for `make run`

