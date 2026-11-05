---
id: YAR-034
title: make monitor fails with GCC (Linux GCC 12, MinGW-w64 GCC 16): -Werror on a format-truncation warning in yarrow_src.c
type: bug
status: confirmed
severity: medium
platforms: [linux, windows, gcc, mingw]
area: build
action:
next: Bound the copy in ysrc_gmc_find() ("%.63s", or pass the 64-byte port names through), then build the monitor with GCC on Linux and with MinGW-w64
blocked_by: []
pattern:
aliases: []
reported: 2026-11-04
source: M4 benchmark session 2026-11-04 (MinGW and container builds)
where: monitor/yarrow_src.c:44 @ ea3ab38
fixed_in:
---

## Symptom

`make monitor` (`gcc -std=c11 -O2 -Wall -Wextra -Werror ...`, Makefile:151-153) stops with GCC:

```
monitor/yarrow_src.c:44:63: error: '%s' directive output may be truncated writing up to 4095 bytes into a region of size 64 [-Werror=format-truncation=]
```

Reproduced on 2026-11-04 at ea3ab38 (and on upstream/main 64e5d5d) with Debian GCC 12.2 (Linux arm64 container) and
with MinGW-w64 GCC 16.2 (the Windows build the Makefile describes, `-lhid -lsetupapi`). Debian clang 14 and Apple clang
build it (Apple clang stops at `yarrow_watch.c` instead, YAR-026). Compiling the file alone (`-fsyntax-only`) does not
show it: the warning needs the optimised build.

## Cause

`ysrc_gmc_find()` remembers the ports it could not open in `static char told[16][64]` with
`snprintf(told[n_told++], sizeof told[0], "%s", ports[i])` (`monitor/yarrow_src.c:44`). The names come from
`yser_ports()`, 64 bytes each, so nothing is truncated in practice, but GCC's range analysis sees a source of up to 4095
bytes and `-Werror` turns the warning into an error. No effect on a running monitor; it blocks the documented build.

## Fix

Not done here (found while checking the Windows build of a performance change). Bound the copy (`"%.63s"`), or let
GCC see the 64-byte bound, then `make monitor` with GCC and a MinGW-w64 build. Whether the BOINC build of the monitor
(not in this repository) uses `-Werror` is not known.

## Test

`make monitor` with GCC on Linux; the same line list with `x86_64-w64-mingw32-gcc ... -lhid -lsetupapi`.

## Log

- 2026-11-04: created; reproduced with GCC 12 (Linux) and MinGW-w64 GCC 16.
