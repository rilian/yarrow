---
id: YAR-028
title: Question - does the server keep the first count per device and bin start for every counter type?
type: question
status: new
severity: low
platforms: [all]
area: trickle
next: Confirm: does the server ignore a repeated bin (same device, same start) for every counter type, without counting it as replay or copy?
pattern:
aliases: [plan-8.2]
reported: 2026-10-21
source: audit 2026-10-21 (plan section 8, question 2)
where: monitor/yarrow_monitor.c:57-72 @ 78b1446
fixed_in:
---

## Symptom

The monitor delivers bins at least once: a quit between a send and the save of the state sends the same bins again
after the restart (YAR-007), and fixes for YAR-006 may re-send a trickle left in the slot directory.

## Cause

Question for the maintainers. The docs say the server "keeps the first count it received for a bin"
(docs/boinc-monitor.md:12) and stores only bins "not already received for the device" (:100; docs/data.md:33). Please
confirm:

1. This holds for every counter type (Yarrow, Radioactive@Home, GQ GMC) and for repeats that arrive days later, inside
   the 14-day window.
2. A repeated bin is ignored silently and is not counted as a replay or a copy by the side-by-side checks.
3. For Radioactive@Home and GQ GMC the device id contains the computer's name: is a renamed computer meant to become a
   new device (with the new-device credit ramp)?

## Fix

Waiting for the answer. YAR-006 and YAR-007 assume (1) and (2).

## Test

n/a.

## Log

- 2026-10-21: asked in the audit plan (section 8, question 2).
- 2026-10-27: recorded with the docs' current wording at 78b1446.
