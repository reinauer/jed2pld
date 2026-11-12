/* OPAL equation (.eqn) writer. */
#ifndef JED2PLD_OPAL_H
#define JED2PLD_OPAL_H

#include "design.h"

/* Write design D as an OPAL equation file. INPUT is the JEDEC file name for
 * the header; ALT selects the alternate operator set. */
void opal_write(FILE *out, const struct design *d, const char *input, int alt);

#endif
