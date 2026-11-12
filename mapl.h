#ifndef MAPL_H
#define MAPL_H
#include "design.h"
int mapl_supports_device(const char *device);
int mapl_decode(const struct conversion *c, struct design *d);
#endif
