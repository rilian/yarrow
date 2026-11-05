---
id: YAR-033
title: Gap fill fails when a log page's base64 text contains END
type: bug
status: confirmed
severity: medium
platforms: [all]
area: hardware-io
action: fix
next: Match the terminator only at the start of a line in ybf_page_from_reply() (monitor/yarrow_backfill.c:108-110) and add a page whose encoding contains END to monitor/test_backfill.c
blocked_by: []
pattern:
aliases: []
reported: 2026-11-04
source: code reading while writing the benchmark harness (scripts/bench/fake/), 2026-11-04
where: monitor/yarrow_backfill.c:100-126, monitor/yarrow_src_yarrow.c:131-140, Makefile:28 @ 64e5d5d
fixed_in:
---

## Symptom

Not seen in a report yet. After the monitor has been away from a Yarrow detector, it reads the detector's log page by
page (`LOGPAGE n`) to fill the gap. When the base64 text of any used page contains the three letters `END`, the gap
fill gives up and fills nothing; stderr.txt says only "the detector's log could not be used to fill the gap"
(monitor/yarrow_monitor.c:94), the same as for a detector without a usable log.

## Cause

`ybf_page_from_reply()` (monitor/yarrow_backfill.c:100-126) stops decoding at the first `E`, `N`, `D` it meets anywhere
in the reply (`if (p[0] == 'E' && p[1] == 'N' && p[2] == 'D') break;`, line 109-110). The terminator the firmware sends
is a line `END`, but `E`, `N` and `D` are also base64 digits, so the sequence can occur inside the page's text. The
decoder then returns fewer bytes than the page size. `yy_backfill()` (monitor/yarrow_src_yarrow.c:131-140) treats a
short page as a failure, leaves the page loop, and with `pg != pages` returns -1 for the whole gap.

How often: the firmware is built with 4096-byte log pages (`-DYLOG_MAX_PAGE=4096`, Makefile:28), about 5,460 base64
characters per page. If the compressed log bytes look random, each position starts `END` with probability 64^-3, so
about 2 % of used pages contain it. Erased pages (0xFF) encode as `////` and never do. The gap fill needs every page,
so with 31 used pages (a full 13-day gap in the benchmark's `gap-fill` profile) it fails about half the time. These
figures are estimates; the code path was confirmed by reading it at 64e5d5d, not by a run with a detector.

`monitor/test_backfill.c:96-124` encodes one page and checks that it decodes, but that page's text happens not to
contain `END`, so the test passes.

## Fix

Proposed: accept `END` only at the start of a line (at the start of the text after the `OK` line, or right after a
`\n`), as the firmware writes it. No change to the firmware or the protocol. For volunteers: gap fills after an
absence succeed instead of failing in a share of cases that grows with the log's size.

## Test

Add to `monitor/test_backfill.c` a page whose encoding contains `END` inside a line (choose the bytes so that it does)
and check that it decodes to the page; `make monitor-check`.

## Log

- 2026-11-04: created; found by reading the code while writing the benchmark harness, whose fake log works around it
  (`scripts/bench/fake/`, docs/BENCHMARK.md).
