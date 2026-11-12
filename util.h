/* Helpers shared by the JEDEC parser and the device decoders. */
#ifndef JED2PLD_UTIL_H
#define JED2PLD_UTIL_H

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#ifdef _MSC_VER
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

/* Print the "Device name" / "Equations file" lines. Decoders call this once
 * the fuse map has passed their configuration checks, so an illegal macrocell
 * configuration is reported before those lines appear. */
void announce_output(void);

/* Pin and label arrays are sized for the largest supported package. A label
 * holds a pin name of up to 79 characters, with a leading '/' if active low. */
#define MAX_PINS  128
#define MAX_LABEL 80
typedef char label_t[MAX_LABEL];

/* The input of a device decoder: the fuse map and its pin labels. A decoder
 * returns 1 when it filled the design, or 0 after a diagnostic on ERR. */
struct conversion
{
    FILE *err;
    const char *device;         /* canonical name, e.g. "PAL16L8" */
    const unsigned char *fuses; /* COUNT fuse values, 0 or 1 */
    unsigned count;
    const label_t *labels; /* NOTE PINS label of each pin, "" if none */
};

/* A label written as "/name" declares the pin active low. */
static inline int label_is_inverted(const char *s)
{
    return *s == '/';
}

static inline const char *label_without_polarity(const char *s)
{
    return label_is_inverted(s) ? s + 1 : s;
}

/* Map a 24-pin DIP pin number to the matching 28-pin PLCC pin number. */
static inline int dip24_to_plcc28_pin(int pin)
{
    static const int map[] = {0,  2,  3,  4,  5,  6,  7,  9,  10, 11, 12, 13, 14,
                              16, 17, 18, 19, 20, 21, 23, 24, 25, 26, 27, 28};
    return map[pin];
}

#endif
