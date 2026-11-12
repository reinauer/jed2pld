/* MAPL128 and MAPL144 decoder. */
#include "mapl.h"

/* 128 page terms in 8 pages of 16, each 58 AND fuses (29 input columns)
 * followed by 54 OR fuses, two per node: its D or J sum and its clock enable
 * or K sum. Then four control terms without OR fuses: the reset and three
 * output enable terms. Then one configuration plane per node property and,
 * on the MAPL128, the 128-bit user signature. */
enum
{
    PAGE_TERMS    = 128,
    ROW           = 112,
    AND           = 58,
    CONTROL_TERMS = 14336,
    RESET_TERM    = 128, /* term numbers of the control terms */
    OE_TERM       = 129, /* OE_TERM - 1 + output enable select 1-3 */
    PLANE_0       = 14568,
    PLANE_1       = 14595,
    PLANE_2       = 14622,
    PLANE_3       = 14649, /* not for page nodes */
    OE_SELECT_0   = 14673, /* outputs only */
    UES_128       = 14705,
};

/* Nodes 0-2 are the page registers. On the MAPL128, nodes 3-10 are buried
 * and 11-26 drive outputs; on the MAPL144, nodes 3-26 drive outputs. */
enum
{
    NODES      = 27,
    PAGE_NODES = 3,
};

static const int columns128[] = {6,  5,  4,  3,  2,  27, 26, 25, 24, 11, 10, 9,  8,  7, 17,
                                 18, 19, 20, 21, 22, 23, 29, 30, 31, 32, 33, 34, 35, 36};
static const int columns144[] = {6,  5,  4,  3,  2,  43, 42, 41, 40, 19, 18, 17, 14, 13, 24,
                                 25, 26, 27, 28, 29, 32, 7,  10, 11, 12, 36, 35, 34, 33};
static const int nodes128[]   = {37, 38, 39, 29, 30, 31, 32, 33, 34, 35, 36, 23, 22, 21,
                                 20, 19, 18, 17, 7,  8,  9,  10, 11, 12, 13, 15, 16};
static const int nodes144[]   = {45, 46, 47, 7,  10, 11, 12, 36, 35, 34, 33, 32, 29, 28,
                                 27, 26, 25, 24, 13, 14, 17, 18, 19, 20, 21, 22, 23};

struct mapl
{
    const unsigned char *f;
    int large;
    int first_output;   /* node number of the first output node */
    int pins;           /* package pins; higher numbers are nodes */
    int oe_pin;         /* the output enable pin */
    int columns[29];    /* pin or node of each input column pair */
    int nodes[NODES];   /* pin or node driven by each node */
    int state[132];     /* each term: 0 inactive, 1 constant high, -1 literals */
    int page[8];        /* the page has active terms */
    int fed[2 * NODES]; /* an active page term feeds the OR column */
    int cfg[NODES][6];  /* the configuration fuses of each node, see analyse() */
    int ins[MAX_PINS];  /* the pin is an input to a term with literals */
    int outs[MAX_PINS]; /* the pin is driven */
    int term[132];      /* design term of each active term */
    label_t names[MAX_PINS];
};

int mapl_supports_device(const char *s)
{
    return !strcasecmp(s, "MAPL128") || !strcasecmp(s, "MAPL144");
}

static int term_address(int r)
{
    return r < PAGE_TERMS ? r * ROW : CONTROL_TERMS + (r - PAGE_TERMS) * AND;
}

static void analyse(struct mapl *m)
{
    const unsigned char *f = m->f;
    /* Only an entirely cleared term is inactive (0); a term with every fuse
     * set is constant high (1); anything else keeps all its literals (-1),
     * even a column pair with both fuses cleared, which prints as "x * /x". */
    for (int r = 0; r < 132; r++) {
        int ones = 0;
        for (int c = 0; c < AND; c++)
            ones += f[term_address(r) + c];
        m->state[r] = ones == 0 ? 0 : ones == AND ? 1 : -1;
        if (r < PAGE_TERMS && m->state[r])
            m->page[r / 16] = 1;
    }
    /* A page or buried node is driven when its D or J sum is fed, an output
     * node when a term with literals feeds either sum. */
    for (int c = 0; c < 2 * NODES; c++)
        for (int r = 0; r < PAGE_TERMS; r++)
            if (m->state[r] && !f[r * ROW + AND + c]) {
                m->fed[c] = 1;
                if (c / 2 < m->first_output ? c % 2 == 0 : m->state[r] == -1)
                    m->outs[m->nodes[c / 2]] = 1;
            }
    for (int r = 0; r < 132; r++)
        if (m->state[r] == -1)
            for (int c = 0; c < 29; c++)
                if (!f[term_address(r) + c * 2] || !f[term_address(r) + c * 2 + 1])
                    m->ins[m->columns[c]] = 1;
    /* Planes 0-2 (D/J polarity, clock enable/K polarity, JK mode) cover all
     * nodes, plane 3 omits the page nodes and the output enable select
     * planes 4-5 cover only output nodes. */
    for (int n = 0; n < NODES; n++) {
        m->cfg[n][0] = f[PLANE_0 + n];
        m->cfg[n][1] = f[PLANE_1 + n];
        m->cfg[n][2] = f[PLANE_2 + n];
        if (n >= PAGE_NODES)
            m->cfg[n][3] = f[PLANE_3 + n - PAGE_NODES];
        if (n >= m->first_output) {
            int output   = n - m->first_output;
            m->cfg[n][4] = f[OE_SELECT_0 + output];
            m->cfg[n][5] = f[(m->large ? 14697 : 14689) + output];
        }
    }
}

static void name_default(struct mapl *m, int p, const char *name)
{
    if (!m->names[p][0])
        snprintf(m->names[p], MAX_LABEL, "%s", name);
}

/* Labels come first; default names fill in only the pins without one. */
static void name_pins(struct mapl *m, const label_t *labels)
{
    for (int p = 1; p < MAX_PINS; p++)
        if (labels[p][0])
            snprintf(m->names[p], MAX_LABEL, "%s", labels[p]);
    for (int p = 1; p < MAX_PINS; p++)
        if (p != m->nodes[0] && p != m->nodes[1] && p != m->nodes[2] && !m->names[p][0] && (m->ins[p] || m->outs[p])) {
            char type = 'i';
            for (int n = PAGE_NODES; n < NODES; n++)
                if (m->nodes[n] == p && m->outs[p])
                    type = n < m->first_output ? 'b' : m->ins[p] ? 'f' : 'o';
            snprintf(m->names[p], MAX_LABEL, "%c%d", type, p);
        }
    name_default(m, 1, "CLK");
    if (m->large) {
        static const int gnd[] = {8, 9, 30, 31}, vcc[] = {15, 16, 37, 38};
        for (int i = 0; i < 4; i++) {
            name_default(m, gnd[i], "GND");
            name_default(m, vcc[i], "VCC");
        }
        name_default(m, 39, "/OE");
        name_default(m, 44, "NC");
    } else {
        name_default(m, 14, "GND");
        name_default(m, 28, "VCC");
        name_default(m, 24, "/OE");
    }
    for (int n = 0; n < PAGE_NODES; n++)
        if (!m->names[m->nodes[n]][0])
            snprintf(m->names[m->nodes[n]], MAX_LABEL, "pb%d", n);
}

/* Repeated power names and input-only aliases are allowed. A collision is
 * diagnosed when both pins take part and one of them drives a package pin. */
static int check_duplicate_labels(struct mapl *m, FILE *err)
{
    int duplicates = 0, last = m->large ? 44 : 36;
    m->ins[1] = 1; /* Fixed clock/OE controls have no AND-array columns. */
    if (m->large)
        m->ins[39] = 1;
    for (int p = 1; p <= last; p++) {
        if (!(m->ins[p] || m->outs[p]))
            continue;
        for (int q = 1; q < p; q++) {
            if (!(m->ins[q] || m->outs[q]) || !((p <= m->pins && m->outs[p]) || (q <= m->pins && m->outs[q])) ||
                strcmp(label_without_polarity(m->names[p]), label_without_polarity(m->names[q])))
                continue;
            fprintf(err, "E1803: Duplicate label '%s' found on pins %d and %d.\n", label_without_polarity(m->names[q]),
                    p, q);
            duplicates++;
        }
    }
    return duplicates == 0;
}

/* The page terms feeding OR column COLUMN, each in its page. */
static struct sum paged_sum(const struct mapl *m, int column, struct design *d)
{
    struct sum sum = design_sum(d);
    for (int r = 0; r < PAGE_TERMS; r++)
        if (m->state[r] && !m->f[r * ROW + AND + column])
            design_sum_add(d, &sum, m->term[r], r / 16);
    return sum;
}

static struct sum control_term(const struct mapl *m, int r, struct design *d)
{
    return design_single(d, m->state[r] ? m->term[r] : -1);
}

static void output_enable(const struct mapl *m, int n, int p, int any_written, struct design *d)
{
    const int *cfg = m->cfg[n];
    int select     = cfg[4] * 2 + cfg[5] + 1; /* 1-3: a control term, 4: the OE pin */
    struct sum sum;
    if (m->large && cfg[3] && select == 1)
        select = 4;
    if (select == 4 && m->large && cfg[4] && cfg[5]) {
        sum                                             = design_single(d, cfg[3] ? -1 : design_term(d, 0, NULL));
        design_equation(d, p, EQ_OUTPUT_ENABLE, 0)->sum = sum;
    } else if (select == 4) {
        struct equation *e;
        sum     = design_single(d, design_term(d, 0, NULL));
        e       = design_equation(d, p, EQ_OUTPUT_ENABLE, 0);
        e->sum  = sum;
        e->gate = (struct literal){(unsigned char)m->oe_pin, 1};
    } else if (m->state[OE_TERM - 1 + select] || any_written) {
        sum                                             = control_term(m, OE_TERM - 1 + select, d);
        design_equation(d, p, EQ_OUTPUT_ENABLE, 0)->sum = sum;
    }
}

/* Each node's D or J and clock enable or K sums, then the reset and output
 * enable of the nodes that have either sum. An empty sum is written when its
 * polarity fuse is programmed. */
static void build_equations(struct mapl *m, struct design *d)
{
    int any_written = 0;
    for (int r = 0; r < 132; r++)
        if (m->state[r])
            m->term[r] = design_fuse_term(d, m->f + term_address(r), AND, m->columns, NULL);
    for (int n = 0; n < NODES; n++) {
        const int *cfg = m->cfg[n];
        int p = m->nodes[n], jk = cfg[2], local = 0;
        int sum_fed = m->fed[n * 2], ce_fed = m->fed[n * 2 + 1];
        struct sum sum;
        if (!m->names[p][0])
            continue;
        if (sum_fed || !cfg[0] || !cfg[1]) {
            sum = sum_fed ? paged_sum(m, n * 2, d) : design_sum(d);
            design_equation(d, p, jk ? EQ_J : EQ_REGISTERED, !cfg[0])->sum = sum;
            any_written = local = 1;
        }
        if (ce_fed) {
            sum                                                              = paged_sum(m, n * 2 + 1, d);
            design_equation(d, p, jk ? EQ_K : EQ_CLOCK_ENABLE, !cfg[1])->sum = sum;
            any_written                                                      = 1;
        } else if (local || !cfg[1]) {
            if (jk || cfg[1])
                design_equation(d, p, jk ? EQ_K : EQ_CLOCK_ENABLE, !cfg[1])->sum = design_sum(d);
            any_written = 1;
        }
        if (!sum_fed && !ce_fed)
            continue;
        /* The reset term is shared; each node repeats it. */
        if (m->state[RESET_TERM]) {
            sum                                     = control_term(m, RESET_TERM, d);
            design_equation(d, p, EQ_RESET, 0)->sum = sum;
        }
        if (n >= m->first_output)
            output_enable(m, n, p, any_written, d);
    }
}

int mapl_decode(const struct conversion *conv, struct design *d)
{
    struct mapl m = {.f = conv->fuses};
    announce_output();
    m.large = !strcasecmp(conv->device, "MAPL144");
    if (conv->count < (unsigned)(m.large ? 14721 : 14833)) {
        fputs("E0408: Illegal macrocell fuse configuration in JEDEC map.\n", conv->err);
        return 0;
    }
    m.first_output = m.large ? 3 : 11;
    m.pins         = m.large ? 44 : 28;
    m.oe_pin       = m.large ? 39 : 24;
    memcpy(m.columns, m.large ? columns144 : columns128, sizeof(m.columns));
    memcpy(m.nodes, m.large ? nodes144 : nodes128, sizeof(m.nodes));
    analyse(&m);
    name_pins(&m, conv->labels);
    if (!check_duplicate_labels(&m, conv->err))
        return 0;
    design_init(d, FAMILY_MAPL, m.large ? "MAPL144" : "MAPL128");
    design_set_names(d, m.names, MAX_PINS - 1);
    d->package_pins = m.pins;
    for (int c = 0; c < PAGE_NODES; c++)
        d->page_pin[c] = m.nodes[c];
    for (int r = 0; r < 8; r++)
        d->pages_used |= m.page[r] << r;
    if (!m.large) {
        d->ues      = m.f + UES_128;
        d->ues_bits = 128;
    }
    build_equations(&m, d);
    return 1;
}
