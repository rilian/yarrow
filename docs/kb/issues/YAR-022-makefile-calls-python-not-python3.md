---
id: YAR-022
title: The Makefile and tools/buildall.sh call python, which is missing on many systems that have python3
type: gotcha
status: documented
severity: low
platforms: [all]
area: build
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 3.3)
where: Makefile:42, 49, 69, 73-74, 110-111, 126, 133; tools/buildall.sh:13 @ 78b1446
fixed_in:
---

## Symptom

`make` stops with `python: command not found` (or `No such file or directory`) on Debian and Ubuntu without the
`python-is-python3` package and on recent macOS, at the font generator (Makefile:42) or the image finaliser (69).
`make check` and `make monitor-check` do not need Python.

## Cause

The recipes call `python`; docs/building.md asks for Python 3.10 or newer, and the scripts start with
`#!/usr/bin/env python3` (for example tools/genfont.py:1) but are run through `python`. Verified by reading at 78b1446.

## Fix

Workaround: install `python-is-python3`, or put a `python` that is Python 3 first in `PATH` (a virtual environment).
Proposed: `PYTHON ?= python3` in the Makefile and `$(PYTHON)` in the recipes, and the same in tools/buildall.sh.

## Test

`make -n YLANG=en` shows the commands; run `make` on a system without `python`.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified by reading at 78b1446.
