/* GAL16V8, GAL20V8 and GAL6001 decoders. */
#include "gal.h"

/* Device names handled here, without the GAL (or PAL) prefix. */
static const char *const variants[] = {
    "16V8", "16V8A", "16V8QS", "20V8", "20V8A", "20V8QS", "20V8V", "20V8AV", "20V8QSV", "6001", "6001V",
};

struct gal
{
    const unsigned char *fuses;
    int plcc;
    int columns[40];    /* pin or node of each AND-array input column pair */
    int used[MAX_PINS]; /* pin is an input to an active product term */
    label_t names[MAX_PINS];

    /* GAL16V8/20V8 only */
    int width;  /* fuses per product term */
    int config; /* first fuse after the AND array */
    int mode;   /* enum v8_mode */
    int pins, clk, oe, gnd, vcc;
    int outputs[8], registered[8], any_registered;
};

static const char *without_prefix(const char *device)
{
    if (!strncasecmp(device, "GAL", 3) || !strncasecmp(device, "PAL", 3))
        return device + 3;
    return device;
}

int gal_supports_device(const char *device)
{
    const char *s = without_prefix(device);
    for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++)
        if (!strcasecmp(s, variants[i]))
            return 1;
    return 0;
}

/* Check for duplicate labels on pins 1..LAST. A pin takes part if PRESENT,
 * and two equal names are an error if both take part and either is ACTIVE.
 * Otherwise, with RENAME, the later pin gets its number appended. Labels on
 * otherwise unused inputs are kept but do not make a pin active. */
static int resolve_label_collisions(struct gal *g, FILE *err, int last, const int present[], const int active[],
                                    int rename)
{
    int errors = 0;
    for (int p = 1; p <= last && p < MAX_PINS; p++)
        if (present[p] && g->names[p][0])
            for (int q = 1; q < p; q++)
                if (g->names[q][0] &&
                    !strcmp(label_without_polarity(g->names[p]), label_without_polarity(g->names[q]))) {
                    if (present[q] && (active[p] || active[q])) {
                        fprintf(err, "E1803: Duplicate label '%s' found on pins %d and %d.\n",
                                label_without_polarity(g->names[q]), p, q);
                        errors++;
                    } else if (rename) {
                        label_t name;
                        snprintf(name, sizeof(name), "%.74s%d", g->names[p], p);
                        strcpy(g->names[p], name);
                    }
                }
    return errors == 0;
}

/* GAL16V8/20V8: 64 product terms of 32 or 40 fuses, eight per output, then the
 * fuses below, as offsets from the end of the AND array. */
enum
{
    V8_XOR    = 0,   /* output polarity, one per macrocell */
    V8_UES    = 8,   /* 64-bit user electronic signature */
    V8_AC1    = 72,  /* macrocell configuration, one per macrocell */
    V8_PTD    = 80,  /* product term disable, one per term */
    V8_SYN    = 144, /* SYN and AC0 select the device mode */
    V8_AC0    = 145,
    V8_CONFIG = 146,
};

/* Device modes, named as in the ;$GALMODE directive. The data sheets call
 * small and medium mode simple and complex mode. */
enum v8_mode
{
    V8_REGISTERED,
    V8_SMALL,
    V8_MEDIUM,
};

/* Only an entirely cleared term is inactive; mixed terms keep every literal. */
static int v8_term_is_active(const struct gal *g, int row)
{
    for (int c = 0; c < g->width; c++)
        if (g->fuses[row * g->width + c])
            return 1;
    return 0;
}

/* Read the device mode and pin layout. Returns 0 for an illegal configuration. */
static int v8_configure(struct gal *g, int twenty, unsigned count)
{
    /* Pin of each input column pair, by mode. */
    static const int columns16[3][16] = {{2, 19, 3, 18, 4, 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 12},
                                         {2, 1, 3, 19, 4, 18, 5, 17, 6, 14, 7, 13, 8, 12, 9, 11},
                                         {2, 1, 3, 18, 4, 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 11}};
    static const int columns20[3][20] = {{2, 23, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 14},
                                         {2, 1, 3, 23, 4, 22, 5, 21, 6, 20, 7, 17, 8, 16, 9, 15, 10, 14, 11, 13},
                                         {2, 1, 3, 23, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 14, 11, 13}};
    const unsigned char *config;
    g->width  = twenty ? 40 : 32;
    g->config = 64 * g->width;
    config    = g->fuses + g->config;
    if (count < (unsigned)(g->config + V8_CONFIG) || (!config[V8_SYN] && !config[V8_AC0]))
        return 0;
    g->mode = !config[V8_SYN] ? V8_REGISTERED : config[V8_AC0] ? V8_MEDIUM : V8_SMALL;
    for (int i = 0; i < 8; i++) {
        if (g->mode == V8_MEDIUM && !config[V8_AC1 + i])
            return 0;
        g->registered[i] = g->mode == V8_REGISTERED && !config[V8_AC1 + i];
        g->any_registered |= g->registered[i];
    }
    g->clk  = 1;
    g->oe   = twenty ? 13 : 11;
    g->gnd  = twenty ? 12 : 10;
    g->vcc  = twenty ? 24 : 20;
    g->pins = g->vcc;
    for (int i = 0; i < 8; i++)
        g->outputs[i] = (twenty ? 22 : 19) - i;
    for (int i = 0; i < g->width / 2; i++)
        g->columns[i] = twenty ? columns20[g->mode][i] : columns16[g->mode][i];
    if (g->plcc) {
        g->pins = 28;
        g->clk  = dip24_to_plcc28_pin(g->clk);
        g->oe   = dip24_to_plcc28_pin(g->oe);
        g->gnd  = dip24_to_plcc28_pin(g->gnd);
        g->vcc  = 28;
        for (int i = 0; i < 8; i++)
            g->outputs[i] = dip24_to_plcc28_pin(g->outputs[i]);
        for (int i = 0; i < g->width / 2; i++)
            g->columns[i] = dip24_to_plcc28_pin(g->columns[i]);
    }
    return 1;
}

/* Mark the inputs of active terms as used. Returns 1 when an inactive term has
 * its product term disable fuse set, which the equations note as @ptd unused. */
static int v8_mark_used_pins(struct gal *g)
{
    int ptd_unused = 0;
    for (int row = 0; row < 64; row++) {
        if (!v8_term_is_active(g, row)) {
            ptd_unused |= g->fuses[g->config + V8_PTD + row];
            continue;
        }
        for (int c = 0; c < g->width / 2; c++)
            if (!g->fuses[row * g->width + c * 2] || !g->fuses[row * g->width + c * 2 + 1])
                g->used[g->columns[c]] = 1;
    }
    return ptd_unused;
}

static void v8_name_pins(struct gal *g)
{
    int named_controls = g->any_registered || g->plcc;
    if (g->plcc) {
        strcpy(g->names[1], "NC");
        strcpy(g->names[8], "NC");
        strcpy(g->names[15], "NC");
        strcpy(g->names[22], "NC");
    }
    for (int p = 1; p <= g->pins; p++)
        if (g->used[p])
            snprintf(g->names[p], MAX_LABEL, "i%d", p);
    for (int i = 0; i < 8; i++) {
        int p = g->outputs[i];
        snprintf(g->names[p], MAX_LABEL, "%s%c%d", g->registered[i] ? "r" : "", g->used[p] ? 'f' : 'o', p);
    }
    if (!g->used[g->clk])
        strcpy(g->names[g->clk], named_controls ? "CLK" : "nc1");
    if (g->used[g->oe])
        snprintf(g->names[g->oe], MAX_LABEL, "/i%d", g->oe);
    else
        snprintf(g->names[g->oe], MAX_LABEL, named_controls ? "/OE" : "/nc%d", g->oe);
    if (g->plcc && !g->any_registered) {
        strcpy(g->names[1], "nc1");
        strcpy(g->names[15], "nc15");
    }
    strcpy(g->names[g->gnd], "GND");
    strcpy(g->names[g->vcc], "VCC");
}

/* Replace the default names with the labels from the file and check them. */
static int v8_apply_labels(struct gal *g, const struct conversion *conv)
{
    int present[MAX_PINS] = {0}, active[MAX_PINS] = {0};
    for (int p = 1; p <= g->pins; p++) {
        present[p] = g->names[p][0] && strcmp(g->names[p], "NC") && p != g->gnd && p != g->vcc;
        /* The default nc1/nc15 pins of a PLCC part without registers never collide. */
        if (g->plcc && !g->any_registered && (p == 1 || p == 15))
            present[p] = 0;
    }
    for (int i = 0; i < 8; i++)
        for (int row = 0; row < 8; row++)
            if (v8_term_is_active(g, i * 8 + row))
                active[g->outputs[i]] = 1;
    for (int p = 1; p <= g->pins; p++)
        if (conv->labels[p][0])
            snprintf(g->names[p], MAX_LABEL, "%s", conv->labels[p]);
    /* The output enable pin is active low whatever its label says. */
    if (conv->labels[g->oe][0] && !label_is_inverted(g->names[g->oe])) {
        label_t name;
        snprintf(name, sizeof(name), "/%.78s", g->names[g->oe]);
        strcpy(g->names[g->oe], name);
    }
    return resolve_label_collisions(g, conv->err, g->pins, present, active, 1);
}

static void v8_build_equations(const struct gal *g, struct design *d)
{
    const unsigned char *config = g->fuses + g->config;
    int term[64], enabled = design_term(d, 0, NULL);
    for (int row = 0; row < 64; row++)
        term[row] =
            v8_term_is_active(g, row) ? design_fuse_term(d, g->fuses + row * g->width, g->width, g->columns, NULL) : -1;
    for (int i = 0; i < 8; i++) {
        int pin = g->outputs[i];
        /* In medium mode, and for combinational outputs in registered mode,
         * the first term of each output is its output enable. */
        int oe_term    = g->mode == V8_MEDIUM || (g->mode == V8_REGISTERED && !g->registered[i]);
        struct sum sum = design_sum(d);
        struct equation *e;
        if (g->mode == V8_SMALL && config[V8_AC1 + i])
            continue; /* used as an input */
        for (int row = oe_term; row < 8; row++)
            if (term[8 * i + row] >= 0)
                design_sum_add(d, &sum, term[8 * i + row], -1);
        design_equation(d, pin, g->registered[i] ? EQ_REGISTERED : EQ_COMBINATORIAL, !config[V8_XOR + i])->sum = sum;
        if (g->registered[i]) {
            sum      = design_single(d, enabled);
            e        = design_equation(d, pin, EQ_OUTPUT_ENABLE, 0);
            e->fixed = 1;
            e->sum   = sum;
            e->gate  = (struct literal){(unsigned char)g->oe, 1};
        } else if (oe_term) {
            sum                                               = design_single(d, term[8 * i]);
            design_equation(d, pin, EQ_OUTPUT_ENABLE, 0)->sum = sum;
        }
    }
}

static int v8_decode(const struct conversion *conv, const char *variant, struct design *d)
{
    static const enum gal_mode modes[] = {GAL_MODE_REGISTERED, GAL_MODE_SIMPLE, GAL_MODE_COMPLEX};
    struct gal g                       = {.fuses = conv->fuses};
    int twenty                         = !strncmp(variant, "20", 2);
    char type[40];
    g.plcc = twenty && variant[strlen(variant) - 1] == 'V';
    if (!v8_configure(&g, twenty, conv->count)) {
        fputs("E0408: Illegal macrocell fuse configuration in JEDEC map.\n", conv->err);
        return 0;
    }
    announce_output();
    int ptd_unused = v8_mark_used_pins(&g);
    v8_name_pins(&g);
    if (!v8_apply_labels(&g, conv))
        return 0;
    snprintf(type, sizeof(type), "GAL%s", variant);
    design_init(d, FAMILY_GAL_V8, type);
    design_set_names(d, g.names, g.pins);
    d->package_pins = g.pins;
    d->gal_mode     = modes[g.mode];
    d->ues          = g.fuses + g.config + V8_UES;
    d->ues_bits     = 64;
    d->ptd_unused   = ptd_unused;
    v8_build_equations(&g, d);
    return 1;
}

/* GAL6001: 64 shared product terms, each 78 AND fuses (39 input columns)
 * followed by 36 OR-array fuses, two per macrocell: one for its sum and one
 * for its clock or clock enable sum. Then come the ten output enable terms,
 * last output first, the asynchronous reset term and the configuration.
 * Macrocells 0-9 are output macrocells, 10-17 buried macrocells. */
enum
{
    G6001_AND        = 78,
    G6001_ROW        = 114,
    G6001_OE_TERMS   = 7296,
    G6001_AR_TERM    = 8076,
    G6001_BURIED_CFG = 8154, /* three fuses per buried macrocell, last first */
    G6001_OUTPUT_CFG = 8178, /* four fuses per output macrocell */
    G6001_INPUT_MODE = 8218, /* two fuses per input group */
    G6001_UES        = 8222,
    G6001_FUSES      = 8294,
};

/* Macrocell configuration fuses, as offsets from the first. */
enum
{
    MC_CLOCK_ENABLE  = 0, /* clock enable (.ce) instead of clock (.c) */
    MC_COMBINATORIAL = 1,
    MC_CLOCK_INVERT  = 2,
    MC_ACTIVE_HIGH   = 3, /* output macrocells only */
};

struct g6001
{
    struct gal g;
    const label_t *labels;
    int shared[64];   /* each shared term: 0 inactive, 1 constant high, -1 literals */
    int oe[10];       /* the output enable term of each output macrocell, likewise */
    int ar;           /* the reset term, likewise */
    int sum[36];      /* an active shared term feeds this OR column */
    int equation[18]; /* the macrocell has a sum or clock equation */
    int config[18];   /* first configuration fuse of each macrocell */
    int config_clear; /* all configuration and input mode fuses of a macrocell are 0 */
};

static int g6001_classify_term(const unsigned char *f, int addr)
{
    int ones = 0;
    for (int i = 0; i < G6001_AND; i++)
        ones += f[addr + i];
    return ones == G6001_AND ? 1 : ones ? -1 : 0;
}

/* Pin or node number P of the DIP package in the selected package. */
static int g6001_pin(const struct g6001 *s, int p)
{
    return !s->g.plcc ? p : p <= 24 ? dip24_to_plcc28_pin(p) : p + 4;
}

/* Output macrocells drive pins 14-23 when they have an output enable term and
 * are nodes 33-42 otherwise; buried macrocells are nodes 25-32. */
static int g6001_macrocell_pin(const struct g6001 *s, int i)
{
    return g6001_pin(s, i < 10 ? (s->oe[i] ? 14 + i : 33 + i) : 15 + i);
}

static void g6001_analyse(struct g6001 *s)
{
    static const int columns[39] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 32, 31, 30, 29, 28, 27, 26, 25, 23,
                                    22, 21, 20, 19, 18, 17, 16, 15, 14, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42};
    const unsigned char *f       = s->g.fuses;
    for (int i = 0; i < 39; i++)
        s->g.columns[i] = g6001_pin(s, columns[i]);
    for (int t = 0; t < 75; t++) {
        int addr  = t < 64 ? t * G6001_ROW : G6001_OE_TERMS + (t - 64) * G6001_AND;
        int state = g6001_classify_term(f, addr);
        if (t < 64)
            s->shared[t] = state;
        else if (t < 74)
            s->oe[73 - t] = state;
        else
            s->ar = state;
        if (state)
            for (int c = 0; c < 39; c++)
                if (!f[addr + c * 2] || !f[addr + c * 2 + 1])
                    s->g.used[s->g.columns[c]] = 1;
    }
    for (int c = 0; c < 36; c++)
        for (int r = 0; r < 64; r++)
            if (s->shared[r] && !f[r * G6001_ROW + G6001_AND + c])
                s->sum[c] = 1;
    s->config_clear =
        !f[G6001_INPUT_MODE] && !f[G6001_INPUT_MODE + 1] && !f[G6001_INPUT_MODE + 2] && !f[G6001_INPUT_MODE + 3];
    for (int i = 0; i < 18; i++) {
        s->config[i] = i < 10 ? G6001_OUTPUT_CFG + 4 * i : G6001_BURIED_CFG + 3 * (17 - i);
        /* An enabled output drives its value even without terms. */
        s->equation[i] = s->sum[i * 2] || s->sum[i * 2 + 1] || (i < 10 && s->oe[i]);
    }
}

/* Name the macrocells and inputs. */
static void g6001_name_pins(struct g6001 *s, int lastpin)
{
    struct gal *g = &s->g;
    for (int i = 0; i < 18; i++) {
        int p = g6001_macrocell_pin(s, i), clear = s->config_clear;
        for (int k = 0; k < (i < 10 ? 4 : 3); k++)
            clear &= !g->fuses[s->config[i] + k];
        if (!s->equation[i])
            continue;
        if (p < g6001_pin(s, 25))
            snprintf(g->names[p], MAX_LABEL, "%s%c%d", clear ? "r" : "", g->used[p] ? 'f' : 'o', p);
        else
            snprintf(g->names[p], MAX_LABEL, "%c%d", i >= 10 && !s->sum[i * 2] && g->used[p] ? 'i' : 'b', p);
    }
    for (int p = 1; p <= lastpin; p++)
        if (g->used[p] && !g->names[p][0]) {
            int node = p - g6001_pin(s, 33); /* output macrocell of a node 33-42 */
            snprintf(g->names[p], MAX_LABEL, "%c%d", node >= 0 && !s->oe[node] ? 'b' : 'i', p);
        }
    if (!g->names[g6001_pin(s, 1)][0])
        strcpy(g->names[g6001_pin(s, 1)], "ICLK");
    strcpy(g->names[g6001_pin(s, 12)], "GND");
    strcpy(g->names[g6001_pin(s, 13)], "OCLK");
    strcpy(g->names[g6001_pin(s, 24)], "VCC");
    if (g->plcc) {
        strcpy(g->names[1], "NC");
        strcpy(g->names[8], "NC");
        strcpy(g->names[15], "NC");
        strcpy(g->names[22], "NC");
    }
}

static int g6001_apply_labels(struct g6001 *s, FILE *err, int lastpin)
{
    struct gal *g         = &s->g;
    int present[MAX_PINS] = {0}, active[MAX_PINS] = {0};
    for (int p = 1; p <= lastpin; p++)
        present[p] = g->names[p][0] != 0;
    for (int i = 0; i < 18; i++)
        if (s->equation[i])
            active[g6001_macrocell_pin(s, i)] = 1;
    present[g6001_pin(s, 12)] = present[g6001_pin(s, 13)] = present[g6001_pin(s, 24)] = 0;
    if (!g->used[g6001_pin(s, 1)])
        present[g6001_pin(s, 1)] = 0;
    if (g->plcc)
        present[1] = present[8] = present[15] = present[22] = 0;
    for (int p = 1; p <= lastpin; p++)
        if (s->labels[p][0])
            strcpy(g->names[p], s->labels[p]);
    return resolve_label_collisions(g, err, lastpin, present, active, 0);
}

/* Record the input groups, pins 2-11 and 14-23, whose used inputs pass
 * through a register or latch. Returns 0 for an illegal input mode. */
static int g6001_input_modes(const struct g6001 *s, FILE *err, struct design *d)
{
    const struct gal *g = &s->g;
    for (int group = 0; group < 2; group++) {
        int first = group ? 14 : 2, last = group ? 23 : 11;
        const unsigned char *mode = g->fuses + G6001_INPUT_MODE + group * 2;
        struct input_group *in    = &d->input[d->ninputs];
        if (mode[0] && !mode[1]) {
            fputs("E1802: Illegal input mode for GAL6001.\n", err);
            return 0;
        }
        if (mode[0])
            continue;
        in->latched = !mode[1];
        in->count   = 0;
        for (int p = first; p <= last; p++)
            if (g->used[g6001_pin(s, p)])
                in->pin[in->count++] = g6001_pin(s, p);
        if (in->count)
            d->ninputs++;
    }
    return 1;
}

/* The shared terms feeding OR column COLUMN. */
static struct sum g6001_sum(const struct g6001 *s, const int term[], int column, struct design *d)
{
    struct sum sum = design_sum(d);
    for (int r = 0; r < 64; r++)
        if (s->shared[r] && !s->g.fuses[r * G6001_ROW + G6001_AND + column])
            design_sum_add(d, &sum, term[r], -1);
    return sum;
}

static void g6001_build_equations(const struct g6001 *s, struct design *d)
{
    const unsigned char *f = s->g.fuses;
    int term[64];
    struct sum sum;
    for (int r = 0; r < 64; r++)
        term[r] = s->shared[r] ? design_fuse_term(d, f + r * G6001_ROW, G6001_AND, s->g.columns, NULL) : -1;
    /* A reset term with a contradictory column pair (x * /x) is never true. */
    int reset = s->ar != 0;
    for (int c = 0; s->ar == -1 && c < G6001_AND; c += 2)
        if (!f[G6001_AR_TERM + c] && !f[G6001_AR_TERM + c + 1])
            reset = 0;
    if (reset) {
        sum = design_single(d, design_fuse_term(d, f + G6001_AR_TERM, G6001_AND, s->g.columns, NULL));
        design_equation(d, 0, EQ_RESET, 0)->sum = sum;
        design_break(d);
    }
    for (int i = 0; i < 18; i++) {
        const unsigned char *config = f + s->config[i];
        int p                       = g6001_macrocell_pin(s, i);
        if (s->equation[i]) {
            sum = g6001_sum(s, term, i * 2, d);
            design_equation(d, p, config[MC_COMBINATORIAL] ? EQ_COMBINATORIAL : EQ_REGISTERED,
                            i < 10 && !config[MC_ACTIVE_HIGH])
                ->sum = sum;
        }
        if (s->sum[i * 2 + 1]) {
            if (!config[MC_COMBINATORIAL]) {
                sum = g6001_sum(s, term, i * 2 + 1, d);
                design_equation(d, p, config[MC_CLOCK_ENABLE] ? EQ_CLOCK_ENABLE : EQ_CLOCK, config[MC_CLOCK_INVERT])
                    ->sum = sum;
            }
            if (i >= 10)
                design_break(d);
        }
        if (i < 10 && s->oe[i]) {
            int oe = design_fuse_term(d, f + G6001_OE_TERMS + (9 - i) * G6001_AND, G6001_AND, s->g.columns, NULL);
            sum    = design_single(d, oe);
            design_equation(d, p, EQ_OUTPUT_ENABLE, 0)->sum = sum;
            design_break(d);
        }
    }
}

static int g6001_decode(const struct conversion *conv, int plcc, struct design *d)
{
    struct g6001 s = {.g = {.fuses = conv->fuses, .plcc = plcc}, .labels = conv->labels};
    int lastpin    = plcc ? 46 : 42;
    announce_output();
    if (conv->count < G6001_FUSES) {
        fputs("E0100: Unknown internal error.\n", conv->err);
        return 0;
    }
    g6001_analyse(&s);
    g6001_name_pins(&s, lastpin);
    if (!g6001_apply_labels(&s, conv->err, lastpin))
        return 0;
    design_init(d, FAMILY_GAL6001, plcc ? "GAL6001V" : "GAL6001");
    design_set_names(d, s.g.names, lastpin);
    d->package_pins = plcc ? 28 : 24;
    d->ues          = conv->fuses + G6001_UES;
    d->ues_bits     = 72;
    if (!g6001_input_modes(&s, conv->err, d))
        return 0;
    g6001_build_equations(&s, d);
    return 1;
}

int gal_decode(const struct conversion *conv, struct design *d)
{
    const char *variant = without_prefix(conv->device);
    if (!strcasecmp(variant, "6001") || !strcasecmp(variant, "6001V"))
        return g6001_decode(conv, !strcasecmp(variant, "6001V"), d);
    return v8_decode(conv, variant, d);
}
