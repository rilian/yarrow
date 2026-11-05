/* monitor/yarrow_monitor.c itself, with three changes: its main() is called fk_monitor_main(), time() is the virtual wall clock,
 * and ypending_add() goes through fk_add_bin() (fake_glue.c), which notes how long after its end each bin arrived. The headers of
 * the file come first, so that the macros change the calls of the file and nothing else. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fake.h"
#include "yarrow_bin.h"
#include "yarrow_glue.h"
#include "yarrow_lock.h"
#include "yarrow_serial.h"
#include "yarrow_src.h"
#include "yarrow_trickle.h"

int fk_add_bin(ypending_t *p, uint32_t start, uint32_t count);

#define time(p) fk_time(p)
#define ypending_add(p, start, count) fk_add_bin((p), (start), (count))
#define main fk_monitor_main

#include "yarrow_monitor.c"
