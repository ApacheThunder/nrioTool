#ifndef NRIO_CART_DLDI_H
#define NRIO_CART_DLDI_H

#include <nds.h>
#include <nds/disc_io.h>

const DISC_INTERFACE *nrioTool_open_cart_sectors(bool writable);
void nrioTool_close_cart_sectors(void);

#endif
