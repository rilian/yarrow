---
id: YAR-009
title: The placeholder result file out is written without checking the write before boinc_finish
type: bug
status: not-a-bug
severity: info
platforms: [all]
area: io
pattern: PAT-006
aliases: []
reported: 2026-10-27
source: pattern check 2026-10-27
where: monitor/yarrow_boinc.cpp:112-123 @ 78b1446
fixed_in:
---

## Symptom

None. If writing `out` failed, the task would still call `boinc_finish(status)`.

## Cause

`glue_done()` opens `out` with `boinc_fopen(path, "w")`, then `fputs` and `fclose`, and checks none of them
(monitor/yarrow_boinc.cpp:115-121). The file only holds the line `yarrow monitor finished` for the result template; the
measurements travel as trickles. `glue_done(0)` is reached only from the quit branch (YAR-004), which the BOINC library
normally pre-empts. `glue_done(1)` follows a failed `boinc_init_options()` (monitor/yarrow_monitor.c:102-105).
Verified by reading at 78b1446.

## Fix

Nothing to do while no task ends normally. If a real end is added (YAR-027), check `fputs` and `fclose` and end with
an error or a temporary exit when they fail.

## Test

On Linux: `ln -s /dev/full out` in the slot directory of a stub run that reaches `glue_done(0)`; the exit status must
not be 0.

## Log

- 2026-10-27: found while checking PAT-006 at 78b1446, verified by reading.
- 2026-10-27: re-checked after PAT-006 was corrected: a placeholder result that carries no data counts as not-affected. Status not-a-bug; the ledger row for PAT-006 is not-affected and keeps this issue as the reference.
