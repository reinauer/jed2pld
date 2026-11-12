#ifndef V10_H
#define V10_H
#include "design.h"
int v10_supports_device(const char *device);
int v10_decode(const struct conversion *c, struct design *d);
#endif
