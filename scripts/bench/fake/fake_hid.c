/* Stands in for monitor/yarrow_hid.c (see fake.h): the Radioactive@Home counter when the profile says so, nothing otherwise,
 * and the virtual wall clock and a fixed computer name for the sources that ask for them. */
#include "fake.h"
#include "yarrow_hid.h"

#include <stdio.h>
#include <string.h>

static void rah_close(void *ctx)
{
    (void)ctx;
    fk_st.n_close++;
}

int yhid_open(uint16_t vid, uint16_t pid, const char *name_part, yhid_t *out)
{
    if (fk_cfg.kind != FK_RAH || vid != 0x16c0 || pid != 0x05df || !name_part || strcmp(name_part, "radioactiveathome") != 0)
        return -1;
    if (!fk_rah_present())
        return -1;
    fk_st.n_open++;
    memset(out, 0, sizeof *out);
    out->ctx = NULL;
    out->get_feature = fk_rah_get;
    out->set_feature = NULL;
    out->close = rah_close;
    out->vid = vid;
    out->pid = pid;
    snprintf(out->name, sizeof out->name, "%s", "radioactiveathome");
    snprintf(out->serial, sizeof out->serial, "%s", "0001");
    out->version = 0x0251;
    return 0;
}

double yhost_wall(void)
{
    fk_st.n_clock++;
    return (double)fk_wall_ns() / 1e9;
}

void yhost_name(char *out, size_t cap) { snprintf(out, cap, "%s", "benchhost"); }
