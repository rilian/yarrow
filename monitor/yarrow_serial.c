#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE 1
#endif
#include "yarrow_serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

struct yser { HANDLE h; };

yser_t *yser_open(const char *path)
{
    char full[64];
    snprintf(full, sizeof full, "\\\\.\\%s", path);
    HANDLE h = CreateFileA(full, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    DCB dcb;
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    GetCommState(h, &dcb);
    dcb.BaudRate = 115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;      /* the device streams only to a host that has the port open */
    SetCommState(h, &dcb);
    /* a read waits at most 20 ms for a byte and returns at once when one is there. The earlier setting (20 ms between two bytes
     * and nothing else) never times out when the port is silent: the search for a detector blocked for good on the first
     * COM port that did not answer, for instance the USB serial chip of a GQ GMC counter. */
    COMMTIMEOUTS to = {MAXDWORD, MAXDWORD, 20, 0, 0};
    SetCommTimeouts(h, &to);
    yser_t *s = malloc(sizeof *s);
    if (!s) {
        CloseHandle(h);
        return NULL;
    }
    s->h = h;
    return s;
}

void yser_close(yser_t *s)
{
    if (s) {
        CloseHandle(s->h);
        free(s);
    }
}

static int rd(yser_t *s, char *c)
{
    DWORD n = 0;
    return ReadFile(s->h, c, 1, &n, NULL) && n == 1;
}

static int wr(yser_t *s, const char *p, size_t n)
{
    DWORD w = 0;
    return WriteFile(s->h, p, (DWORD)n, &w, NULL) && w == n;
}

double yser_now(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

void yser_sleep_ms(int ms) { Sleep((DWORD)ms); }

static int port_name(int i, char *out, size_t cap)
{
    snprintf(out, cap, "COM%d", i + 1);
    return i < 64;
}
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>

/* buf holds bytes read from the port but not handed out yet: rd() reads what is there in one read() instead of one
 * byte per poll() and read() (2026-11-04, docs/BENCHMARK.md "Results"). Bytes after the end of a reply (a live-stream
 * line, the next reply) stay here for the next call, as they stayed in the driver's buffer before. */
struct yser { int fd; size_t pos, len; unsigned char buf[256]; };

yser_t *yser_open(const char *path)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return NULL;
    struct termios t;
    if (tcgetattr(fd, &t) == 0) {
        cfmakeraw(&t);
        cfsetspeed(&t, B115200);
        t.c_cflag |= CLOCAL | CREAD;
        tcsetattr(fd, TCSANOW, &t);
    }
    yser_t *s = malloc(sizeof *s);
    if (!s) {
        close(fd);
        return NULL;
    }
    s->fd = fd;
    s->pos = s->len = 0;
    return s;
}

void yser_close(yser_t *s)
{
    if (s) {
        close(s->fd);
        free(s);
    }
}

static int rd(yser_t *s, char *c)
{
    if (s->pos < s->len) {
        *c = (char)s->buf[s->pos++];
        return 1;
    }
    struct pollfd p = {s->fd, POLLIN, 0};
    if (poll(&p, 1, 20) <= 0)                                    /* a signal only shortens the 20 ms: the caller asks again */
        return 0;
    ssize_t r = read(s->fd, s->buf, sizeof s->buf);
    if (r <= 0)
        return 0;
    s->len = (size_t)r;
    s->pos = 1;
    *c = (char)s->buf[0];
    return 1;
}

static int wr(yser_t *s, const char *p, size_t n) { return write(s->fd, p, n) == (ssize_t)n; }

double yser_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

void yser_sleep_ms(int ms)
{
    /* a signal ends nanosleep early (the BOINC library fires a timer signal many times a second, so a 300 ms wait took 50 ms):
     * go on with what is left of the time */
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L}, rem;
    while (nanosleep(&ts, &rem) == -1 && errno == EINTR)
        ts = rem;
}
#endif

int yser_command(yser_t *s, const char *cmd, char *reply, size_t cap, int timeout_ms)
{
    char line[160];
    size_t n = strlen(cmd);
    if (n + 2 > sizeof line)
        return -1;
    memcpy(line, cmd, n);
    line[n] = '\n';
    if (!wr(s, line, n + 1))
        return -1;
    double end = yser_now() + timeout_ms / 1000.0;
    size_t len = 0;
    while (yser_now() < end) {
        char c;
        if (!rd(s, &c))
            continue;
        if (c == '\r')
            continue;
        if (c != '\n') {
            if (len + 1 < cap && len < sizeof line - 1)
                reply[len++] = c;
            continue;
        }
        reply[len] = 0;
        if (reply[0] == 'L' && reply[1] == ' ') {      /* a live-stream line, not the reply */
            len = 0;
            continue;
        }
        if (len == 0)
            continue;
        return strncmp(reply, "OK", 2) == 0 ? 0 : -1;
    }
    return -1;
}

int yser_lines(yser_t *s, const char *cmd, char *out, size_t cap, int timeout_ms)
{
    char line[160];
    size_t n = strlen(cmd);
    if (n + 2 > sizeof line)
        return -1;
    memcpy(line, cmd, n);
    line[n] = '\n';
    if (!wr(s, line, n + 1))
        return -1;
    size_t len = 0;
    double end = yser_now() + timeout_ms / 1000.0;
    while (yser_now() < end) {
        char c;
        if (!rd(s, &c))
            continue;
        if (c == '\r')
            continue;
        if (len + 2 >= cap)
            return -1;
        out[len++] = c;
        out[len] = 0;
        if (c == '\n' && len >= 4 && strcmp(out + len - 4, "END\n") == 0 && (len == 4 || out[len - 5] == '\n'))
            return strncmp(out, "OK", 2) == 0 ? 0 : -1;
        if (len >= 4 && out[0] == 'E' && out[1] == 'R' && out[2] == 'R' && c == '\n')
            return -1;
    }
    return -1;
}

yser_t *yser_find(char *path_out, size_t cap)
{
    char reply[160];
#ifdef _WIN32
    for (int i = 0; i < 64; i++) {
        char name[16];
        port_name(i, name, sizeof name);
        yser_t *s = yser_open(name);
        if (!s)
            continue;
        yser_command(s, "LIVE 0", reply, sizeof reply, 300);
        if (yser_command(s, "INFO", reply, sizeof reply, 600) == 0 && strstr(reply, "yarrow=")) {
            snprintf(path_out, cap, "%s", name);
            return s;
        }
        yser_close(s);
    }
#else
    /* each port open failure is reported once (the search repeats every 15 seconds): EACCES means the
     * BOINC client's user is not in the device's group (usually dialout) */
    static struct { char path[64]; int err; } seen[64];
    static int n_seen;
    static const char *pats[] = {"/dev/ttyACM*", "/dev/cu.usbmodem*", NULL};
    for (int k = 0; pats[k]; k++) {
        glob_t g;
        if (glob(pats[k], 0, NULL, &g) != 0)
            continue;
        for (size_t i = 0; i < g.gl_pathc; i++) {
            errno = 0;
            yser_t *s = yser_open(g.gl_pathv[i]);
            if (!s) {
                int err = errno ? errno : EIO;
                int known = 0;
                for (int j = 0; j < n_seen; j++)
                    if (seen[j].err == err && strcmp(seen[j].path, g.gl_pathv[i]) == 0) {
                        known = 1;
                        break;
                    }
                if (!known && n_seen < 64) {
                    snprintf(seen[n_seen].path, sizeof seen[n_seen].path, "%.63s", g.gl_pathv[i]);
                    seen[n_seen].err = err;
                    n_seen++;
                    if (err == EACCES)
                        fprintf(stderr, "yser: %s: permission denied; the BOINC client's user may not open it. Install the udev "
                                        "rule from %s (or add that user to the device's group, usually dialout) and plug the "
                                        "counter in again\n", g.gl_pathv[i], "https://yarrow.athena.org.tr/linux");
                    else
                        fprintf(stderr, "yser: %s: %s (errno %d)\n", g.gl_pathv[i], strerror(err), err);
                }
                continue;
            }
            yser_command(s, "LIVE 0", reply, sizeof reply, 300);
            if (yser_command(s, "INFO", reply, sizeof reply, 600) == 0 && strstr(reply, "yarrow=")) {
                snprintf(path_out, cap, "%s", g.gl_pathv[i]);
                globfree(&g);
                return s;
            }
            yser_close(s);
        }
        globfree(&g);
    }
#endif
    return NULL;
}

/* ---- raw (binary) access, for counters that do not speak the Yarrow line protocol ---- */

#ifdef _WIN32
yser_t *yser_open_baud(const char *path, int baud)
{
    yser_t *s = yser_open(path);
    if (!s)
        return NULL;
    DCB dcb;
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    GetCommState(s->h, &dcb);
    dcb.BaudRate = (DWORD)baud;
    SetCommState(s->h, &dcb);
    return s;
}

int yser_write(yser_t *s, const void *p, size_t n) { return wr(s, p, n) ? 0 : -1; }

int yser_read(yser_t *s, void *buf, size_t n, int timeout_ms)
{
    size_t got = 0;
    double end = yser_now() + timeout_ms / 1000.0;
    while (got < n) {
        double left = end - yser_now();
        if (left <= 0)
            break;
        COMMTIMEOUTS to = {MAXDWORD, MAXDWORD, (DWORD)(left * 1000) + 1, 0, 0};   /* wait for the first byte, take what is there */
        SetCommTimeouts(s->h, &to);
        DWORD r = 0;
        if (!ReadFile(s->h, (char *)buf + got, (DWORD)(n - got), &r, NULL))
            break;
        got += r;
    }
    return (int)got;
}

void yser_drain(yser_t *s)
{
    char junk[256];
    while (yser_read(s, junk, sizeof junk, 30) > 0)
        ;
}

int yser_ports(char out[][64], int max)
{
    int n = 0;
    for (int i = 0; i < 64 && n < max; i++)
        snprintf(out[n++], 64, "COM%d", i + 1);
    return n;
}
#else
yser_t *yser_open_baud(const char *path, int baud)
{
    yser_t *s = yser_open(path);
    if (!s)
        return NULL;
    struct termios t;
    speed_t sp = baud == 57600 ? B57600 : baud == 38400 ? B38400 : baud == 19200 ? B19200 : baud == 9600 ? B9600 : B115200;
    if (tcgetattr(s->fd, &t) == 0) {
        cfsetspeed(&t, sp);
        tcsetattr(s->fd, TCSANOW, &t);
    }
    return s;
}

int yser_write(yser_t *s, const void *p, size_t n) { return write(s->fd, p, n) == (ssize_t)n ? 0 : -1; }

int yser_read(yser_t *s, void *buf, size_t n, int timeout_ms)
{
    size_t got = 0;
    while (got < n && s->pos < s->len)                 /* what rd() has already read from the port comes first */
        ((unsigned char *)buf)[got++] = s->buf[s->pos++];
    double end = yser_now() + timeout_ms / 1000.0;
    while (got < n) {
        double left = end - yser_now();
        if (left <= 0)
            break;
        struct pollfd p = {s->fd, POLLIN, 0};
        int pr = poll(&p, 1, (int)(left * 1000) + 1);
        if (pr < 0 && errno == EINTR)
            continue;                  /* a signal (the BOINC library fires them all the time) is not the end of the data */
        if (pr <= 0)
            break;
        ssize_t r = read(s->fd, (char *)buf + got, n - got);
        if (r < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (r <= 0)
            break;
        got += (size_t)r;
    }
    return (int)got;
}

void yser_drain(yser_t *s)
{
    s->pos = s->len = 0;
    char junk[256];
    while (yser_read(s, junk, sizeof junk, 30) > 0)
        ;
}

int yser_ports(char out[][64], int max)
{
    static const char *pats[] = {"/dev/ttyUSB*", "/dev/ttyACM*", "/dev/cu.usbserial*", "/dev/cu.usbmodem*", NULL};
    int n = 0;
    for (int k = 0; pats[k]; k++) {
        glob_t g;
        if (glob(pats[k], 0, NULL, &g) != 0)
            continue;
        for (size_t i = 0; i < g.gl_pathc && n < max; i++)
            snprintf(out[n++], 64, "%s", g.gl_pathv[i]);
        globfree(&g);
    }
    return n;
}
#endif
