---
id: YAR-007
title: The state file is written on the monitor's own cadence outside a critical section, and every write is reported as a checkpoint
type: bug
status: confirmed
severity: low
platforms: [all]
area: checkpoint
next: Wrap send, drop and save in flush() in boinc_begin/end_critical_section(); stop calling boinc_checkpoint_completed() in glue_checkpoint_write()
pattern: PAT-004
aliases: [Y5]
reported: 2026-10-27
source: audit 2026-10-21 (Y5, critical-section part); pattern check 2026-10-27
where: monitor/yarrow_monitor.c:34-39, 67-71, 204-206 @ 78b1446
fixed_in:
---

## Symptom

None visible. Consequences: (1) a quit between `boinc_send_trickle_up()` and the save sends that chunk again after the
restart, which is harmless only if the server ignores repeats (YAR-028); (2) the client's checkpoint bookkeeping is
reset on the monitor's rhythm; (3) the volunteer's checkpoint interval is ignored.

## Cause

`save_state()` runs after every finished bin (`on_bin()`, monitor/yarrow_monitor.c:34-39, which during a gap fill
means one full rewrite per filled bin, up to 1008 in a row), after every send (70), every 60 s while a counter is
attached (204-206), before the no-counter exit (164) and at the end (224). No write is gated by
`boinc_time_to_checkpoint()` or placed inside `boinc_begin_critical_section()` / `boinc_end_critical_section()`, and
`glue_checkpoint_write()` calls `boinc_checkpoint_completed()` after each one (monitor/yarrow_boinc.cpp:88). The
sequence send, drop, save (yarrow_monitor.c:67-71) is not protected, so a quit in between leaves a state that still
holds bins already handed to the client. The file is small (about 20 bytes per pending bin, normally a few lines) and
written through a temporary file, so a write cut by a quit is harmless. Verified by reading at 78b1446.

## Fix

Proposed. Keep the event-driven writes: they are what makes the state useful, and gating them on the BOINC timer would
lose bins. But put send, drop and save inside `boinc_begin_critical_section()` / `boinc_end_critical_section()`, so a
quit waits for the save; stop calling `boinc_checkpoint_completed()` after these writes (or call it only when a
`boinc_time_to_checkpoint()` kept in a variable returned 1, PAT-001); write the state once after a gap fill instead of
once per bin. With the critical section a quit that arrives inside it is carried out by `boinc_end_critical_section()`
(`api/boinc_api.cpp:1696-1705` at BOINC 4d1529d), so the loop still does not see it (YAR-004).

## Test

A BOINC stub that logs critical-section entries and checkpoint reports; a quit injected between the send and the save
must take effect after the save.

## Log

- 2026-10-21: the missing critical section around send, drop and save was part of plan item Y5.
- 2026-10-27: recorded while checking PAT-004 at 78b1446, verified by reading.
- 2026-10-27: re-checked after PAT-004 gained its monitor rule (own cadence, critical section, no `boinc_checkpoint_completed()`); still affected, because there is no critical section and every write is reported. The proposed Fix is that rule. Reporting a checkpoint before a temporary exit has a use of its own (YAR-030).
