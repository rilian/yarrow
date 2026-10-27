# AGENTS.md: yarrow

Orientation for coding agents and new contributors. Written on 2026-10-27 against commit `78b1446`; file:line
references below refer to that commit. User-facing details are in [README.md](README.md) and [docs/](docs); this file
links them instead of repeating them.

## What one work unit is

The BOINC application in this repository is the **monitor** (`monitor/`). A monitor work unit has no input file. The
task is a non-CPU-intensive (NCI) app that runs for as long as the BOINC client does
([docs/boinc-monitor.md](docs/boinc-monitor.md), "Running it on a project"). It reads `yarrow_interval`,
`yarrow_country` and `yarrow_city` from the project preferences in `init_data.xml`, takes a per-computer lock, and looks
for one counter every 15 s: a FNIRSI GC-03 with the Yarrow firmware (USB serial), a Radioactive@Home counter (USB HID)
or a GQ GMC counter (USB serial). It polls the counter's cumulative raw pulses, cuts them into exact bins of 600, 1800
or 3600 s, keeps the finished but unsent bins in `yarrow_state.txt`, and sends them as trickle-up messages of variety
`yarrow` (at most one every 10 minutes, at most 144 bin slots each). The only result file is a one-line placeholder
`out`. Validation and credit come from the server's trickle job, not from the result.

The other parts are not run by BOINC: the GC-03 firmware (`src/`, `hal/gc03/`), the desktop app Yarrow Hub (`hub/`,
never uploads), the website (`site/`) and the hardware designs (`hardware/`).

## Layout

| Path | What it holds |
|---|---|
| `monitor/yarrow_monitor.c` | main loop: preferences, lock, state load, detector search, polling, bins, gap fill, sends, temporary exits |
| `monitor/yarrow_glue.h` | the interface between the loop and its host |
| `monitor/yarrow_boinc.cpp` | BOINC host: init, preferences, state file, trickle send, quit check, temporary exits, `boinc_finish` |
| `monitor/yarrow_standalone.c` | command-line host: prints `TRICKLE <xml>` lines instead of sending; SIGINT/SIGTERM end it |
| `monitor/yarrow_bin.[ch]` | bin cutter: only exact bins, never estimated or padded |
| `monitor/yarrow_trickle.[ch]` | pending queue (1008 bins), trickle XML, state text, country/city check |
| `monitor/yarrow_backfill.[ch]` | bins from the detector's flash log (gap fill), LOGPAGE base64 decoding |
| `monitor/yarrow_src.[ch]` | source interface; search order Yarrow, Radioactive@Home, GQ GMC; GMC port scan |
| `monitor/yarrow_src_yarrow.c` | Yarrow firmware source: INFO, UID, COUNTS, CAL, TIME, clock fix, log read-back |
| `monitor/yarrow_src_rah.c` | Radioactive@Home source (HID feature report, bin edges on the host clock) |
| `monitor/yarrow_src_gmc.c`, `yarrow_gmc.h` | GQ GMC source (`<GETVER>>`, `<GETCFG>>`, one heartbeat per second) |
| `monitor/yarrow_serial.[ch]` | serial ports on Windows and POSIX: open, line commands, raw reads, port lists, monotonic clock, sleep |
| `monitor/yarrow_hid.[ch]` | HID: Linux hidraw, Windows hid.dll, a stub elsewhere; wall clock and host name |
| `monitor/yarrow_lock.[ch]` | one monitor per BOINC data directory |
| `monitor/test_*.c` | host tests run by `make monitor-check` |
| `monitor/yarrow_rah_probe*.c`, `yarrow_rah_gui.c` | Radioactive@Home test program (console, Windows window); not part of the BOINC app |
| `monitor/yarrow_watch.c`, `yarrow_batlog.c` | bench tools for a GC-03 (rate check, battery log); not part of the BOINC app |
| `src/` | firmware, portable code: application, screens, measurement, alarm, battery, calibration, settings store (`kv`), log (`ylog`, `ylc`), USB protocol (`proto.c`), fonts; `src/lang/*.h` one header per language |
| `hal/gc03/` | GC-03 hardware layer: start-up, clocks, display, USB CDC (`usb_gc03.c`), linker script |
| `tests/` | firmware host tests (`make check`); `dump_log.c` for `make hostcheck` |
| `sim/sim.c` | screen simulator (`make sim`) |
| `canary/` | bring-up images ([docs/bringup.md](docs/bringup.md)) |
| `tools/` | font generator, image finaliser, ARM emulator tests, Python log decoder, translation tools, `yarrow.py` (serial CLI), `buildall.sh`, `sanitize.sh` |
| `assets/fonts.spec` | which glyphs go into the firmware fonts |
| `hub/` | Yarrow Hub, a Rust workspace: `crates/yarrow-core` (protocol, GC-03/R@H/GMC drivers, Rust ports of `ylog`/`ylc`, bins), `crates/yarrow-store`, `app/` (Tauri) with `app/ui/` (TypeScript) |
| `linux/` | udev rule and setup script that let the BOINC user open the counters ([docs/linux.md](docs/linux.md)) |
| `site/` | project website (`build.py` renders, `app.py` serves) |
| `hardware/` | open hardware: `basic/` (KiCad board and scripts), `stand/` (OpenSCAD dock); see [hardware/README.md](hardware/README.md) |
| `docs/` | user and developer documentation (table in [README.md](README.md#documentation)) |
| `third_party/` | vendored fonts with their licences (Inter; Noto CJK is fetched, see its README) |

## Invariants

Break one of these and data become silently wrong or the server rejects them.

* **Trickle format.** `<yarrow v="1" start interval dev fw sens dead cc city bins/>` is written only by `ypending_xml()`
  (`monitor/yarrow_trickle.c:33-60`) and parsed by the server's trickle handler (not here). `start` is a multiple of
  `interval`; `bins` has one slot per interval, `-` for a gap, at most `YT_MAX_SPAN` = 144 slots. `sens` is counts per
  minute for 1 uSv/h times 100, `dead` is microseconds. See [docs/boinc-monitor.md](docs/boinc-monitor.md#trickle-up)
  and [docs/data.md](docs/data.md).
* **Device identity** (`dev`, 16 hex digits). Yarrow: the firmware's `UID` (a hash of the chip id). Radioactive@Home:
  FNV-1a 64 of `rah|<USB serial>|<host name>` (`yarrow_src_rah.c:96-106`). GQ GMC: FNV-1a 64 of
  `gmc|<model>|<serial>`, or of the host name when the serial is blank (`yarrow_src_gmc.c:121-140`). A new recipe makes
  every counter a new device on the server (new-device credit ramp, copy checks).
* **Exact bins.** `ybin_feed()` (`monitor/yarrow_bin.c:29-58`) emits a bin only when both edge readings are in the edge
  second and not marked inexact, the tube was on for the span (1 s + 0.5 % slack), and nothing jumped. The server's
  Poisson checks assume raw, unpadded counts.
* **State file** (one writer `save_state()` + `ypending_save()`, one reader in `main()` + `ypending_load()`): an optional
  `D uid fw sens dead interval` line, an optional `S uid sens dead interval t` line, then `start count` lines. `fw`
  must be one non-empty token without spaces, or the `D` line is misread at the next start (usually: the identity is
  not restored and the pending bins are dropped).
* **Log format**, decoded in four places: `src/ylog.c` + `src/ylc.c` (firmware; also linked into the monitor for the
  gap fill), `tools/ylog_decode.py`, and `hub/crates/yarrow-core/src/ylog.rs` + `ylc.rs`.
  [docs/log-format.md](docs/log-format.md). `YARROW_MIN_TIME` = 1790812800 (`src/ylog.h:22`) is repeated as `SEEN_MIN_T`
  (`monitor/yarrow_monitor.c:29`) and `MIN_TIME` (`hub/crates/yarrow-core/src/ylog.rs:12`).
* **Serial protocol.** `src/proto.c` is the device side; `monitor/yarrow_src_yarrow.c`, `hub/crates/yarrow-core/src/client.rs`
  and `tools/yarrow.py` are hosts ([docs/serial-protocol.md](docs/serial-protocol.md)). The monitor expects the `UID`
  reply to be exactly 19 characters (`yarrow_src_yarrow.c:169`).
* **USB ids.** The firmware's 1209:0001 (`hal/gc03/usb_gc03.c:107`) must match `hub/crates/yarrow-core/src/transport.rs:9-10`
  and the udev rule. `linux/99-yarrow.rules` and the copy inside `linux/yarrow-linux-setup.sh` must stay identical.
  The Radioactive@Home ids are in `yarrow_src_rah.c:24`, the udev rule and `hub/crates/yarrow-core/src/rah.rs`.
* **Firmware image header** (length, CRC-32) is filled in by `tools/finalize.py` and checked by the start-up code
  ([docs/building.md](docs/building.md)).

## Checkpoint/resume contract (monitor)

* **Files.** `yarrow_state.txt` in the slot directory (`boinc_resolve_filename`, `monitor/yarrow_boinc.cpp:58-67`);
  `yarrow_monitor.lock` in the project directory (`yarrow_boinc.cpp:49-56`); `trickle_up.xml`, written by the BOINC
  library; the placeholder `out`.
* **Authoritative.** The state file is the only record of finished, unsent bins. The bin in progress is never saved:
  it is dropped on every restart, by design. For a Yarrow detector the gap since the `S` line's time is refilled from
  the detector's own log (at most 13 days, same uid, calibration and interval, clock not reset when found;
  `monitor/yarrow_monitor.c:76-96`).
* **Writes.** The whole file, through `.tmp` and `boinc_rename` (`yarrow_boinc.cpp:79-89`), after every finished bin
  (also every gap-filled bin), after every successful send, every 60 s while a detector is attached, before the
  no-detector exit and at the end of the loop. Nothing is checked ([YAR-005](docs/kb/issues/YAR-005-state-file-write-unchecked-and-reported-anyway.md)),
  no write is inside a critical section, and `boinc_checkpoint_completed()` follows every write without
  `boinc_time_to_checkpoint()` ([YAR-007](docs/kb/issues/YAR-007-state-written-on-own-cadence-outside-critical-section.md)).
* **Sends.** `flush()` takes the first chunk, calls `boinc_send_trickle_up()`, then drops the chunk and saves. Delivery
  is at least once: a quit between the send and the save sends the chunk again after the restart, and the server keeps
  the first count it received ([docs/boinc-monitor.md](docs/boinc-monitor.md) "Principles";
  [YAR-028](docs/kb/issues/YAR-028-question-server-dedupe-for-every-counter-type.md)). A send just before an exit is
  lost ([YAR-006](docs/kb/issues/YAR-006-trickle-lost-when-the-task-exits-right-after-a-send.md)).
* **Resume.** Read the state. With a valid `D` line the identity is restored and leftovers are sent at once; without one
  the pending bins are dropped. If the first counter found differs in uid, calibration or interval, one chunk is sent
  and the rest dropped ([YAR-024](docs/kb/issues/YAR-024-pending-bins-dropped-on-identity-change.md)).
* **Exits.** Quit and abort are normally carried out by the BOINC library itself (default `direct_process_action`, no
  critical section); the loop's own quit branch ends in `boinc_finish(0)`
  ([YAR-004](docs/kb/issues/YAR-004-quit-or-abort-ends-in-boinc-finish-0.md)). Temporary exits: settings missing 1 h,
  no counter for 5 min then 30 min, lock held by another monitor 2 h
  ([YAR-020](docs/kb/issues/YAR-020-temporary-exit-durations.md)). The client aborts a task after more than 100
  temporary exits without a reported checkpoint, about 2.5 days without a counter
  ([YAR-030](docs/kb/issues/YAR-030-repeated-temporary-exits-hit-the-client-limit.md)).

## Building

Details: [docs/building.md](docs/building.md) and [README.md](README.md). Commands as the Makefile and scripts have them:

```
make                          # firmware, Turkish: build/yarrow-gc03.bin (arm-none-eabi-gcc 12+, GNU make, python)
make YLANG=en                 # build/en/yarrow-gc03.bin; zh/ja/ko first need the Noto CJK fonts (third_party/noto-cjk/README.md)
make release YLANG=en         # copies it to dist/yarrow-gc03-en.bin
tools/buildall.sh [lang ...]  # every language: langcheck, sim, release, emulator test; MAKE defaults to mingw32-make
make monitor                  # build/monitor/yarrow_monitor (command-line host), yarrow_batlog, yarrow_watch
cd hub/app && cargo tauri build   # Yarrow Hub (README.md)
```

* On Windows the Makefile adds `-lhid -lsetupapi` for the monitor when `OS` is `Windows_NT` (MinGW gcc, `mingw32-make`).
* **The BOINC build of the monitor is not in this repository.** `Makefile:135-136` says `monitor/yarrow_boinc.cpp`
  replaces `monitor/yarrow_standalone.c` inside it; it needs the BOINC API headers `boinc_api.h` and `filesys.h` and
  their libraries. Where those come from, the compiler flags and the per-platform builds are kept by the project.
* Version: the firmware reports its version in the `INFO` reply (`src/proto.c:66`); the monitor has no version string
  of its own (the BOINC app version is set by the server).

## Tests

```
make check            # firmware host tests (range coder, log with power cuts, measurement, ..., whole app), then monitor-check
make monitor-check    # monitor: bin cutter and trickle text, gap fill, Radioactive@Home and GQ GMC sources with fake devices
make test             # firmware image in an ARM emulator (pip install unicorn)
make hostcheck        # Python log decoder against the C decoder
make sim              # renders every screen (make sim YLANG=en)
bash tools/sanitize.sh   # every make-check test again with ASan and UBSan (gcc on Linux)
```

| Change in | Run |
|---|---|
| `monitor/` | `make monitor-check` and `make monitor`; for `yarrow_serial.c`/`yarrow_hid.c`/`yarrow_lock.c` also a MinGW compile |
| `src/ylog.c`, `src/ylc.c` | `make check`, `make hostcheck`, the hub's decode test |
| other `src/`, `hal/` | `make check`, `make`, `make test`, `make sim` |
| `linux/` | compare the rule file with the copy in the setup script |

Not covered by any test: `monitor/yarrow_boinc.cpp` (there is no BOINC stub), real serial and HID I/O, the Windows code,
quit and abort, trickle pick-up, and the BOINC timer signal (only `test_rah.c` simulates a shortened wait). A monitor
change can be tried by hand with a counter attached: `build/monitor/yarrow_monitor --country TR --city Denizli
[--interval 600] [--port COM5] [--state yarrow_state.txt]` (usage line in `monitor/yarrow_standalone.c:2`). The hub has
Cargo tests in `hub/crates/yarrow-core/tests/`; the repository does not document how it runs them.

`make check` (including `monitor-check`) passed on macOS with Apple clang 14 on 2026-10-27 at `78b1446`, in about 50 s.

## Environment notes

* `gcc` on macOS is Apple clang. `make check` passes; `make monitor` stops at `monitor/yarrow_watch.c:73-74` with
  `-Werror` ([YAR-026](docs/kb/issues/YAR-026-make-monitor-fails-with-clang-werror.md)).
* The Makefile and `tools/buildall.sh` call `python`, which must be Python 3
  ([YAR-022](docs/kb/issues/YAR-022-makefile-calls-python-not-python3.md)).
* `tools/buildall.sh` is bash and defaults to `MAKE=mingw32-make`; elsewhere run it with `MAKE=make`.
  `tools/sanitize.sh` needs GNU grep (`grep -P`).
* Under a BOINC client on Linux and macOS the worker thread gets SIGALRM ten times a second; the tests and the
  command-line host do not. Every wait and read must retry on EINTR with the remaining time, as
  `yser_sleep_ms()` and `yser_read()` do ([YAR-003](docs/kb/issues/YAR-003-eintr-from-boinc-sigalrm-cuts-waits-and-reads.md),
  pattern PAT-010).
* Windows: COM1 to COM64 are probed; a serial read needs a total timeout or it waits forever
  ([YAR-002](docs/kb/issues/YAR-002-windows-serial-read-waits-forever.md)).
* Linux device access for the BOINC user: [docs/linux.md](docs/linux.md) and the gotchas in [docs/kb/INDEX.md](docs/kb/INDEX.md).
* Hub: Rust 1.88 or newer (`hub/app/Cargo.toml:6`), Tauri 2, Node for `hub/app/ui/`. Hardware: KiCad (`kicad-cli`; the
  helper scripts look in `/usr/share/kicad`) and OpenSCAD.

## Conventions for changes

* Small branches, one fix per commit, each with the test that fails before it where that is possible.
* Commit messages name the issue (`KB: YAR-NNN`) and the pattern if any (`Pattern: PAT-NNN`).
* Measured claims only (which machine, which command, which commit); date comments that record a measurement.
* Keep the two monitor hosts (`yarrow_boinc.cpp`, `yarrow_standalone.c`), the Windows and POSIX halves of
  `yarrow_serial.c`/`yarrow_hid.c`/`yarrow_lock.c`, and the monitor and hub drivers for the same counter in step.
* Do not change the trickle format, the units, the identity recipes or the bin rules without a matching server change.
* A new firmware text goes into every `src/lang/*.h`; check with `tools/langcheck.py`.

## Not in this repository

* Server side: the trickle handler, the validator job, the credit batch, the work generator, the database schema, the
  map and history pages, the project preferences page ([docs/boinc-monitor.md](docs/boinc-monitor.md) "Parts").
* The BOINC build of the monitor (scripts, BOINC libraries, app versions per platform) and the BOINC library itself;
  its behaviour as the monitor relies on it is in [docs/kb/shared/boinc-api.md](docs/kb/shared/boinc-api.md).
* Ready-made builds of the Radioactive@Home test program; the Noto CJK font files; the GC-03's stock bootloader.

## Knowledge base

`docs/kb/` holds this repository's known bugs, platform traps and their fixes, and the bug patterns shared with the
other bitboinc applications. Start at [docs/kb/INDEX.md](docs/kb/INDEX.md): its first table, "Fix next", is the work
that is ready, with the next step for each issue. Before debugging a symptom, search it (`grep -ril "<words>" docs/kb`).
After a fix or a new finding, update the issue. Workflow: [docs/kb/shared/PROCESS.md](docs/kb/shared/PROCESS.md).

- Overview without opening files: `python3 docs/kb/shared/kb.py list --open [--platform windows] [--area checkpoint]`
- New report: `python3 docs/kb/shared/kb.py new issue "symptom in a few words"`
- A fix commit names its issue (`KB: YAR-NNN`) and, if any, its pattern (`Pattern: PAT-NNN`).
- Before a pull request: `python3 docs/kb/shared/kb.py index && python3 docs/kb/shared/kb.py lint --strict`
