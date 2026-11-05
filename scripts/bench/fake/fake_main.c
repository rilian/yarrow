/* monbench: runs the real monitor against the fake counter and the fake serial layer for the simulated hours of a profile, then
 * prints one line, "FK key=value ...", which scripts/bench/run_bench.sh collects.
 * usage: monbench <profile file> [key value ...]     (extra pairs override the profile; the keys are the ones set() below
 *        accepts and docs/BENCHMARK.md describes: source interval hours cpm seed start_s sigalrm absent_hours unplug_at
 *        unplug_for foreign_ports rtt_us link_bps hb_phase_ms)
 * It runs in the current directory (the state file, the lock file and trickles.txt are written there). */
#include "fake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

static void defaults(void)
{
    memset(&fk_cfg, 0, sizeof fk_cfg);
    fk_cfg.kind = FK_YARROW;
    fk_cfg.interval = 600;
    fk_cfg.hours = 72;
    fk_cfg.cpm = 20;
    fk_cfg.seed = 1;
    fk_cfg.start_s = 137;
    fk_cfg.rtt_us = 1000;
    fk_cfg.link_bps = 100000;
    fk_cfg.hb_phase_ms = 370;
}

static int set(const char *k, const char *v)
{
    unsigned long n = strtoul(v, NULL, 10);
    if (strcmp(k, "source") == 0) {
        if (strcmp(v, "yarrow") == 0) fk_cfg.kind = FK_YARROW;
        else if (strcmp(v, "gmc") == 0) fk_cfg.kind = FK_GMC;
        else if (strcmp(v, "rah") == 0) fk_cfg.kind = FK_RAH;
        else if (strcmp(v, "none") == 0) fk_cfg.kind = FK_NONE;
        else {
            fprintf(stderr, "monbench: source must be yarrow, gmc, rah or none, not %s\n", v);
            return -1;
        }
    }
    else if (strcmp(k, "interval") == 0) fk_cfg.interval = (unsigned)n;
    else if (strcmp(k, "hours") == 0) fk_cfg.hours = (unsigned)n;
    else if (strcmp(k, "cpm") == 0) fk_cfg.cpm = (unsigned)n;
    else if (strcmp(k, "seed") == 0) fk_cfg.seed = (unsigned)n;
    else if (strcmp(k, "start_s") == 0) fk_cfg.start_s = (unsigned)n;
    else if (strcmp(k, "sigalrm") == 0) fk_cfg.sigalrm_hz = (unsigned)n;
    else if (strcmp(k, "absent_hours") == 0) fk_cfg.absent_hours = (unsigned)n;
    else if (strcmp(k, "unplug_at") == 0) fk_cfg.unplug_at_s = (unsigned)n;
    else if (strcmp(k, "unplug_for") == 0) fk_cfg.unplug_for_s = (unsigned)n;
    else if (strcmp(k, "foreign_ports") == 0) fk_cfg.foreign_ports = (unsigned)n;
    else if (strcmp(k, "rtt_us") == 0) fk_cfg.rtt_us = (unsigned)n;
    else if (strcmp(k, "link_bps") == 0) fk_cfg.link_bps = (unsigned)n;
    else if (strcmp(k, "hb_phase_ms") == 0) fk_cfg.hb_phase_ms = (unsigned)n;
    else {
        fprintf(stderr, "monbench: unknown key %s\n", k);
        return -1;
    }
    return 0;
}

static int load_profile(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "monbench: cannot read %s\n", path);
        return -1;
    }
    char line[256], k[64], v[64];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, "%63s %63s", k, v) != 2)
            continue;
        if (set(k, v) != 0) {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

static int valid(void)
{
    if (fk_cfg.interval != 600 && fk_cfg.interval != 1800 && fk_cfg.interval != 3600) {
        fprintf(stderr, "monbench: interval must be 600, 1800 or 3600\n");
        return 0;
    }
    if (fk_cfg.hours < 1 || fk_cfg.cpm < 1 || fk_cfg.cpm > 900 || fk_cfg.sigalrm_hz > 1000) {
        fprintf(stderr, "monbench: hours >= 1, 1 <= cpm <= 900, sigalrm <= 1000\n");
        return 0;
    }
    return 1;
}

static double tv(struct timeval t) { return (double)t.tv_sec + (double)t.tv_usec / 1e6; }

static const char *kind_name(void)
{
    switch (fk_cfg.kind) {
    case FK_YARROW: return "yarrow";
    case FK_GMC: return "gmc";
    case FK_RAH: return "rah";
    default: return "none";
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: monbench <profile> [key value ...]\n");
        return 2;
    }
    defaults();
    if (load_profile(argv[1]) != 0)
        return 2;
    for (int i = 2; i + 1 < argc; i += 2)
        if (set(argv[i], argv[i + 1]) != 0)
            return 2;
    if (!valid())
        return 2;

    fk_clock_init();
    fk_dev_setup();
    fk_glue_start();

    struct rusage r0, r1;
    struct timespec w0, w1;
    getrusage(RUSAGE_SELF, &r0);
    clock_gettime(CLOCK_MONOTONIC, &w0);
    char *av[] = {"monbench", NULL};
    int rc = fk_monitor_main(1, av);
    clock_gettime(CLOCK_MONOTONIC, &w1);
    getrusage(RUSAGE_SELF, &r1);
    fk_glue_finish();

#ifdef __APPLE__
    const long rss_div = 1024;          /* ru_maxrss is in bytes */
#else
    const long rss_div = 1;             /* and in KiB elsewhere */
#endif
    double sim_s = (double)(fk_mono_ns() - fk_start_mono) / 1e9;
    double wall = (double)(w1.tv_sec - w0.tv_sec) + (double)(w1.tv_nsec - w0.tv_nsec) / 1e9;
    printf("FK source=%s interval=%u sim_s=%.0f cpu_user=%.6f cpu_sys=%.6f wall=%.3f rss_kb=%ld rss0_kb=%ld", kind_name(),
           fk_cfg.interval, sim_s, tv(r1.ru_utime) - tv(r0.ru_utime), tv(r1.ru_stime) - tv(r0.ru_stime), wall,
           r1.ru_maxrss / rss_div, r0.ru_maxrss / rss_div);
    printf(" loops=%llu open=%llu close=%llu read=%llu write=%llu poll=%llu sleep=%llu ioctl=%llu glob=%llu",
           (unsigned long long)fk_st.n_loops, (unsigned long long)fk_st.n_open, (unsigned long long)fk_st.n_close,
           (unsigned long long)fk_st.n_read, (unsigned long long)fk_st.n_write, (unsigned long long)fk_st.n_poll,
           (unsigned long long)fk_st.n_sleep, (unsigned long long)fk_st.n_ioctl, (unsigned long long)fk_st.n_glob);
    printf(" eintr=%llu wakeups=%llu clock=%llu tx=%llu rx=%llu logs=%llu", (unsigned long long)fk_st.n_eintr,
           (unsigned long long)fk_st.n_wakeups, (unsigned long long)fk_st.n_clock, (unsigned long long)fk_st.tx_bytes,
           (unsigned long long)fk_st.rx_bytes, (unsigned long long)fk_st.n_log);
    printf(" state_writes=%llu state_bytes=%llu trickles=%llu trickle_bytes=%llu bins=%llu bins_live=%llu temp_exits=%llu",
           (unsigned long long)fk_st.state_writes, (unsigned long long)fk_st.state_bytes, (unsigned long long)fk_st.trickles,
           (unsigned long long)fk_st.trickle_bytes, (unsigned long long)fk_st.bins, (unsigned long long)fk_st.bins_live,
           (unsigned long long)fk_st.temp_exits);
    printf(" lat_bin_med=%.3f lat_bin_max=%.3f lat_trk_med=%.1f lat_trk_max=%.1f", fk_st.lat_bin_med, fk_st.lat_bin_max,
           fk_st.lat_trk_med, fk_st.lat_trk_max);
    printf(" hash_t=%016llx hash_s=%016llx\n", (unsigned long long)fk_st.hash_trickles, (unsigned long long)fk_st.hash_state);
    return rc;
}
