#ifndef NRIO_XULU_INSTALL_H
#define NRIO_XULU_INSTALL_H

#include "udisk-setup-io.h"

enum nrio_xulu_result {
    NRIO_XULU_OK, NRIO_XULU_ALREADY, NRIO_XULU_UNCHANGED,
    NRIO_XULU_ALIAS_CONFLICT, NRIO_XULU_ERROR, NRIO_XULU_RESTORE_FAILED,
};

enum nrio_xulu_result nrio_restore_stock_menu(const char *root,
    struct nrio_file_id legacy_launcher, struct nrio_file_id stock, nrio_file_source stream,
    unsigned stock_asset);
#endif
