/* JEDEC fuse map to WinCUPL source or OPAL equations converter. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "banner.h"
#include "cupl.h"
#include "device_geometry.h"
#include "gal.h"
#include "mapl.h"
#include "opal.h"
#include "pal.h"
#include "util.h"
#include "v10.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#define close  _close
#define fdopen _fdopen
#define fileno _fileno
#define getpid _getpid
#define open   _open
#else
#include <unistd.h>
#endif

#define MAX_FUSES 131072
#define MAX_FILE  (256L * 1024 * 1024) /* sanity limit */

struct jedec
{
    unsigned char fuse[MAX_FUSES];
    unsigned count;
    unsigned max_written;
    int had_l, had_qf;
    char device[80];
    label_t label[MAX_PINS];
    char label_words[256][160];
    unsigned label_count;
};

/* Device decoders, tried in order. */
static const struct decoder
{
    int (*supports)(const char *device);
    int (*decode)(const struct conversion *c, struct design *d);
} decoders[] = {
    {v10_supports_device, v10_decode},
    {pal_supports_device, pal_decode},
    {gal_supports_device, gal_decode},
    {mapl_supports_device, mapl_decode},
};

enum output_format
{
    FORMAT_OPAL,
    FORMAT_CUPL,
};

static int alternate, silent, announced;
static enum output_format output_format;
static const char *pin_file; /* --pins: names that replace the file's labels */
static FILE *errors;
static const char *announce_device, *announce_path;

void announce_output(void)
{
    if (announced)
        return;
    announced = 1;
    if (!silent)
        printf("Device name    : %s\n%s : %s\n", announce_device,
               output_format == FORMAT_CUPL ? "Output file   " : "Equations file", announce_path);
}

static int starts_with_ignore_case(const char *s, const char *prefix)
{
    return !strncasecmp(s, prefix, strlen(prefix));
}

static void copy_substring(char *dst, size_t size, const char *begin, const char *end)
{
    size_t n = (size_t)(end - begin);
    if (n >= size)
        n = size - 1;
    memcpy(dst, begin, n);
    dst[n] = 0;
}

static const char *path_basename(const char *path)
{
    const char *p = path, *base = path;
    while (*p) {
        if (*p == '/' || *p == '\\' || *p == ':')
            base = p + 1;
        p++;
    }
    return base;
}

static void replace_extension(char *dst, size_t size, const char *src, const char *extension)
{
    const char *dot = strrchr(path_basename(src), '.');
    size_t n        = dot ? (size_t)(dot - src) : strlen(src);
    if (n + strlen(extension) + 1 > size)
        n = size - strlen(extension) - 1;
    memcpy(dst, src, n);
    strcpy(dst + n, extension);
}

/* Compare file identities, including hard links and symbolic links. */
static int stream_matches_path(FILE *stream, const char *path)
{
    if (!*path)
        return 0;
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION a, b;
    HANDLE other = CreateFileA(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (other == INVALID_HANDLE_VALUE)
        return 0;
    int same = GetFileInformationByHandle((HANDLE)_get_osfhandle(fileno(stream)), &a) &&
               GetFileInformationByHandle(other, &b) && a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
               a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
    CloseHandle(other);
    return same;
#else
    struct stat a, b;
    return !fstat(fileno(stream), &a) && !stat(path, &b) && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
#endif
}

/* Create beside the destination so that publishing the completed file is a
 * single rename. Exclusive creation also protects against existing links. */
static FILE *open_equation_temporary(const char *path, char *temporary, size_t size)
{
    for (unsigned attempt = 0; attempt < 100; attempt++) {
        if (snprintf(temporary, size, "%s.%lu.%u.tmp", path, (unsigned long)getpid(), attempt) >= (int)size) {
            errno = ENAMETOOLONG;
            return NULL;
        }
#ifdef _WIN32
        int fd = open(temporary, _O_WRONLY | _O_CREAT | _O_EXCL | _O_TEXT, _S_IREAD | _S_IWRITE);
#else
        int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0666);
#endif
        if (fd < 0) {
            if (errno == EEXIST)
                continue;
            return NULL;
        }
#ifndef _WIN32
        /* Replacing a private file must not make it more widely readable. */
        struct stat previous;
        if (!stat(path, &previous) && S_ISREG(previous.st_mode) && fchmod(fd, previous.st_mode & 0777)) {
            int saved_errno = errno;
            close(fd);
            remove(temporary);
            errno = saved_errno;
            return NULL;
        }
#endif
        FILE *stream = fdopen(fd, "w");
        if (!stream) {
            int saved_errno = errno;
            close(fd);
            remove(temporary);
            errno = saved_errno;
        }
        return stream;
    }
    errno = EEXIST;
    return NULL;
}

static int publish_equations(const char *temporary, const char *path)
{
#ifdef _WIN32
    /* Unlike rename(), this also replaces an existing destination on Windows. */
    return MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(temporary, path) == 0;
#endif
}

static char *read_file(const char *path, size_t *length)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || n > MAX_FILE || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    /* Read as DOS text: Ctrl-Z ends the file and NUL bytes are skipped. */
    {
        size_t in, out = 0;
        for (in = 0; in < (size_t)n && buf[in] != 0x1a; in++)
            if (buf[in] != 0)
                buf[out++] = buf[in];
        n = (long)out;
    }
    buf[n]  = 0;
    *length = (size_t)n;
    return buf;
}

static void collect_label_tokens(struct jedec *j, const char *begin, const char *end)
{
    const char *p = begin;
    while (p < end && j->label_count < 256) {
        while (p < end && isspace((unsigned char)*p))
            p++;
        const char *word = p;
        while (p < end && !isspace((unsigned char)*p))
            p++;
        if (p > word)
            copy_substring(j->label_words[j->label_count++], 160, word, p);
    }
}

/* The highest pin or node number a label may name on DEVICE. */
static unsigned label_pin_limit(const char *device)
{
    unsigned pins   = MAX_PINS;
    const char *key = device;
    if (starts_with_ignore_case(key, "GAL") || starts_with_ignore_case(key, "PAL"))
        key += 3;
    for (size_t i = 0; i < sizeof(geometries) / sizeof(geometries[0]); i++)
        if (!strcasecmp(key, geometries[i].name)) {
            pins = geometries[i].pins;
            break;
        }
    if (strstr(key, "6001"))
        pins += 18;
    if (strstr(key, "MAPL"))
        pins += 64;
    return pins < MAX_PINS ? pins : MAX_PINS - 1;
}

static void assign_pin_labels(struct jedec *j, const char *device, const char *path)
{
    unsigned pins = label_pin_limit(device);
    for (unsigned i = 0; i < j->label_count; i++) {
        /* Split at the last colon: "a:1:2" is label "a:1" on pin 2. A label must be
         * non-empty (":2" cannot be assembled) and the pin must be positive. */
        char *word = j->label_words[i], *colon = strrchr(word, ':');
        int pin = colon ? atoi(colon + 1) : 0;
        if (pin <= 0 || pin > (int)pins || pin >= MAX_PINS || colon == word) {
            fprintf(errors, "W0401: Missing/invalid pin number for label '%s' in file '%s'.\n", word, path);
        } else if (j->label[pin][0]) {
            fprintf(errors, "W0403: More than one label assigned to pin %d in file '%s'.\n", pin, path);
        } else {
            copy_substring(j->label[pin], MAX_LABEL, word, colon);
            continue;
        }
        fputs("W0402: Default labels will be used for equations.\n", errors);
        return;
    }
}

/* Name pins from PATH: "name:pin" entries as in NOTE PINS, separated by white
 * space, with "#" starting a comment to the end of the line. The names replace
 * the labels of the JEDEC file. Returns 0 after an error. */
static int read_pin_file(struct jedec *j, const char *device, const char *path)
{
    size_t length;
    char *buf           = read_file(path, &length);
    unsigned limit      = label_pin_limit(device);
    int named[MAX_PINS] = {0}, line = 1, ok = 1;
    if (!buf) {
        fprintf(errors, "E0001: Could not open file '%s'.\n", path);
        return 0;
    }
    for (char *p = buf; *p && ok;) {
        if (*p == '#') {
            while (*p && *p != '\n')
                p++;
            continue;
        }
        if (isspace((unsigned char)*p)) {
            line += *p++ == '\n';
            continue;
        }
        char *word = p, *colon = NULL, *digits;
        for (; *p && !isspace((unsigned char)*p); p++)
            if (*p == ':')
                colon = p;
        char saved = *p;
        *p         = 0;
        long pin   = colon ? strtol(colon + 1, &digits, 10) : 0;
        ok         = 0;
        if (!colon || colon == word + (*word == '/') || !isdigit((unsigned char)colon[1]) || *digits)
            fprintf(errors, "%s:%d: Expected name:pin, not '%s'.\n", path, line, word);
        else if (pin < 1 || pin > (long)limit)
            fprintf(errors, "%s:%d: The %s has no pin %ld.\n", path, line, device, pin);
        else if (colon - word >= MAX_LABEL)
            fprintf(errors, "%s:%d: The name for pin %ld is longer than %d characters.\n", path, line, pin,
                    MAX_LABEL - 1);
        else if (named[pin])
            fprintf(errors, "%s:%d: Pin %ld is named twice.\n", path, line, pin);
        else {
            copy_substring(j->label[pin], MAX_LABEL, word, colon);
            named[pin] = ok = 1;
        }
        *p = saved;
    }
    free(buf);
    return ok;
}

/* Find the device name in the header (the text before the first '*'). */
static void scan_device(struct jedec *j, const char *buf, size_t length)
{
    const char *p, *first_star = memchr(buf, '*', length);
    memset(j, 0, sizeof(*j));
    if (!first_star)
        return;
    for (p = buf; p < first_star; p++) {
        const char *q;
        if (!isalpha((unsigned char)*p))
            continue;
        if (!starts_with_ignore_case(p, "PAL") && !starts_with_ignore_case(p, "GAL") &&
            !starts_with_ignore_case(p, "MAPL") && !starts_with_ignore_case(p, "PLA"))
            continue;
        q = p;
        while (q < first_star && (isalnum((unsigned char)*q) || *q == '_'))
            q++;
        /* A bare prefix such as the "gal" in "Legal" is not a device name;
         * keep scanning in that case. */
        if (q - p <= (starts_with_ignore_case(p, "MAPL") ? 4 : 3))
            continue;
        copy_substring(j->device, sizeof(j->device), p, q);
        return;
    }
    /* CUPL headers name the part on a "Device  g22v10" line. Use it only when no
     * PAL/GAL/MAPL word was found. */
    for (p = buf; p + 6 < first_star; p++) {
        const char *q;
        if (!starts_with_ignore_case(p, "Device") || (p > buf && isalnum((unsigned char)p[-1])) ||
            !isspace((unsigned char)p[6]))
            continue;
        for (q = p + 6; q < first_star && isspace((unsigned char)*q); q++)
            ;
        p = q;
        while (q < first_star && (isalnum((unsigned char)*q) || *q == '_'))
            q++;
        if (q > p)
            copy_substring(j->device, sizeof(j->device), p, q);
        return;
    }
}

static const char *device_library_key(const char *d)
{
    if (starts_with_ignore_case(d, "PAL") || starts_with_ignore_case(d, "GAL"))
        return d + 3;
    return d;
}

/* Rewrite DEVICE to its library name. Returns 0 when it matches no known device. */
static int canonicalize_device_name(char device[80], int automatic)
{
    static const char *generic[] = {
        "10H8",    "10L8",   "10P8",   "12H10", "12H6",  "12L10",   "12L6",    "12P10",   "12P6",    "14H4",
        "14H8",    "14L4",   "14L8",   "14P4",  "14P8",  "16C1",    "16C4",    "16H2",    "16H6",    "16H8",
        "16L2",    "16L6",   "16L8",   "16P2",  "16P4",  "16P6",    "16P8",    "16PE8",   "16R4",    "16R6",
        "16R8",    "16RA8",  "16RD8",  "16RM4", "16RP4", "16RP6",   "16RP8",   "16V8QS",  "16V8A",   "16V8",
        "18L4",    "18H4",   "18P4",   "20C1",  "20H10", "20H2",    "20H8",    "20L10",   "20L2",    "20L8",
        "20P10",   "20P2",   "20P8",   "20R4",  "20R6",  "20R8",    "20RA10",  "20RP4",   "20RP6",   "20RP8",
        "20V8QSV", "20V8QS", "20V8AV", "20V8A", "20V8V", "20V8",    "20X10",   "20X4",    "20X8",    "22CV10V",
        "22CV10",  "22V10V", "22V10",  "6001V", "6001",  "MAPL128", "MAPL144", "MAPL244", "MAPL268",
    };
    char raw[80];
    snprintf(raw, sizeof(raw), "%s", device);
    /* The header scanner consumes only one package/speed suffix. */
    if (automatic) {
        char *v = strstr(raw, "V8");
        if (v && (v[2] == 'A' || v[2] == 'B'))
            v[3] = 0;
        else if (v && v[2] == 'Q' && v[3] == 'S')
            v[v[4] == 'V' ? 5 : 4] = 0; /* 20V8QSV is the 28-pin package */
    }
    for (size_t i = 0; i < sizeof(generic) / sizeof(generic[0]); i++) {
        const char *key = generic[i];
        if (!strstr(raw, key))
            continue;
        if (!strcmp(key, "16P8") && (strstr(raw, "EC") || strstr(raw, "10")))
            strcpy(device, "EC16P8");
        else if ((!strcmp(key, "22V10") || !strcmp(key, "22V10V") || !strcmp(key, "20RA10")) && strchr(raw, 'G'))
            snprintf(device, 80, "G%s", key);
        else if (strstr(key, "V8") || strstr(key, "CV10") || strstr(key, "6001"))
            snprintf(device, 80, "GAL%s", key);
        else if (strstr(key, "MAPL"))
            snprintf(device, 80, "%s", key);
        else
            snprintf(device, 80, "PAL%s", key);
        return 1;
    }
    return 0;
}

/* Library key for DEVICE: the name without its PAL/GAL prefix, except that the
 * GAL 22V10 and 20RA10 keep a leading G. KEY must hold 80 bytes. */
static const char *geometry_key(const char *device, char key_buffer[80])
{
    const char *key = device;
    if (starts_with_ignore_case(key, "PAL"))
        key += 3;
    else if (starts_with_ignore_case(key, "GAL")) {
        key += 3;
        if (starts_with_ignore_case(key, "22V10") || starts_with_ignore_case(key, "20RA10")) {
            snprintf(key_buffer, 80, "G%s", key);
            key = key_buffer;
        }
    }
    return key;
}

static const struct geometry *find_geometry(const char *device)
{
    char key_buffer[80];
    const char *key = geometry_key(device, key_buffer);
    for (size_t i = 0; i < sizeof(geometries) / sizeof(geometries[0]); i++)
        if (!strcasecmp(key, geometries[i].name))
            return &geometries[i];
    return NULL;
}

/* Accept maps with or without the 64-bit signature for the GAL 22V10
 * and CV10 variants, including the 28-pin packages, and the GAL20RA10 map
 * without its 16 trailing unused fuses. */
static int qf_is_valid(const char *key, const struct geometry *g, unsigned count)
{
    int signature_count = (starts_with_ignore_case(key, "G22V10") || starts_with_ignore_case(key, "22CV10")) &&
                          (count == 5828 || count == 5892);
    int ra10_count      = !strcasecmp(key, "G20RA10") && count == 3274;
    return !g || count == g->fuses || signature_count || ra10_count;
}

/* Check the wide result before narrowing it to a fuse address or count. */
static int parse_unsigned(const char *text, char **end, unsigned *result)
{
    while (isspace((unsigned char)*text))
        text++;
    errno               = 0;
    unsigned long value = strtoul(text, end, 10);
    if (*text == '-' || errno == ERANGE || value > UINT_MAX)
        return 0;
    *result = (unsigned)value;
    return 1;
}

/* Read the fields after the header. Report the first fault and stop: QF/QP are checked as they are read, not after the
 * whole file. */
static int parse_fields(struct jedec *j, const char *buf, size_t length, const char *device, const char *path)
{
    const char *end = buf + length, *first_star = memchr(buf, '*', length);
    const char *p = first_star ? first_star + 1 : buf;
    char key_buffer[80];
    const char *key          = geometry_key(device, key_buffer);
    const struct geometry *g = find_geometry(device);
    unsigned cursor          = 0;
    while (p < end) {
        const char *star = memchr(p, '*', (size_t)(end - p));
        const char *q    = p;
        int unterminated = !star;
        if (unterminated)
            star = end; /* Trailing text is a field, e.g. after a missing ETX */
        while (q < star && isspace((unsigned char)*q))
            q++;
        const char *field = q;
        if (*q == 'N' && starts_with_ignore_case(q, "NOTE") && isspace((unsigned char)q[4])) {
            /* "NOTE <word> labels...": only the first letter must be upper case;
             * the word must start with PIN or NODE and is skipped whole, so
             * "NOTE PINS a:2" and "NOTE PIN a:2" both label pin 2 but
             * "NOTE PINSa:2" does not. Any other note is ignored. */
            const char *word = q + 4, *after;
            while (word < star && isspace((unsigned char)*word))
                word++;
            for (after = word; after < star && !isspace((unsigned char)*after); after++)
                ;
            if (starts_with_ignore_case(word, "PIN") || starts_with_ignore_case(word, "NODE"))
                collect_label_tokens(j, after, star);
        } else if (*q == 'Q' && q + 1 < star && toupper((unsigned char)q[1]) == 'F') {
            j->had_qf = 1;
            if (!parse_unsigned(q + 2, NULL, &j->count) || !qf_is_valid(key, g, j->count)) {
                fprintf(errors, "E0404: Incorrect QF value for '%s'. Correct value is %u.\n", key, g ? g->fuses : 0);
                return 0;
            }
        } else if (*q == 'Q' && q + 1 < star && q[1] == 'P') {
            unsigned pins;
            if (!parse_unsigned(q + 2, NULL, &pins) || (g && pins != g->pins)) {
                fprintf(errors, "E0406: Incorrect QP value for '%s'. Correct value is %u.\n", key, g ? g->pins : 0);
                return 0;
            }
        } else if (*q == 'F') {
            if (j->had_l) {
                fputs("E0411: F Field must appear before L Field in JEDEC file.\n", errors);
                return 0;
            }
            memset(j->fuse, atoi(q + 1) & 1, sizeof(j->fuse));
        } else if (*q == 'L' && q + 1 < star) {
            j->had_l = 1;
            /* Read only digits for the fuse number (no sign or leading space), then
             * always consume one more character, even when it
             * is the '*', in which case the field runs on to the next '*'. */
            q++;
            cursor = 0;
            if (isdigit((unsigned char)*q)) {
                char *after;
                if (!parse_unsigned(q, &after, &cursor)) {
                    fputs("E0405: Fuse number in L field is greater than QF value.\n", errors);
                    return 0;
                }
                q = after;
            }
            if (q < end)
                q++;
            if (q > star && !unterminated) {
                const char *next = memchr(q, '*', (size_t)(end - q));
                star             = next ? next : end;
                unterminated     = !next;
            }
            for (; q < star; q++)
                if (*q == '0' || *q == '1') {
                    if (cursor < MAX_FUSES)
                        j->fuse[cursor] = (unsigned char)(*q - '0');
                    if (cursor < ~0u)
                        cursor++;
                } else if (!isspace((unsigned char)*q))
                    fprintf(errors, "W0407: Illegal character '%c' in JEDEC fusemap ignored.\n", *q);
            if (cursor > j->max_written)
                j->max_written = cursor;
            /* Reported as soon as the field is read, against QF if it has been
             * seen, else the device size. */
            if (cursor > (j->had_qf ? j->count : g ? g->fuses : ~0u)) {
                fputs("E0405: Fuse number in L field is greater than QF value.\n", errors);
                return 0;
            }
            if (unterminated) {
                fprintf(errors, "E0010: Unexpected EOF encountered in file '%s'.\n", path);
                return 0;
            }
        } else if (q < star && !strchr("NQCGVXPADT\003", *q)) {
            fprintf(errors, "E0410: Illegal JEDEC field character '%c' (%d).\n", *q, (unsigned char)*q);
            return 0;
        }
        /* A field cut off by the end of the file is an error, except the ETX
         * and checksum that follow the last field. */
        if (unterminated && field < end && *field != 0x03 && *field != 'L') {
            fprintf(errors, "E0010: Unexpected EOF encountered in file '%s'.\n", path);
            return 0;
        }
        if (unterminated)
            break;
        p = star + 1;
    }
    return 1;
}

/* Checks that need the whole file. */
static int finish_parse(struct jedec *j, const char *device, const char *path)
{
    const struct geometry *g = find_geometry(device);
    if (!j->had_qf && g)
        j->count = g->fuses;
    if (j->max_written > j->count) {
        fputs("E0405: Fuse number in L field is greater than QF value.\n", errors);
        return 0;
    }
    if (!j->had_l) {
        fprintf(errors, "E0409: No fuse number found in JEDEC file %s.\n", path);
        return 0;
    }
    return 1;
}

/* Close the -e error log, if any, and pass the exit status through. */
static int finish(int status)
{
    if (errors && errors != stderr) {
        int failed = ferror(errors);
        if (fclose(errors) == EOF)
            failed = 1;
        if (failed) {
            fputs("Could not write error log.\n", stderr);
            status = 2;
        }
    }
    errors = stderr;
    return status;
}

struct options
{
    const char *input, *device, *output, *error, *pins;
    const char *unknown;       /* first unknown option or extra file name */
    const char *bad_format;    /* an unknown --format value */
    char missing[32];          /* the option missing its argument */
    enum output_format format; /* FORMAT_CUPL unless --format says otherwise */
    int help, version;
};

/* Options without a short form are told apart by codes that are not letters. */
enum
{
    OPT_FORMAT = 1,
    OPT_PINS,
    OPT_HELP,
    OPT_VERSION,
};

/* Long options. The DOS tool knew none, so they are only recognised as "--"
 * followed by a letter, and take their argument as "--name=value" or as the
 * next word. */
static const struct long_option
{
    const char *name;
    int code, argument;
} long_options[] = {
    {"alternate", 'a', 0}, {"silent", 's', 0},     {"device", 'd', 1},
    {"output", 'o', 1},    {"error-file", 'e', 1}, {"format", OPT_FORMAT, 1},
    {"pins", OPT_PINS, 1}, {"help", OPT_HELP, 0},  {"version", OPT_VERSION, 0},
};

static void set_option(struct options *opt, int code, const char *value)
{
    switch (code) {
    case 'a':
        alternate = 1;
        break;
    case 's':
        silent = 1;
        break;
    case 'd':
        opt->device = value;
        break;
    case 'o':
        opt->output = value;
        break;
    case 'e':
        opt->error = value;
        break;
    case OPT_FORMAT:
        if (!strcasecmp(value, "opal") || !strcasecmp(value, "eqn"))
            opt->format = FORMAT_OPAL;
        else if (!strcasecmp(value, "cupl") || !strcasecmp(value, "pld"))
            opt->format = FORMAT_CUPL;
        else if (!opt->bad_format)
            opt->bad_format = value;
        break;
    case OPT_PINS:
        opt->pins = value;
        break;
    case OPT_HELP:
        opt->help = 1;
        break;
    case OPT_VERSION:
        opt->version = 1;
        break;
    }
}

/* Parse "--name[=value]" at ARGV[*I]. Returns 0 after recording an error. */
static int parse_long_option(int argc, char **argv, int *i, struct options *opt)
{
    const char *name = argv[*i] + 2, *equals = strchr(name, '=');
    size_t length = equals ? (size_t)(equals - name) : strlen(name);
    for (size_t k = 0; k < sizeof(long_options) / sizeof(long_options[0]); k++) {
        const struct long_option *o = &long_options[k];
        const char *value           = NULL;
        if (strlen(o->name) != length || strncmp(o->name, name, length))
            continue;
        if (!o->argument && equals)
            break;
        if (o->argument) {
            value = equals ? equals + 1 : *i + 1 < argc ? argv[++*i] : NULL;
            if (!value) {
                snprintf(opt->missing, sizeof(opt->missing), "--%s", o->name);
                return 0;
            }
        }
        set_option(opt, o->code, value);
        return 1;
    }
    opt->unknown = argv[*i];
    return 0;
}

static void parse_command_line(int argc, char **argv, struct options *opt)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (*arg != '-') {
            if (opt->input) {
                opt->unknown = arg; /* A second filename is reported as an unknown option */
                return;
            }
            opt->input = arg;
            continue;
        }
        if (arg[1] == '-' && isalpha((unsigned char)arg[2])) {
            if (!parse_long_option(argc, argv, &i, opt))
                return;
            continue;
        }
        char letter       = (char)tolower((unsigned char)arg[1]);
        const char *value = arg + 2;
        if ((letter == 'a' || letter == 's') && !arg[2])
            set_option(opt, letter, NULL);
        else if (letter == 'd' || letter == 'o' || letter == 'e') {
            if (!*value) {
                if (i + 1 == argc || argv[i + 1][0] == '-') {
                    snprintf(opt->missing, sizeof(opt->missing), "-%c", letter);
                    return;
                }
                value = argv[++i];
            }
            set_option(opt, letter, value);
        } else {
            opt->unknown = arg;
            return;
        }
    }
}

/* Copy PATH to DST, adding EXTENSION when the file name has none. */
static void with_default_extension(char *dst, size_t size, const char *path, const char *extension)
{
    if (strchr(path_basename(path), '.'))
        snprintf(dst, size, "%s", path);
    else
        replace_extension(dst, size, path, extension);
}

/* Open the -e error log as ERRORS. Returns 0, or 2 after a message on stderr. */
static int open_error_log(const char *path, const char *input_path, const char *output_path)
{
    /* Open without truncating until aliases of input and output have been
     * ruled out. Opening first also detects aliases of a newly created log. */
    FILE *log = fopen(path, "a");
    if (!log) {
        fprintf(stderr, "Cannot write error file %s: %s\n", path, strerror(errno));
        return 2;
    }
    if (stream_matches_path(log, input_path) || stream_matches_path(log, output_path)) {
        fclose(log);
        fprintf(stderr, "Error log must be different from input and output files: %s\n", path);
        return 2;
    }
#ifdef _WIN32
    int failed = _chsize(fileno(log), 0);
#else
    int failed = ftruncate(fileno(log), 0);
#endif
    if (failed) {
        fprintf(stderr, "Cannot write error file %s: %s\n", path, strerror(errno));
        fclose(log);
        return 2;
    }
    errors = log;
    return 0;
}

/* Find the device, select its decoder and read the fuse map and labels.
 * Returns the decoder, or NULL after a diagnostic. */
static const struct decoder *load_fuse_map(struct jedec *j, char device[80], const char *buf, size_t length,
                                           const char *device_arg, const char *path)
{
    const struct decoder *decoder = NULL;
    scan_device(j, buf, length);
    if (device_arg)
        snprintf(j->device, sizeof(j->device), "%s", device_arg);
    if (!*j->device) {
        fprintf(errors, "E1804: Device name not found in file %s.\n", path);
        return NULL;
    }
    snprintf(device, 80, "%s", j->device);
    for (char *c = device; *c; c++)
        *c = (char)toupper((unsigned char)*c);
    int known_device = canonicalize_device_name(device, device_arg == NULL);
    /* An unknown name in the file header is "not found" (E1804); a known device
     * that cannot be decoded, or any unknown -d name, is E0004. This is
     * checked before QF and QP are read. */
    for (size_t i = 0; i < sizeof(decoders) / sizeof(decoders[0]) && !decoder; i++)
        if (decoders[i].supports(device))
            decoder = &decoders[i];
    if (!decoder) {
        if (!device_arg && !known_device)
            fprintf(errors, "E1804: Device name not found in file %s.\n", path);
        else
            fprintf(errors, "E0004: Invalid/unsupported device name %s.\n",
                    known_device ? device_library_key(device) : device);
        return NULL;
    }
    if (!parse_fields(j, buf, length, device, path) || !finish_parse(j, device, path))
        return NULL;
    if (j->count > MAX_FUSES) {
        fprintf(errors, "Invalid JEDEC fuse map: %s\n", path);
        return NULL;
    }
    assign_pin_labels(j, device, path);
    if (pin_file && !read_pin_file(j, device, pin_file))
        return NULL;
    return decoder;
}

/* Decode the fuse map and write the design as equations to OUT. */
static int write_design(FILE *out, const struct conversion *c, const struct decoder *decoder, const char *input_path)
{
    struct design *d = malloc(sizeof(*d));
    int ok           = d && decoder->decode(c, d);
    if (!d || (ok && d->overflow)) {
        fputs("E0100: Unknown internal error.\n", c->err);
        ok = 0;
    }
    if (ok && output_format == FORMAT_CUPL)
        ok = cupl_write(out, c->err, d, input_path);
    else if (ok)
        opal_write(out, d, input_path, alternate);
    free(d);
    return ok;
}

/* Write the equations to a temporary file and move it into place once it is
 * complete. Returns 1 on success. */
static int write_equation_file(const struct jedec *j, const char *device, const struct decoder *decoder,
                               const char *input_path, const char *output_path)
{
    char temporary_path[1080];
    /* Never replace the input, including through a hard link or symlink. */
    FILE *input       = fopen(input_path, "rb");
    int aliases_input = !strcmp(input_path, output_path) || (input && stream_matches_path(input, output_path));
    if (input)
        fclose(input);
    if (aliases_input) {
        fprintf(errors, "Refusing to overwrite the input file %s with equations.\n", input_path);
        return 0;
    }
    FILE *pins       = pin_file ? fopen(pin_file, "rb") : NULL;
    int aliases_pins = pins && stream_matches_path(pins, output_path);
    if (pins)
        fclose(pins);
    if (aliases_pins) {
        fprintf(errors, "Refusing to overwrite the pin file %s with equations.\n", pin_file);
        return 0;
    }
    FILE *output = open_equation_temporary(output_path, temporary_path, sizeof(temporary_path));
    if (!output) {
        fprintf(errors, "E0001: Could not open file '%s'.\n", output_path);
        return 0;
    }
    announce_device                    = device_library_key(device);
    announce_path                      = output_path;
    const struct conversion conversion = {
        .err    = errors,
        .device = device,
        .fuses  = j->fuse,
        .count  = j->count,
        .labels = (const label_t *)j->label,
    };
    int ok           = write_design(output, &conversion, decoder, input_path);
    int write_failed = ferror(output);
    if (fclose(output) == EOF)
        write_failed = 1;
    if (write_failed) {
        fprintf(errors, "Could not write output file '%s'.\n", output_path);
        ok = 0;
    } else if (ok && !publish_equations(temporary_path, output_path)) {
        fprintf(errors, "Could not replace output file '%s'.\n", output_path);
        ok = 0;
    }
    if (!ok)
        remove(temporary_path);
    return ok;
}

static int convert(const char *input_path, const char *output_path, const char *device_arg)
{
    const struct decoder *decoder = NULL;
    char device[80];
    size_t length;
    char *buf = read_file(input_path, &length);
    if (!buf) {
        fprintf(errors, "E0001: Could not open file '%s'.\n", input_path);
        return 0;
    }
    struct jedec *j = malloc(sizeof(*j));
    if (j)
        decoder = load_fuse_map(j, device, buf, length, device_arg, input_path);
    free(buf);
    int ok = decoder && write_equation_file(j, device, decoder, input_path, output_path);
    free(j);
    return ok;
}

static void usage(void)
{
    fputs(JED2PLD_BANNER("") "Usage:\n    jed2pld [Options] jedfile\n"
                             "\t    where jedfile is the JEDEC file to be disassembled.\n\n"
                             "Options:\n    -a           Select alternate operator set i.e.,\n"
                             "\t\t     ! = NOT, & = AND, # = OR and $ = XOR\n"
                             "    -d device    Override the device name in JEDEC file (if any).\n"
                             "    -e errfile   Specify the error log file for error messages.\n"
                             "    -o outfile   Specify the output file.\n"
                             "    -s           Shut off diagnostic messages.\n"
                             "    --pins file  Name pins from file, as name:pin entries.\n"
                             "    --format=cupl|opal\n"
                             "                 Write a WinCUPL source file (default) or OPAL equations.\n"
                             "    --help, --version\n"
                             "                 Show this text or the version.\n\n"
                             "Long forms: --alternate, --device, --error-file, --output, --silent.\n\n"
                             "Default extensions:\n"
                             "JEDEC file=jed          Output file=pld (eqn with --format=opal)\n"
                             "Error file=err\n",
          stdout);
}

int main(int argc, char **argv)
{
    struct options opt    = {.format = FORMAT_CUPL};
    char input_path[1024] = "", output_path[1024] = "", error_path[1024];
    if (argc == 1) {
        usage();
        return 1;
    }
    parse_command_line(argc, argv, &opt);
    if (opt.help && !opt.unknown && !opt.missing[0]) {
        usage();
        return 0;
    }
    if (opt.version && !opt.unknown && !opt.missing[0]) {
        fputs(JED2PLD_BANNER(""), stdout);
        return 0;
    }
    output_format         = opt.format;
    pin_file              = opt.pins;
    const char *extension = opt.format == FORMAT_CUPL ? ".pld" : ".eqn";
    if (opt.input)
        with_default_extension(input_path, sizeof(input_path), opt.input, ".jed");
    if (opt.output)
        with_default_extension(output_path, sizeof(output_path), opt.output, extension);
    else if (opt.input)
        replace_extension(output_path, sizeof(output_path), input_path, extension);
    errors = stderr;
    if (opt.error) {
        with_default_extension(error_path, sizeof(error_path), opt.error, ".err");
        if (open_error_log(error_path, input_path, output_path))
            return 2;
    }
    if (opt.unknown) {
        fprintf(errors, "E0012: Unknown option %s on command line.\n", opt.unknown);
        return finish(2);
    }
    if (opt.missing[0]) {
        fprintf(errors, "E0013: Expect an argument after %s option on command line.\n", opt.missing);
        return finish(2);
    }
    if (opt.bad_format) {
        fprintf(errors, "Unknown output format %s on command line; use opal or cupl.\n", opt.bad_format);
        return finish(2);
    }
    if (!opt.input) {
        fputs("E1801: Missing JEDEC filename on command line.\n", errors);
        return finish(2);
    }
    if (!silent)
        printf("\n" JED2PLD_BANNER("") "\nJEDEC file     : %s\n", input_path);
    return finish(convert(input_path, output_path, opt.device) ? 0 : 2);
}
