/* Helpers shared by the output writers. */
#ifndef JED2PLD_OUTPUT_H
#define JED2PLD_OUTPUT_H

#include "design.h"

/* The file name of PATH without its directory or extension. */
void file_stem(char *dst, size_t size, const char *path);

/* Today's date as month, day and year. Returns 0 if it is unknown. */
int today(int *month, int *day, int *year);

#endif
