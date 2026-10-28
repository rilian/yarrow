# Pattern ledger

One row per cross-repository pattern ([shared/patterns](shared/patterns)): has this repository been checked for it,
at which commit, and what was found. `kb.py sync` adds an `unchecked` row whenever a new pattern arrives; checking it
means following the pattern's "How to check a repository" section and filling in the row. Status values:
[shared/vocabulary.md](shared/vocabulary.md#ledger). A row that is `affected` or `fixed` names the local issue, and that
issue names the pattern in its `pattern:` field; its Evidence says where, in one clause, and the issue has the rest.
Write rows with `kb.py ledger` (date and commit filled in) rather than by hand.

When code that a row describes changes a lot, check the pattern again and update Checked, Commit and Evidence.

| Pattern | Status | Checked | Commit | Issue | Evidence |
|---|---|---|---|---|---|
| PAT-001 | not-affected | 2026-10-27 | 78b1446 |  | monitor/: no boinc_time_to_checkpoint() or boinc_begin_critical_section(); boinc_checkpoint_completed() alone (yarrow_boinc.cpp:88) cannot leave a section open, the counter is clamped at 0. Missing gating and section are YAR-007 (PAT-004) |
| PAT-002 | n/a | 2026-10-27 | 78b1446 |  | result file out is a one-line placeholder written once at the end (yarrow_boinc.cpp:112-123); measurements go out as trickles; a chunk sent but not yet saved is sent again after a restart (at least once, YAR-007, YAR-028) |
| PAT-003 | affected | 2026-10-27 | 78b1446 | YAR-005 | yarrow_boinc.cpp:79-89: tmp + boinc_rename (a, c ok) but fwrite/fclose/boinc_rename unchecked (b) and boinc_checkpoint_completed() after a failed write (d) |
| PAT-004 | affected | 2026-10-27 | 78b1446 | YAR-007 | yarrow_monitor.c:34-39, 67-71, 204-206: state saved per bin, per send, every 60 s and per gap-filled bin (up to 1008 rewrites), never gated, no critical section around send-drop-save |
| PAT-005 | affected | 2026-10-27 | 78b1446 | YAR-008 | yarrow_boinc.cpp:69-77: boinc_fopen(state, rb) NULL (missing or unopenable) gives an empty state |
| PAT-006 | not-affected | 2026-10-27 | 78b1446 | YAR-009 | yarrow_boinc.cpp:115-122: out is a placeholder that carries no data (bins go out as trickles); written unchecked, reached with boinc_finish(0) only on the latent quit branch (YAR-004); not-affected by the corrected rule |
| PAT-007 | n/a | 2026-10-27 | 78b1446 |  | no boinc_fraction_done() anywhere: a monitor task has no end, so progress has no meaning (git grep boinc_fraction_done: no hits) |
| PAT-008 | affected | 2026-10-27 | 78b1446 | YAR-004 | yarrow_monitor.c:151, 223-227 and yarrow_boinc.cpp:103-123: quit or abort leaves the loop into flush(1), glue_done(0), boinc_finish(0) |
| PAT-009 | affected | 2026-10-27 | 78b1446 | YAR-006 | yarrow_monitor.c:162-165: flush(1) then boinc_temporary_exit() with no wait |
| PAT-010 | fixed | 2026-10-27 | 78b1446 | YAR-003 | fixed in 5e1164b: nanosleep loops (yarrow_serial.c:137-144, yarrow_src_rah.c:182-189), yser_read retries poll/read EINTR (345-367), HID ioctl retry (yarrow_hid.c:169-177); no remaining blocking call without retry |
| PAT-011 | n/a | 2026-10-27 | 78b1446 |  | no GPU code |
| PAT-012 | n/a | 2026-10-27 | 78b1446 |  | no OpenCL code |
| PAT-013 | n/a | 2026-10-27 | 78b1446 |  | no GPU code |
| PAT-014 | n/a | 2026-10-27 | 78b1446 |  | no OpenCL code |
| PAT-015 | n/a | 2026-10-27 | 78b1446 |  | no OpenCL code |
| PAT-016 | n/a | 2026-10-27 | 78b1446 |  | no GPU code |
| PAT-017 | n/a | 2026-10-27 | 78b1446 |  | no Android or OpenCL build |
| PAT-018 | n/a | 2026-10-27 | 78b1446 |  | results are not compared across hosts: integer counts per device, checked statistically by the server; the only float-derived value is the GMC sensitivity read from the device (yarrow_src_gmc.c:163-178) |
| PAT-019 | not-affected | 2026-10-27 | 78b1446 | YAR-010 | yarrow_boinc.cpp:116: only the placeholder out is opened with w (text mode), nothing in the repository compares or trims it by bytes; state file wb/rb (82, 71); validator not public |
| PAT-020 | n/a | 2026-10-27 | 78b1446 |  | monitor is single-threaded C, no thread_local in monitor/ (git grep); yarrow_rah_gui.c starts a worker thread but is a test tool, not the BOINC app |
| PAT-021 | n/a | 2026-10-27 | 78b1446 |  | corrected grep over *.sh, *.ps1, *.bat, *.py, Makefile: no hits (KiCad helper scripts use the system path /usr/share/kicad) |
| PAT-022 | n/a | 2026-10-27 | 78b1446 |  | no Linux release build here: the BOINC build of the monitor is kept outside this repository (Makefile:135-136, docs/boinc-monitor.md:94-95); make monitor is a local build; not checkable here |
| PAT-023 | n/a | 2026-10-27 | 78b1446 |  | no download or data-preparation scripts (tools/translate.py only calls a local model server); the monitor's gap fill reads the device log once, with no progress file |
| PAT-024 | n/a | 2026-10-27 | 78b1446 |  | no CUDA or other GPU build |
| PAT-025 | n/a | 2026-10-27 | 78b1446 |  | the BOINC monitor starts no threads (git grep thread APIs in monitor/: only yarrow_rah_gui.c:159, a Windows test tool outside the BOINC app) |
| PAT-026 | n/a | 2026-10-27 | 78b1446 |  | no std::sort/qsort in monitor/ or src/; the hub's Rust sorts are stable and it never uploads |
| PAT-027 | n/a | 2026-10-27 | 78b1446 |  | no GPU code |
| PAT-028 | not-affected | 2026-10-27 | 78b1446 | YAR-015 | monitor/: no -D feature flags, only OS guards; HID exists for Linux and Windows, other OSes get a silent stub (yarrow_hid.c:329-335), no macOS monitor known to ship; firmware: -DYLOG_MAX_PAGE=4096 (Makefile:28) matches FLASH_SECTOR 0x1000 (hal/gc03/image.h:22), one build script |
| PAT-029 | affected | 2026-10-27 | 78b1446 | YAR-031 | third_party/noto-cjk/README.md:8-10 fetches the CJK fonts from raw/main, docs/building.md:8 pip install pillow unpinned, both feed font_data.c of released firmware |
| PAT-030 | affected | 2026-10-27 | 78b1446 | YAR-030 | no detector: 300 s search then boinc_temporary_exit(1800) (monitor/yarrow_boinc.cpp:129) without a checkpoint the client sees: aborted after about 2.5 days |
