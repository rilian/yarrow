---
id: YAR-019
title: The one-monitor lock covers one BOINC data directory only; several clients on one computer can read one counter twice
type: design-note
status: documented
severity: low
platforms: [all]
area: boinc-lifecycle
pattern:
aliases: []
reported: 2026-10-27
source: audit 2026-10-21 (plan section 6.3); review 2026-10-27
where: monitor/yarrow_boinc.cpp:49-56; monitor/yarrow_monitor.c:112-117 @ 78b1446
fixed_in:
---

## Symptom

Inside one BOINC client a second monitor task logs "another Yarrow monitor is already running on this computer" and
sleeps for two hours, again and again. With several BOINC clients on one computer (separate data directories,
containers) both monitors run and look for the same counter.

## Cause

The lock is `<project_dir>/yarrow_monitor.lock` (monitor/yarrow_boinc.cpp:49-56). A second task of the same client
finds it locked and takes a 2 h temporary exit (yarrow_monitor.c:112-117, yarrow_boinc.cpp:130; 6 h before c8acefc).
The server normally sends one task per host (`max_jobs_in_progress`, docs/boinc-monitor.md:88). Clients with their own
data directories have their own project directories and locks. On Linux and macOS a serial port can be opened by two
processes (YAR-011), so two monitors can interleave commands on one Yarrow detector, and two can read the same R@H
feature reports. For R@H and GQ GMC the device id contains the host name (Invariants in AGENTS.md), so
containers with different host names report the same counter under two ids; the server's side-by-side checks then
see a copy (docs/boinc-monitor.md, "Server checks"). Verified by reading at 78b1446.

## Fix

None needed for one client. For several clients on one computer: a system-wide lock (a fixed path, or a named mutex on
Windows) or an exclusive open of the device (`TIOCEXCL`).

## Test

None.

## Log

- 2026-10-27: recorded, verified by reading at 78b1446.
