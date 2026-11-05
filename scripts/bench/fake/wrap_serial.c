/* monitor/yarrow_serial.c itself, with its system calls answered by the fake serial layer (fake_sys.h). The file is included, not
 * copied, so that the benchmark runs the code of the tree that is being measured. */
#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE 1
#endif
#include "yarrow_serial.h"
#include "fake_sys.h"

#include "yarrow_serial.c"
