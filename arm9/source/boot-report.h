#ifndef NRIO_BOOT_REPORT_H
#define NRIO_BOOT_REPORT_H

#include "recovery_nand/recovery.h"

enum nrio_report_action {
    NRIO_REPORT_BACK, NRIO_REPORT_BACKUP, NRIO_REPORT_RETRY,
};

enum nrio_report_action nrio_boot_report(const char *root, const struct nrio_raw_info *info);
#endif
