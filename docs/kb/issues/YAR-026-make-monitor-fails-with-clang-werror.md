---
id: YAR-026
title: make monitor fails with clang because of -Werror on a sometimes-uninitialized warning in yarrow_watch.c
type: bug
status: confirmed
severity: low
platforms: [clang]
area: build
next: Initialise a, b, c and p, h, t to 0 in monitor/yarrow_watch.c:72-73 so that make monitor builds with clang -Werror
pattern:
aliases: []
reported: 2026-10-27
source: build of a scratch copy 2026-10-27
where: monitor/yarrow_watch.c:73-74 @ 78b1446
fixed_in:
---

## Symptom

On macOS (Apple clang 14.0.0, which is `gcc` there), 2026-10-27, scratch copy of 78b1446:

```
monitor/yarrow_watch.c:74:18: error: variable 'p' is used uninitialized whenever '&&' condition is false [-Werror,-Wsometimes-uninitialized]
3 errors generated.
make: *** [monitor] Error 1
```

(the same for `h` and `t`). `yarrow_monitor` and `yarrow_batlog` build; `yarrow_watch` does not. `make check` passes on
the same machine.

## Cause

`unsigned long p, h, t;` are copied after `ok = sscanf(r, ...) == 3 && sscanf(cnt, "OK %lu %lu %lu", &p, &h, &t) == 3
&& ...` also when the first `sscanf` failed (monitor/yarrow_watch.c:73-82). The copies are not used when `ok` is false
(the loop continues at 83-87), so the warning is a false alarm, but the Makefile builds with `-Werror` (Makefile:155).
GCC on Linux was not tried.

## Fix

Initialise `p`, `h`, `t` (and `a`, `b`, `c`) to 0.

## Test

`make monitor` with clang.

## Log

- 2026-10-27: found by building `make monitor` in a scratch copy of 78b1446 on macOS.
