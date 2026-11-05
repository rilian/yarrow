/* The virtual clock and the system calls that monitor/yarrow_serial.c and monitor/yarrow_src_rah.c make, answered in memory
 * (see fake.h). A wait moves the virtual clock and costs nothing; every call is counted. With sigalrm_hz set, every wait is
 * ended early by a timer tick, as the BOINC library's 10 Hz SIGALRM ends nanosleep() and poll() (docs/kb/shared/boinc-api.md);
 * read() and write() are restarted by the kernel and are not interrupted. */
#include "fake.h"

#include <errno.h>
#include <fnmatch.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

fk_cfg_t fk_cfg;
fk_stats_t fk_st;
fk_port_t fk_ports[FK_MAX_PORTS];
int fk_nports;
int64_t fk_start_mono, fk_end_mono;

#define NS 1000000000LL
#define BASE_WALL_S 1792368000LL    /* 2026-10-19 00:00:00 UTC: a multiple of 3600 s, after the firmware's YARROW_MIN_TIME */

static int64_t mono_ns, wall_off_ns;   /* wall = mono + wall_off */

void fk_clock_init(void)
{
    mono_ns = 1000LL * NS;             /* a monotonic clock that starts at zero would hide mistakes */
    wall_off_ns = BASE_WALL_S * NS + (int64_t)fk_cfg.start_s * NS + NS / 4 - mono_ns;
    fk_start_mono = mono_ns;
    fk_end_mono = mono_ns + (int64_t)fk_cfg.hours * 3600LL * NS;
}

int64_t fk_mono_ns(void) { return mono_ns; }
int64_t fk_wall_ns(void) { return mono_ns + wall_off_ns; }

void fk_advance(int64_t ns)
{
    if (ns > 0)
        mono_ns += ns;
}

int fk_ended(void) { return mono_ns >= fk_end_mono; }

/* Lets up to `want` ns pass and returns how many did. With a BOINC timer, the next tick inside the wait ends it early (*cut). */
static int64_t wait_ns(int64_t want, int *cut)
{
    *cut = 0;
    if (want <= 0)
        return 0;
    if (fk_cfg.sigalrm_hz) {
        int64_t period = NS / (int64_t)fk_cfg.sigalrm_hz;
        int64_t to_tick = period - mono_ns % period;
        if (to_tick < want) {
            want = to_tick;
            *cut = 1;
        }
    }
    mono_ns += want;
    return want;
}

static fk_port_t *port_of(int fd)
{
    int i = fd - FK_FD0;
    if (i < 0 || i >= fk_nports || !fk_ports[i].is_open)
        return NULL;
    return &fk_ports[i];
}

int fk_open(const char *path, int flags, ...)
{
    (void)flags;
    fk_st.n_open++;
    for (int i = 0; i < fk_nports; i++) {
        fk_port_t *p = &fk_ports[i];
        if (strcmp(path, p->name) != 0)
            continue;
        if (!fk_port_present(p)) {
            errno = ENOENT;
            return -1;
        }
        p->is_open = 1;
        p->rx_len = p->rx_pos = 0;
        p->cmd_len = 0;
        p->hb_on = 0;
        return FK_FD0 + i;
    }
    errno = ENOENT;
    return -1;
}

int fk_close(int fd)
{
    fk_st.n_close++;
    fk_port_t *p = port_of(fd);
    if (!p) {
        errno = EBADF;
        return -1;
    }
    p->is_open = 0;
    p->hb_on = 0;
    return 0;
}

ssize_t fk_read(int fd, void *buf, size_t n)
{
    fk_st.n_read++;
    fk_port_t *p = port_of(fd);
    if (!p) {
        errno = EBADF;
        return -1;
    }
    if (!fk_port_present(p)) {
        errno = EIO;
        return -1;
    }
    fk_dev_fill(p);
    size_t avail = p->rx_len - p->rx_pos;
    if (avail == 0) {
        errno = EAGAIN;                /* the real descriptor is O_NONBLOCK */
        return -1;
    }
    size_t k = n < avail ? n : avail;
    memcpy(buf, p->rx + p->rx_pos, k);
    p->rx_pos += k;
    if (p->rx_pos == p->rx_len)
        p->rx_pos = p->rx_len = 0;
    fk_st.rx_bytes += k;
    return (ssize_t)k;
}

ssize_t fk_write(int fd, const void *buf, size_t n)
{
    fk_st.n_write++;
    fk_port_t *p = port_of(fd);
    if (!p) {
        errno = EBADF;
        return -1;
    }
    if (!fk_port_present(p)) {
        errno = EIO;
        return -1;
    }
    size_t before = p->rx_len;
    fk_dev_write(p, (const uint8_t *)buf, n);
    fk_st.tx_bytes += n;
    size_t made = p->rx_len > before ? p->rx_len - before : 0;
    if (made) {                         /* the reply takes the line's round trip and its transfer time */
        int64_t xfer = fk_cfg.link_bps ? (int64_t)made * NS / (int64_t)fk_cfg.link_bps : 0;
        fk_advance((int64_t)fk_cfg.rtt_us * 1000LL + xfer);
    }
    return (ssize_t)n;
}

int fk_poll(struct pollfd *fds, nfds_t nfds, int timeout_ms)
{
    fk_st.n_poll++;
    if (nfds != 1) {
        errno = EINVAL;
        return -1;
    }
    fds[0].revents = 0;
    fk_port_t *p = port_of(fds[0].fd);
    if (!p) {
        fds[0].revents = POLLNVAL;
        return 1;
    }
    int64_t want = timeout_ms < 0 ? (int64_t)1 << 40 : (int64_t)timeout_ms * 1000000LL;
    int present = fk_port_present(p);
    if (present) {
        fk_dev_fill(p);
        if (p->rx_pos < p->rx_len) {    /* data is there: no wait */
            fds[0].revents = POLLIN;
            return 1;
        }
    }
    int64_t ev = present ? fk_dev_next_event_ns(p) : -1;
    int arrives = ev >= 0 && ev <= want;
    int cut;
    int64_t got = wait_ns(arrives ? ev : want, &cut);
    if (got > 0)
        fk_st.n_wakeups++;
    if (cut) {
        fk_st.n_eintr++;
        errno = EINTR;
        return -1;
    }
    if (arrives) {
        fk_dev_fill(p);
        if (p->rx_pos < p->rx_len) {
            fds[0].revents = POLLIN;
            return 1;
        }
    }
    return 0;
}

int fk_nanosleep(const struct timespec *req, struct timespec *rem)
{
    fk_st.n_sleep++;
    int64_t want = (int64_t)req->tv_sec * NS + (int64_t)req->tv_nsec;
    int cut;
    int64_t got = wait_ns(want, &cut);
    if (got > 0)
        fk_st.n_wakeups++;
    if (cut) {
        fk_st.n_eintr++;
        if (rem) {
            rem->tv_sec = (time_t)((want - got) / NS);
            rem->tv_nsec = (long)((want - got) % NS);
        }
        errno = EINTR;
        return -1;
    }
    return 0;
}

int fk_clock_gettime(clockid_t clk, struct timespec *ts)
{
    fk_st.n_clock++;
    int64_t v = clk == CLOCK_REALTIME ? fk_wall_ns() : mono_ns;
    ts->tv_sec = (time_t)(v / NS);
    ts->tv_nsec = (long)(v % NS);
    return 0;
}

time_t fk_time(time_t *t)
{
    fk_st.n_clock++;
    time_t s = (time_t)(fk_wall_ns() / NS);
    if (t)
        *t = s;
    return s;
}

int fk_tcgetattr(int fd, struct termios *t)
{
    (void)fd;
    fk_st.n_ioctl++;
    memset(t, 0, sizeof *t);
    return 0;
}

int fk_tcsetattr(int fd, int action, const struct termios *t)
{
    (void)fd;
    (void)action;
    (void)t;
    fk_st.n_ioctl++;
    return 0;
}

/* The serial ports that exist at the moment, matched against the patterns yarrow_serial.c looks for. A glob is a few system
 * calls on a real system; it is counted as one. */
int fk_glob(const char *pattern, int flags, int (*errfn)(const char *, int), glob_t *g)
{
    (void)flags;
    (void)errfn;
    fk_st.n_glob++;
    memset(g, 0, sizeof *g);
    char **v = malloc((FK_MAX_PORTS + 1) * sizeof *v);
    if (!v)
        return GLOB_NOSPACE;
    size_t n = 0;
    for (int i = 0; i < fk_nports; i++)
        if (fk_port_present(&fk_ports[i]) && fnmatch(pattern, fk_ports[i].name, 0) == 0)
            v[n++] = strdup(fk_ports[i].name);
    v[n] = NULL;
    if (n == 0) {
        free(v);
        return GLOB_NOMATCH;
    }
    g->gl_pathc = n;
    g->gl_pathv = v;
    return 0;
}

void fk_globfree(glob_t *g)
{
    for (size_t i = 0; i < g->gl_pathc; i++)
        free(g->gl_pathv[i]);
    free(g->gl_pathv);
    g->gl_pathv = NULL;
    g->gl_pathc = 0;
}
