---
id: YAR-005
title: The state file write is not checked, and a checkpoint is reported even when the write failed
type: bug
status: confirmed
severity: low
platforms: [all]
area: checkpoint
next: In glue_checkpoint_write(), check fwrite, fflush, fclose and boinc_rename, return success, and report a checkpoint only when it worked
pattern: PAT-003
aliases: [Y5]
reported: 2026-10-21
source: audit 2026-10-21
where: monitor/yarrow_boinc.cpp:79-89 @ 78b1446
fixed_in:
---

## Symptom

None reported. On a full disk, a quota or an I/O error the monitor can replace a good `yarrow_state.txt` with a
truncated one. At the next start the pending bins, the identity line and the last-seen time can be gone: when the `D`
line does not parse, all pending bins are dropped (monitor/yarrow_monitor.c:128-143).

## Cause

`glue_checkpoint_write()` (monitor/yarrow_boinc.cpp:79-89) writes `yarrow_state.txt.tmp` and renames it with
`boinc_rename()`, so PAT-003 (a) and (c) do not apply. But (b) the results of `fwrite` (85), `fclose` (86) and
`boinc_rename` (87) are ignored, so a short temporary file is renamed over the good one; and (d)
`boinc_checkpoint_completed()` (88) is called whatever happened (only a failed open returns early, 82-84). The function
returns `void`, so `save_state()` cannot know that the save failed. Verified by reading at 78b1446.

The command-line host writes the state in place with `"wb"` (monitor/yarrow_standalone.c:54-61); a kill during the
write leaves a partial file there. It is not BOINC code.

## Fix

The PAT-003 helper: write the temporary file, check `fwrite == n`, `fflush` and `fclose`, call `boinc_rename` only when
all succeeded and check it; return the result. Report a checkpoint only on success, and only paired with
`boinc_time_to_checkpoint()` (YAR-007). Log a failure once. No change for volunteers in normal operation.

## Test

On Linux, make the temporary path a symbolic link to `/dev/full` (or stub `fwrite`) and save: the old state file must
be unchanged and no checkpoint reported. No test exists; the BOINC host has no stub yet.

## Log

- 2026-10-21: found by the audit at 1ecf955 (plan item Y5).
- 2026-10-27: recorded from the 2026-10-21 audit, verified by reading at 78b1446. The critical-section part of Y5 is in YAR-007.
