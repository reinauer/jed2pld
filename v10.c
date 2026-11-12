/* 22V10 and 22CV10 decoder. */
#include "v10.h"

/* 132 product terms of 44 fuses (22 input columns): the asynchronous reset,
 * the terms of the ten outputs, the first of each being its output enable,
 * and the synchronous preset. Then two configuration fuses per output, S0
 * (active high) and S1 (combinational), and the 64-bit user signature. */
enum
{
    V10_WIDTH = 44,
    V10_TERMS = 132,
    V10_AR    = 0,
    V10_SP    = 131,
    V10_S0    = 5808,
    V10_UES   = 5828,
};

/* First term and number of terms of each output, including the enable. */
static const int first_term[] = {1, 10, 21, 34, 49, 66, 83, 98, 111, 122};
static const int term_count[] = {9, 11, 13, 15, 17, 17, 15, 13, 11, 9};

struct v10
{
    const unsigned char *f;
    int plcc, pins, clk;
    int columns[22];      /* pin of each input column pair */
    int outputs[10];      /* pin of each output */
    int state[V10_TERMS]; /* each term: 0 inactive, 1 constant high, -1 literals */
    int used[MAX_PINS];   /* the pin is an input to an active term */
    int driven[MAX_PINS]; /* the pin is an enabled or configured output */
    label_t names[MAX_PINS];
};

int v10_supports_device(const char *d)
{
    return !strcmp(d, "PAL22V10") || !strcmp(d, "GAL22CV10") || !strcmp(d, "GAL22CV10V") || !strcmp(d, "G22V10") ||
           !strcmp(d, "G22V10V");
}

static int s0(const struct v10 *v, int output)
{
    return v->f[V10_S0 + 2 * output];
}

static int s1(const struct v10 *v, int output)
{
    return v->f[V10_S0 + 2 * output + 1];
}

/* Classify every term and find the used inputs and driven outputs. */
static void analyse(struct v10 *v)
{
    for (int c = 0; c < 22; c++) {
        int p         = c % 2 ? 23 - c / 2 : 1 + c / 2;
        v->columns[c] = v->plcc ? dip24_to_plcc28_pin(p) : p;
    }
    for (int i = 0; i < 10; i++)
        v->outputs[i] = v->plcc ? dip24_to_plcc28_pin(23 - i) : 23 - i;
    for (int r = 0; r < V10_TERMS; r++) {
        int ones = 0;
        for (int c = 0; c < V10_WIDTH; c++)
            ones += v->f[r * V10_WIDTH + c];
        v->state[r] = ones == 0 ? 0 : ones == V10_WIDTH ? 1 : -1;
        if (ones)
            for (int c = 0; c < V10_WIDTH; c++)
                if (!v->f[r * V10_WIDTH + c])
                    v->used[v->columns[c / 2]] = 1;
    }
    for (int i = 0; i < 10; i++)
        v->driven[v->outputs[i]] = v->state[first_term[i]] || s0(v, i) || s1(v, i);
}

static void name_pins(struct v10 *v)
{
    for (int p = 1; p <= v->pins; p++)
        if (v->used[p])
            snprintf(v->names[p], MAX_LABEL, "i%d", p);
    for (int i = 0; i < 10; i++) {
        int p = v->outputs[i];
        if (v->used[p] || v->driven[p])
            snprintf(v->names[p], MAX_LABEL, "%c%d", v->used[p] ? 'f' : 'o', p);
    }
    if (!v->used[v->clk])
        strcpy(v->names[v->clk], "CLK");
    strcpy(v->names[v->plcc ? 14 : 12], "GND");
    strcpy(v->names[v->pins], "VCC");
    if (v->plcc)
        for (int p = 1; p <= 28; p += 7)
            strcpy(v->names[p], "NC");
}

/* Replace the default names with the labels from the file. Two pins may not
 * share a name when both are used and either is a driven output. */
static int apply_labels(struct v10 *v, const label_t *labels, FILE *err)
{
    int participates[MAX_PINS] = {0}, duplicates = 0;
    for (int p = 1; p <= v->pins; p++)
        participates[p] = v->used[p] || v->driven[p] || p == v->clk;
    for (int p = 1; p <= v->pins; p++)
        if (labels[p][0])
            strcpy(v->names[p], labels[p]);
    for (int p = 1; p <= v->pins; p++)
        if (participates[p])
            for (int q = 1; q < p; q++)
                if (participates[q] && (v->driven[p] || v->driven[q]) &&
                    !strcmp(label_without_polarity(v->names[p]), label_without_polarity(v->names[q]))) {
                    fprintf(err, "E1803: Duplicate label '%s' found on pins %d and %d.\n",
                            label_without_polarity(v->names[p]), p, q);
                    duplicates++;
                }
    return duplicates == 0;
}

static void build_equations(const struct v10 *v, struct design *d)
{
    int term[V10_TERMS], complemented[MAX_PINS] = {0};
    struct sum sum;
    /* A registered output feeds back from its register's complement, so for an
     * active-high one the array sees the complement of the pin. */
    for (int i = 0; i < 10; i++)
        complemented[v->outputs[i]] = s0(v, i) && !s1(v, i);
    for (int r = 0; r < V10_TERMS; r++)
        term[r] = v->state[r] ? design_fuse_term(d, v->f + r * V10_WIDTH, V10_WIDTH, v->columns, complemented) : -1;
    if (v->state[V10_AR]) {
        sum                                     = design_single(d, term[V10_AR]);
        design_equation(d, 0, EQ_RESET, 0)->sum = sum;
    }
    if (v->state[V10_SP]) {
        sum                                           = design_single(d, term[V10_SP]);
        design_equation(d, 0, EQ_SYNC_PRESET, 0)->sum = sum;
    }
    design_break(d);
    for (int i = 0; i < 10; i++) {
        int p = v->outputs[i], r = first_term[i];
        if (!v->driven[p])
            continue;
        sum = design_sum(d);
        for (int t = 1; t < term_count[i]; t++)
            if (v->state[r + t])
                design_sum_add(d, &sum, term[r + t], -1);
        design_equation(d, p, s1(v, i) ? EQ_COMBINATORIAL : EQ_REGISTERED, !s0(v, i))->sum = sum;
        sum                                                                                = design_single(d, term[r]);
        design_equation(d, p, EQ_OUTPUT_ENABLE, 0)->sum                                    = sum;
    }
}

int v10_decode(const struct conversion *conv, struct design *d)
{
    const char *dev = conv->device;
    struct v10 v    = {.f = conv->fuses};
    /* Only the G22V10 names get a GAL type and a user signature. The 22CV10
     * has none: EQN2JED rejects @ues for it (E0126). */
    int gal = dev[0] == 'G' && dev[1] == '2';
    char type[40];
    v.plcc = dev[strlen(dev) - 1] == 'V';
    v.pins = v.plcc ? 28 : 24;
    v.clk  = v.plcc ? 2 : 1;
    announce_output();
    analyse(&v);
    name_pins(&v);
    if (!apply_labels(&v, conv->labels, conv->err))
        return 0;
    snprintf(type, sizeof(type), "%s%s", gal ? "GAL" : "", gal ? dev + 1 : dev);
    design_init(d, FAMILY_V10, type);
    design_set_names(d, v.names, v.pins);
    d->package_pins = v.pins;
    if (gal) {
        d->ues      = v.f + V10_UES;
        d->ues_bits = 64;
    }
    build_equations(&v, d);
    return 1;
}
