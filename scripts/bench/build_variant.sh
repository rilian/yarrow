#!/bin/bash
# Builds one variant of the Yarrow monitor into monbench, the benchmark program run_bench.sh runs (docs/BENCHMARK.md).
# usage: build_variant.sh <name> [patch ...]
#   Copies the tracked files of this repository (git ls-files; or REF=<git ref> via git archive) to $YARROW_WORK/build/<name>,
#   applies the patches (git diff format, patch -p1), and compiles the monitor sources of that copy together with the fake
#   detector, serial layer and host of scripts/bench/fake into $YARROW_WORK/bins/<name>/monbench. The fake files always come from
#   this working tree, so every variant is measured with the same harness; a new file the variant needs has to be tracked
#   (git add -N) to be copied.
#   env: REF           build a git ref (e.g. upstream/main) instead of the working tree
#        OPT           compiler flags (default -O2, as the Makefile's monitor target)
#        CC            compiler (default gcc, as the Makefile; on macOS that is Apple clang)
#        EXTRA         more compiler flags, e.g. "-DSOME_SWITCH=1" for a patch that has one
#        YARROW_WORK   default ~/yarrow-work: builds in build/, programs in bins/, results in bench-runs/
#        YARROW_SRC    the tree to copy (default: this repository)
# The monitor files that are built: yarrow_monitor.c, yarrow_serial.c, yarrow_src.c, yarrow_src_yarrow.c, yarrow_src_rah.c,
# yarrow_src_gmc.c, yarrow_bin.c, yarrow_trickle.c, yarrow_backfill.c, yarrow_lock.c and src/ylog.c, src/ylc.c; the host
# (yarrow_boinc.cpp, yarrow_standalone.c) and yarrow_hid.c are replaced. Windows code paths are not built. If a change renames
# or splits one of these files or changes monitor/yarrow_glue.h, update the list below and the files in scripts/bench/fake.
set -e -o pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo=${YARROW_SRC:-$(cd "$here/../.." && pwd)}
[ $# -ge 1 ] || { sed -n 2,18p "$0"; exit 2; }
name=$1; shift
W=${YARROW_WORK:-$HOME/yarrow-work}
CC=${CC:-gcc}; OPT=${OPT:--O2}; EXTRA=${EXTRA:-}
T=$W/build/$name; out=$W/bins/$name
command -v "$CC" > /dev/null || { echo "no compiler: $CC"; exit 2; }
command -v git > /dev/null || { echo "git is needed"; exit 2; }
rm -rf "$T"; mkdir -p "$T" "$out"
rm -f "$out/monbench"

if [ -n "${REF:-}" ]; then
  (cd "$repo" && git archive "$REF") | tar -xf - -C "$T"
  what=$(cd "$repo" && git rev-parse --short "$REF")
  what="$REF ($what)"
else
  # the tracked files as they are now; a tracked file that was deleted in the working tree is left out
  (cd "$repo" && git ls-files -z) | while IFS= read -r -d '' f; do
    if [ -e "$repo/$f" ] || [ -L "$repo/$f" ]; then printf '%s\0' "$f"; fi
  done | (cd "$repo" && tar --null -T - -cf -) | tar -xf - -C "$T"
  what="working tree"
fi
for p in "$@"; do
  a="$(cd "$(dirname "$p")" && pwd)/$(basename "$p")"
  (cd "$T" && patch -p1 -s < "$a")
done
for f in monitor/yarrow_monitor.c monitor/yarrow_serial.c monitor/yarrow_src.c monitor/yarrow_src_yarrow.c monitor/yarrow_src_rah.c \
         monitor/yarrow_src_gmc.c monitor/yarrow_bin.c monitor/yarrow_trickle.c monitor/yarrow_backfill.c monitor/yarrow_lock.c \
         src/ylog.c src/ylc.c; do
  [ -f "$T/$f" ] || { echo "$what has no $f: update the list in build_variant.sh"; exit 1; }
done

F=$here/fake
log=$T/build.log
# -DYLOG_MAX_PAGE=4096: the detector's log pages (the fake detector writes its log with src/ylog.c); the monitor only reads it
# -D_DEFAULT_SOURCE: the POSIX calls under -std=c11 on glibc, as monitor/yarrow_serial.c asks for itself
$CC -std=c11 $OPT $EXTRA -Wall -Wextra -DYLOG_MAX_PAGE=4096 -D_DEFAULT_SOURCE -I "$T/monitor" -I "$T/src" -I "$F" \
    "$F/fake_main.c" "$F/fake_sys.c" "$F/fake_dev.c" "$F/fake_glue.c" "$F/fake_hid.c" \
    "$F/wrap_monitor.c" "$F/wrap_serial.c" "$F/wrap_src_yarrow.c" "$F/wrap_src_rah.c" \
    "$T/monitor/yarrow_src.c" "$T/monitor/yarrow_src_gmc.c" "$T/monitor/yarrow_bin.c" "$T/monitor/yarrow_trickle.c" \
    "$T/monitor/yarrow_backfill.c" "$T/monitor/yarrow_lock.c" "$T/src/ylog.c" "$T/src/ylc.c" \
    -lm -o "$out/monbench" > "$log" 2>&1 || true
[ -x "$out/monbench" ] || { grep -E 'error' "$log" | head -20; echo "build failed, see $log"; exit 1; }
{
  echo "name $name"
  echo "source $what"
  echo "patches $*"
  echo "compiler $($CC --version 2>&1 | head -1)"
  echo "flags $OPT $EXTRA"
} > "$out/info.txt"
echo "built $out/monbench ($what, $($CC --version 2>&1 | head -1), $OPT $EXTRA${*:+, patches: $*})"
