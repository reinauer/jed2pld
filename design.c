/* Building a design. */
#include "design.h"

void design_init(struct design *d, enum family family, const char *device)
{
    memset(d, 0, sizeof(*d));
    d->family = family;
    snprintf(d->device, sizeof(d->device), "%s", device);
}

void design_set_names(struct design *d, const label_t names[], int last)
{
    d->npins = last;
    for (int p = 1; p <= last; p++) {
        d->inverted[p] = label_is_inverted(names[p]);
        snprintf(d->name[p], MAX_LABEL, "%s", label_without_polarity(names[p]));
    }
}

int design_fuse_term(struct design *d, const unsigned char *fuses, int width, const int *column_pin,
                     const int *complement)
{
    struct literal lit[MAX_TERM_LITERALS];
    int count = 0;
    for (int c = 0; c < width && count < MAX_TERM_LITERALS; c++)
        if (!fuses[c]) {
            int pin              = column_pin[c / 2];
            lit[count].pin       = (unsigned char)pin;
            lit[count++].negated = (unsigned char)((c & 1) ^ (complement ? complement[pin] : 0));
        }
    return design_term(d, count, lit);
}

int design_term(struct design *d, int count, const struct literal *lit)
{
    if (d->nterms == MAX_TERMS) {
        d->overflow = 1;
        return 0;
    }
    d->term[d->nterms].count = count;
    if (count)
        memcpy(d->term[d->nterms].lit, lit, (size_t)count * sizeof(*lit));
    return d->nterms++;
}

struct sum design_sum(const struct design *d)
{
    struct sum s = {d->nentries, 0};
    return s;
}

void design_sum_add(struct design *d, struct sum *s, int term, int page)
{
    if (d->nentries == MAX_SUM_ENTRIES || s->first + s->count != d->nentries) {
        d->overflow = 1;
        return;
    }
    d->entry[d->nentries].term   = (short)term;
    d->entry[d->nentries++].page = (signed char)page;
    s->count++;
}

struct sum design_single(struct design *d, int term)
{
    struct sum s = design_sum(d);
    if (term >= 0)
        design_sum_add(d, &s, term, -1);
    return s;
}

struct equation *design_equation(struct design *d, int pin, enum equation_kind kind, int negated)
{
    static struct equation spare;
    struct equation *e;
    if (d->neq == MAX_EQUATIONS) {
        d->overflow = 1;
        return memset(&spare, 0, sizeof(spare));
    }
    e          = memset(&d->eq[d->neq++], 0, sizeof(*e));
    e->kind    = kind;
    e->pin     = pin;
    e->negated = negated;
    return e;
}

void design_break(struct design *d)
{
    design_equation(d, 0, EQ_BREAK, 0);
}
