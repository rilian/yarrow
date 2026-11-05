/* Yarrow monitor: reads a Yarrow detector live, cuts the counts into bins and reports them as trickles. */
#include "yarrow_bin.h"
#include "yarrow_glue.h"
#include "yarrow_lock.h"
#include "yarrow_serial.h"
#include "yarrow_src.h"
#include "yarrow_trickle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SEND_EVERY_S 600.0
#define RECAL_EVERY_S 60.0

static ypending_t pending;
static ydev_t dev;
static ybinner_t binner;

/* When the monitor last heard from the detector, and the set-up it counted under: the log fills the time in between, if the set-up is
 * still the same. */
static struct {
    char uid[17];
    unsigned sens, dead, interval;
    uint32_t t;                   /* the detector's clock at the last good reading */
} seen;

#define SEEN_MIN_T 1790812800u    /* no real clock reads earlier (the firmware's YARROW_MIN_TIME) */
#define BACKFILL_MAX_AGE_S (13 * 86400u) /* the server takes bins up to 14 days old */

static void save_state(void);

/* Bins that fill_gap() added since its last save. While a gap is filled from the detector's log, on_bin() does not save
 * the state file after every bin (a 13-day gap is about 1870 bins, each a full rewrite of the file): fill_gap() saves
 * once at the end. A crash in between loses nothing, because the next start fills the same gap again from the same log
 * (the last-seen time is not moved by a gap fill). 2026-11-04, docs/BENCHMARK.md "Results". */
static int filling_gap, gap_bins;

static void on_bin(void *ctx, uint32_t start, uint32_t count)
{
    (void)ctx;
    ypending_add(&pending, start, count);
    if (filling_gap)
        gap_bins++;
    else
        save_state();              /* a finished bin survives a crash or a closed window */
}

static void save_state(void)
{
    static char buf[YT_MAX_PENDING * 24 + 128];
    size_t n = 0;
    if (dev.uid[0])
        n = (size_t)snprintf(buf, 128, "D %s %s %u %u %u\n", dev.uid, dev.fw, dev.sens_x100, dev.dead_us, dev.interval);
    if (seen.t) {
        int k = snprintf(buf + n, 128, "S %s %u %u %u %u\n", seen.uid, seen.sens, seen.dead, seen.interval, (unsigned)seen.t);
        if (k > 0)
            n += (size_t)k;
    }
    glue_checkpoint_write(buf, n + ypending_save(&pending, buf + n, sizeof buf - n));
}

static double last_send;

static void flush(int force)
{
    if (!pending.n || dev.uid[0] == 0)
        return;
    if (!force && yser_now() - last_send < SEND_EVERY_S)
        return;
    static char xml[4096];
    uint32_t k = ypending_xml(&pending, &dev, xml, sizeof xml);
    if (!k)
        return;
    if (glue_send_trickle(xml) == 0) {
        ypending_drop(&pending, k);
        last_send = yser_now();
        save_state();
    }
}

/* The detector was away from the monitor for a while (BOINC closed, cable out, computer off) but kept counting: its log has the bins
 * that were missed. Only when it is the same detector with the same calibration and bin length, and its clock was right. */
static void fill_gap(ysrc_t *src, uint32_t interval)
{
    if (!src->backfill || !seen.t || strcmp(seen.uid, dev.uid) != 0 || seen.sens != dev.sens_x100 || seen.dead != dev.dead_us ||
        seen.interval != interval)
        return;
    uint32_t now = (uint32_t)time(NULL);
    uint32_t from = (seen.t + interval - 1) / interval * interval;
    uint32_t until = now / interval * interval;
    if (now > BACKFILL_MAX_AGE_S && from < (now - BACKFILL_MAX_AGE_S + interval - 1) / interval * interval)
        from = (now - BACKFILL_MAX_AGE_S + interval - 1) / interval * interval;
    if (from + interval > until)
        return;
    filling_gap = 1;
    gap_bins = 0;
    int n = src->backfill(src, interval, from, until, on_bin, 0);
    filling_gap = 0;
    if (gap_bins > 0)
        save_state();
    char line[120];
    if (n > 0) {
        snprintf(line, sizeof line, "the detector's log filled %d bins that were missed while this program was not running", n);
        glue_log(line);
    } else if (n < 0) {
        glue_log("the detector's log could not be used to fill the gap");
    }
}

int main(int argc, char **argv)
{
    yprefs_t pr;
    memset(&pr, 0, sizeof pr);
    if (glue_init(argc, argv, &pr) != 0) {
        glue_done(1);
        return 1;
    }
    if (pr.interval != 600 && pr.interval != 1800 && pr.interval != 3600)
        pr.interval = 600;
    if (!ydev_valid_place(pr.cc, pr.city)) {
        glue_wait_for_prefs("Set your country (two capital letters) and your city in the project preferences on the web site");
        return 1;
    }
    if (!ylock_acquire(glue_lock_path())) {
        glue_log("another Yarrow monitor is already running on this computer");
        glue_other_monitor("Another Yarrow monitor is already running on this computer; one is enough.");
        glue_done(1);
        return 1;
    }
    static char saved[YT_MAX_PENDING * 24];
    saved[glue_checkpoint_read(saved, sizeof saved - 1)] = 0;
    ypending_load(&pending, saved);
    for (const char *ln = saved; ln && *ln; ln = strchr(ln, 10) ? strchr(ln, 10) + 1 : NULL) {
        unsigned t = 0;
        if (ln[0] == 'S' && sscanf(ln, "S %16s %u %u %u %u", seen.uid, &seen.sens, &seen.dead, &seen.interval, &t) == 5)
            seen.t = t;
        else if (ln[0] == 'S')
            memset(&seen, 0, sizeof seen);
    }
    if (saved[0] == 'D') {
        char uid[17] = "", fw[17] = "";
        unsigned sens = 0, dead = 0, iv = 0;
        if (sscanf(saved, "D %16s %16s %u %u %u", uid, fw, &sens, &dead, &iv) == 5 && strlen(uid) == 16) {
            snprintf(dev.uid, sizeof dev.uid, "%s", uid);
            snprintf(dev.fw, sizeof dev.fw, "%s", fw);
            snprintf(dev.cc, sizeof dev.cc, "%s", pr.cc);
            snprintf(dev.city, sizeof dev.city, "%s", pr.city);
            dev.sens_x100 = sens;
            dev.dead_us = dead;
            dev.interval = iv;
            flush(1);                                    /* bins left over from the last run */
        }
    }
    if (pending.n && !dev.uid[0])
        ypending_drop(&pending, pending.n);
    ybin_init(&binner, pr.interval, on_bin, 0);

    ysrc_t *src = NULL;
    glue_log("looking for a detector (plugged in, switched on, on the main screen)");
    double next_find = 0, next_cal = 0, searching_since = yser_now();
    ysample_t s;
    memset(&s, 0, sizeof s);
    while (!glue_quit_requested()) {
        if (!src) {
            if (yser_now() < next_find) {
                yser_sleep_ms(500);
                continue;
            }
            next_find = yser_now() + 15;
            ydev_t nd;
            memset(&nd, 0, sizeof nd);
            src = ysrc_find(pr.port, &nd);
            if (!src) {
                if (yser_now() - searching_since > 300) {      /* nothing plugged in: do not hold a CPU for it */
                    flush(1);
                    save_state();
                    glue_no_detector("No detector found. Plug it in and switch it on (a Yarrow detector on its main screen). On Linux and Raspberry Pi the BOINC client also needs permission to open it: see https://yarrow.athena.org.tr/linux");
                    searching_since = yser_now();
                }
                continue;
            }
            snprintf(nd.cc, sizeof nd.cc, "%s", pr.cc);
            snprintf(nd.city, sizeof nd.city, "%s", pr.city);
            nd.interval = pr.interval;
            if (dev.uid[0] && (strcmp(dev.uid, nd.uid) != 0 || dev.sens_x100 != nd.sens_x100 || dev.dead_us != nd.dead_us ||
                               dev.interval != nd.interval)) {
                flush(1);                                /* what is pending was counted by another set-up */
                ypending_drop(&pending, pending.n);
            }
            dev = nd;
            ybin_break(&binner);
            fill_gap(src, pr.interval);
            next_cal = yser_now() + RECAL_EVERY_S;
            last_send = yser_now();
            char line[96];
            snprintf(line, sizeof line, "found: %s", src->name);
            glue_log(line);
            searching_since = yser_now();
        }
        if (src->poll(src, pr.interval, &s) != 0) {
            src->close(src);
            src = NULL;
            ybin_break(&binner);
            glue_log("detector lost");
            searching_since = yser_now();
            continue;
        }
        ybin_feed(&binner, &s);
        if (s.t >= SEEN_MIN_T) {
            snprintf(seen.uid, sizeof seen.uid, "%s", dev.uid);
            seen.sens = dev.sens_x100;
            seen.dead = dev.dead_us;
            seen.interval = pr.interval;
            seen.t = s.t;
        }
        if (yser_now() >= next_cal) {
            next_cal = yser_now() + RECAL_EVERY_S;
            save_state();                                  /* keeps the last-seen time fresh for the next start */
            if (src->housekeeping) {
                yhk_t hk;
                src->housekeeping(src, &dev, &s, &hk);
                if (hk.clock_set)
                    ybin_break(&binner);                   /* the bin that spans the change is not exact */
                if (hk.cal_changed) {
                    flush(1);                              /* bins sent so far keep the calibration they were counted with */
                    dev.sens_x100 = hk.sens_x100;
                    dev.dead_us = hk.dead_us;
                    ybin_break(&binner);
                }
            }
        }
        flush(0);
        yser_sleep_ms(src->next_wait_ms(src, ybin_to_edge(&binner, s.t), pr.interval));
    }
    flush(1);
    save_state();
    if (src)
        src->close(src);
    glue_done(0);
    return 0;
}
