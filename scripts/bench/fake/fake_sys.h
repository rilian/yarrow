/* Sends the system calls of the file that is included after this header to fake_sys.c (see fake.h). Used by wrap_serial.c only:
 * every system header that the wrapped file includes comes first, so that the macros below change the calls of the file and
 * nothing else. A file with struct members named open, close, read or write cannot be wrapped this way (wrap_src_rah.c). */
#ifndef FAKE_SYS_H
#define FAKE_SYS_H

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "fake.h"

#define open(...) fk_open(__VA_ARGS__)
#define close(fd) fk_close(fd)
#define read(fd, buf, n) fk_read((fd), (buf), (n))
#define write(fd, buf, n) fk_write((fd), (buf), (n))
#define poll(fds, n, ms) fk_poll((fds), (n), (ms))
#define nanosleep(req, rem) fk_nanosleep((req), (rem))
#define clock_gettime(clk, ts) fk_clock_gettime((clk), (ts))
#define tcgetattr(fd, t) fk_tcgetattr((fd), (t))
#define tcsetattr(fd, action, t) fk_tcsetattr((fd), (action), (t))
#define glob(pattern, flags, errfn, g) fk_glob((pattern), (flags), (errfn), (g))
#define globfree(g) fk_globfree(g)

#endif
