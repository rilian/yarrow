#!/bin/bash
# Interleaved benchmark of builds of the Yarrow monitor (build_variant.sh) on one profile of the fake detector (docs/BENCHMARK.md).
# usage: run_bench.sh <profile> <rounds> <label>=<binary> [<label>=<binary> ...]
#   profile: a file in scripts/bench/profiles/ without .prof, or the path of a .prof file
#   Runs A B C ... A B C ... (rounds) in fresh run directories, so that drift of the machine's load hits every build alike. The
#   first label is the baseline. Giving the same binary under two labels makes a control: it should come out "no clear gain".
#   env: BENCH_OUT     default $YARROW_WORK/bench-runs (YARROW_WORK default ~/yarrow-work); the run directories are there
#        RUN_PREFIX    a command the program is run through, e.g. "nice -n 19" or "taskpolicy -b"
#        FK_SET        more "key value" pairs for the program, e.g. "hours 24 seed 7" (they override the profile)
#        QUIET=1       print only the summary
#   The program is deterministic and runs on a virtual clock: its wall time is the cost of the simulation, not a result. Each run
#   prints one line of counts and times, collected as results.tsv, and the summary of summarize_bench.py follows.
# macOS and Linux; needs python3 for the summary.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
[ $# -ge 3 ] || { sed -n 2,13p "$0"; exit 2; }
PROFILE=$1; ROUNDS=$2; shift 2
PF=$PROFILE; [ -f "$PF" ] || PF=$here/profiles/$PROFILE.prof
[ -f "$PF" ] || { echo "no profile $PROFILE"; exit 2; }
PF="$(cd "$(dirname "$PF")" && pwd)/$(basename "$PF")"
for lb in "$@"; do
  case $lb in *=*) ;; *) echo "$lb: give label=binary"; exit 2 ;; esac
  [ -x "${lb#*=}" ] || { echo "${lb#*=} is not an executable"; exit 2; }
done
STAMP=$(date +%Y%m%d-%H%M%S)
OUT=${BENCH_OUT:-${YARROW_WORK:-$HOME/yarrow-work}/bench-runs}/$STAMP-$(basename "$PF" .prof)
mkdir -p "$OUT"
RESULTS=$OUT/results.tsv
: > "$RESULTS"
emit() { if [ "${QUIET:-0}" = 1 ]; then cat >> "$RESULTS"; else tee -a "$RESULTS"; fi; }
cpu_model() {
  local m
  if [ "$(uname)" = Darwin ]; then m=$(sysctl -n machdep.cpu.brand_string 2> /dev/null)
  else m=$(grep -m1 'model name' /proc/cpuinfo 2> /dev/null | sed 's/.*: *//'); fi
  echo "${m:-$(uname -m)}"
}
{
  echo "# $(date '+%Y-%m-%d %H:%M:%S') profile $PF rounds $ROUNDS os $(uname -sr) cpu $(cpu_model)"
  echo "# settings: $(grep -v '^#' "$PF" | tr '\n' ';') ${FK_SET:-}"
  echo "# load: $(uptime | sed 's/.*load/load/')"
  for lb in "$@"; do echo "# ${lb%%=*} = ${lb#*=} cksum $(cksum "${lb#*=}" | cut -d' ' -f1)"; done
} | emit

run_one() { # label binary round
  local label=$1 bin=$2 round=$3 dir code line
  dir=$OUT/$label-$round
  mkdir -p "$dir"
  ( cd "$dir" && ${RUN_PREFIX:-} "$bin" "$PF" ${FK_SET:-} > stdout.txt 2> stderr.txt )
  code=$?
  line=$(grep '^FK ' "$dir/stdout.txt" | tail -1 | cut -c4-)
  printf '%s\t%s\t%s\t%s\n' "$label" "$round" "$code" "$line" | emit
}
r=1
while [ "$r" -le "$ROUNDS" ]; do
  for lb in "$@"; do run_one "${lb%%=*}" "${lb#*=}" "$r"; done
  r=$((r + 1))
done
python3 -B "$here/summarize_bench.py" "$RESULTS" | tee "$OUT/summary.txt"
echo "(runs, trickles.txt and stderr.txt of every run: $OUT)"
