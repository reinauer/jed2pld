/* WinCUPL source writer.
 *
 * The devices are those of WinCUPL's cupl.dl library. CUPL places product
 * terms itself, so equations are written as plain sums of products, and
 * wiring that the device fixes, such as the /OE pin of a registered output,
 * is left out. */
#include "cupl.h"
#include "banner.h"
#include "output.h"
#include <ctype.h>

#define MAX_NAME 32

struct cupl
{
    FILE *out;
    const struct design *d;
    int used[MAX_PINS];          /* the pin appears in an equation */
    int registered[MAX_PINS];    /* the pin has a D register equation */
    int combinational[MAX_PINS]; /* the pin has a combinational equation */
    const char *input_suffix[MAX_PINS];
    char name[MAX_PINS][MAX_NAME];
    int col;
};

/* PAL table devices that cupl.dl knows, by their jed2pld names. */
static const char *const pal_devices[] = {
    "PAL10H8",  "PAL10L8",  "PAL10P8",   "PAL12H6",   "PAL12L10",  "PAL12L6",  "PAL12P10", "PAL12P6",  "PAL14H4",
    "PAL14L4",  "PAL14L8",  "PAL14P4",   "PAL14P8",   "PAL16C1",   "PAL16H2",  "PAL16H8",  "PAL16L2",  "PAL16L6",
    "PAL16L8",  "PAL16P2",  "PAL16P6",   "PAL16P8",   "PAL16R4",   "PAL16R6",  "PAL16R8",  "PAL16RA8", "PAL16RP4",
    "PAL16RP6", "PAL16RP8", "PAL18L4",   "PAL18P4",   "PAL20C1",   "PAL20L10", "PAL20L2",  "PAL20L8",  "PAL20P2",
    "PAL20P8",  "PAL20R4",  "PAL20R6",   "PAL20R8",   "PAL20RA10", "PAL20RP4", "PAL20RP6", "PAL20RP8", "PAL20X10",
    "PAL20X4",  "PAL20X8",  "PAL1016P8", "GAL20RA10",
};

static void lower(char *dst, size_t size, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < size; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = 0;
}

/* The cupl.dl device name for D, or 0 if there is none. */
static int device_name(const struct design *d, char *dst, size_t size)
{
    static const char *const modes[] = {"", "ms", "s", "ma"}; /* by enum gal_mode */
    const char *dev                  = d->device;
    int plcc                         = dev[strlen(dev) - 1] == 'V';
    switch (d->family) {
    case FAMILY_GAL_V8:
        snprintf(dst, size, "g%sv8%s%s", strstr(dev, "20V8") ? "20" : "16", modes[d->gal_mode], plcc ? "lcc" : "");
        return 1;
    case FAMILY_V10:
        /* The 22CV10 has the 22V10 fuse map without a user signature. */
        snprintf(dst, size, "%s22v10%s", strncmp(dev, "GAL22V10", 8) ? "p" : "g", plcc ? "lcc" : "");
        return 1;
    case FAMILY_GAL6001:
        snprintf(dst, size, "g6001%s", plcc ? "lcc" : "");
        return 1;
    case FAMILY_PAL:
        for (size_t i = 0; i < sizeof(pal_devices) / sizeof(pal_devices[0]); i++)
            if (!strcmp(dev, pal_devices[i])) {
                lower(dst, size, dev + 2);
                dst[0] = dev[0] == 'G' ? 'g' : 'p';
                return 1;
            }
        return 0;
    case FAMILY_MAPL:
        /* CUPL compiles MAPL designs only through National's mapl2jed
         * fitter, which WinCUPL does not include. */
        return 0;
    }
    return 0;
}

/* The CUPL number of pin or node P. */
static int cupl_pin(const struct design *d, int p)
{
    /* CUPL numbers the GAL6001 buried macrocells in the opposite order. */
    int offset = d->package_pins - 24;
    if (d->family == FAMILY_GAL6001 && p >= 25 + offset && p <= 32 + offset)
        return 57 + 2 * offset - p;
    return p;
}

static int is_reserved(const char *name)
{
    static const char *const words[] = {
        "append",  "assembly", "assy",     "b",         "company",    "condition",  "d",         "date",
        "declare", "default",  "demorgan", "designer",  "device",     "exit",       "extern",    "field",
        "fld",     "format",   "function", "global",    "group",      "grp",        "h",         "if",
        "init_to", "loc",      "location", "min",       "msg",        "name",       "next",      "node",
        "o",       "out",      "partno",   "pin",       "pinnode",    "present",    "property",  "register_select",
        "rev",     "revision", "sequence", "sequenced", "sequencejk", "sequencers", "sequencet", "table",
        "troff",   "tron",
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        if (!strcasecmp(name, words[i]))
            return 1;
    return 0;
}

static int is_identifier(const char *name)
{
    if (!isalpha((unsigned char)*name) || strlen(name) >= MAX_NAME)
        return 0;
    for (const char *s = name; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '_')
            return 0;
    return !is_reserved(name);
}

static int never_true(const struct term *t);

static void mark_sum(struct cupl *c, const struct sum *s)
{
    for (int i = 0; i < s->count; i++) {
        const struct term *t = &c->d->term[sum_entry(c->d, s, i)->term];
        for (int k = 0; k < t->count && !never_true(t); k++)
            c->used[t->lit[k].pin] = 1;
    }
}

/* Find the pins in use and give each a CUPL name: its own when that is a
 * valid identifier not taken by another pin, otherwise one made from its
 * number. */
static void choose_names(struct cupl *c)
{
    const struct design *d = c->d;
    for (int i = 0; i < d->neq; i++) {
        const struct equation *e = &d->eq[i];
        if (e->kind == EQ_BREAK || e->fixed)
            continue;
        c->used[e->pin] = 1;
        c->registered[e->pin] |= e->kind == EQ_REGISTERED;
        c->combinational[e->pin] |= e->kind == EQ_COMBINATORIAL;
        mark_sum(c, &e->sum);
        mark_sum(c, &e->xor_sum);
    }
    for (int g = 0; g < d->ninputs; g++)
        for (int i = 0; i < d->input[g].count; i++)
            c->input_suffix[d->input[g].pin[i]] = d->input[g].latched ? ".lq" : ".dq";
    /* On the GAL6001 the name of a registered output reads its register, a
     * different column from the pin. */
    if (d->family == FAMILY_GAL6001)
        for (int p = 1; p <= d->package_pins; p++)
            if (c->registered[p] && !c->input_suffix[p])
                c->input_suffix[p] = ".io";
    for (int p = 1; p <= d->npins; p++) {
        if (!c->used[p])
            continue;
        if (is_identifier(d->name[p]))
            snprintf(c->name[p], MAX_NAME, "%s", d->name[p]);
        else
            snprintf(c->name[p], MAX_NAME, "%s%d", p > d->package_pins ? "node" : "pin", cupl_pin(d, p));
        for (int q = 1; q < p; q++)
            if (c->used[q] && !strcasecmp(c->name[p], c->name[q])) {
                snprintf(c->name[p], MAX_NAME, "%s%d", p > d->package_pins ? "node" : "pin", cupl_pin(d, p));
                break;
            }
    }
}

/* The user signature as PartNo text, if it is printable characters followed
 * by zero bytes; CUPL stores PartNo in the signature. */
static int signature_text(const struct design *d, char *dst, size_t size)
{
    int bytes = d->ues_bits / 8, len = 0, end = 0, ok = 1;
    for (int i = 0; i < bytes && ok; i++) {
        int b = 0;
        for (int k = 0; k < 8; k++)
            b = b << 1 | d->ues[8 * i + k];
        if (!b) {
            end = 1;
            continue;
        }
        /* CUPL trims spaces, and a '*' would end the JEDEC header field. */
        ok = !end && b >= 0x20 && b <= 0x7e && b != ';' && b != '*' && (b != ' ' || len) && (size_t)len + 1 < size;
        if (ok)
            dst[len++] = (char)b;
    }
    if (len && dst[len - 1] == ' ')
        ok = 0;
    dst[ok ? len : 0] = 0;
    return ok;
}

static void write_header(const struct cupl *c, const char *input, const char *device)
{
    const struct design *d = c->d;
    char stem[1024], partno[16] = "";
    int month = 1, day = 1, year = 1980, known = today(&month, &day, &year);
    file_stem(stem, sizeof(stem), input);
    for (char *s = stem; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-')
            *s = '_';
    fputs("/*\n" JED2PLD_BANNER(" * ") " * Disassembled from ", c->out);
    for (const char *s = input; *s; s++)
        fputs(s[0] == '*' && s[1] == '/' ? "* " : (char[]){*s, 0}, c->out);
    fputs(".\n */\n\n", c->out);
    if (d->ues && !signature_text(d, partno, sizeof(partno))) {
        fputs("/* The user signature @ues ", c->out);
        for (int i = 0; i < d->ues_bits / 4; i++)
            fprintf(c->out, "%x",
                    d->ues[4 * i] << 3 | d->ues[4 * i + 1] << 2 | d->ues[4 * i + 2] << 1 | d->ues[4 * i + 3]);
        fputs(" is not text, so PartNo cannot reproduce it. */\n", c->out);
    }
    fprintf(c->out, "Name     %s;\n", *stem ? stem : "jed2pld");
    fprintf(c->out, "PartNo   %s;\n", partno);
    if (known)
        fprintf(c->out, "Date     %d/%d/%d;\n", month, day, year);
    else
        fputs("Date     ;\n", c->out);
    fprintf(c->out, "Revision 01;\nDesigner None;\nCompany  None;\nAssembly None;\nLocation None;\nDevice   %s;\n\n",
            device);
}

static void write_pins(const struct cupl *c)
{
    const struct design *d = c->d;
    for (int nodes = 0; nodes < 2; nodes++)
        for (int p = 1; p <= d->npins; p++)
            if (c->used[p] && (p > d->package_pins) == nodes) {
                fprintf(c->out, "%s %d = %s%s;", nodes ? "PINNODE" : "PIN", cupl_pin(d, p), d->inverted[p] ? "!" : "",
                        c->name[p]);
                if (strcmp(c->name[p], d->name[p]))
                    fprintf(c->out, " /* %s */", d->name[p]);
                fputs("\n", c->out);
            }
    fputs("\n", c->out);
}

/* Write TEXT, starting a new indented line first when the current one is
 * full. */
static void put(struct cupl *c, const char *text)
{
    if (c->col + (int)strlen(text) > 76) {
        fputs("\n        ", c->out);
        c->col = 8;
    }
    c->col += fprintf(c->out, "%s", text);
}

static void write_term(struct cupl *c, const struct term *t)
{
    char literal[MAX_NAME + 8];
    if (!t->count)
        put(c, "'b'1");
    for (int i = 0; i < t->count; i++) {
        int p = t->lit[i].pin;
        snprintf(literal, sizeof(literal), "%s%s%s%s", i ? "& " : "", t->lit[i].negated ^ c->d->inverted[p] ? "!" : "",
                 c->name[p], c->input_suffix[p] ? c->input_suffix[p] : "");
        put(c, literal);
        if (i + 1 < t->count)
            put(c, " ");
    }
}

/* A term that contains a signal and its complement is never true. */
static int never_true(const struct term *t)
{
    for (int i = 0; i < t->count; i++)
        for (int k = 0; k < i; k++)
            if (t->lit[i].pin == t->lit[k].pin && t->lit[i].negated != t->lit[k].negated)
                return 1;
    return 0;
}

static void write_sum(struct cupl *c, const struct sum *s)
{
    int written = 0;
    for (int i = 0; i < s->count; i++) {
        const struct term *t = &c->d->term[sum_entry(c->d, s, i)->term];
        if (never_true(t))
            continue;
        if (written++) {
            fputs("\n    # ", c->out);
            c->col = 6;
        }
        write_term(c, t);
    }
    if (!written)
        put(c, "'b'0");
}

/* A GAL6001 output macrocell without an output pin is one of the nodes after
 * the buried macrocells. CUPL takes the polarity of its equation the other
 * way round from the listing, which shows the polarity of the output. */
static int g6001_output_node(const struct design *d, int pin)
{
    int offset = d->package_pins - 24;
    return d->family == FAMILY_GAL6001 && pin >= 33 + offset && pin <= 42 + offset;
}

static void write_assignment(struct cupl *c, const struct equation *e, int pin)
{
    static const char *const extension[] = {
        [EQ_COMBINATORIAL] = "",  [EQ_REGISTERED] = ".d",     [EQ_J] = ".j",      [EQ_K] = ".k",
        [EQ_CLOCK] = ".ck",       [EQ_CLOCK_ENABLE] = ".ce",  [EQ_RESET] = ".ar", [EQ_PRESET] = ".ap",
        [EQ_SYNC_PRESET] = ".sp", [EQ_OUTPUT_ENABLE] = ".oe",
    };
    const struct design *d = c->d;
    int neg                = e->negated;
    if (e->kind == EQ_COMBINATORIAL || e->kind == EQ_REGISTERED)
        neg ^= d->inverted[pin] ^ g6001_output_node(d, pin);
    c->col = fprintf(c->out, "%s%s%s = ", neg ? "!" : "", c->name[pin], extension[e->kind]);
    if (e->xor_sum.count) {
        put(c, "(");
        write_sum(c, &e->sum);
        fputs(")\n    $ (", c->out);
        c->col = 7;
        write_sum(c, &e->xor_sum);
        put(c, ")");
    } else
        write_sum(c, &e->sum);
    fputs(";\n", c->out);
}

static void write_equation(struct cupl *c, const struct equation *e)
{
    const struct design *d = c->d;
    int any                = 0;
    if (e->kind == EQ_BREAK) {
        fputs("\n", c->out);
        return;
    }
    if (e->fixed)
        return;
    /* Holding both reset and preset bypasses an asynchronous register; CUPL
     * sets them itself for a combinational output. */
    if (e->pin && c->combinational[e->pin] && (e->kind == EQ_RESET || e->kind == EQ_PRESET))
        return;
    if (e->pin) {
        write_assignment(c, e, e->pin);
        return;
    }
    /* A shared reset or preset is written for every register. */
    for (int p = 1; p <= d->npins; p++)
        if (c->registered[p]) {
            write_assignment(c, e, p);
            any = 1;
        }
    if (!any)
        fprintf(c->out, "/* The shared %s term acts on no register. */\n", e->kind == EQ_RESET ? "reset" : "preset");
}

/* In simple mode CUPL makes every output pin that the logic reads into an
 * input, dropping its equation: compilers route simple-mode feedback through
 * the adjacent pins and never read back an output. */
static void warn_simple_feedback(const struct cupl *c, FILE *err)
{
    const struct design *d = c->d;
    int read[MAX_PINS] = {0}, any = 0;
    if (d->family != FAMILY_GAL_V8 || d->gal_mode != GAL_MODE_SIMPLE)
        return;
    for (int i = 0; i < d->neq; i++) {
        const struct sum *s = &d->eq[i].sum;
        for (int k = 0; k < s->count; k++) {
            const struct term *t = &d->term[sum_entry(d, s, k)->term];
            for (int l = 0; l < t->count && !never_true(t); l++)
                read[t->lit[l].pin] = 1;
        }
    }
    for (int i = 0; i < d->neq; i++) {
        int p = d->eq[i].pin;
        if (d->eq[i].kind == EQ_COMBINATORIAL && read[p]) {
            if (!any++)
                fputs("/* Simple mode cannot read back an output; CUPL makes these pins inputs:", c->out);
            fprintf(c->out, " %d", p);
            read[p] = 0;
        }
    }
    if (any) {
        fputs(" */\n\n", c->out);
        fprintf(err, "Warning: simple-mode outputs that the logic reads back become inputs in CUPL.\n");
    }
}

int cupl_write(FILE *out, FILE *err, const struct design *d, const char *input)
{
    struct cupl c = {0};
    char device[24];
    if (!device_name(d, device, sizeof(device))) {
        fprintf(err, "The %s has no WinCUPL device; write OPAL equations instead.\n", d->device);
        return 0;
    }
    c.out = out;
    c.d   = d;
    choose_names(&c);
    write_header(&c, input, device);
    warn_simple_feedback(&c, err);
    write_pins(&c);
    for (int i = 0; i < d->neq; i++)
        write_equation(&c, &d->eq[i]);
    /* Without an equation, CUPL would take the pin for an input. */
    for (int p = 1; p <= d->npins; p++)
        if (d->idle_register[p] && c.used[p])
            fprintf(out, "\n/* No terms; the logic reads its register. */\n%s%s.d = 'b'0;\n",
                    (d->idle_register[p] < 0) ^ d->inverted[p] ? "!" : "", c.name[p]);
    return 1;
}
