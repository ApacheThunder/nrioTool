#ifndef NRIO_GM9_BUNDLE_H
#define NRIO_GM9_BUNDLE_H

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The caller owns both streams and must discard the output on failure. */
bool nrio_unpack_gm9(FILE *source, FILE *destination);

#ifdef __cplusplus
}
#endif
#endif
