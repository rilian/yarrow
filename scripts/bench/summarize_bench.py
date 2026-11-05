#!/usr/bin/env python3
"""Summary of a run_bench.sh results.tsv: two tables per profile.

1. CPU and output. Per label the median, minimum and maximum CPU time (user + system, of the monitor's code and the fake
   detector, which is the same for every build) per simulated hour, the speed relative to the first label, the verdict of the
   acceptance rule below, and whether the label's results are the same as the first label's.
2. Work per simulated hour. The counts the fake layers make: wake-ups (returns from a wait that took time), system calls,
   state-file writes and bytes, trickles and their size, bins against the bins the run should hold, the delay from the end of a
   bin to the monitor having it and to its trickle being handed over, and peak memory. These counts are exact: every run of a
   label gives the same number, and a number that varies between runs is marked with *.

Acceptance rule (as in docs/BENCHMARK.md): a candidate is faster when its median CPU time is at least 5 % below the first
label's and its slowest run is faster than the first label's fastest, or when its median is at least 10 % below. A count needs
no overlap test: it is lower when it is at least 5 % below the first label's. With fewer than 3 runs of either label the
CPU verdict is "too few runs". A change that is "faster" or "lower" but not "same output" is an output-changing change and
goes to the maintainers (docs/BENCHMARK.md, "Two kinds of change").

usage: summarize_bench.py <results.tsv>
"""
import statistics
import sys

NAN = float("nan")


def num(kv, key):
    try:
        return float(kv[key])
    except (KeyError, ValueError):
        return NAN


def med(xs):
    xs = [x for x in xs if x == x]
    return statistics.median(xs) if xs else NAN


def parse(path):
    header, runs, order = [], {}, []
    for line in open(path):
        line = line.rstrip("\n")
        if line.startswith("#"):
            header.append(line)
            continue
        if not line.strip():
            continue
        f = line.split("\t")
        if len(f) < 3:
            continue
        kv = {}
        if len(f) > 3:
            for tok in f[3].split():
                if "=" in tok:
                    k, v = tok.split("=", 1)
                    kv[k] = v
        label = f[0]
        if label not in runs:
            runs[label] = []
            order.append(label)
        runs[label].append({"code": f[2], "kv": kv})
    return header, runs, order


def derived(r):
    """Per-run numbers; per simulated hour where that makes sense."""
    kv = r["kv"]
    sim_h = num(kv, "sim_s") / 3600.0
    d = {"sim_h": sim_h}
    d["cpu_ms_h"] = 1000.0 * (num(kv, "cpu_user") + num(kv, "cpu_sys")) / sim_h if sim_h > 0 else NAN
    sysc = 0.0
    for k in ("open", "close", "read", "write", "poll", "sleep", "ioctl", "glob"):
        sysc += num(kv, k)
    d["sys_h"] = sysc / sim_h if sim_h > 0 else NAN
    for key, name in (("wakeups", "wake_h"), ("state_writes", "sw_h"), ("trickles", "trk_h")):
        d[name] = num(kv, key) / sim_h if sim_h > 0 else NAN
    d["state_kb_h"] = num(kv, "state_bytes") / 1024.0 / sim_h if sim_h > 0 else NAN
    d["b_trk"] = num(kv, "trickle_bytes") / num(kv, "trickles") if num(kv, "trickles") > 0 else NAN
    d["bins"] = num(kv, "bins")
    d["live"] = num(kv, "bins_live")
    iv = num(kv, "interval")
    d["expected"] = float(int(num(kv, "sim_s") // iv)) if iv > 0 else NAN
    d["rss_mb"] = num(kv, "rss_kb") / 1024.0
    d["key"] = (kv.get("hash_t"), kv.get("hash_s"))
    return d


def verdict(m, hi, rmed, rmin, n, nref):
    if not rmed or rmed != rmed or rmed <= 0:
        return "no clear gain (too little CPU time to compare)"
    gain = 1 - m / rmed
    if min(n, nref) < 3:
        # one or two runs say nothing about the spread, so the rule cannot be applied
        return "too few runs (%+.1f %%)" % (100 * gain)
    if (gain >= 0.05 and hi < rmin) or gain >= 0.10:
        return "ACCEPT (%+.1f %%)" % (100 * gain)
    if gain <= -0.05:
        return "slower (%+.1f %%)" % (100 * gain)
    return "no clear gain (%+.1f %%)" % (100 * gain)


def output_text(lb, order, ds, runs):
    bad = sorted(set(r["code"] for r in runs[lb] if r["code"] != "0"))
    if bad:
        return "FAILED (exit %s)" % ",".join(bad)
    if any(d["key"][0] is None for d in ds[lb]):
        return "FAILED (no result line)"
    keys = set(d["key"] for d in ds[lb])
    if len(keys) > 1:
        return "results differ between its own runs"
    ref = order[0]
    ref_keys = set(d["key"] for d in ds[ref])
    if keys == ref_keys:
        return "same output"
    if set(k[0] for k in keys) == set(k[0] for k in ref_keys):
        return "same trickles, other state file"
    for o in order[: order.index(lb)]:
        if set(d["key"] for d in ds[o]) == keys:
            return "same output as " + o
    db = med(d["bins"] for d in ds[lb]) - med(d["bins"] for d in ds[ref])
    dt = med(num(r["kv"], "trickles") for r in runs[lb]) - med(num(r["kv"], "trickles") for r in runs[ref])
    return "OTHER output (bins %+.0f, trickles %+.0f)" % (db, dt)


def count(lb, ds, key):
    """Median of a per-run count and whether it varies between the runs of the label."""
    vals = [d[key] for d in ds[lb] if d[key] == d[key]]
    if not vals:
        return NAN, False
    return statistics.median(vals), max(vals) - min(vals) > 1e-9 * max(1.0, abs(max(vals)))


def fmt(v, spec, varies=False):
    if v != v:
        return "-"
    return (spec % v) + ("*" if varies else "")


def pct(v, ref):
    if v != v or ref != ref or ref == 0:
        return "-"
    return "%+.0f %%" % (100.0 * (v / ref - 1))


def main(path):
    header, runs, order = parse(path)
    if not order:
        print("no runs")
        return
    for h in header[:3]:
        print(h)
    ds = {lb: [derived(r) for r in runs[lb]] for lb in order}
    ref = order[0]

    # 1. CPU and output
    rcpu = [d["cpu_ms_h"] for d in ds[ref]]
    rmed, rmin = med(rcpu), min(x for x in rcpu if x == x) if any(x == x for x in rcpu) else NAN
    nref = sum(1 for x in rcpu if x == x)
    print()
    print("CPU time per simulated hour (user + system, fake detector included)")
    print("%-14s %4s %10s %9s %9s  %7s  %s" % ("label", "runs", "median ms", "min ms", "max ms", "vs 1st", "verdict / output"))
    for lb in order:
        cpus = [d["cpu_ms_h"] for d in ds[lb] if d["cpu_ms_h"] == d["cpu_ms_h"]]
        out = output_text(lb, order, ds, runs)
        if not cpus:
            print("%-14s %4d %10s %9s %9s  %7s  %s" % (lb, len(runs[lb]), "-", "-", "-", "-", out))
            continue
        m, lo, hi = statistics.median(cpus), min(cpus), max(cpus)
        v = "baseline" if lb == ref else verdict(m, hi, rmed, rmin, len(cpus), nref)
        print("%-14s %4d %10.2f %9.2f %9.2f  %6.3fx  %s; %s" % (lb, len(cpus), m, lo, hi, (rmed / m) if m > 0 else NAN, v, out))

    # 2. work per simulated hour
    print()
    print("Work per simulated hour (exact counts; * = varies between runs)")
    print("%-14s %9s %7s %10s %7s %7s %8s %6s %7s %-12s %-14s %-14s %6s" % (
        "label", "wake-ups", "vs 1st", "syscalls", "vs 1st", "state w", "state KB", "trkl", "B/trkl", "live/expect",
        "bin delay s", "send delay s", "RSS MB"))
    rw, _ = count(ref, ds, "wake_h")
    rs, _ = count(ref, ds, "sys_h")
    for lb in order:
        w, wv = count(lb, ds, "wake_h")
        s, sv = count(lb, ds, "sys_h")
        sw, swv = count(lb, ds, "sw_h")
        kb, kbv = count(lb, ds, "state_kb_h")
        tk, tkv = count(lb, ds, "trk_h")
        bt, _ = count(lb, ds, "b_trk")
        bins, binsv = count(lb, ds, "bins")
        live, livev = count(lb, ds, "live")
        exp, _ = count(lb, ds, "expected")
        rss, _ = count(lb, ds, "rss_mb")
        kvs = [r["kv"] for r in runs[lb] if "lat_bin_med" in r["kv"]]
        if kvs:
            bd = "%s/%s" % (fmt(med(num(k, "lat_bin_med") for k in kvs), "%.2f"), fmt(med(num(k, "lat_bin_max") for k in kvs), "%.2f"))
            sd = "%s/%s" % (fmt(med(num(k, "lat_trk_med") for k in kvs), "%.0f"), fmt(med(num(k, "lat_trk_max") for k in kvs), "%.0f"))
            bd = "-" if bd.startswith("-1.00") else bd
            sd = "-" if sd.startswith("-1/") else sd
        else:
            bd = sd = "-"
        filled = bins - live
        le = "%s/%s" % (fmt(live, "%.0f", livev), fmt(exp, "%.0f"))
        if filled == filled and filled > 0:
            le += "+%.0f" % filled
        print("%-14s %9s %7s %10s %7s %7s %8s %6s %7s %-12s %-14s %-14s %6s" % (
            lb, fmt(w, "%.0f", wv), "base" if lb == ref else pct(w, rw), fmt(s, "%.0f", sv), "base" if lb == ref else pct(s, rs),
            fmt(sw, "%.1f", swv), fmt(kb, "%.1f", kbv), fmt(tk, "%.2f", tkv), fmt(bt, "%.0f"), le, bd, sd, fmt(rss, "%.1f")))
    print()
    print("wake-ups: returns from a sleep or a poll that took time; syscalls: open, close, read, write, poll, sleep, ioctl, glob "
          "calls; state w / state KB: state-file writes and kilobytes written; trkl: trickles; live/expect: bins counted live "
          "against the bins a run of that length holds (+n: bins filled from the detector's log); bin delay: median/max seconds "
          "from the end of a live bin to the monitor having it; send delay: to its trickle being handed over.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    main(sys.argv[1])
