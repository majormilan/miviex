#!/usr/bin/env python3
# Boots the make check ISO on the serial port and reports what the tests
# printed. Exits 0 only if every check passed and the system powered off.
import os, select, subprocess, sys, time

iso, limit = sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 300
log = open(os.path.splitext(iso)[0] + ".log", "wb")
q = subprocess.Popen(["qemu-system-x86_64", "-cdrom", iso, "-m", "256M", "-nographic",
                      "-no-reboot"], stdin=subprocess.DEVNULL,
                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
out, line, end = b"", b"", time.time() + limit
ok = bad = 0
while time.time() < end and q.poll() is None:
    if not select.select([q.stdout], [], [], 0.5)[0]:
        continue
    data = os.read(q.stdout.fileno(), 4096)
    log.write(data)
    out += data
    *lines, out = out.split(b"\n")
    for l in lines:
        l = l.decode("latin1").rstrip("\r")
        if l.startswith(("ok - ", "not ok - ", "###", "check: ")) or "FAIL ] trap" in l or "panic" in l.lower():
            print(l, flush=True)
        ok += l.startswith("ok - ")
        bad += l.startswith("not ok - ")
        if "FAIL ] trap" in l or "panic" in l.lower():
            bad += 1
        if "Kernel panic" in l:
            end = 0             # nothing more will come
if q.poll() is None:
    q.kill()
    print(f"make check: no poweroff within {limit:.0f} s (see {log.name})")
    bad += 1
print(f"make check: {ok} passed, {bad} failed")
sys.exit(1 if bad or not ok else 0)
