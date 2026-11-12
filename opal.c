/* OPAL equation writer.
 *
 * The listing follows the conventions of the DOS JED2EQN, which differ a
 * little between device families. Those differences are collected in
 * struct style. */
#include "opal.h"
#include "banner.h"
#include "output.h"

/* Operator spellings: the default set, or the alternate set selected by -a. */
static const char *op_not(int alt)
{
    return alt ? "!" : "/";
}

static int op_and(int alt)
{
    return alt ? '&' : '*';
}

static int op_or(int alt)
{
    return alt ? '#' : '+';
}

/* Writes the literals of one product term, joined by AND and wrapped after
 * column 60. Start with {out, alt} and call term_literal for each literal;
 * LITERALS counts them. */
struct term_writer
{
    FILE *out;
    int alt, col, literals;
};

/* The banner, the "Disassembled from" line, an optional DIRECTIVE line and
 * the "chip <name> <type>" line. The chip name is INPUT without directory or
 * extension. */
static void write_equation_header(FILE *out, const char *input, const char *directive, const char *chip_type)
{
    char stem[1024];
    int month, day, year;
    fputs(JED2PLD_BANNER("; "), out);
    if (today(&month, &day, &year))
        fprintf(out, "; Disassembled from %s. Date: %d-%d-%d\n", input, month, day, year);
    else
        fprintf(out, "; Disassembled from %s.\n", input);
    if (directive)
        fprintf(out, "%s\n", directive);
    file_stem(stem, sizeof(stem), input);
    fprintf(out, "\nchip %s %s\n\n", stem, chip_type);
}

/* One "name=pin" entry of the pin list, wrapping after column 60. *COL
 * tracks the current column and starts at 0. */
static void write_pin(FILE *out, int *col, int alt, int negated, const char *name, int pin)
{
    if (*col > 60) {
        fputs("\n", out);
        *col = 0;
    }
    *col += fprintf(out, "%s%s=%d ", negated ? op_not(alt) : "", name, pin);
}

/* "@ues" and the user electronic signature as NIBBLES hex digits, most
 * significant fuse first. */
static void write_ues(FILE *out, const unsigned char *fuses, int nibbles)
{
    fputs("@ues ", out);
    for (int i = 0; i < nibbles; i++)
        fprintf(out, "%x", fuses[4 * i] << 3 | fuses[4 * i + 1] << 2 | fuses[4 * i + 2] << 1 | fuses[4 * i + 3]);
    fputs("\n", out);
}

static void term_literal(struct term_writer *t, int negated, const char *name)
{
    if (t->col > 60) {
        fputs("\n     ", t->out);
        t->col = 0;
    }
    if (t->literals++) {
        fprintf(t->out, " %c ", op_and(t->alt));
        t->col += 3;
    }
    if (negated) {
        fputs(op_not(t->alt), t->out);
        t->col++;
    }
    fputs(name, t->out);
    t->col += (int)strlen(name);
}

struct style
{
    const char *global_reset; /* name of a shared reset equation */
    int dot_d;                /* registers are written "name.d =", not "name :=" */
    int slash;                /* '/' marks complements on equation targets and gates even with -a */
    int disabled_is_gnd;      /* an output that is never enabled is written "= gnd" */
};

struct opal
{
    FILE *out;
    const struct design *d;
    int alt;
    struct style style;
    char page_prefix[80];
};

static struct style style_of(enum family family)
{
    struct style s = {"global.re", 0, 0, 0};
    if (family == FAMILY_GAL6001)
        s.global_reset = "global.ar";
    if (family == FAMILY_MAPL)
        s.dot_d = s.slash = 1;
    if (family == FAMILY_V10)
        s.disabled_is_gnd = 1;
    return s;
}

static const char *complement(const struct opal *o, int negated)
{
    return !negated ? "" : o->style.slash ? "/" : op_not(o->alt);
}

/* Page names are "p0".."p7", with more p's until no pin name starts with the
 * prefix. */
static void choose_page_prefix(struct opal *o)
{
    strcpy(o->page_prefix, "p");
    for (int n = 1; n < 78; n++) {
        int collision = 0;
        for (int p = 1; p <= o->d->npins && !collision; p++)
            collision = o->d->name[p][0] && !strncmp(o->d->name[p], o->page_prefix, n);
        if (!collision)
            break;
        o->page_prefix[n]     = 'p';
        o->page_prefix[n + 1] = 0;
    }
}

static void write_page_defines(const struct opal *o)
{
    const struct design *d = o->d;
    for (int r = 0; r < 8; r++)
        if (d->pages_used & 1 << r) {
            fprintf(o->out, "@define %s%d \"", o->page_prefix, r);
            for (int c = 2; c >= 0; c--) {
                int p = d->page_pin[c];
                if (c < 2)
                    fputc('*', o->out);
                fprintf(o->out, "%s%s", (!(r & 1 << c) ^ d->inverted[p]) ? "/" : " ", d->name[p]);
            }
            fputs("\"\n", o->out);
        }
}

static void write_header(struct opal *o, const char *input)
{
    static const char *const modes[] = {NULL, ";$GALMODE REGISTERED", ";$GALMODE SMALL", ";$GALMODE MEDIUM"};
    const struct design *d           = o->d;
    int col = 0, directives = 0;
    write_equation_header(o->out, input, modes[d->gal_mode], d->device);
    for (int p = 1; p <= d->npins; p++)
        if (d->name[p][0])
            write_pin(o->out, &col, o->alt, d->inverted[p], d->name[p], p);
    fputs("\n\n", o->out);
    if (d->family == FAMILY_MAPL) {
        choose_page_prefix(o);
        write_page_defines(o);
        fputs("\n", o->out);
        if (d->ues)
            fputs("\n", o->out);
    }
    if (d->ues) {
        write_ues(o->out, d->ues, d->ues_bits / 4);
        directives = 1;
    }
    if (d->ptd_unused) {
        fputs("@ptd unused\n", o->out);
        directives = 1;
    }
    for (int g = 0; g < d->ninputs; g++) {
        const struct input_group *in = &d->input[g];
        fprintf(o->out, "@%s %d", in->latched ? "ilch" : "ireg", in->count);
        for (int i = 0; i < in->count; i++)
            fprintf(o->out, " %s", d->name[in->pin[i]]);
        fputs("\n", o->out);
        directives = 1;
    }
    if (directives || d->family == FAMILY_MAPL)
        fputs("\n", o->out);
    fputs("equations\n\n", o->out);
}

/* One product term, wrapped after column 60, and the page it applies in. */
static void write_term(const struct opal *o, const struct sum_entry *e)
{
    const struct design *d = o->d;
    const struct term *t   = &d->term[e->term];
    struct term_writer w   = {o->out, o->alt, 0, 0};
    for (int i = 0; i < t->count; i++)
        term_literal(&w, t->lit[i].negated ^ d->inverted[t->lit[i].pin], d->name[t->lit[i].pin]);
    if (e->page >= 0) {
        if (w.literals)
            fprintf(o->out, " %c ", op_and(o->alt));
        fprintf(o->out, "%s%d", o->page_prefix, e->page);
    } else if (!w.literals)
        fputs("vcc", o->out);
}

/* The terms of S, one per line, the first after FIRST and the others after
 * an OR. */
static void write_terms(const struct opal *o, const struct sum *s, const char *first)
{
    for (int i = 0; i < s->count; i++) {
        if (i)
            fprintf(o->out, "    %c ", op_or(o->alt));
        else
            fputs(first, o->out);
        write_term(o, sum_entry(o->d, s, i));
        fputs("\n", o->out);
    }
}

static void write_target(const struct opal *o, const struct equation *e)
{
    static const char *const suffix[] = {
        [EQ_J] = "j",      [EQ_K] = "k",       [EQ_CLOCK] = "c",        [EQ_CLOCK_ENABLE] = "ce",
        [EQ_RESET] = "re", [EQ_PRESET] = "pr", [EQ_SYNC_PRESET] = "sp", [EQ_OUTPUT_ENABLE] = "oe",
    };
    const struct design *d = o->d;
    const char *name       = d->name[e->pin];
    enum equation_kind k   = e->kind;
    int neg                = e->negated;
    if (!e->pin) {
        fprintf(o->out, "%s = ", k == EQ_RESET ? o->style.global_reset : k == EQ_SYNC_PRESET ? "global.sp" : "global");
        return;
    }
    /* A target named by its complement takes the complemented value; the J and
     * K inputs of its register swap roles. */
    if (k == EQ_COMBINATORIAL || k == EQ_REGISTERED)
        neg ^= d->inverted[e->pin];
    if ((k == EQ_J || k == EQ_K) && d->inverted[e->pin])
        k = k == EQ_J ? EQ_K : EQ_J;
    if (k == EQ_COMBINATORIAL)
        fprintf(o->out, "%s%s = ", complement(o, neg), name);
    else if (k == EQ_REGISTERED && !o->style.dot_d)
        fprintf(o->out, "%s%s := ", complement(o, neg), name);
    else
        fprintf(o->out, "%s%s.%s = ", complement(o, neg), name, k == EQ_REGISTERED ? "d" : suffix[k]);
}

static int never_enabled(const struct design *d, int pin)
{
    for (int i = 0; i < d->neq; i++)
        if (d->eq[i].pin == pin && d->eq[i].kind == EQ_OUTPUT_ENABLE)
            return d->eq[i].sum.count == 0;
    return 0;
}

/* The sum ANDed with a pin: gnd, the pin alone, or a single term and the
 * pin. The AND is printed as '*' even with -a. */
static void write_gated(const struct opal *o, const struct equation *e)
{
    const struct design *d = o->d;
    int pin = e->gate.pin, neg = e->gate.negated ^ d->inverted[pin];
    if (!e->sum.count) {
        fputs("gnd\n", o->out);
        return;
    }
    if (d->term[sum_entry(d, &e->sum, 0)->term].count) {
        write_term(o, sum_entry(d, &e->sum, 0));
        fputs(" * ", o->out);
    }
    fprintf(o->out, "%s%s\n", complement(o, neg), d->name[pin]);
}

static void write_equation(const struct opal *o, const struct equation *e)
{
    if (e->kind == EQ_BREAK) {
        fputs("\n", o->out);
        return;
    }
    write_target(o, e);
    if (e->gate.pin) {
        write_gated(o, e);
        return;
    }
    if (!e->sum.count || (o->style.disabled_is_gnd && e->kind <= EQ_REGISTERED && never_enabled(o->d, e->pin)))
        fputs("gnd\n", o->out);
    else
        write_terms(o, &e->sum, "");
    write_terms(o, &e->xor_sum, o->alt ? "  $ " : "  :+: ");
}

void opal_write(FILE *out, const struct design *d, const char *input, int alt)
{
    struct opal o = {out, d, alt, style_of(d->family), ""};
    write_header(&o, input);
    for (int i = 0; i < d->neq; i++)
        write_equation(&o, &d->eq[i]);
}
