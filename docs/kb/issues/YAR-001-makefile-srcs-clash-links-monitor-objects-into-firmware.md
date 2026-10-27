---
id: YAR-001
title: The Makefile reassigned SRCS for the monitor, so the firmware ELF was linked from monitor objects
type: bug
status: fixed
severity: high
platforms: [mcu]
area: build
pattern:
aliases: [Y1]
reported: 2026-10-21
source: audit 2026-10-21
where: Makefile:32-33, 138 @ 1ecf955
fixed_in: 5b67b71
---

## Symptom

`make`, `make release` and `make test` failed: `build/yarrow-gc03.elf` was linked from the monitor's host objects
(`yarrow_lock.o`, `yarrow_src*.o`, `yarrow_hid.o`) and `font_data.o` instead of the firmware objects.

## Cause

`OBJS = $(SRCS:%.c=$(BUILD)/%.o) $(BUILD)/font_data.o` (Makefile:33 at 1ecf955) is a recursive variable: it is expanded
when the link rule runs, after the whole Makefile was read. Makefile:138 at 1ecf955 assigned `SRCS` again, for the
monitor (introduced in c47cdda), so the firmware link used the monitor's file list.

Verified on 2026-10-27 with `make -n all` in a scratch copy of 5b67b71^ (GNU make 3.81): the link line lists
`yarrow_lock.o yarrow_src.o yarrow_src_yarrow.o yarrow_src_rah.o yarrow_src_gmc.o yarrow_hid.o font_data.o`.

## Fix

5b67b71 (2026-10-25, "Makefile variable clash fixed") renamed the monitor list to `MON_SRCS` (Makefile:138 at 78b1446)
and uses it in the `monitor` target. Verified with `make -n all` in a scratch copy of 78b1446: the link line lists the 23
firmware objects and `font_data.o`, and no monitor object. A real link needs `arm-none-eabi-gcc`, which was not
available here.

## Test

`make -n all | grep -- '-o build/yarrow-gc03.elf'` must list firmware objects only. `tools/buildall.sh` builds and
emulator-tests the real image for every language.

## Log

- 2026-10-21: found by the audit at 1ecf955 (plan item Y1), confirmed with `make -n`.
- 2026-10-27: recorded from the 2026-10-21 audit; fixed upstream in 5b67b71, verified with `make -n` at 78b1446 and at 5b67b71^.
