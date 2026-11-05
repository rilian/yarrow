#!/bin/bash
# One-command A/B of the monitor: the default branch of upstream, built as the Makefile's monitor target builds it, against this
# working tree, on the profiles of scripts/bench/profiles, with a control. macOS and Linux alike: the fake detector needs no
# hardware, no BOINC and no root, and nothing is flashed.
# usage: scripts/bench/ab.sh [rounds per profile, default 5] [profiles, default all]
#   env: BASE_REF      the baseline (default upstream/main if that remote branch exists, else origin/main)
#        YARROW_WORK   default ~/yarrow-work: builds in build/ and bins/, results in bench-runs/<date>-<profile>/
#        OPT, CC, EXTRA, RUN_PREFIX, FK_SET   as in build_variant.sh and run_bench.sh (they apply to both sides)
# Needs: gcc or clang (CC), git, tar, python3. Linux: the same command, with the compiler of the distribution installed
# (apt install build-essential git python3; the C library needs no extra package). A profile is a simulated couple of hours
# to a week on a virtual clock, so a run should take seconds, not hours (expected, not timed yet: see docs/BENCHMARK.md).
#
# Labels:
#   base      BASE_REF, built from a clean copy (git archive)
#   new       this working tree, built from a clean copy of its tracked files
#   control   the program of base again, run under another label: it measures the noise of this machine. If control comes out
#             "ACCEPT" or "slower" against base, the machine is too loaded to read the CPU column: use the counts.
# With nothing changed in monitor/ and src/ the verdict of new is "no clear gain; same output", which shows that the suite itself
# is steady. Pass the profile names to run fewer, e.g.  scripts/bench/ab.sh 3 "yarrow-600 gap-fill".
set -e -o pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
ROUNDS=${1:-5}
PROFILES=${2:-"yarrow-600 yarrow-600-boinc yarrow-3600 gap-fill unplug no-detector gmc-600 rah-600"}
W=${YARROW_WORK:-$HOME/yarrow-work}
export YARROW_WORK=$W
if [ -z "${BASE_REF:-}" ]; then
  for r in upstream/main origin/main upstream/master origin/master; do
    if git -C "$repo" rev-parse -q --verify "$r" > /dev/null; then BASE_REF=$r; break; fi
  done
fi
[ -n "${BASE_REF:-}" ] || { echo "no upstream/main or origin/main to compare with: BASE_REF=<git ref> $0"; exit 2; }
if [ -n "$(git -C "$repo" status --porcelain --untracked-files=no)" ]; then
  echo "note: this working tree has uncommitted changes to tracked files; 'new' is built from them as they are"
fi
echo "== baseline $BASE_REF ($(git -C "$repo" rev-parse --short "$BASE_REF")), new: $(git -C "$repo" rev-parse --short HEAD) plus the working tree"

REF=$BASE_REF bash "$here/build_variant.sh" base
bash "$here/build_variant.sh" new
B=$W/bins
for prof in $PROFILES; do
  echo
  echo "================ profile $prof"
  QUIET=1 bash "$here/run_bench.sh" "$prof" "$ROUNDS" base=$B/base/monbench new=$B/new/monbench control=$B/base/monbench
done
