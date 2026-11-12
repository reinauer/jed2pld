/* WinCUPL source (.pld) writer. */
#ifndef JED2PLD_CUPL_H
#define JED2PLD_CUPL_H

#include "design.h"

/* Write design D as a CUPL source file. INPUT is the JEDEC file name. Returns
 * 0 after a message on ERR when the device has no CUPL equivalent. */
int cupl_write(FILE *out, FILE *err, const struct design *d, const char *input);

#endif
