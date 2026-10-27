---
id: YAR-027
title: Question - should a monitor task ever end with boinc_finish, and what should a client quit do?
type: question
status: new
severity: low
platforms: [all]
area: boinc-lifecycle
next: Decide: on a client quit, should a monitor save and exit without boinc_finish (resumed later)? What, if anything, ends a task?
pattern:
aliases: [plan-8.1]
reported: 2026-10-21
source: audit 2026-10-21 (plan section 8, question 1)
where: monitor/yarrow_monitor.c:151, 223-227 @ 78b1446
fixed_in:
---

## Symptom

The docs say "One BOINC result stays running for as long as the BOINC client does" (docs/boinc-monitor.md:32), "A task
ends only when the client does" (:88) and "The task is valid when it ends normally" (:91). The code ends the task with
`boinc_finish(0)` when its loop sees a quit or an abort (YAR-004), but the BOINC library carries out quit and abort
itself first, so at 78b1446 a monitor task in practice never finishes: it is resumed after each client restart.

## Cause

Question for the maintainers:

1. Is a monitor task meant to be resumed after a client restart (quit: save and exit without `boinc_finish`), or to
   end and be replaced by a new task?
2. What should end a task normally, if anything: the year-long deadline, a maximum age, a message from the server
   (trickle-down)?
3. Is there a server-side reason (validation, credit, the generator's count of ready tasks) for a quit to finish the
   task?

The audit's proposal: a quit saves and exits without `boinc_finish`; an abort is left to the client; `boinc_finish` is
kept for an explicit end condition, if there is one.

## Fix

Waiting for the answer. YAR-004 and YAR-009 depend on it.

## Test

n/a.

## Log

- 2026-10-21: asked in the audit plan (section 8, question 1).
- 2026-10-27: recorded; the library behaviour on quit checked in BOINC 4d1529d (see YAR-004).
