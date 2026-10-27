---
id: YAR-018
title: The one-monitor lock fails open when the lock file cannot be opened, for example a non-ASCII path through CreateFileA
type: design-note
status: documented
severity: low
platforms: [all]
area: boinc-lifecycle
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan sections 3.3 and 6.3)
where: monitor/yarrow_lock.c:8-10, 25-27 @ 78b1446
fixed_in:
---

## Symptom

None reported. When the lock file cannot be opened, a second monitor task on the same client is not kept out, and two
monitors look for the same counter.

## Cause

`ylock_acquire()` returns 1 ("this process holds the lock") when the file cannot be opened at all, on purpose
(monitor/yarrow_lock.h:5-6; monitor/yarrow_lock.c:8-10 on Windows, 25-27 on POSIX). On Windows it uses `CreateFileA`
with a path built from `aid.project_dir` (monitor/yarrow_boinc.cpp:49-56); characters the ANSI code page cannot
express make that open fail. Whether the BOINC client passes `project_dir` in UTF-8 or in the ANSI code page was not
checked; the default BOINC data directory on Windows has an ASCII path. Verified by reading at 78b1446.

## Fix

None needed by default (failing open keeps a monitor running when the lock is broken). If it matters: convert the path
from UTF-8 and use `CreateFileW`, and log once when the lock could not be taken.

## Test

None.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified by reading at 78b1446.
