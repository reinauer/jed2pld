/* Helpers shared by the output writers. */
#include "output.h"
#include <time.h>

void file_stem(char *dst, size_t size, const char *path)
{
    const char *base = path, *dot;
    for (const char *s = path; *s; s++)
        if (*s == '/' || *s == '\\' || *s == ':')
            base = s + 1;
    dot = strrchr(base, '.');
    snprintf(dst, size, "%.*s", (int)(dot ? (size_t)(dot - base) : strlen(base)), base);
}

int today(int *month, int *day, int *year)
{
    time_t now          = time(NULL);
    const struct tm *tm = localtime(&now);
    if (!tm)
        return 0;
    *month = tm->tm_mon + 1;
    *day   = tm->tm_mday;
    *year  = tm->tm_year + 1900;
    return 1;
}
