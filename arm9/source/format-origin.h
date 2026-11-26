#ifndef NRIO_FORMAT_ORIGIN_H
#define NRIO_FORMAT_ORIGIN_H

#include <stdbool.h>
#include <string.h>

static inline bool nrio_slot1_launch_path(const char *path)
{
    return path && (!strncmp(path, "slot1:/", 7) || !strncmp(path, "cart:/", 6));
}

/* Installed DSi System Menu titles can have no argv. In DSi mode, use
   the mounted internal SD without relying on a launcher path.
   DS mode still requires the independent, writable Slot-2 DLDI. */
static inline bool nrio_can_format(const char *launch_path, bool dsi_mode,
                                  bool sd_mounted, bool slot2_driver,
                                  bool slot2_mounted)
{
    if (dsi_mode) return sd_mounted;
    return !nrio_slot1_launch_path(launch_path) && slot2_driver && slot2_mounted;
}

#endif
