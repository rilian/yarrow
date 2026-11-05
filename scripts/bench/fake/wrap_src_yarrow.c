/* monitor/yarrow_src_yarrow.c itself, with time() answered by the virtual wall clock. The headers of the file come first, so that
 * the macro changes the calls of the file and nothing else. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fake.h"
#include "yarrow_backfill.h"
#include "yarrow_glue.h"
#include "yarrow_serial.h"
#include "yarrow_src.h"

#define time(p) fk_time(p)

#include "yarrow_src_yarrow.c"
