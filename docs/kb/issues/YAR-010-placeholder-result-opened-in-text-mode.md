---
id: YAR-010
title: The placeholder result file out is opened in text mode, so it ends in CRLF on Windows
type: bug
status: not-a-bug
severity: info
platforms: [windows]
area: io
pattern: PAT-019
aliases: []
reported: 2026-10-27
source: pattern check 2026-10-27
where: monitor/yarrow_boinc.cpp:116 @ 78b1446
fixed_in:
---

## Symptom

None known. On Windows `out` holds `yarrow monitor finished\r\n`, on other platforms `...\n`.

## Cause

`boinc_fopen(path, "w")` (monitor/yarrow_boinc.cpp:116) is the only produced file in text mode: the state file is
written with `"wb"` and read with `"rb"` (82, 71), and `trickle_up.xml` is written by the BOINC library with `"wb"`.
Whether the server compares the bytes of `out` is not known (the validator is not public); the docs say a monitor task
is valid when it ends normally (docs/boinc-monitor.md:91). `out` is written only on the paths described in YAR-009.
Verified by reading at 78b1446.

## Fix

Open with `"wb"`. No effect for volunteers.

## Test

Build with MinGW, run a stub that reaches `glue_done()`; `grep -c $'\r' out` must print 0.

## Log

- 2026-10-27: found while checking PAT-019 at 78b1446, verified by reading.
- 2026-10-27: re-checked after PAT-019 was corrected: placeholders that carry no data, and that nothing in this repository compares or trims by bytes, count as not-affected. Status not-a-bug. Still unknown: whether the server's validator (not public) compares `out` by bytes.
