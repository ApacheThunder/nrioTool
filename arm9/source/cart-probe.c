#include "cart-probe.h"
#include "nrio_card.h"

#include <string.h>

static enum nrio_cart_probe_state state;
static uint32_t last_id;

static bool probe(enum nrio_raw_mode mode, struct nrio_raw_info *info)
{
    const bool started = nrio_recovery_startup();
    last_id = nrio_recovery_last_probe_id();
    return started && nrio_recovery_select_mode(mode) && nrio_recovery_get_info(info);
}

static bool has_id(void)
{
    return last_id != 0 && last_id != UINT32_MAX;
}

bool nrio_open_cart_view(enum nrio_raw_mode mode, struct nrio_raw_info *info)
{
    nrio_recovery_shutdown();
    if (probe(mode, info)) {
        state = NRIO_CART_PROBE_DIRECT;
        return true;
    }
    state = has_id() ? NRIO_CART_PROBE_UNSUPPORTED : NRIO_CART_PROBE_NO_ID;
    /* A cart with no bootable ROM never reaches the normal post-ROM setup.
       Try those existing ASIC mode commands once, without loading an SRL.
       This retry is diagnostic only and does not open a writable view. */
    if (state == NRIO_CART_PROBE_NO_ID &&
        (mode == NRIO_RAW_MAIN_READ || mode == NRIO_RAW_SPARE_READ)) {
        InitCartNandReadMode(0);
        if (probe(mode, info)) {
            uint32_t first[128], second[128];
            if (nrio_recovery_read(0, 1, first) && nrio_recovery_read(0, 1, second) &&
                !memcmp(first, second, sizeof(first))) {
                state = NRIO_CART_PROBE_RETRY_READY;
                return true;
            }
            state = NRIO_CART_PROBE_RETRY_UNSTABLE;
        } else {
            state = has_id() ? NRIO_CART_PROBE_UNSUPPORTED : NRIO_CART_PROBE_RETRY_NO_ID;
        }
    }
    nrio_recovery_shutdown();
    return false;
}

enum nrio_cart_probe_state nrio_cart_probe_result(uint32_t *id)
{
    *id = last_id;
    return state;
}
