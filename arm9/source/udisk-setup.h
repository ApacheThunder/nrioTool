#ifndef NRIO_UDISK_SETUP_H
#define NRIO_UDISK_SETUP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum nrio_udisk_setup_action {
    NRIO_UDISK_COPY_MENU,
    NRIO_UDISK_XULU_FRESH,
    NRIO_UDISK_XULU_SWITCH,
    NRIO_UDISK_REMOVE_UPDATER,
};

void nrioTool_quick_format(const char *backup_root);
void nrioTool_full_format(const char *backup_root);
void nrioTool_udisk_setup(enum nrio_udisk_setup_action action, const char *backup_root);
enum nrio_cart_filesystem {
    NRIO_CART_FS_READY, NRIO_CART_FS_PROBE_FAILED, NRIO_CART_FS_NO_MAP,
};
enum nrio_cart_filesystem nrioTool_check_cart_filesystem(void);
struct nrio_fat_check {
    unsigned markers, rejected, read_errors;
    uint32_t map_address;
};
enum nrio_cart_filesystem nrioTool_check_report_filesystem(struct nrio_fat_check *check);
bool nrioTool_mount_setup_cart(void);
bool nrioTool_mount_report_cart(void);
void nrioTool_unmount_setup_cart(void);

#ifdef __cplusplus
}
#endif

bool nrio_udisk_confirm(void);
void nrio_udisk_wait_back(void);

#endif
