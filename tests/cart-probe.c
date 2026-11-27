#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cart-probe.h"
#include "nrio_card.h"

static uint32_t ids[2];
static bool accepts[2], stable, readable;
static unsigned starts, setup_calls, reads, shutdowns;
static enum nrio_raw_mode selected;

bool nrio_recovery_startup(void)
{
    assert(starts < 2);
    return accepts[starts++];
}
uint32_t nrio_recovery_last_probe_id(void)
{
    return ids[starts - 1];
}
void nrio_recovery_shutdown(void)
{
    ++shutdowns;
}
bool nrio_recovery_select_mode(enum nrio_raw_mode mode)
{
    selected = mode;
    return true;
}
bool nrio_recovery_get_info(struct nrio_raw_info *info)
{
    info->nand_id = ids[starts - 1];
    return true;
}
bool nrio_recovery_read(uint32_t sector, uint32_t count, void *data)
{
    assert(selected == NRIO_RAW_MAIN_READ || selected == NRIO_RAW_SPARE_READ);
    assert(sector == 0 && count == 1);
    ++reads;
    memset(data, stable ? 0xff : (int)reads, 512);
    return readable;
}
bool InitCartNandReadMode(uint32_t card_type)
{
    assert(card_type == 0);
    ++setup_calls;
    return true;
}

static void reset(bool direct, uint32_t first, bool second, uint32_t next)
{
    accepts[0] = direct;
    accepts[1] = second;
    ids[0] = first;
    ids[1] = next;
    stable = readable = true;
    starts = setup_calls = reads = shutdowns = 0;
}

int main(void)
{
    const uint32_t known = 0x2555d5ec, unknown = 0x12345678;
    struct nrio_raw_info info;
    uint32_t id;

    reset(true, known, false, 0);
    assert(nrio_open_cart_view(NRIO_RAW_MAIN_READ, &info) && info.nand_id == known);
    assert(nrio_cart_probe_result(&id) == NRIO_CART_PROBE_DIRECT && id == known);
    assert(starts == 1 && !setup_calls && !reads);
    reset(false, unknown, true, known);
    assert(!nrio_open_cart_view(NRIO_RAW_MAIN_READ, &info));
    assert(nrio_cart_probe_result(&id) == NRIO_CART_PROBE_UNSUPPORTED && id == unknown);
    assert(starts == 1 && !setup_calls && !reads);

    reset(false, 0, true, known);
    assert(!nrio_open_cart_view(NRIO_RAW_SD_BLOCK0_RESTORE, &info));
    assert(nrio_cart_probe_result(&id) == NRIO_CART_PROBE_NO_ID);
    assert(starts == 1 && !setup_calls && !reads);

    reset(false, UINT32_MAX, true, known);
    assert(nrio_open_cart_view(NRIO_RAW_MAIN_READ, &info) && info.nand_id == known);
    assert(nrio_cart_probe_result(&id) == NRIO_CART_PROBE_RETRY_READY && id == known);
    assert(starts == 2 && setup_calls == 1 && reads == 2);
    reset(false, 0, true, known);
    assert(nrio_open_cart_view(NRIO_RAW_SPARE_READ, &info) && selected == NRIO_RAW_SPARE_READ);

    reset(false, 0, true, known);
    stable = false;
    assert(!nrio_open_cart_view(NRIO_RAW_MAIN_READ, &info));
    assert(nrio_cart_probe_result(&id) == NRIO_CART_PROBE_RETRY_UNSTABLE && id == known);
    assert(reads == 2 && shutdowns == 2);
    reset(false, 0, true, known);
    readable = false;
    assert(!nrio_open_cart_view(NRIO_RAW_MAIN_READ, &info));
    assert(nrio_cart_probe_result(&id) == NRIO_CART_PROBE_RETRY_UNSTABLE && id == known);
    assert(reads == 1 && shutdowns == 2);
    reset(false, 0, false, UINT32_MAX);
    assert(!nrio_open_cart_view(NRIO_RAW_MAIN_READ, &info));
    assert(nrio_cart_probe_result(&id) == NRIO_CART_PROBE_RETRY_NO_ID && id == UINT32_MAX);
    assert(starts == 2 && setup_calls == 1 && !reads);
    puts("Cart probe policy tests passed");
}
