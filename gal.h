#ifndef GAL_H
#define GAL_H
#include "design.h"
int gal_supports_device(const char *device);
int gal_decode(const struct conversion *c, struct design *d);
#endif
