#ifndef NRIO_CART_PROBE_H
#define NRIO_CART_PROBE_H

#include "recovery_nand/recovery.h"

enum nrio_cart_probe_state {
    NRIO_CART_PROBE_DIRECT,
    NRIO_CART_PROBE_NO_ID,
    NRIO_CART_PROBE_UNSUPPORTED,
    NRIO_CART_PROBE_RETRY_READY,
    NRIO_CART_PROBE_RETRY_NO_ID,
    NRIO_CART_PROBE_RETRY_UNSTABLE,
};

bool nrio_open_cart_view(enum nrio_raw_mode mode, struct nrio_raw_info *info);
enum nrio_cart_probe_state nrio_cart_probe_result(uint32_t *id);

#endif
