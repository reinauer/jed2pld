#ifndef PAL_H
#define PAL_H
#include "design.h"
int pal_supports_device(const char *device);
int pal_decode(const struct conversion *c, struct design *d);
#endif
