# Benchmarking the Yarrow monitor (`scripts/bench/`)

How to measure whether a change makes the monitor cheaper to run and whether it keeps what the server receives, the scripts
that do it, and where the results go. Written on 2026-11-04 at `64e5d5d`, after the method of the Kampylos benchmark, which
is repeated below in the form that fits this app.

**Status (2026-11-04).** Written on an Apple M1 and first run on an Apple M4 Pro the same day: `ab.sh` built and ran all eight
profiles without a fix, 5 rounds in 75 seconds, and the control label read "no clear gain; same output" on every profile. The
first two candidates are measured in Results. The "expected" sections are kept as written; Results says what they got right.

## What is measured, and why

The BOINC app of this repository is the monitor: a non-CPU-intensive task that runs for as long as the client does, looks for
one counter, polls it, cuts bins and sends them as trickles (`docs/boinc-monitor.md`). It does almost no arithmetic. A volunteer
pays for it in wake-ups, system calls, disk writes and memory while the machine is otherwise idle, and the project cares that
every bin the counter counted reaches the server, once and unchanged. So the speed of a task is not the metric. The metrics
are work per simulated hour, and an output check that nothing the server receives has changed.

| Part | Needs hardware? | What is measured |
|---|---|---|
| Monitor, steady (Yarrow detector, GQ GMC, Radioactive@Home) | no: a fake counter | per simulated hour: CPU time, wake-ups, system calls, state-file writes and bytes, trickles and their size; bins counted against bins expected; delay from the end of a bin to the monitor having it and to its trickle; peak memory |
| Monitor, gap fill after the monitor was away (up to 13 days) | no | the same, as a burst: log pages read, bins cut, state-file bytes written, peak memory |
| Monitor, no counter attached or counter unplugged | no | the same: the search every 15 s, the temporary exits |
| Monitor under the BOINC client | partly | the client's 10 Hz timer signal is simulated (it ends every sleep and poll early); the client's own work is not |
| Real USB and kernel cost of a system call, latency of a real counter, power draw | yes | not measured; multiply the counts by a per-call cost measured on the target, see "Hardware check" below |
| Firmware speed, battery life, USB response time | hardware only | not measured. Host proxies that exist: the image size that `make` prints, and "most drawing operations in one frame" that `make sim` prints (`sim/sim.c`, a screen simulator; the monitor needs a different one, `scripts/bench/fake`). The ARM emulator of `make test` is not cycle accurate |
| Log decoder (`src/ylog.c`, `src/ylc.c`) | no | inside the gap-fill profile: the monitor decodes the detector's log there |
| `tools/ylog_decode.py`, Yarrow Hub, `site/` | no | not the BOINC app; no performance requirement is known; not covered |
| Server side (trickle handler, validator) | not in this repository | not covered |

**Why not seconds per task.** A monitor task sleeps almost all the time, so its wall time is the length of the run and its CPU
time is a few milliseconds a minute. A speed-up of that arithmetic is worth nothing to anyone. What can be improved is how
often it wakes up, how many system calls each wake-up makes, how much it writes, and how many trickles it sends, and what a
change of any of these does to the bins.

**The metrics** (per simulated hour of monitoring unless said otherwise; `summarize_bench.py` prints them):

* **CPU ms per hour**: user plus system time of the process, divided by the simulated hours. The fake detector's own cost is in
  it and is the same for every build, so differences are real but look smaller than on a real system. Noisy: it needs the
  acceptance rule below.
* **Wake-ups**: returns from a sleep or a poll that waited. **System calls**: `open`, `close`, `read`, `write`, `poll`,
  `nanosleep`, terminal and HID ioctls and port searches (`glob`, counted as one). These are exact counts: every run of a
  label gives the same number. They are the main metric for changes that touch how the monitor waits and reads. A real
  system call costs more than the fake's, so the counts decide, and the CPU column only confirms.
* **State-file writes and kilobytes written**: `save_state()` through the host's glue (a whole file, `.tmp` and a rename, as
  `monitor/yarrow_boinc.cpp` does it).
* **Trickles per hour and bytes per trickle** (the XML text only), and the **bins**: counted live against the number a run of
  that length holds, and filled from the detector's log.
* **Bin delay**: seconds from the end of a bin to the moment the monitor has it (median and maximum). **Send delay**: to the
  moment its trickle is handed to the host.
* **Peak memory** (`ru_maxrss`). The fake detector's own buffers are in it (a few hundred kilobytes at most by construction,
  the same for every build), so compare labels, not absolute values. Except in the gap-fill profile, where the monitor
  allocates the whole log image, it is expected to show nothing.
* **Output**: a hash of every trickle text and a hash of the final state file. "same output" means both match the baseline's.

**What the fake is.** `scripts/bench/build_variant.sh` builds the real monitor code of the tree that is measured
(`yarrow_monitor.c`, the three sources, the bin cutter, the trickle writer, the gap fill, `yarrow_serial.c`, `ylog.c`,
`ylc.c`) into one program, `monbench`, with three things replaced (`scripts/bench/fake/fake.h` describes them):

1. the system calls of `yarrow_serial.c` and of the Radioactive@Home source go to an in-memory layer on a **virtual clock**: a
   sleep moves the clock and costs nothing, so a simulated week takes seconds, and every call is counted;
2. the counter on the other end: a Yarrow detector (the serial protocol of `docs/serial-protocol.md`, including `LOGINFO` and
   `LOGPAGE` over a log written by the real `src/ylog.c`), a GQ GMC counter (one two-byte heartbeat a second) or a
   Radioactive@Home counter (HID report), or nothing. All three count the same seeded Poisson process;
3. the host, `fake_glue.c` instead of `yarrow_boinc.cpp` or `yarrow_standalone.c`: it keeps the state file, writes every
   trickle to `trickles.txt` and counts. A temporary exit (`glue_no_detector`) moves the virtual clock by the 30 minutes the
   task would be away; the cost of ending and starting the process is not modelled.

Everything is deterministic: fixed seeds, no real time in the output. The harness always comes from the working tree, also
when the baseline is built from a git ref, so both sides run the same harness. A change of the files in `scripts/bench/fake`
changes every number: record the commit of the suite with the results.

**Assumptions of the fake that are not measured**: one serial command and its reply take 1 ms (`rtt_us`); a log page arrives at
100 kB/s (`link_bps`); counts are Poisson at 20 a minute (a small tube at background); the detector's clock is exact; the
counters never glitch (no restart, no clock set, no tube-voltage gap: `make monitor-check` tests the bin cutter with those).
All are profile keys and can be changed.

**Contract with the monitor.** The harness relies on: the file names listed in `build_variant.sh`; `main()` and `time()` in
`yarrow_monitor.c` and `yarrow_src_yarrow.c`; `ypending_add()` called from `on_bin()` only; `nanosleep()` in
`yarrow_src_rah.c`; the system calls in `yarrow_serial.c` (POSIX half); the `glue_*` functions of `monitor/yarrow_glue.h`;
`yhid_open()`, `yhost_wall()` and `yhost_name()` of `monitor/yarrow_hid.h`. A change that renames or splits these needs the
same change in `scripts/bench/fake` and `build_variant.sh`; the build then fails, it does not measure something else.

## How to run it

```
scripts/bench/ab.sh                          # 5 rounds of all eight profiles: upstream/main against the working tree
scripts/bench/ab.sh 3 yarrow-600             # 3 rounds of one profile
scripts/bench/ab.sh 5 "gap-fill unplug"      # several profiles, in quotes
BASE_REF=origin/main scripts/bench/ab.sh     # another baseline
```

It needs a C compiler (`gcc`, or `CC=clang`), `git`, `tar` and `python3`. No BOINC, no counter, no root, no hardware. On a Mac
that is the Xcode command line tools. **On Linux the command is the same**: install `build-essential git python3` (or the
distribution's equivalents); the scripts are written for macOS's bash 3.2 and BSD tools as well as for GNU ones. Nothing
leaves `~/yarrow-work` (`YARROW_WORK=` moves it): builds in `build/`, programs in `bins/`, and every run's directory (its
`trickles.txt`, `stdout.txt`, `stderr.txt`, `results.tsv`, `summary.txt`) in `bench-runs/<date>-<profile>/`.

`ab.sh` builds the baseline (`BASE_REF`, default `upstream/main`, from a clean `git archive`) and the working tree (a clean copy
of its tracked files; a new file must be tracked, `git add -N`, to be copied), then runs every profile with three labels:
`base`, `new`, and `control`, which is the baseline's program again under another name. Run it first with nothing changed: `new`
and `control` must both say "no clear gain" and "same output", or the suite or the machine is not steady.

The pieces, to run by hand:

| file | what it does |
|---|---|
| `build_variant.sh <name> [patch ...]` | copies the tracked files (or `REF=<git ref>`), applies patches (`patch -p1`), compiles `bins/<name>/monbench`. `OPT` (default `-O2`), `CC` (default `gcc`), `EXTRA` (more flags, e.g. a `-D` switch of a patch) |
| `run_bench.sh <profile> <rounds> label=binary ...` | interleaved runs A B C A B C in fresh directories, one line per run, then the summary. `RUN_PREFIX` runs the program through a command (`nice -n 19`, `taskpolicy -b`), `FK_SET="hours 24 seed 7"` overrides profile keys, `QUIET=1` prints only the summary |
| `summarize_bench.py <results.tsv>` | the two tables and the verdicts |
| `profiles/*.prof` | one simulated scenario each (below) |
| `fake/` | the fake counter, serial layer and host, and the wrappers that build the monitor's own files against them |
| `ab.sh` | everything above in one command |

`monbench <profile> [key value ...]` can also be run directly in an empty directory; it prints one `FK key=value ...` line.

**Profiles** (keys: `source` yarrow, gmc, rah or none; `interval` 600, 1800 or 3600; `hours`; `cpm`; `seed`; `start_s`, the
seconds after a multiple of one hour at which the monitor starts, default 137; `sigalrm`, the rate of the BOINC timer signal;
`absent_hours`; `unplug_at` and `unplug_for` in seconds; `foreign_ports`; `rtt_us`; `link_bps`; `hb_phase_ms`):

| profile | what it is | why |
|---|---|---|
| `yarrow-600` | Yarrow detector, 600 s bins, 72 hours, no timer signal | the steady state of the project's own detector; the monitor's own wake-ups and reads |
| `yarrow-600-boinc` | the same with the BOINC timer signal at 10 Hz | what the client's timer adds; the floor of wake-ups no monitor change can lower |
| `yarrow-3600` | hour bins, 72 hours | six times fewer bin edges: nearly all cost is the poll between edges |
| `gap-fill` | the monitor starts after 13 days away, detector log full of it, 2 hours | the burst: 31 log pages as base64, about 1870 bins, state file saved after every bin, backlog sent 144 slots every 10 minutes |
| `unplug` | cable out for 90 minutes in 6 hours, one foreign serial port | lost detector, search, two temporary exits, find, gap fill |
| `no-detector` | nothing attached, a week, two foreign serial ports | the idle search every 15 s, the probing of other ports, temporary exits (about 288 of them, a week divided by 35 minutes; the client aborts a task after more than 100 without a checkpoint, YAR-030, which the simulation does not model) |
| `gmc-600` | GQ GMC counter | one blocking read a second, bins on the host clock |
| `rah-600` | Radioactive@Home counter | one HID report a second, one every 40 ms around edges, bins on the host clock |

The first bin edge is 463 s after the start with the default `start_s`, so every profile of 2 hours or more holds bin edges;
the bin in progress at the start is dropped, as it is on every restart.

How to read the summary (the shape; no numbers are claimed here):

```
CPU time per simulated hour (user + system, fake detector included)
label          runs  median ms    min ms    max ms   vs 1st  verdict / output
base              5        ...       ...       ...      ...x   baseline; same output
new               5        ...       ...       ...      ...x   ACCEPT (+nn %) | no clear gain (...) | slower (...);
                                                                same output | same trickles, other state file | OTHER output
control           5        ...       ...       ...      ...x   should read: no clear gain; same output

Work per simulated hour (exact counts; * = varies between runs)
label        wake-ups  vs 1st   syscalls  vs 1st  state w  state KB  trkl  B/trkl  live/expect  bin delay s  send delay s  RSS MB
```

## The method

### Two kinds of change

* **Same output.** The monitor does the same, at lower cost: block reads from the serial port, one state-file write after a
  gap fill instead of one per bin, build flags, buffer sizes. The trickle texts and the final state file must be
  byte-identical to the baseline's on every profile, and `live/expect` must not fall. Such a change needs a measured gain,
  the output check and the correctness checks below, nothing more.
* **Output-changing.** Anything that changes which bins are accepted, what a trickle holds, when and how often trickles are
  sent, what the state file holds, or when a counter is found: poll cadence, send cadence, state refresh cadence, search
  back-off, bin rules. Measure it the same way, say exactly how the output differs, and treat it as a design decision for the
  maintainers and the server (AGENTS.md: do not change the trickle format, the units, the identity recipes or the bin rules
  without a matching server change; the validator's Poisson, copy, replay and region checks and the new-device credit ramp
  all read the stream). Until then keep it behind a switch that is off by default.

### Rules for every measurement

1. **Build both sides the same way, from clean copies.** The baseline is a git ref (`REF=upstream/main`) built with the
   Makefile's compiler line (`-std=c11 -O2`); the candidate is the working tree, or the baseline plus a patch.
   `build_variant.sh` copies the tracked files, so a stale object file or an untracked scratch file cannot leak in, and uses
   the harness of the working tree for both.
2. **Find out where the cost goes first.** Read the counts of the profile closest to production (`yarrow-600-boinc`) and the
   share of the thing you change in them. A change to 10 % of the system calls cannot lower them by more than 10 %. Under
   BOINC the client's timer alone makes the 36,000 wake-ups an hour that the monitor cannot remove (expected, from
   `docs/kb/shared/boinc-api.md`; not measured). On hardware, count the real ones (see "Hardware check").
3. **Use a profile that exercises the change.** A change to the poll cadence does nothing on `gap-fill`, and a change to the
   gap fill does nothing on `yarrow-600`:

   | change in | profiles |
   |---|---|
   | reads and writes on the serial port (`yarrow_serial.c`, `yser_command`, `yser_lines`) | `yarrow-600`, `yarrow-3600`, `gap-fill`, `unplug` |
   | poll cadence (`yy_wait`, `rah_wait`, `RECAL_EVERY_S`) | `yarrow-600`, `yarrow-3600`, `yarrow-600-boinc`, `rah-600` |
   | state file (`save_state`, the glue) | `yarrow-600`, `gap-fill`, `unplug` |
   | gap fill (`fill_gap`, `yy_backfill`, `yarrow_backfill.c`, `ylog.c`) | `gap-fill`, `unplug` |
   | search and temporary exits (`ysrc_find`, `yser_find`, the 15 s, 300 s, 1800 s timers) | `no-detector`, `unplug` |
   | trickle cadence and text (`flush`, `ypending_xml`) | `yarrow-600`, `yarrow-3600`, `gap-fill` |
   | a source (`yarrow_src_gmc.c`, `yarrow_src_rah.c`) | `gmc-600`, `rah-600` |

4. **Interleave and repeat.** `run_bench.sh` runs A B C A B C ... in fresh directories; the runs are short, so use at least
   5 rounds (the default of `ab.sh`). The control label shows what this machine does to two runs of the same program.
5. **Accept by the rule, not by eye.** For CPU time: a candidate is faster when its median is at least 5 % below the
   baseline's and its slowest run beats the baseline's fastest, or when its median is at least 10 % below. Anything else is
   "no clear gain" and is reported as that. For a count (wake-ups, system calls, writes, bytes, trickles): it is lower when
   it is at least 5 % below the baseline's; no overlap test is needed because the count is the same in every run of a label
   (a count marked `*` is not, and something is wrong). A lower count claims fewer wake-ups or calls, not less CPU time: say
   which one was measured.
6. **Check the output of every run.** Each run's trickle texts and final state file are hashed. `same output` means every run
   of a label matches the baseline in both; `same trickles, other state file` is an output-changing change of the state
   file. Look at `live/expect` too: **never trade bins for wake-ups**. Compare like with like: the three counters differ by
   design (the Radioactive@Home and GQ GMC bins are cut on the host's clock), and both sides need the same compiler (those two
   sources compute their edge times in floating point).
7. **Measure on a quiet machine.** Pause the BOINC client and other heavy programs, keep a laptop on power, and look at
   `uptime` (`run_bench.sh` records the load). A run is a second or so of CPU time, so the CPU column is sensitive to
   frequency scaling and to what else runs; the counts are not. On a loaded machine an interleaved comparison of counts
   is still fair.
8. **Write it down.** Date, machine, OS, compiler, load, profile, rounds, medians and spread, verdict and output go into the
   Results below, with the commit of the suite. A number in a commit message, a README or a code comment comes from there,
   and a platform that was not measured is called unmeasured.

### Steps for one change

1. Run `scripts/bench/ab.sh 3` with nothing changed. `control` and `new` must read "no clear gain; same output".
2. Read the counts of the profile closest to the change and note where they go.
3. Make the change in the working tree, or as a patch (`build_variant.sh <name> <patch>`; the baseline with `REF=`). For an
   output-changing change, put it behind a `-D` switch that is off by default and build both (`EXTRA="-DSWITCH=1"`).
4. Run one round (`run_bench.sh <profile> 1 ...`) to see that the output stays the same (or differs the way you expect).
5. Run 5 or more rounds on each profile the change touches (rule 3).
6. Run the correctness checks that AGENTS.md routes the touched files to: `make monitor-check` and `make monitor` for
   `monitor/` (on macOS `make monitor` stops at YAR-026), `bash tools/sanitize.sh` on Linux for C changes, a MinGW compile
   for `yarrow_serial.c`, `yarrow_hid.c` and `yarrow_lock.c`, and a run by hand with a counter attached for at least one bin
   (`build/monitor/yarrow_monitor --country TR --city Denizli`; usage line in `monitor/yarrow_standalone.c`).
7. Confirm on hardware where you can (below). Name the platforms that were not measured: Windows is never measured here.
8. Record the results here.

### Traps (expected from reading the code; none has been seen yet)

* **NCI scheduling.** The client runs the monitor next to a full set of CPU tasks, at low priority. A poll that waits for a
  CPU slot can miss the edge second of a bin, and the bin is then dropped (`ybin_feed`, "both edge readings in the edge
  second"). The fake never waits for a CPU: bin yield under load is a hardware measurement, and a change that makes the monitor
  poll less around the edges must be checked there. The client can also suspend the task (the worker then sits in the signal
  handler, `docs/kb/shared/boinc-api.md`), which the simulation does not do. On Apple Silicon a background-priority process
  runs on the efficiency cores: compare `RUN_PREFIX="taskpolicy -b"` and `RUN_PREFIX="nice -n 19"` before trusting a CPU
  figure, as the client's way of lowering the priority decides what a volunteer's Mac does.
* **Trickle cadence is the product, not overhead.** At most one trickle every 10 minutes (`SEND_EVERY_S`) and 144 slots each
  (`YT_MAX_SPAN`) are tied to the server and to the live map ("about one bin behind"). A change in how many trickles are sent
  or what is in them changes what the validator and the credit batch see, and the client picks a trickle up at its own pace
  (YAR-006 for two sends close together). It is output-changing even when every bin arrives.
* **One task per host.** The server sends one monitor per computer and a lock file keeps a second out (YAR-019). The cost of a
  change is per host and multiplied by the project's hosts, and a saving of a fraction of a percent of a CPU is still a
  saving of every volunteer's idle wake-ups. The lock is in the project directory under BOINC and in the run directory here,
  so runs do not collide; but never run the standalone monitor on a counter that a BOINC monitor also reads: the port is
  opened without exclusion and the replies interleave.
* **Timers and sleeps make wall time meaningless.** A real run lasts as long as the time it simulates; the fake's wall time
  is the cost of the simulation. Never compare wall times. Waits, the 15 s search, the 300 s and 1800 s temporary-exit
  timers, the 10-minute send timer and the 60 s housekeeping all run on the virtual clock. The GQ GMC source calls
  `nanosleep(0)` every turn (`gmc_wait`), which is a system call that waits for nothing.
* **The BOINC timer is a floor.** Under the client every sleep is cut about ten times a second, whatever the monitor asks
  for: a monitor that sleeps 5 s instead of 1 s makes no fewer wake-ups than the timer (expected, not measured; compare
  `yarrow-600` and `yarrow-600-boinc`).
* **The edge windows.** The monitor polls ten times a second in the 6 seconds around each bin edge (40 ms for Radioactive@Home).
  A run shorter than a bin has none. The profiles are long enough; a custom `FK_SET="hours 1"` is not.
* **A state file hash includes the last-seen time** (the `S` line), which is the time of the last poll: a change of the poll
  cadence changes it. That is why it is reported apart from the trickles.
* **The log pages and the word END (YAR-033).** `ybf_page_from_reply()` (`monitor/yarrow_backfill.c`) stops decoding a page at any `END`
  in the base64 text, not only at the terminator line. By arithmetic (64^3 values at 5,464 positions) about 2 % of pages of
  compressed data contain one, and the gap fill then gives up with "the detector's log could not be used". Found by reading,
  not seen on a detector. The fake rebuilds its log with a tiny change until no page has one, so the `gap-fill` profile
  measures a gap fill; once YAR-033 is fixed in the monitor, the workaround in `fake_dev.c` can go.
* **Moving the harness.** A change in `scripts/bench/fake` changes every number. Results from before and after it are not
  comparable.

## Where the cost is expected (expected, not measured)

Read from the code at `64e5d5d`; the first run is meant to confirm or correct this list.

| Where | What | Expected size |
|---|---|---|
| `monitor/yarrow_serial.c:122-126` (`rd`), `:147-180` (`yser_command`), `:182-210` (`yser_lines`) | the reply is read one byte at a time: a `poll()` and a `read()` for every byte, and a clock read for every loop turn. A `COUNTS` reply is about 25 to 30 bytes | about 55 system calls per poll of the detector; with 3,900 polls an hour at 600 s bins, about 215,000 system calls an hour. A `LOGPAGE` reply is about 5.5 kB, so about 11,000 system calls a page and about 340,000 for the 31 pages of a gap fill |
| `monitor/yarrow_src_yarrow.c:91-95` (`yy_wait`), `monitor/yarrow_monitor.c:221` | one poll a second, ten a second in the 6 seconds around each edge | 654 polls per 600 s bin; with hour bins about 3,650 polls an hour, nearly all between edges |
| `monitor/yarrow_serial.c:137-144` (`yser_sleep_ms`) | under BOINC, `nanosleep` is cut every 0.1 s by SIGALRM and started again with the time left | about 36,000 wake-ups an hour from the library's timer, plus the monitor's own, about 4,000 |
| `monitor/yarrow_monitor.c:41-58` (`save_state`), `:34-39` (`on_bin`), `:204-206`, `:57-74` (`flush`); `yarrow_boinc.cpp:79-90` | the whole file is formatted and rewritten (`.tmp`, rename, `boinc_checkpoint_completed()`) after every bin, every 60 seconds, after every send and at the exit | about 70 writes an hour with 600 s bins, each a few hundred bytes while the pending list is short |
| `monitor/yarrow_monitor.c:76-97` (`fill_gap`), `monitor/yarrow_src_yarrow.c:107-146` (`yy_backfill`) | each gap-filled bin calls `save_state()`; the pending list keeps 1008 bins (YAR-025) | up to about 1870 file writes of up to about 15 kB: about 20 MB written and as many renames, for one start |
| `monitor/yarrow_monitor.c:148-170`, `monitor/yarrow_src.c:18-71` | with no counter: a 500 ms sleep loop, a search every 15 s that tries the Yarrow ports, the Radioactive@Home HID devices, then `<GETVER>>` on every serial port at two baud rates with an 0.8 s wait, then a temporary exit of 30 minutes after 5 minutes | about 2 wake-ups a second while searching, and about 1.7 seconds of waiting per foreign port per scan (virtual time); also writes to other devices' ports (YAR-011) |
| `monitor/yarrow_src_gmc.c:182` (`gmc_wait`), `monitor/yarrow_src_rah.c:86-90` | GQ GMC: one blocking read a second, no sleep; Radioactive@Home: a HID ioctl a second, 40 ms around the edges | one to two wake-ups a second; the GMC diagnostics (histogram, correlation) are a few arithmetic operations a second |
| `monitor/yarrow_trickle.c:33-60` (`ypending_xml`) | one trickle per bin at most every 10 minutes; about 140 bytes of fixed text (device, calibration, place) plus 2 to 4 bytes a bin | 6 trickles an hour at 600 s bins, 1 at hour bins |
| everything else in user space (`sscanf` of the reply, `ybin_feed`, `ybf_bins`, the range decoder) | microseconds per poll | negligible next to the system calls; compiler flags are not expected to matter |

Latency is set by the edge window: a bin is expected to reach the monitor within the 100 ms poll step (40 ms for
Radioactive@Home) after its end. Its trickle follows as soon as 600 s have passed since the last one, so in a stream that is
already running it is expected to be handed over within about a second of the bin, and the first one after a start at once.

## Candidate improvements, ranked

By expected gain for the effort and the risk, to be measured first in this order. "Same" and "changes" are the output class of
the method above.

1. **Read the serial reply in blocks** (`yarrow_serial.c`: one `poll()` and one `read()` of everything that is there, a small
   buffer per port that `yser_command`, `yser_lines` and `yser_read` share, reset on open and drain). Output: **same**. Expect
   the system calls an hour to fall by 90 % or more on every Yarrow profile and a gap fill to fall from hundreds of
   thousands of calls to a few hundred; wake-ups do not change. Care: bytes after the end of a reply (a live-stream line, the
   next reply) must stay in the buffer. Profiles: `yarrow-600`, `yarrow-3600`, `gap-fill`, `unplug`.
2. **Save the state file once after a gap fill** instead of after every gap-filled bin (`on_bin` called from `fill_gap`).
   Output: **same** (trickles and final state file; only the intermediate states differ). Expect the bytes written and the
   renames of `gap-fill` and `unplug` to fall by more than 99 %. Profiles: `gap-fill`, `unplug`.
3. **Poll the Yarrow detector less between the edge windows** (every few seconds instead of every second; 100 ms around the
   edges as now). Output: **changes** in edge cases: a lost detector, a restart of the counter or a clock jump is noticed up
   to that many seconds later, the last-seen time is coarser, and `discontinuity()` compares samples further apart. On a clean
   profile the bins are expected to be the same; `make monitor-check`'s slow-polling test shows that bins may be lost, never
   wrong. Expect fewer polls and system calls by the same factor, and no change in the wake-ups under BOINC beyond the monitor's
   own few thousand, so measure `yarrow-600` and `yarrow-600-boinc` together. Profiles: `yarrow-600`, `yarrow-3600`,
   `yarrow-600-boinc`, `rah-600`.
4. **Refresh the state file every 5 minutes instead of every minute** while a detector is attached (`RECAL_EVERY_S` also
   drives the calibration check; the save would be separated from it). Output: **changes**: the `S` line is up to 5 minutes
   older, so a gap fill after a restart starts earlier and can send bins a second time (the server keeps the first count,
   YAR-028 is the open question). Expect state-file writes to fall from about 70 an hour to about 20. Profile: `yarrow-600`.
5. **Back off the search for a counter and probe only what can be one**: after the first 5 minutes ask every minute instead
   of every 15 s; with YAR-011, open only ports with the Yarrow USB ids. Output: **changes** (the time to find a counter that is
   plugged in, which ports are written to), not the bins. Expect the scan wake-ups and the foreign-port probes of
   `no-detector` and `unplug` to fall by the factor of the back-off. Profiles: `no-detector`, `unplug`.
6. **Send trickles less often** (for example every 30 minutes for 600 s bins). Output: **changes**: the number of trickles
   falls three-fold at 600 s bins and each carries up to three bins; the live map is up to 30 minutes behind instead of 10;
   `YT_MAX_SPAN` still bounds it. This is the one that lowers the load on the server and on the volunteer's network, and it is
   the server's to decide. Profiles: `yarrow-600`, `gap-fill`.
7. **Compiler flags** (`-O3`, `-Os`, link-time optimisation). Output: **same**. Expected to read "no clear gain", because the
   monitor is bound by system calls, not by instructions; worth one run to say so with a number.

Not a candidate: lowering the BOINC library's own timer wake-ups. They are not the monitor's.

## What the suite does not cover

* `monitor/yarrow_boinc.cpp` and the BOINC library: there is no BOINC stub in this repository (AGENTS.md), so
  `boinc_checkpoint_completed()`, trickle pick-up by the client, `boinc_temporary_exit()` as a real process exit and restart,
  suspend and resume, CPU throttling and the client's priority and scheduling of the NCI task are not run. Only the timer
  signal is imitated, and only for sleeps and polls.
* Real hardware: USB and tty driver behaviour (for example `poll()` on a port that vanished in the middle of a reply),
  latency and jitter of a real detector, a counter that glitches, GQ GMC bursts after a host stall, the Radioactive@Home
  clock that runs 0.13 % slow, the cost of a real system call, power states, timer coalescing, sleep and wake of a laptop
  (YAR-016).
* Windows (`QueryPerformanceFrequency` on every `yser_now()`, the COM1 to COM64 probe, the HID code) and macOS HID (YAR-015:
  there is no backend, so a Radioactive@Home counter is found only on Linux and Windows; the fake supplies one anywhere).
* The firmware (speed, power, USB response), Yarrow Hub, the site, `tools/`, the hardware designs, and the server.
* Memory growth over weeks: a run is at most a week of virtual time.

### Hardware check

The suite predicts counts; confirm the important ones once on a real counter. Build the command-line host (`make monitor`),
attach a counter, run `build/monitor/yarrow_monitor --country TR --city Denizli --state /tmp/ys.txt` for at least 6 hours on
the target machine, and count. On Linux: `strace -c -f -p <pid>` for 10 minutes (system calls by kind) and
`pidstat -w -p <pid> 60` or `perf stat -e context-switches -p <pid>` (wake-ups). On macOS: `sudo powermetrics --samplers
tasks -i 10000` (idle wake-ups a second) and `sample <pid> 10` (`dtruss` needs System Integrity Protection relaxed). On both:
`ps -o time= -p <pid>` at the start and the end for CPU time. Compare with the fake's counts for `yarrow-600` per hour and note
the per-call cost. This run has no BOINC client, so it has no timer signal; run the BOINC build with the same counters for
the figure with it.

## Results

### 2026-11-04: candidates 1 and 2 (serial reads in blocks, one state save after a gap fill)
Machine: Apple M4 Pro (8 performance + 4 efficiency cores, 24 GB), macOS 27.0, Apple clang 21.0.0 (as `gcc`), load 2.3-2.4
(the benchmark itself, the window server and the BOINC Manager window), on power, BOINC client's tasks suspended.
Baseline: `upstream/main` (64e5d5d). Candidate: this tree: `monitor/yarrow_serial.c` (POSIX half) reads what the port holds in
one `read()` into a 256-byte buffer per port and hands it out byte by byte (candidate 1); `monitor/yarrow_monitor.c` saves the
state file once after a gap fill instead of after every gap-filled bin (candidate 2).
Command: `scripts/bench/ab.sh` (5 rounds, all eight profiles). Control: no clear gain and same output on every profile.

| profile | CPU ms/h base -> new | wake-ups/h | syscalls/h base -> new | state writes/h | state KB/h | output | verdict (CPU) |
|---|---|---|---|---|---|---|---|
| `yarrow-600` | 11.53 -> 10.99 | 3874, same | 217,567 -> 15,677 (**-93 %**) | 71.9, same | 5.8, same | same | no clear gain (+4.6 %) |
| `yarrow-600-boinc` | 11.81 -> 11.34 | 39,824, same | 253,517 -> 51,627 (**-80 %**) | 71.9 | 5.8 | same | no clear gain (+4.0 %) |
| `yarrow-3600` | 10.14 -> 9.69 | 3643, same | 204,818 -> 14,752 (**-93 %**) | 61.9 | 5.0 | same | no clear gain (+4.5 %) |
| `gap-fill` | 194.72 -> 16.28 (**12.0x**) | 3874, same | 409,024 -> 16,384 (**-96 %**) | 1007 -> 72 (**-93 %**) | 10,510 -> 347 (**-97 %**) | same | ACCEPT |
| `unplug` | 9.00 -> 7.91 (**1.14x**) | 3058, same | 206,683 -> 11,872 (**-94 %**) | 52.5 -> 51.2 | 4.9 -> 4.7 | same | ACCEPT |
| `no-detector` | 0.22 -> 0.22 | 1106, same | 2324, same | 1.7 | 0.0 | same | no clear gain |
| `gmc-600` | 10.33 -> 10.22 | 3600, same | 10,800, same | 72.0 | 6.4 | same | no clear gain |
| `rah-600` | 10.22 -> 10.20 | 4176, same | 8352, same | 72.0 | 6.1 | same | no clear gain |

Output: the same trickles and the same final state file on every run of every profile; bins live/expected and the delays
unchanged. The GQ GMC source reads its binary heartbeats through `yser_read()` (block reads already) and Radioactive@Home uses
HID, so their counts do not move, as expected. The CPU time per hour is mostly the fake detector's own work, so a 93 % cut in
the monitor's system calls shows as only 4-5 % there (below the rule): on a real system each saved call is a real kernel entry
(`Hardware check` above gives the per-call cost to multiply by).

What the estimates got right: candidate 1 "90 % or more" (80-96 %), candidate 2 "more than 99 %" of the bytes (97 %, the
per-minute saves remain), wake-ups unchanged.

Correctness checks run: `make monitor-check` (all monitor, backfill, Radioactive@Home and GQ GMC checks passed), `yarrow_serial.c`
and `yarrow_monitor.c` compiled with Apple clang 21, Debian GCC 12 (Linux arm64 container) and MinGW-w64 GCC 16.2 at
`-O2 -Wall -Wextra -Werror`. `make monitor` itself fails with GCC on Linux and with MinGW-w64, before and after this change, on a
format-truncation warning in `yarrow_src.c` (YAR-034).
Hardware check: not done (no counter attached).
Platforms not measured: Linux and Windows on real hardware. The Windows half of `yarrow_serial.c` is unchanged: its reads
depend on `COMMTIMEOUTS` (YAR-002), and a block `ReadFile` would wait for the inter-byte gap after every reply, which cannot be
checked without a Windows host and a counter (the convention to keep both halves in step is kept by recording this here).

### Next steps by platform (plan of 2026-11-04)

| platform | next step | expected | evidence |
|---|---|---|---|
| Linux, macOS | release candidates 1 and 2 | 80-96 % fewer system calls, 97 % fewer state bytes in a gap fill | above |
| Windows | block reads with a total timeout (together with YAR-002), measured on a Windows host with a counter | the same order as POSIX | not measured |
| all | candidate 4 (state every 5 minutes) and 6 (fewer trickles), decide (YAR-028, the server) | state writes 70 -> 20 an hour; a third of the trickles | the candidates list |
| all | candidate 3 (poll less between edges) and 5 (search back-off), decide | fewer polls and probes | the candidates list |
| Linux, Windows (GCC) | fix YAR-034 so `make monitor` builds with GCC | the documented build works again | YAR-034 |

One entry per later change or per platform, newest first, in this form:

```
### <date>: <change, or "A/A of the suite">
Machine: <CPU model and cores, RAM>, <OS and version>, <compiler and version>, load <uptime>, <on power | on battery>
Suite commit: <sha>     Baseline: <ref and sha>     Candidate: <sha, or patch name and switch>
Command: scripts/bench/ab.sh <rounds> "<profiles>"      (RUN_PREFIX=<...> if used)
Control: <its verdict on each profile; "ok" if no clear gain and same output>

| profile | label | CPU ms/h median (min-max) | wake-ups/h | syscalls/h | state writes/h | state KB/h | trickles/h | live/expected | output | verdict |
|---|---|---|---|---|---|---|---|---|---|---|

Output: <same output on every profile, or how it differs>
Correctness checks run: <make monitor-check, make monitor, tools/sanitize.sh, hand run with a counter, ...>
Hardware check: <what was counted on a real counter, or "not done">
Platforms not measured: <...>
```
