#ifndef NRIO_SLOT1_WATCH_H
#define NRIO_SLOT1_WATCH_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* A removal stays pending even if another cart arrives before the UI wakes. */
struct nrio_slot1_state { bool present, pending; };
static inline void nrio_slot1_observe(volatile struct nrio_slot1_state *state, bool present)
{
    if (present != state->present) {
        state->present = present;
        state->pending = true;
    }
}

void nrio_slot1_watch_start(bool enabled);
void nrio_slot1_watch_vblank(void);
bool nrio_slot1_changed(void);
bool nrio_slot1_allowed(void);
bool nrio_slot1_take_change(bool *inserted);

#ifdef __cplusplus
}
#endif
#endif
