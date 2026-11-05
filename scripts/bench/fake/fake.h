/* Fake detector, fake serial layer and fake host for the monitor benchmark (scripts/bench, docs/BENCHMARK.md).
 *
 * The real monitor code (monitor/yarrow_monitor.c, the three sources, the bin cutter, the trickle writer, the gap fill, the
 * serial layer monitor/yarrow_serial.c) is built into one program, monbench, with three things replaced:
 *   - the system calls of yarrow_serial.c and yarrow_src_rah.c (open, read, write, poll, nanosleep, clock_gettime, time, glob, ...)
 *     go to fake_sys.c, which answers them in memory on a virtual clock: a sleep moves the clock and costs nothing, so a
 *     simulated week takes seconds, and every call is counted;
 *   - the counter on the other end (fake_dev.c): a Yarrow detector, a GQ GMC counter or a Radioactive@Home counter, with
 *     seeded Poisson counts, or nothing at all;
 *   - the host (fake_glue.c): the monitor's glue_* functions, which keep the state file and record every trickle.
 * Nothing here is part of the BOINC app. A change to the monitor's file names or to the glue interface needs a change here. */
#ifndef FAKE_H
#define FAKE_H

#include <glob.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>

typedef enum { FK_NONE = 0, FK_YARROW, FK_GMC, FK_RAH } fk_kind_t;

/* One simulated run, from a profile file (a .prof file in scripts/bench/profiles) */
typedef struct {
    fk_kind_t kind;          /* the counter that is attached; FK_NONE: nothing */
    unsigned interval;       /* the bin length of the project preferences: 600, 1800 or 3600 s */
    unsigned hours;          /* simulated hours of monitoring */
    unsigned cpm;            /* mean counts per minute: 20 is background for a small tube */
    unsigned seed;           /* seed of the counts */
    unsigned start_s;        /* the monitor starts this many seconds after a multiple of 3600 s */
    unsigned sigalrm_hz;     /* 0, or the rate of the BOINC library's timer signal (10): it ends every sleep and poll early */
    unsigned absent_hours;   /* the monitor was not running for this long before the start; a Yarrow detector's log holds it */
    unsigned unplug_at_s;    /* the counter is unplugged from this second of the run ... */
    unsigned unplug_for_s;   /* ... for this many seconds (0: never) */
    unsigned foreign_ports;  /* serial ports of other devices that never answer (at most 6) */
    unsigned rtt_us;         /* serial command to reply, microseconds */
    unsigned link_bps;       /* bytes a second of a long reply (a log page) */
    unsigned hb_phase_ms;    /* GQ GMC: where in the second the heartbeat arrives */
} fk_cfg_t;

/* What happened, counted by the fake layers; printed by fake_main.c */
typedef struct {
    uint64_t n_open, n_close, n_read, n_write, n_poll, n_sleep, n_ioctl, n_glob;   /* calls of the system functions */
    uint64_t n_eintr;        /* sleeps and polls that the simulated BOINC timer ended early */
    uint64_t n_wakeups;      /* returns from a wait that took time (a sleep, a poll that did not find data at once) */
    uint64_t n_clock;        /* clock_gettime and time calls (no system call on Linux and macOS) */
    uint64_t n_loops;        /* turns of the monitor's main loop */
    uint64_t n_log;          /* lines the monitor wrote to its log (stderr.txt under BOINC) */
    uint64_t tx_bytes, rx_bytes;
    uint64_t state_writes, state_bytes;
    uint64_t trickles, trickle_bytes;
    uint64_t bins, bins_live;        /* bins handed to the pending list; those that were counted live (the rest came from the log) */
    uint64_t temp_exits;
    double lat_bin_med, lat_bin_max;   /* seconds from the end of a live bin to the monitor having it */
    double lat_trk_med, lat_trk_max;   /* seconds from the end of a live bin to its trickle being handed over */
    uint64_t hash_trickles, hash_state;
} fk_stats_t;

#define FK_RX_MAX 16384
#define FK_MAX_PORTS 8
#define FK_FD0 1000                /* file descriptors of the fake ports: FK_FD0 + index */

typedef struct {
    char name[32];
    fk_kind_t kind;            /* what answers on it: FK_YARROW, FK_GMC, or FK_NONE (another device that never answers) */
    int is_open;
    uint8_t rx[FK_RX_MAX];     /* bytes the device has sent and the host has not read */
    size_t rx_len, rx_pos;
    char cmd[160];             /* the command being received */
    size_t cmd_len;
    int hb_on;                 /* GQ GMC: heartbeats are streaming */
    int64_t hb_next_ns;        /* wall time of the next heartbeat */
} fk_port_t;

extern fk_cfg_t fk_cfg;
extern fk_stats_t fk_st;
extern fk_port_t fk_ports[FK_MAX_PORTS];
extern int fk_nports;
extern int64_t fk_start_mono, fk_end_mono;

/* fake_sys.c: virtual clock */
void fk_clock_init(void);
int64_t fk_mono_ns(void);
int64_t fk_wall_ns(void);
void fk_advance(int64_t ns);
int fk_ended(void);

/* fake_sys.c: the system calls, same signatures as the real ones */
int fk_open(const char *path, int flags, ...);
int fk_close(int fd);
ssize_t fk_read(int fd, void *buf, size_t n);
ssize_t fk_write(int fd, const void *buf, size_t n);
int fk_poll(struct pollfd *fds, nfds_t nfds, int timeout_ms);
int fk_nanosleep(const struct timespec *req, struct timespec *rem);
int fk_clock_gettime(clockid_t clk, struct timespec *ts);
time_t fk_time(time_t *t);
int fk_tcgetattr(int fd, struct termios *t);
int fk_tcsetattr(int fd, int action, const struct termios *t);
int fk_glob(const char *pattern, int flags, int (*errfn)(const char *, int), glob_t *g);
void fk_globfree(glob_t *g);

/* fake_dev.c: the counters */
void fk_dev_setup(void);
int fk_port_present(const fk_port_t *p);
int fk_rah_present(void);
void fk_dev_write(fk_port_t *p, const uint8_t *b, size_t n);
void fk_dev_fill(fk_port_t *p);
int64_t fk_dev_next_event_ns(const fk_port_t *p);
int fk_rah_get(void *ctx, uint8_t *buf, size_t len);

/* fake_glue.c: the host */
void fk_glue_start(void);
void fk_glue_finish(void);

/* wrap_monitor.c: the real main() of the monitor, renamed */
int fk_monitor_main(int argc, char **argv);

#endif
