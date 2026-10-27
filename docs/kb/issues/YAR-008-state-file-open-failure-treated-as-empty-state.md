---
id: YAR-008
title: A state file that exists but cannot be opened is treated as no state, and the next save overwrites it
type: bug
status: confirmed
severity: low
platforms: [all]
area: checkpoint
next: In glue_checkpoint_read(), when the state file exists but cannot be opened or read, log it and boinc_temporary_exit(300)
pattern: PAT-005
aliases: []
reported: 2026-10-27
source: pattern check 2026-10-27
where: monitor/yarrow_boinc.cpp:69-77 @ 78b1446
fixed_in:
---

## Symptom

None reported. After a restart the monitor would forget its pending bins, the identity and the last-seen time (so
no gap fill), without a message.

## Cause

`glue_checkpoint_read()` returns 0 bytes when `boinc_fopen(state, "rb")` returns NULL (monitor/yarrow_boinc.cpp:71-73).
`boinc_fopen()` returns NULL for a missing file and also, on Windows after retrying for 5 s, for a file it cannot open
(`lib/filesys.cpp:581-624` at BOINC 4d1529d; [boinc-api.md](../shared/boinc-api.md#files)). `main()` then starts with
an empty state (monitor/yarrow_monitor.c:118-143), and the first `save_state()` (after the first bin, or within a
minute of finding a counter) replaces the old file. A short `fread` is not noticed either. Most likely on Windows,
where antivirus or backup tools hold files. Verified by reading at 78b1446.

## Fix

When `boinc_file_exists(state)` is true but the open or the read fails, log it and `boinc_temporary_exit(300, ...)`
instead of starting empty (PAT-005). The task then waits five minutes instead of losing up to 1008 pending bins.

## Test

On Linux with a BOINC stub: `chmod 000 yarrow_state.txt` in the slot directory and start; the file must stay unchanged
and the task must ask to be restarted.

## Log

- 2026-10-27: found while checking PAT-005 at 78b1446, verified by reading.
