---
id: YAR-003
title: The BOINC timer signal (SIGALRM, EINTR) cut waits and device reads short on Linux and macOS
type: bug
status: fixed
severity: high
platforms: [linux, macos]
area: hardware-io
pattern: PAT-010
aliases: [Y3]
reported: 2026-10-21
source: audit 2026-10-21; commit message of 5e1164b
where: monitor/yarrow_serial.c:121, 136, 323 @ 1ecf955
fixed_in: 5e1164b
---

## Symptom

Under a BOINC client on Linux every Radioactive@Home counter was refused (`rah: a device with the Radioactive@Home id
answered, but it does not behave like the counter ...`), because the 300 ms wait between two reads took about 50 ms
(commit message of 5e1164b). A GQ GMC heartbeat read could come back short and the counter be reported lost. The
command-line host and the tests have no such signal and passed.

## Cause

The BOINC library sends the worker thread a SIGALRM every 0.1 s ([boinc-api.md](../shared/boinc-api.md#timer-and-signals-unix)).
It installs the handler with `SA_RESTART` (`api/boinc_api.cpp:1610` at BOINC 4d1529d), which restarts `read()` but
never `nanosleep()` or `poll()`. At 1ecf955:

- `yser_sleep_ms()` and the R@H `sleep_real()` called `nanosleep(&ts, NULL)` once (monitor/yarrow_serial.c:136,
  monitor/yarrow_src_rah.c:170), so a wait ended at the next signal;
- the R@H counter check then required the device timer to advance 200 to 600 ms (yarrow_src_rah.c:124), which a 50 ms
  wait cannot give;
- `yser_read()` stopped at the first `poll()` that returned -1 (yarrow_serial.c:323), so the 2-byte heartbeat read of
  `gmc_poll()` (yarrow_src_gmc.c:46) could return short.

Verified by reading 1ecf955 and the diff of 5e1164b.

## Fix

5e1164b (2026-10-25) is the reference fix of PAT-010:

- `yser_sleep_ms()` (yarrow_serial.c:137-144 at 78b1446), `sleep_real()` (yarrow_src_rah.c:182-189) and `nap()`
  (yarrow_rah_probe_core.c) loop `while (nanosleep(&ts, &rem) == -1 && errno == EINTR) ts = rem;`;
- POSIX `yser_read()` continues after `poll()` EINTR and after `read()` EINTR or EAGAIN, and takes the remaining time
  from `yser_now()` (CLOCK_MONOTONIC) on every round (yarrow_serial.c:345-367);
- `posix_get_feature()` retries the `HIDIOCGFEATURE` ioctl up to 5 times on EINTR (yarrow_hid.c:169-177);
- the R@H check compares the device timer with the host time that really passed between the two reads instead of
  assuming 300 ms (yarrow_src_rah.c:121-133).

`rd()`, used by the line protocol, was already safe: a `poll()` cut short returns "no byte" and `yser_command()` /
`yser_lines()` loop until a deadline on `yser_now()` (yarrow_serial.c:122-126, 157-179).

Checked on 2026-10-27 at 78b1446 (`git grep` for sleep, poll, read, ioctl and write calls in `monitor/`): no blocking
call reached from the BOINC entry point lacks the retry, and every timeout is measured with a clock. Left as they are:
`write()` on the `O_NONBLOCK` serial descriptor (yarrow_serial.c:128, 343) does not block, so EINTR does not occur; a
short write fails the command and the search starts again. `yarrow_watch.c` and `yarrow_batlog.c` are bench tools, not
BOINC code.

## Test

`make monitor-check` runs `test_rah`, whose `wait_not_exact()` (added in 5e1164b) accepts waits of 300 % and 50 % of
300 ms and refuses 5 %. A test with a real signal (PAT-010 "How to test": a SIGALRM handler without `SA_RESTART` and a
100 ms `setitimer`, then `yser_sleep_ms(300)` and `yser_read()` on a pty) does not exist yet.

## Log

- 2026-10-21: suspected by the audit at 1ecf955 (plan item Y3), "to confirm".
- 2026-10-25: fixed upstream in 5e1164b after the Linux report in its commit message.
- 2026-10-27: recorded from the 2026-10-21 audit; fix verified by reading at 78b1446; `make monitor-check` passed on macOS (Apple clang 14).
