/* The decoded logic of a fuse map, independent of any output language.
 *
 * Each device decoder fills a design from the fuse map; each output writer
 * prints one. A design states what the programmed part does: every signal is
 * a pin or node, every literal and equation carries its polarity at that pin,
 * and each equation says which part of the macrocell it drives.
 *
 * Pin names come from the file's labels or the default naming. A name marked
 * inverted ("/name" in the pin list) is the complement of the pin; writers
 * apply that to literals and to the targets of value equations. */
#ifndef JED2PLD_DESIGN_H
#define JED2PLD_DESIGN_H

#include "util.h"

/* Pool sizes, enough for the largest device: 132 terms of up to 80 literals
 * (MAPL, 22V10), 54 sums of up to 128 terms (MAPL). */
#define MAX_TERMS         256
#define MAX_TERM_LITERALS 80
#define MAX_SUM_ENTRIES   8192
#define MAX_EQUATIONS     256

enum family
{
    FAMILY_PAL,
    FAMILY_GAL_V8,
    FAMILY_GAL6001,
    FAMILY_V10,
    FAMILY_MAPL,
};

/* GAL16V8/20V8 architecture modes. */
enum gal_mode
{
    GAL_MODE_NONE,
    GAL_MODE_REGISTERED,
    GAL_MODE_SIMPLE,
    GAL_MODE_COMPLEX,
};

/* A pin or node, true or complemented, at the pin's electrical level. */
struct literal
{
    unsigned char pin, negated;
};

/* The AND of its literals; constant true without any. */
struct term
{
    int count;
    struct literal lit[MAX_TERM_LITERALS];
};

/* The OR of COUNT terms listed from FIRST in the design's entries; constant
 * false when empty. */
struct sum
{
    int first, count;
};

/* A term of a sum. On a paged device it applies only while the page
 * registers hold PAGE, otherwise PAGE is -1. */
struct sum_entry
{
    short term;
    signed char page;
};

enum equation_kind
{
    EQ_COMBINATORIAL, /* the output value */
    EQ_REGISTERED,    /* the D input of the register */
    EQ_J,             /* JK register inputs */
    EQ_K,
    EQ_CLOCK,
    EQ_CLOCK_ENABLE,
    EQ_RESET,       /* asynchronous reset */
    EQ_PRESET,      /* asynchronous preset */
    EQ_SYNC_PRESET, /* synchronous preset */
    EQ_OUTPUT_ENABLE,
    EQ_BREAK, /* ends a group of related equations; no logic */
};

/* PIN's KIND input is SUM, complemented when NEGATED. SUM is XORed with XOR_SUM
 * and ANDed with the GATE pin, when GATE.pin is not 0. PIN 0 is the whole
 * device, for shared reset and preset terms. A FIXED equation is wired in the
 * device rather than programmed, such as an output enable that is always the
 * /OE pin. */
struct equation
{
    enum equation_kind kind;
    int pin, negated, fixed;
    struct sum sum, xor_sum;
    struct literal gate;
};

/* Input pins that pass through a register or latch, set per group. */
struct input_group
{
    int latched, count, pin[12];
};

struct design
{
    enum family family;
    char device[16];  /* canonical part name, e.g. "PAL16R8", "GAL22V10" */
    int npins;        /* pins and nodes are numbered 1..npins */
    int package_pins; /* pins 1..package_pins are package pins, the rest nodes */
    label_t name[MAX_PINS];
    int inverted[MAX_PINS];

    struct term term[MAX_TERMS];
    int nterms;
    struct sum_entry entry[MAX_SUM_ENTRIES];
    int nentries;
    struct equation eq[MAX_EQUATIONS];
    int neq;
    int overflow; /* a pool was too small; the design is incomplete */

    /* Device configuration that is not an equation. */
    const unsigned char *ues; /* user electronic signature, most significant first */
    int ues_bits;
    enum gal_mode gal_mode;
    int ptd_unused; /* inactive terms have their product term disable fuse set */
    struct input_group input[2];
    int ninputs;
    int page_pin[3]; /* the page register driving each page bit */
    int pages_used;  /* bit N: some term needs page N */
    /* Registered outputs without equations whose register the logic reads
     * all the same: 1 when active high, -1 when active low. */
    signed char idle_register[MAX_PINS];
};

void design_init(struct design *d, enum family family, const char *device);

/* Copy pin names 1..LAST, splitting off a leading '/' as inversion. */
void design_set_names(struct design *d, const label_t names[], int last);

/* Add the product term of the WIDTH fuses at FUSES: a literal for each cleared
 * fuse, of pin COLUMN_PIN[c / 2], complemented for odd c and, when given, for
 * the pins marked in COMPLEMENT. Returns the term number. */
int design_fuse_term(struct design *d, const unsigned char *fuses, int width, const int *column_pin,
                     const int *complement);

/* Add a term of the given literals; with none it is constant true. */
int design_term(struct design *d, int count, const struct literal *lit);

/* Start an empty sum, then add terms to it. Only the newest sum may grow. */
struct sum design_sum(const struct design *d);
void design_sum_add(struct design *d, struct sum *s, int term, int page);

/* A sum of the single TERM, or of nothing when TERM is -1. */
struct sum design_single(struct design *d, int term);

/* Append an equation; its sums start empty and its gate unused. */
struct equation *design_equation(struct design *d, int pin, enum equation_kind kind, int negated);

void design_break(struct design *d);

static inline const struct sum_entry *sum_entry(const struct design *d, const struct sum *s, int i)
{
    return &d->entry[s->first + i];
}

#endif
