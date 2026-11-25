#ifndef NRIO_BUTTON_LEGEND_H
#define NRIO_BUTTON_LEGEND_H

#include <stdio.h>

/* Leave column 31 unused to keep libnds from wrapping the border. */
static inline void nrio_button_legend(const char *top, const char *bottom)
{
    puts("+---------- BUTTONS ----------+");
    printf("| %-27.27s |\n", top);
    if (bottom) printf("| %-27.27s |\n", bottom);
    printf("+-----------------------------+");
}

#endif
