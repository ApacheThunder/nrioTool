#include <nds.h>
#include "slot1-watch.h"

static volatile bool watch;
static volatile struct nrio_slot1_state state;

static bool present_now(void) { return !(REG_SCFG_MC & 1u); }

void nrio_slot1_watch_start(bool enabled)
{
    state.present = !enabled || present_now();
    state.pending = false;
    watch = enabled;
}

void nrio_slot1_watch_vblank(void)
{
    if (watch) nrio_slot1_observe(&state, present_now());
}

bool nrio_slot1_changed(void) { return watch && state.pending; }

bool nrio_slot1_allowed(void)
{
    return !watch || (!state.pending && state.present && present_now());
}

/* Only the idle UI consumes the latch. I/O and write confirmation never do. */
bool nrio_slot1_take_change(bool *inserted)
{
    if (!watch) return false;
    const int irq = enterCriticalSection();
    nrio_slot1_observe(&state, present_now());
    const bool pending = state.pending;
    *inserted = state.present;
    state.pending = false;
    leaveCriticalSection(irq);
    return pending;
}
