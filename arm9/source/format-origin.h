#ifndef NRIO_FORMAT_ORIGIN_H
#define NRIO_FORMAT_ORIGIN_H

#include <stdbool.h>
#include <string.h>

static inline bool nrio_sd_launch_path(const char *path)
{
    return path && ((!strncmp(path, "sd:/", 4) && path[4]) ||
                    (!strncmp(path, "sdmc:/", 6) && path[6]));
}

/* Mounted DSi SD by itself does not establish where the tool was launched.
   Slot-2 is independent only if the launch DLDI identifies that interface. */
static inline bool nrio_can_format(bool sd_launch, bool sd_mounted,
                                  bool slot2_driver, bool slot2_mounted)
{
    return (sd_launch && sd_mounted) || (slot2_driver && slot2_mounted);
}

#endif
