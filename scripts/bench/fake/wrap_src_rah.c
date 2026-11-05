/* monitor/yarrow_src_rah.c itself, with its nanosleep() answered by the fake clock. Only that one call is replaced: the file has
 * struct members called close, so the whole set of macros of fake_sys.h cannot be used on it. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fake.h"

#define nanosleep(req, rem) fk_nanosleep((req), (rem))

#include "yarrow_src_rah.c"
