/* The counters on the other end of the fake serial port (see fake.h): a Yarrow detector, a GQ GMC counter, a Radioactive@Home
 * counter, or another device that never answers. All three counters count the same seeded Poisson process, so the same
 * profile gives the same bins whichever counter is attached. A Yarrow detector also keeps a log of one-minute samples, written
 * by the real src/ylog.c, which the monitor reads back to fill a gap (the gap-fill profile). */
#include "fake.h"
#include "ylog.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UID "9f3c0a1b22d4e5f6"
#define FW "0.2.1"
#define SENS_X100 18000u            /* 180 counts per minute for 1 uSv/h, as a small tube */
#define DEAD_US 90u
#define PULSES0 4000u               /* pulses counted before the run starts */
#define LOG_PAGES 31u               /* the GC-03's log window: (LOG_END - LOG_BASE) / 4 KB, hal/gc03/image.h */
#define LOG_PSIZE 4096u
#define LOG_IV 60u
#define NS 1000000000LL

static uint32_t t_start;            /* device second at the start of the run */
static uint32_t absent_s;
static uint32_t t_on;               /* device second the counter was switched on */
static uint32_t cur_sec, cur_cum;   /* pulses since switch-on, up to and including second cur_sec */
static int64_t dev_off_ns;          /* the Yarrow detector's clock minus the wall clock (TIME sets it) */

/* ---- the counts: a seeded Poisson process, a hash of the second, integer thresholds ---- */

static uint32_t mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

#define NTHR 64
static uint32_t thr[NTHR];          /* P(count <= k) as a fraction of 2^32 */

static void make_thresholds(void)
{
    double lam = fk_cfg.cpm / 60.0, p = exp(-lam), c = p;
    for (int k = 0; k < NTHR; k++) {
        thr[k] = c >= 1.0 ? 0xFFFFFFFFu : (uint32_t)(c * 4294967296.0);
        p *= lam / (k + 1);
        c += p;
    }
    thr[NTHR - 1] = 0xFFFFFFFFu;
}

static uint32_t count_at(uint32_t sec)          /* pulses in second `sec` */
{
    uint32_t u = mix(fk_cfg.seed ^ mix(sec + 0x9e3779b9U));
    int k = 0;
    while (k < NTHR - 1 && u >= thr[k])
        k++;
    return (uint32_t)k;
}

static uint32_t pulses_at(uint32_t sec)         /* cumulative since switch-on; asked for in time order */
{
    if (sec < cur_sec) {
        cur_sec = t_on;
        cur_cum = PULSES0;
    }
    while (cur_sec < sec) {
        cur_sec++;
        cur_cum += count_at(cur_sec);
    }
    return cur_cum;
}

static uint32_t range_sum(uint32_t a, uint32_t b)   /* pulses in seconds a+1 .. b */
{
    uint32_t s = 0;
    for (uint32_t k = a + 1; k <= b; k++)
        s += count_at(k);
    return s;
}

static uint32_t dev_sec(void) { return (uint32_t)((fk_wall_ns() + dev_off_ns) / NS); }

/* ---- the port's buffer ---- */

static void add(fk_port_t *p, const void *d, size_t n)
{
    if (p->rx_len + n > FK_RX_MAX)
        return;                         /* nobody reads this port: the data is lost */
    memcpy(p->rx + p->rx_len, d, n);
    p->rx_len += n;
}

static void add_str(fk_port_t *p, const char *s) { add(p, s, strlen(s)); }

/* the firmware's send_base64() (src/proto.c): lines of 76 characters, the rest of the chunk on a short line */
static void add_base64(fk_port_t *p, const uint8_t *d, uint32_t n)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char line[80];
    int k = 0;
    for (uint32_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        line[k++] = tbl[(v >> 18) & 63];
        line[k++] = tbl[(v >> 12) & 63];
        line[k++] = i + 1 < n ? tbl[(v >> 6) & 63] : '=';
        line[k++] = i + 2 < n ? tbl[v & 63] : '=';
        if (k >= 76) {
            line[k++] = '\n';
            add(p, line, (size_t)k);
            k = 0;
        }
    }
    if (k) {
        line[k++] = '\n';
        add(p, line, (size_t)k);
    }
}

/* ---- the Yarrow detector's log ---- */

static uint8_t flash[LOG_PAGES * LOG_PSIZE];

static int fl_read(void *c, uint32_t a, void *b, uint32_t n)
{
    (void)c;
    memcpy(b, flash + a, n);
    return 0;
}

static int fl_prog(void *c, uint32_t a, const void *b, uint32_t n)
{
    (void)c;
    const uint8_t *s = b;
    for (uint32_t i = 0; i < n; i++)
        flash[a + i] &= s[i];           /* flash only clears bits */
    return 0;
}

static int fl_erase(void *c, uint32_t pg)
{
    (void)c;
    memset(flash + (size_t)pg * LOG_PSIZE, 0xFF, LOG_PSIZE);
    return 0;
}

static const ylog_flash_t fl = {LOG_PSIZE, LOG_PAGES, fl_read, fl_prog, fl_erase, 0};

/* A log page that sends as base64 text containing "END" ends the monitor's decoding of the page early: ybf_page_from_reply()
 * (monitor/yarrow_backfill.c) stops at any "END" in the text, not only at the terminator line, and the gap fill then gives up
 * (about 2 % of pages of compressed data have one, by the arithmetic of 64^3 values per position; found by reading, not seen on
 * a detector). The fake log is rebuilt, with the first sample changed by `salt`, until no page has it, so that the gap-fill
 * profile measures a gap fill. */
static int page_has_end(unsigned pg)
{
    static fk_port_t scratch;
    scratch.rx_len = 0;
    for (uint32_t off = 0; off < LOG_PSIZE; off += 192) {
        uint32_t len = LOG_PSIZE - off < 192 ? LOG_PSIZE - off : 192;
        add_base64(&scratch, flash + (size_t)pg * LOG_PSIZE + off, len);
    }
    scratch.rx[scratch.rx_len < FK_RX_MAX ? scratch.rx_len : FK_RX_MAX - 1] = 0;
    return strstr((const char *)scratch.rx, "END") != NULL;
}

/* The log as the detector would hold it now: one sample a minute from before the monitor's last run until the last whole minute. */
static void build_log(void)
{
    static ylog_t lw;
    uint32_t now = dev_sec();
    uint32_t first = (t_start - absent_s - 3600u) / 600u * 600u;
    for (uint32_t salt = 0; salt < 64; salt++) {
        memset(flash, 0xFF, sizeof flash);
        if (ylog_open(&lw, &fl) != 0)
            return;
        for (uint32_t t = first; t + LOG_IV <= now; t += LOG_IV)
            ylog_add(&lw, t, LOG_IV, true, range_sum(t, t + LOG_IV) + (t == first ? salt : 0));
        ylog_close(&lw);
        int bad = 0;
        for (unsigned pg = 0; pg < LOG_PAGES && !bad; pg++)
            bad = page_has_end(pg);
        if (!bad)
            return;
    }
}

static void loginfo(char *b, size_t cap)
{
    static uint8_t scratch[LOG_PSIZE];
    ylog_stats_t st;
    build_log();
    ylog_stats(&fl, scratch, &st);
    snprintf(b, cap, "OK pages=%u pagesize=%u used=%u oldest=%u newest=%u\n", LOG_PAGES, LOG_PSIZE, (unsigned)st.pages_used,
             (unsigned)st.oldest_time, (unsigned)st.newest_time);
}

static void logpage(fk_port_t *p, unsigned n)
{
    char head[48];
    if (n >= LOG_PAGES) {
        add_str(p, "ERR page\n");
        return;
    }
    snprintf(head, sizeof head, "OK %u %u\n", n, LOG_PSIZE);
    add_str(p, head);
    for (uint32_t off = 0; off < LOG_PSIZE; off += 192) {
        uint32_t len = LOG_PSIZE - off < 192 ? LOG_PSIZE - off : 192;
        add_base64(p, flash + (size_t)n * LOG_PSIZE + off, len);
    }
    add_str(p, "END\n");
}

/* ---- the three kinds of counter ---- */

static void yarrow_cmd(fk_port_t *p, const char *c)
{
    char b[160];
    uint32_t ds = dev_sec();
    if (strcmp(c, "COUNTS") == 0) {
        uint32_t pulses = pulses_at(ds);
        snprintf(b, sizeof b, "OK %u %u %u\n", (unsigned)pulses, (unsigned)(ds - t_on), (unsigned)ds);
    } else if (strcmp(c, "INFO") == 0) {
        snprintf(b, sizeof b, "OK yarrow=%s stage=2 verified=1 time=%u theme=0 profile=2 pages=%u pagesize=%u\n", FW, (unsigned)ds,
                 LOG_PAGES, LOG_PSIZE);
    } else if (strcmp(c, "UID") == 0) {
        snprintf(b, sizeof b, "OK %s\n", UID);
    } else if (strcmp(c, "CAL") == 0) {
        snprintf(b, sizeof b, "OK sens=%u dead=%u e=20 h=0\n", SENS_X100, DEAD_US);
    } else if (strncmp(c, "LIVE", 4) == 0) {
        snprintf(b, sizeof b, "OK\n");
    } else if (strncmp(c, "TIME ", 5) == 0) {
        dev_off_ns = (int64_t)strtoul(c + 5, NULL, 10) * NS - fk_wall_ns();
        snprintf(b, sizeof b, "OK\n");
    } else if (strcmp(c, "LOGINFO") == 0) {
        loginfo(b, sizeof b);
    } else if (strncmp(c, "LOGPAGE ", 8) == 0) {
        logpage(p, (unsigned)strtoul(c + 8, NULL, 10));
        return;
    } else {
        snprintf(b, sizeof b, "ERR unknown\n");
    }
    add_str(p, b);
}

/* GQ-RFC1201 as yarrow_src_gmc.c uses it; the configuration page holds one calibration point: 153 counts per minute at 1.0 uSv/h */
static void gmc_cmd(fk_port_t *p, const char *c)
{
    if (strcmp(c, "<GETVER>>") == 0) {
        add(p, "GMC-300Re 4.20", 14);
    } else if (strcmp(c, "<GETSERIAL>>") == 0) {
        static const uint8_t serial[7] = {0x00, 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f};
        add(p, serial, 7);
    } else if (strcmp(c, "<GETCFG>>") == 0) {
        uint8_t cfg[256];
        memset(cfg, 0, sizeof cfg);
        cfg[0x08] = 0x00;
        cfg[0x09] = 153;
        cfg[0x0a] = 0x3f;               /* 1.0f, big endian */
        cfg[0x0b] = 0x80;
        add(p, cfg, sizeof cfg);
    } else if (strcmp(c, "<HEARTBEAT1>>") == 0) {
        p->hb_on = 1;
        p->hb_next_ns = (fk_wall_ns() / NS + 1) * NS + (int64_t)fk_cfg.hb_phase_ms * 1000000LL;
    } else if (strcmp(c, "<HEARTBEAT0>>") == 0) {
        p->hb_on = 0;
    }
}

void fk_dev_write(fk_port_t *p, const uint8_t *b, size_t n)
{
    if (p->kind == FK_NONE)
        return;
    for (size_t i = 0; i < n; i++) {
        char c = (char)b[i];
        if (p->kind == FK_YARROW) {
            if (c == '\r')
                continue;
            if (c == '\n') {
                p->cmd[p->cmd_len] = 0;
                if (p->cmd_len)
                    yarrow_cmd(p, p->cmd);
                p->cmd_len = 0;
                continue;
            }
            if (p->cmd_len + 1 < sizeof p->cmd)
                p->cmd[p->cmd_len++] = c;
        } else if (p->kind == FK_GMC) {
            if (p->cmd_len + 1 < sizeof p->cmd)
                p->cmd[p->cmd_len++] = c;
            else
                p->cmd_len = 0;
            if (c == '>' && p->cmd_len >= 2 && p->cmd[p->cmd_len - 2] == '>') {
                p->cmd[p->cmd_len] = 0;
                gmc_cmd(p, p->cmd);
                p->cmd_len = 0;
            }
        }
    }
}

/* the heartbeats a GQ GMC counter has sent by now: two bytes a second, the pulses of that second, high byte first */
void fk_dev_fill(fk_port_t *p)
{
    if (p->kind != FK_GMC || !p->hb_on)
        return;
    int64_t now = fk_wall_ns();
    while (p->hb_next_ns <= now && p->rx_len + 2 <= FK_RX_MAX) {
        uint32_t v = count_at((uint32_t)(p->hb_next_ns / NS));
        uint8_t w[2] = {(uint8_t)((v >> 8) & 0x3f), (uint8_t)(v & 0xff)};
        add(p, w, 2);
        p->hb_next_ns += NS;
    }
}

int64_t fk_dev_next_event_ns(const fk_port_t *p)
{
    if (p->kind != FK_GMC || !p->hb_on)
        return -1;
    int64_t d = p->hb_next_ns - fk_wall_ns();
    return d > 0 ? d : 0;
}

static int unplugged(void)
{
    if (!fk_cfg.unplug_for_s)
        return 0;
    int64_t el = (fk_mono_ns() - fk_start_mono) / NS;
    return el >= (int64_t)fk_cfg.unplug_at_s && el < (int64_t)fk_cfg.unplug_at_s + (int64_t)fk_cfg.unplug_for_s;
}

int fk_port_present(const fk_port_t *p) { return p->kind == FK_NONE || !unplugged(); }
int fk_rah_present(void) { return !unplugged(); }

/* The Radioactive@Home counter: a HID feature report, [0] id, [1..4] timer in ms, [5..8] pulses, little endian (yarrow_src_rah.c) */
int fk_rah_get(void *ctx, uint8_t *buf, size_t len)
{
    (void)ctx;
    if (len < 9 || !fk_rah_present())
        return -1;
    fk_st.n_ioctl++;
    int64_t now = fk_wall_ns();
    uint32_t ms = (uint32_t)((now - (int64_t)t_on * NS) / 1000000LL);
    uint32_t pl = pulses_at((uint32_t)(now / NS));
    memset(buf, 0, len);
    for (int i = 0; i < 4; i++) {
        buf[1 + i] = (uint8_t)(ms >> (8 * i));
        buf[5 + i] = (uint8_t)(pl >> (8 * i));
    }
    return (int)len;
}

/* ---- set-up ---- */

static void add_port(const char *name, fk_kind_t kind)
{
    if (fk_nports >= FK_MAX_PORTS)
        return;
    fk_port_t *p = &fk_ports[fk_nports++];
    memset(p, 0, sizeof *p);
    snprintf(p->name, sizeof p->name, "%s", name);
    p->kind = kind;
}

/* the state file of a monitor that was stopped absent_hours ago: the identity and the time it last heard from the detector */
static void write_state(void)
{
    FILE *f = fopen("yarrow_state.txt", "wb");
    if (!f)
        return;
    fprintf(f, "D %s %s %u %u %u\n", UID, FW, SENS_X100, DEAD_US, fk_cfg.interval);
    fprintf(f, "S %s %u %u %u %u\n", UID, SENS_X100, DEAD_US, fk_cfg.interval, (unsigned)(t_start - absent_s));
    fclose(f);
}

void fk_dev_setup(void)
{
    t_start = (uint32_t)(fk_wall_ns() / NS);
    absent_s = fk_cfg.absent_hours * 3600u;
    t_on = t_start - absent_s - 7200u;
    cur_sec = t_on;
    cur_cum = PULSES0;
    dev_off_ns = 0;
    make_thresholds();
    (void)pulses_at(t_start);           /* from switch-on to the start of the run: not part of what is measured */
    fk_nports = 0;
    if (fk_cfg.kind == FK_YARROW)
        add_port("/dev/ttyACM0", FK_YARROW);
    else if (fk_cfg.kind == FK_GMC)
        add_port("/dev/ttyUSB0", FK_GMC);
    for (unsigned i = 0; i < fk_cfg.foreign_ports && i < 6; i++) {
        char name[32];
        snprintf(name, sizeof name, "/dev/ttyUSB%u", i + (fk_cfg.kind == FK_GMC ? 1u : 0u));
        add_port(name, FK_NONE);
    }
    if (fk_cfg.kind == FK_YARROW && fk_cfg.absent_hours)
        write_state();
}
