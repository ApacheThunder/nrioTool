/* cc -std=c11 -Wall -Wextra -Werror -Itests/mocks/slot1 tests/slot1-watch.c arm9/source/slot1-watch.c -o /tmp/slot1-watch && /tmp/slot1-watch */
#include "../arm9/source/slot1-watch.h"
#include <assert.h>

/* Linked against the real monitor with a mock SCFG and IRQ API. */
volatile unsigned test_scfg_mc;
static void update(bool present)
{
    test_scfg_mc = present ? 0u : 1u;
    nrio_slot1_watch_vblank();
}

int main(void)
{
    bool present;
    test_scfg_mc = 0;
    nrio_slot1_watch_start(true);
    assert(nrio_slot1_allowed() && !nrio_slot1_changed());
    test_scfg_mc = 1;
    assert(!nrio_slot1_allowed()); /* Before the next VBlank. */
    nrio_slot1_watch_vblank();
    update(true);
    assert(nrio_slot1_changed() && !nrio_slot1_allowed());
    assert(nrio_slot1_take_change(&present) && present);
    assert(nrio_slot1_allowed() && !nrio_slot1_changed());

    update(false);
    assert(nrio_slot1_take_change(&present) && !present);
    assert(!nrio_slot1_allowed());
    update(true);
    assert(nrio_slot1_take_change(&present) && present);
    assert(nrio_slot1_allowed());

    test_scfg_mc = 0x11;
    nrio_slot1_watch_start(true);
    assert(!nrio_slot1_allowed() && !nrio_slot1_changed());
    test_scfg_mc = 0x18; /* Ignore unrelated bits. */
    nrio_slot1_watch_vblank();
    assert(nrio_slot1_take_change(&present) && present);

    nrio_slot1_watch_start(false);
    update(false);
    assert(nrio_slot1_allowed() && !nrio_slot1_changed());
    assert(!nrio_slot1_take_change(&present));
}
