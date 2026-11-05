/* The host of the benchmarked monitor (see fake.h), instead of monitor/yarrow_boinc.cpp or monitor/yarrow_standalone.c. It keeps
 * the state file the way the BOINC host does (a whole file through .tmp and a rename, no fsync), writes every trickle to
 * trickles.txt and counts what the monitor does. glue_no_detector() is a temporary exit under BOINC: here the virtual clock
 * jumps the 30 minutes the task would be away (the cost of ending and starting the process is not modelled). */
#include "fake.h"
#include "yarrow_glue.h"
#include "yarrow_trickle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS 1000000000LL
#define STATE "yarrow_state.txt"
#define MAX_BINS 20000

static FILE *tf;
static uint64_t h_trickles = 0xcbf29ce484222325ULL;       /* FNV-1a, 64 bit */

static uint32_t live_start[MAX_BINS];                      /* starts of the bins that were counted live, in time order */
static double bin_lat[MAX_BINS], trk_lat[MAX_BINS];
static unsigned n_live, n_trk;

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const unsigned char *s = p;
    while (n--) {
        h ^= *s++;
        h *= 1099511628211ULL;
    }
    return h;
}

/* The monitor's ypending_add() is renamed to this one in wrap_monitor.c: it notes how long after its end a bin arrived. */
int fk_add_bin(ypending_t *p, uint32_t start, uint32_t count)
{
    double lat = (double)fk_wall_ns() / 1e9 - (double)((uint64_t)start + fk_cfg.interval);
    fk_st.bins++;
    if (lat < 120.0 && n_live < MAX_BINS) {
        live_start[n_live] = start;
        bin_lat[n_live++] = lat;
        fk_st.bins_live++;
    }
    return ypending_add(p, start, count);
}

static int is_live(uint32_t start)
{
    unsigned lo = 0, hi = n_live;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (live_start[mid] < start)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < n_live && live_start[lo] == start;
}

/* the time from the end of each live bin in this trickle to now */
static void note_trickle(const char *xml)
{
    const char *p = strstr(xml, "start=\"");
    unsigned long start = p ? strtoul(p + 7, NULL, 10) : 0;
    p = strstr(xml, "interval=\"");
    unsigned long iv = p ? strtoul(p + 10, NULL, 10) : 0;
    p = strstr(xml, "bins=\"");
    if (!p || !iv)
        return;
    p += 6;
    double now = (double)fk_wall_ns() / 1e9;
    for (unsigned slot = 0; *p && *p != '"'; slot++) {
        if (*p != '-') {
            unsigned long bs = start + slot * iv;
            if (is_live((uint32_t)bs) && n_trk < MAX_BINS)
                trk_lat[n_trk++] = now - (double)(bs + iv);
        }
        while (*p && *p != ',' && *p != '"')
            p++;
        if (*p == ',')
            p++;
    }
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void median_max(double *v, unsigned n, double *med, double *max)
{
    if (!n) {
        *med = *max = -1;
        return;
    }
    qsort(v, n, sizeof v[0], cmp_double);
    *med = v[n / 2];
    *max = v[n - 1];
}

void fk_glue_start(void)
{
    tf = fopen("trickles.txt", "wb");
}

void fk_glue_finish(void)
{
    if (tf)
        fclose(tf);
    tf = NULL;
    fk_st.hash_trickles = h_trickles;
    median_max(bin_lat, n_live, &fk_st.lat_bin_med, &fk_st.lat_bin_max);
    median_max(trk_lat, n_trk, &fk_st.lat_trk_med, &fk_st.lat_trk_max);
    uint64_t h = 0xcbf29ce484222325ULL;
    FILE *f = fopen(STATE, "rb");
    if (f) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0)
            h = fnv(h, buf, n);
        fclose(f);
    }
    fk_st.hash_state = h;
}

/* ---- yarrow_glue.h ---- */

int glue_init(int argc, char **argv, yprefs_t *p)
{
    (void)argc;
    (void)argv;
    p->interval = fk_cfg.interval;
    snprintf(p->cc, sizeof p->cc, "%s", "TR");
    snprintf(p->city, sizeof p->city, "%s", "Denizli");
    p->port[0] = 0;
    return 0;
}

size_t glue_checkpoint_read(char *buf, size_t cap)
{
    FILE *f = fopen(STATE, "rb");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

void glue_checkpoint_write(const char *text, size_t n)
{
    FILE *f = fopen(STATE ".tmp", "wb");
    if (!f)
        return;
    fwrite(text, 1, n, f);
    fclose(f);
    rename(STATE ".tmp", STATE);
    fk_st.state_writes++;
    fk_st.state_bytes += n;
}

int glue_send_trickle(const char *text)
{
    size_t n = strlen(text);
    fk_st.trickles++;
    fk_st.trickle_bytes += n;
    h_trickles = fnv(h_trickles, text, n);
    h_trickles = fnv(h_trickles, "\n", 1);
    if (tf)
        fprintf(tf, "%s\n", text);
    note_trickle(text);
    return 0;
}

const char *glue_lock_path(void) { return "yarrow_monitor.lock"; }

int glue_quit_requested(void)
{
    fk_st.n_loops++;
    return fk_ended();
}

void glue_log(const char *line)
{
    fk_st.n_log++;
    fprintf(stderr, "%s\n", line);
}

void glue_done(int status) { (void)status; }

void glue_wait_for_prefs(const char *why) { glue_log(why); }

void glue_no_detector(const char *why)
{
    (void)why;
    fk_st.temp_exits++;
    fk_advance(1800LL * NS);
}

void glue_other_monitor(const char *why) { glue_log(why); }
