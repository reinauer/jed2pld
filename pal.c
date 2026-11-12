/* PAL matrix decoder. */
#include "pal.h"

/* Macrocell modes used in the device table. Modes come in pairs: the
 * configuration rules may select the odd mode of a pair, which is active high. */
enum
{
    MODE_REGISTERED = 4,  /* D register, enabled by the device's /OE pin */
    MODE_FIXED      = 6,  /* combinational, always enabled */
    MODE_FIXED_IO   = 8,  /* combinational on a pin that is also an array input */
    MODE_COMPLEMENT = 10, /* second pin of a complementary pair, no equation */
    MODE_ASYNC      = 12, /* register with product term enable, clock, reset, preset */
    MODE_XOR        = 16, /* register fed by the XOR of two sums */
    MODE_TRISTATE   = 30, /* 30 to 33: combinational, the first term is the enable */
};

/* An output in mode FROM switches to mode TO when its configuration fuses,
 * read most significant plane first, equal BITS. */
struct pal_rule
{
    int from, bits, to;
};

/* One entry per supported device. Per-output arrays are indexed
 * by output number; the table below uses designated initializers. */
struct pal_device
{
    const char *name;
    int pins;                     /* package pin count */
    int outputs;                  /* number of output macrocells */
    int width;                    /* fuses per product term (2 per input column) */
    int qf;                       /* expected JEDEC QF value */
    int pin[16];                  /* pin number of each output (0 = none) */
    int terms[16];                /* product terms per output */
    int oe[16];                   /* pin number driving the output enable (0 = none) */
    int mode[16];                 /* macrocell mode of each output, see MODE_* */
    int input[32];                /* pin number of each AND-array input column pair */
    int ncfg;                     /* number of configuration fuse planes */
    int cfg[8];                   /* first fuse of each configuration plane */
    int stride[8];                /* fuse stride per output within each plane */
    int nrules;                   /* number of mode-rewrite rules */
    struct pal_rule rules[40];    /* {from mode, config bits, to mode} */
    int nspecial;                 /* number of fixed-function pins */
    int special[16];              /* their pin numbers */
    const char *special_name[16]; /* their default labels ("/X" = inverted) */
};

/* clang-format off */
static const struct pal_device devices[] = {
    {
        .name = "GAL20RA10",
        .pins = 24, .outputs = 10, .width = 40, .qf = 3290,
        .pin = {23, 22, 21, 20, 19, 18, 17, 16, 15, 14},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72},
        .mode = {12, 12, 12, 12, 12, 12, 12, 12, 12, 12},
        .input = {2, 23, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 14},
        .ncfg = 1, .cfg = {3200}, .stride = {1},
        .nrules = 2, .rules = {{12, 0, 12}, {12, 1, 13}},
        .nspecial = 4,
        .special = {1, 12, 13, 24},
        .special_name = {"/PL", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL1016P8",
        .pins = 24, .outputs = 8, .width = 32, .qf = 2056,
        .pin = {21, 4, 20, 5, 18, 7, 17, 8},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 0, 0, 0, 0, 0, 0, 0},
        .mode = {8, 8, 6, 6, 6, 6, 8, 8},
        .input = {1, 23, 2, 22, 3, 21, 4, 17, 8, 16, 9, 15, 10, 14, 11, 13},
        .ncfg = 1, .cfg = {2048}, .stride = {1},
        .nrules = 4, .rules = {{8, 0, 9}, {8, 1, 8}, {6, 0, 7}, {6, 1, 6}},
        .nspecial = 4,
        .special = {6, 12, 19, 24},
        .special_name = {"VCCO", "VEE", "VCCO", "VCC"},
    },
    {
        .name = "PAL16C4",
        .pins = 28, .outputs = 12, .width = 32, .qf = 2056,
        .pin = {0, 0, 25, 24, 5, 6, 22, 21, 8, 9, 0, 0},
        .terms = {8, 8, 8, 0, 8, 0, 8, 0, 8, 0, 8, 8},
        .oe = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        .mode = {10, 10, 6, 7, 6, 7, 7, 6, 7, 6, 10, 10},
        .input = {1, 27, 2, 26, 3, 20, 4, 19, 10, 18, 11, 17, 12, 16, 13, 15},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 4,
        .special = {7, 14, 23, 28},
        .special_name = {"VCCO", "VEE", "VCCO", "VCC"},
    },
    {
        .name = "PAL16P4",
        .pins = 24, .outputs = 8, .width = 32, .qf = 2056,
        .pin = {0, 0, 20, 5, 18, 7, 0, 0},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 0, 0, 0, 0, 0, 0, 0},
        .mode = {10, 10, 6, 6, 6, 6, 10, 10},
        .input = {1, 23, 2, 22, 3, 21, 4, 17, 8, 16, 9, 15, 10, 14, 11, 13},
        .ncfg = 1, .cfg = {2048}, .stride = {1},
        .nrules = 4, .rules = {{10, 0, 11}, {10, 1, 10}, {6, 0, 7}, {6, 1, 6}},
        .nspecial = 4,
        .special = {6, 12, 19, 24},
        .special_name = {"VCCO", "VEE", "VCCO", "VCC"},
    },
    {
        .name = "PAL16PE8",
        .pins = 28, .outputs = 8, .width = 32, .qf = 2056,
        .pin = {25, 5, 24, 6, 22, 8, 21, 9},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 0, 0, 0, 0, 0, 0, 0},
        .mode = {6, 6, 6, 6, 6, 6, 6, 6},
        .input = {1, 27, 2, 26, 3, 20, 4, 19, 10, 18, 11, 17, 12, 16, 13, 15},
        .ncfg = 1, .cfg = {2048}, .stride = {1},
        .nrules = 2, .rules = {{6, 0, 7}, {6, 1, 6}},
        .nspecial = 4,
        .special = {7, 14, 23, 28},
        .special_name = {"VCCO", "VEE", "VCCO", "VCC"},
    },
    {
        .name = "PAL16RD8",
        .pins = 24, .outputs = 8, .width = 32, .qf = 2056,
        .pin = {21, 4, 20, 5, 18, 7, 17, 8},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {13, 13, 13, 13, 13, 13, 13, 13},
        .mode = {4, 4, 4, 4, 4, 4, 4, 4},
        .input = {5, 23, 2, 22, 3, 21, 4, 17, 8, 20, 7, 15, 10, 14, 11, 18},
        .ncfg = 1, .cfg = {2048}, .stride = {1},
        .nrules = 2, .rules = {{4, 0, 5}, {4, 1, 4}},
        .nspecial = 8,
        .special = {1, 6, 9, 12, 13, 16, 19, 24},
        .special_name = {"MR", "VCCO", "CLK1", "VEE", "/OE", "CLK2", "VCCO", "VCC"},
    },
    {
        .name = "PAL16RM4",
        .pins = 24, .outputs = 8, .width = 32, .qf = 2056,
        .pin = {21, 4, 20, 5, 18, 7, 17, 8},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 0, 0, 0, 0, 0, 0, 0},
        .mode = {10, 10, 4, 4, 4, 4, 10, 10},
        .input = {5, 23, 2, 22, 3, 21, 4, 17, 8, 20, 7, 15, 10, 14, 11, 18},
        .ncfg = 1, .cfg = {2048}, .stride = {1},
        .nrules = 4, .rules = {{10, 0, 11}, {10, 1, 10}, {4, 0, 5}, {4, 1, 4}},
        .nspecial = 8,
        .special = {1, 6, 9, 12, 13, 16, 19, 24},
        .special_name = {"MR", "VCCO", "CLK1", "VEE", "CLK3", "CLK2", "VCCO", "VCC"},
    },
    {
        .name = "PAL10H8",
        .pins = 20, .outputs = 8, .width = 20, .qf = 320,
        .pin = {19, 18, 17, 16, 15, 14, 13, 12},
        .terms = {2, 2, 2, 2, 2, 2, 2, 2},
        .oe = {0, 0, 0, 0, 0, 0, 0, 0},
        .mode = {7, 7, 7, 7, 7, 7, 7, 7},
        .input = {2, 1, 3, 4, 5, 6, 7, 8, 9, 11},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL10L8",
        .pins = 20, .outputs = 8, .width = 20, .qf = 320,
        .pin = {19, 18, 17, 16, 15, 14, 13, 12},
        .terms = {2, 2, 2, 2, 2, 2, 2, 2},
        .oe = {0, 0, 0, 0, 0, 0, 0, 0},
        .mode = {6, 6, 6, 6, 6, 6, 6, 6},
        .input = {2, 1, 3, 4, 5, 6, 7, 8, 9, 11},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL10P8",
        .pins = 20, .outputs = 8, .width = 20, .qf = 328,
        .pin = {19, 18, 17, 16, 15, 14, 13, 12},
        .terms = {2, 2, 2, 2, 2, 2, 2, 2},
        .oe = {0, 0, 0, 0, 0, 0, 0, 0},
        .mode = {6, 6, 6, 6, 6, 6, 6, 6},
        .input = {2, 1, 3, 4, 5, 6, 7, 8, 9, 11},
        .ncfg = 1, .cfg = {320}, .stride = {1},
        .nrules = 2, .rules = {{6, 0, 6}, {6, 1, 7}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL12H10",
        .pins = 24, .outputs = 10, .width = 24, .qf = 480,
        .pin = {23, 22, 21, 20, 19, 18, 17, 16, 15, 14},
        .terms = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
        .oe = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        .mode = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7},
        .input = {2, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL12H6",
        .pins = 20, .outputs = 6, .width = 24, .qf = 384,
        .pin = {18, 17, 16, 15, 14, 13},
        .terms = {4, 2, 2, 2, 2, 4},
        .oe = {0, 0, 0, 0, 0, 0},
        .mode = {7, 7, 7, 7, 7, 7},
        .input = {2, 1, 3, 19, 4, 5, 6, 7, 8, 12, 9, 11},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL12L10",
        .pins = 24, .outputs = 10, .width = 24, .qf = 480,
        .pin = {23, 22, 21, 20, 19, 18, 17, 16, 15, 14},
        .terms = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
        .oe = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        .mode = {6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
        .input = {2, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL12L6",
        .pins = 20, .outputs = 6, .width = 24, .qf = 384,
        .pin = {18, 17, 16, 15, 14, 13},
        .terms = {4, 2, 2, 2, 2, 4},
        .oe = {0, 0, 0, 0, 0, 0},
        .mode = {6, 6, 6, 6, 6, 6},
        .input = {2, 1, 3, 19, 4, 5, 6, 7, 8, 12, 9, 11},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL12P10",
        .pins = 24, .outputs = 10, .width = 24, .qf = 490,
        .pin = {23, 22, 21, 20, 19, 18, 17, 16, 15, 14},
        .terms = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
        .oe = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        .mode = {6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
        .input = {2, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13},
        .ncfg = 1, .cfg = {480}, .stride = {1},
        .nrules = 2, .rules = {{6, 0, 6}, {6, 1, 7}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL12P6",
        .pins = 20, .outputs = 6, .width = 24, .qf = 390,
        .pin = {18, 17, 16, 15, 14, 13},
        .terms = {4, 2, 2, 2, 2, 4},
        .oe = {0, 0, 0, 0, 0, 0},
        .mode = {6, 6, 6, 6, 6, 6},
        .input = {2, 1, 3, 19, 4, 5, 6, 7, 8, 12, 9, 11},
        .ncfg = 1, .cfg = {384}, .stride = {1},
        .nrules = 2, .rules = {{6, 0, 6}, {6, 1, 7}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL14H4",
        .pins = 20, .outputs = 4, .width = 28, .qf = 448,
        .pin = {17, 16, 15, 14},
        .terms = {4, 4, 4, 4},
        .oe = {0, 0, 0, 0},
        .mode = {7, 7, 7, 7},
        .input = {2, 1, 3, 19, 4, 18, 5, 6, 7, 13, 8, 12, 9, 11},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL14H8",
        .pins = 24, .outputs = 8, .width = 28, .qf = 560,
        .pin = {22, 21, 20, 19, 18, 17, 16, 15},
        .terms = {4, 2, 2, 2, 2, 2, 2, 4},
        .oe = {0, 0, 0, 0, 0, 0, 0, 0},
        .mode = {7, 7, 7, 7, 7, 7, 7, 7},
        .input = {2, 1, 3, 23, 4, 5, 6, 7, 8, 9, 10, 14, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL14L4",
        .pins = 20, .outputs = 4, .width = 28, .qf = 448,
        .pin = {17, 16, 15, 14},
        .terms = {4, 4, 4, 4},
        .oe = {0, 0, 0, 0},
        .mode = {6, 6, 6, 6},
        .input = {2, 1, 3, 19, 4, 18, 5, 6, 7, 13, 8, 12, 9, 11},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL14L8",
        .pins = 24, .outputs = 8, .width = 28, .qf = 560,
        .pin = {22, 21, 20, 19, 18, 17, 16, 15},
        .terms = {4, 2, 2, 2, 2, 2, 2, 4},
        .oe = {0, 0, 0, 0, 0, 0, 0, 0},
        .mode = {6, 6, 6, 6, 6, 6, 6, 6},
        .input = {2, 1, 3, 23, 4, 5, 6, 7, 8, 9, 10, 14, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL14P4",
        .pins = 20, .outputs = 4, .width = 28, .qf = 452,
        .pin = {17, 16, 15, 14},
        .terms = {4, 4, 4, 4},
        .oe = {0, 0, 0, 0},
        .mode = {6, 6, 6, 6},
        .input = {2, 1, 3, 19, 4, 18, 5, 6, 7, 13, 8, 12, 9, 11},
        .ncfg = 1, .cfg = {448}, .stride = {1},
        .nrules = 2, .rules = {{6, 0, 6}, {6, 1, 7}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL14P8",
        .pins = 24, .outputs = 8, .width = 28, .qf = 568,
        .pin = {22, 21, 20, 19, 18, 17, 16, 15},
        .terms = {4, 2, 2, 2, 2, 2, 2, 4},
        .oe = {0, 0, 0, 0, 0, 0, 0, 0},
        .mode = {6, 6, 6, 6, 6, 6, 6, 6},
        .input = {2, 1, 3, 23, 4, 5, 6, 7, 8, 9, 10, 14, 11, 13},
        .ncfg = 1, .cfg = {560}, .stride = {1},
        .nrules = 2, .rules = {{6, 0, 6}, {6, 1, 7}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL16C1",
        .pins = 20, .outputs = 2, .width = 32, .qf = 512,
        .pin = {16, 15},
        .terms = {16, 0},
        .oe = {0, 0},
        .mode = {7, 6},
        .input = {2, 1, 3, 19, 4, 18, 5, 17, 6, 14, 7, 13, 8, 12, 9, 11},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL16H2",
        .pins = 20, .outputs = 2, .width = 32, .qf = 512,
        .pin = {16, 15},
        .terms = {8, 8},
        .oe = {0, 0},
        .mode = {7, 7},
        .input = {2, 1, 3, 19, 4, 18, 5, 17, 6, 14, 7, 13, 8, 12, 9, 11},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL16H6",
        .pins = 24, .outputs = 6, .width = 32, .qf = 640,
        .pin = {21, 20, 19, 18, 17, 16},
        .terms = {4, 4, 2, 2, 4, 4},
        .oe = {0, 0, 0, 0, 0, 0},
        .mode = {7, 7, 7, 7, 7, 7},
        .input = {2, 1, 3, 23, 4, 22, 5, 6, 7, 8, 9, 15, 10, 14, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL16H8",
        .pins = 20, .outputs = 8, .width = 32, .qf = 2048,
        .pin = {19, 18, 17, 16, 15, 14, 13, 12},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 8, 16, 24, 32, 40, 48, 56},
        .mode = {33, 31, 31, 31, 31, 31, 31, 33},
        .input = {2, 1, 3, 18, 4, 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 11},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL16L2",
        .pins = 20, .outputs = 2, .width = 32, .qf = 512,
        .pin = {16, 15},
        .terms = {8, 8},
        .oe = {0, 0},
        .mode = {6, 6},
        .input = {2, 1, 3, 19, 4, 18, 5, 17, 6, 14, 7, 13, 8, 12, 9, 11},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL16L6",
        .pins = 24, .outputs = 6, .width = 32, .qf = 640,
        .pin = {21, 20, 19, 18, 17, 16},
        .terms = {4, 4, 2, 2, 4, 4},
        .oe = {0, 0, 0, 0, 0, 0},
        .mode = {6, 6, 6, 6, 6, 6},
        .input = {2, 1, 3, 23, 4, 22, 5, 6, 7, 8, 9, 15, 10, 14, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL16L8",
        .pins = 20, .outputs = 8, .width = 32, .qf = 2048,
        .pin = {19, 18, 17, 16, 15, 14, 13, 12},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 8, 16, 24, 32, 40, 48, 56},
        .mode = {32, 30, 30, 30, 30, 30, 30, 32},
        .input = {2, 1, 3, 18, 4, 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 11},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL16P2",
        .pins = 20, .outputs = 2, .width = 32, .qf = 514,
        .pin = {16, 15},
        .terms = {8, 8},
        .oe = {0, 0},
        .mode = {6, 6},
        .input = {2, 1, 3, 19, 4, 18, 5, 17, 6, 14, 7, 13, 8, 12, 9, 11},
        .ncfg = 1, .cfg = {512}, .stride = {1},
        .nrules = 2, .rules = {{6, 0, 6}, {6, 1, 7}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL16P6",
        .pins = 24, .outputs = 6, .width = 32, .qf = 646,
        .pin = {21, 20, 19, 18, 17, 16},
        .terms = {4, 4, 2, 2, 4, 4},
        .oe = {0, 0, 0, 0, 0, 0},
        .mode = {6, 6, 6, 6, 6, 6},
        .input = {2, 1, 3, 23, 4, 22, 5, 6, 7, 8, 9, 15, 10, 14, 11, 13},
        .ncfg = 1, .cfg = {640}, .stride = {1},
        .nrules = 2, .rules = {{6, 0, 6}, {6, 1, 7}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL16P8",
        .pins = 20, .outputs = 8, .width = 32, .qf = 2056,
        .pin = {19, 18, 17, 16, 15, 14, 13, 12},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 8, 16, 24, 32, 40, 48, 56},
        .mode = {32, 30, 30, 30, 30, 30, 30, 32},
        .input = {2, 1, 3, 18, 4, 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 11},
        .ncfg = 1, .cfg = {2048}, .stride = {1},
        .nrules = 4, .rules = {{32, 0, 32}, {32, 1, 33}, {30, 0, 30}, {30, 1, 31}},
        .nspecial = 2,
        .special = {10, 20},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL16R4",
        .pins = 20, .outputs = 8, .width = 32, .qf = 2048,
        .pin = {19, 18, 17, 16, 15, 14, 13, 12},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 8, 11, 11, 11, 11, 48, 56},
        .mode = {30, 30, 4, 4, 4, 4, 30, 30},
        .input = {2, 19, 3, 18, 4, 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 12},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 4,
        .special = {1, 10, 11, 20},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL16R6",
        .pins = 20, .outputs = 8, .width = 32, .qf = 2048,
        .pin = {19, 18, 17, 16, 15, 14, 13, 12},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 11, 11, 11, 11, 11, 11, 56},
        .mode = {30, 4, 4, 4, 4, 4, 4, 30},
        .input = {2, 19, 3, 18, 4, 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 12},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 4,
        .special = {1, 10, 11, 20},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL16R8",
        .pins = 20, .outputs = 8, .width = 32, .qf = 2048,
        .pin = {19, 18, 17, 16, 15, 14, 13, 12},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {11, 11, 11, 11, 11, 11, 11, 11},
        .mode = {4, 4, 4, 4, 4, 4, 4, 4},
        .input = {2, 19, 3, 18, 4, 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 12},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 4,
        .special = {1, 10, 11, 20},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL16RA8",
        .pins = 20, .outputs = 8, .width = 32, .qf = 2056,
        .pin = {19, 18, 17, 16, 15, 14, 13, 12},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 8, 16, 24, 32, 40, 48, 56},
        .mode = {12, 12, 12, 12, 12, 12, 12, 12},
        .input = {2, 19, 3, 18, 4, 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 12},
        .ncfg = 1, .cfg = {2048}, .stride = {1},
        .nrules = 2, .rules = {{12, 0, 12}, {12, 1, 13}},
        .nspecial = 4,
        .special = {1, 10, 11, 20},
        .special_name = {"/PL", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL16RP4",
        .pins = 20, .outputs = 8, .width = 32, .qf = 2056,
        .pin = {19, 18, 17, 16, 15, 14, 13, 12},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 8, 11, 11, 11, 11, 48, 56},
        .mode = {30, 30, 4, 4, 4, 4, 30, 30},
        .input = {2, 19, 3, 18, 4, 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 12},
        .ncfg = 1, .cfg = {2048}, .stride = {1},
        .nrules = 4, .rules = {{30, 0, 30}, {30, 1, 31}, {4, 0, 4}, {4, 1, 5}},
        .nspecial = 4,
        .special = {1, 10, 11, 20},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL16RP6",
        .pins = 20, .outputs = 8, .width = 32, .qf = 2056,
        .pin = {19, 18, 17, 16, 15, 14, 13, 12},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 11, 11, 11, 11, 11, 11, 56},
        .mode = {30, 4, 4, 4, 4, 4, 4, 30},
        .input = {2, 19, 3, 18, 4, 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 12},
        .ncfg = 1, .cfg = {2048}, .stride = {1},
        .nrules = 4, .rules = {{30, 0, 30}, {30, 1, 31}, {4, 0, 4}, {4, 1, 5}},
        .nspecial = 4,
        .special = {1, 10, 11, 20},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL16RP8",
        .pins = 20, .outputs = 8, .width = 32, .qf = 2056,
        .pin = {19, 18, 17, 16, 15, 14, 13, 12},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {11, 11, 11, 11, 11, 11, 11, 11},
        .mode = {4, 4, 4, 4, 4, 4, 4, 4},
        .input = {2, 19, 3, 18, 4, 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 12},
        .ncfg = 1, .cfg = {2048}, .stride = {1},
        .nrules = 2, .rules = {{4, 0, 4}, {4, 1, 5}},
        .nspecial = 4,
        .special = {1, 10, 11, 20},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL18H4",
        .pins = 24, .outputs = 4, .width = 36, .qf = 720,
        .pin = {20, 19, 18, 17},
        .terms = {6, 4, 4, 6},
        .oe = {0, 0, 0, 0},
        .mode = {7, 7, 7, 7},
        .input = {2, 1, 3, 23, 4, 22, 5, 21, 6, 7, 8, 16, 9, 15, 10, 14, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL18L4",
        .pins = 24, .outputs = 4, .width = 36, .qf = 720,
        .pin = {20, 19, 18, 17},
        .terms = {6, 4, 4, 6},
        .oe = {0, 0, 0, 0},
        .mode = {6, 6, 6, 6},
        .input = {2, 1, 3, 23, 4, 22, 5, 21, 6, 7, 8, 16, 9, 15, 10, 14, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL18P4",
        .pins = 24, .outputs = 4, .width = 36, .qf = 724,
        .pin = {20, 19, 18, 17},
        .terms = {6, 4, 4, 6},
        .oe = {0, 0, 0, 0},
        .mode = {6, 6, 6, 6},
        .input = {2, 1, 3, 23, 4, 22, 5, 21, 6, 7, 8, 16, 9, 15, 10, 14, 11, 13},
        .ncfg = 1, .cfg = {720}, .stride = {1},
        .nrules = 2, .rules = {{6, 0, 6}, {6, 1, 7}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL20C1",
        .pins = 24, .outputs = 2, .width = 40, .qf = 640,
        .pin = {19, 18},
        .terms = {16, 0},
        .oe = {0, 0},
        .mode = {7, 6},
        .input = {2, 1, 3, 23, 4, 22, 5, 21, 6, 20, 7, 17, 8, 16, 9, 15, 10, 14, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL20H10",
        .pins = 24, .outputs = 10, .width = 40, .qf = 1600,
        .pin = {23, 22, 21, 20, 19, 18, 17, 16, 15, 14},
        .terms = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4},
        .oe = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36},
        .mode = {33, 31, 31, 31, 31, 31, 31, 31, 31, 33},
        .input = {2, 1, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL20H2",
        .pins = 24, .outputs = 2, .width = 40, .qf = 640,
        .pin = {19, 18},
        .terms = {8, 8},
        .oe = {0, 0},
        .mode = {7, 7},
        .input = {2, 1, 3, 23, 4, 22, 5, 21, 6, 20, 7, 17, 8, 16, 9, 15, 10, 14, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL20H8",
        .pins = 24, .outputs = 8, .width = 40, .qf = 2560,
        .pin = {22, 21, 20, 19, 18, 17, 16, 15},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 8, 16, 24, 32, 40, 48, 56},
        .mode = {33, 31, 31, 31, 31, 31, 31, 33},
        .input = {2, 1, 3, 23, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 14, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL20L10",
        .pins = 24, .outputs = 10, .width = 40, .qf = 1600,
        .pin = {23, 22, 21, 20, 19, 18, 17, 16, 15, 14},
        .terms = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4},
        .oe = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36},
        .mode = {32, 30, 30, 30, 30, 30, 30, 30, 30, 32},
        .input = {2, 1, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL20L2",
        .pins = 24, .outputs = 2, .width = 40, .qf = 640,
        .pin = {19, 18},
        .terms = {8, 8},
        .oe = {0, 0},
        .mode = {6, 6},
        .input = {2, 1, 3, 23, 4, 22, 5, 21, 6, 20, 7, 17, 8, 16, 9, 15, 10, 14, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL20L8",
        .pins = 24, .outputs = 8, .width = 40, .qf = 2560,
        .pin = {22, 21, 20, 19, 18, 17, 16, 15},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 8, 16, 24, 32, 40, 48, 56},
        .mode = {32, 30, 30, 30, 30, 30, 30, 32},
        .input = {2, 1, 3, 23, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 14, 11, 13},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL20P10",
        .pins = 24, .outputs = 10, .width = 40, .qf = 1610,
        .pin = {23, 22, 21, 20, 19, 18, 17, 16, 15, 14},
        .terms = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4},
        .oe = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36},
        .mode = {32, 30, 30, 30, 30, 30, 30, 30, 30, 32},
        .input = {2, 1, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 13},
        .ncfg = 1, .cfg = {1600}, .stride = {1},
        .nrules = 4, .rules = {{30, 0, 30}, {30, 1, 31}, {32, 0, 32}, {32, 1, 33}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL20P2",
        .pins = 24, .outputs = 2, .width = 40, .qf = 642,
        .pin = {19, 18},
        .terms = {8, 8},
        .oe = {0, 0},
        .mode = {6, 6},
        .input = {2, 1, 3, 23, 4, 22, 5, 21, 6, 20, 7, 17, 8, 16, 9, 15, 10, 14, 11, 13},
        .ncfg = 1, .cfg = {640}, .stride = {1},
        .nrules = 2, .rules = {{6, 0, 6}, {6, 1, 7}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL20P8",
        .pins = 24, .outputs = 8, .width = 40, .qf = 2568,
        .pin = {22, 21, 20, 19, 18, 17, 16, 15},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 8, 16, 24, 32, 40, 48, 56},
        .mode = {32, 30, 30, 30, 30, 30, 30, 32},
        .input = {2, 1, 3, 23, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 14, 11, 13},
        .ncfg = 1, .cfg = {2560}, .stride = {1},
        .nrules = 4, .rules = {{30, 0, 30}, {30, 1, 31}, {32, 0, 32}, {32, 1, 33}},
        .nspecial = 2,
        .special = {12, 24},
        .special_name = {"GND", "VCC"},
    },
    {
        .name = "PAL20R4",
        .pins = 24, .outputs = 8, .width = 40, .qf = 2560,
        .pin = {22, 21, 20, 19, 18, 17, 16, 15},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 8, 13, 13, 13, 13, 48, 56},
        .mode = {30, 30, 4, 4, 4, 4, 30, 30},
        .input = {2, 23, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 14},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 4,
        .special = {1, 12, 13, 24},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL20R6",
        .pins = 24, .outputs = 8, .width = 40, .qf = 2560,
        .pin = {22, 21, 20, 19, 18, 17, 16, 15},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 13, 13, 13, 13, 13, 13, 56},
        .mode = {30, 4, 4, 4, 4, 4, 4, 30},
        .input = {2, 23, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 14},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 4,
        .special = {1, 12, 13, 24},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL20R8",
        .pins = 24, .outputs = 8, .width = 40, .qf = 2560,
        .pin = {22, 21, 20, 19, 18, 17, 16, 15},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {13, 13, 13, 13, 13, 13, 13, 13},
        .mode = {4, 4, 4, 4, 4, 4, 4, 4},
        .input = {2, 23, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 14},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 4,
        .special = {1, 12, 13, 24},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL20RA10",
        .pins = 24, .outputs = 10, .width = 40, .qf = 3210,
        .pin = {23, 22, 21, 20, 19, 18, 17, 16, 15, 14},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72},
        .mode = {12, 12, 12, 12, 12, 12, 12, 12, 12, 12},
        .input = {2, 23, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 14},
        .ncfg = 1, .cfg = {3200}, .stride = {1},
        .nrules = 2, .rules = {{12, 0, 12}, {12, 1, 13}},
        .nspecial = 4,
        .special = {1, 12, 13, 24},
        .special_name = {"/PL", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL20RP4",
        .pins = 24, .outputs = 8, .width = 40, .qf = 2568,
        .pin = {22, 21, 20, 19, 18, 17, 16, 15},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 8, 13, 13, 13, 13, 48, 56},
        .mode = {30, 30, 4, 4, 4, 4, 30, 30},
        .input = {2, 23, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 14},
        .ncfg = 1, .cfg = {2560}, .stride = {1},
        .nrules = 4, .rules = {{30, 0, 30}, {30, 1, 31}, {4, 0, 4}, {4, 1, 5}},
        .nspecial = 4,
        .special = {1, 12, 13, 24},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL20RP6",
        .pins = 24, .outputs = 8, .width = 40, .qf = 2568,
        .pin = {22, 21, 20, 19, 18, 17, 16, 15},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {0, 13, 13, 13, 13, 13, 13, 56},
        .mode = {30, 4, 4, 4, 4, 4, 4, 30},
        .input = {2, 23, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 14},
        .ncfg = 1, .cfg = {2560}, .stride = {1},
        .nrules = 4, .rules = {{30, 0, 30}, {30, 1, 31}, {4, 0, 4}, {4, 1, 5}},
        .nspecial = 4,
        .special = {1, 12, 13, 24},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL20RP8",
        .pins = 24, .outputs = 8, .width = 40, .qf = 2568,
        .pin = {22, 21, 20, 19, 18, 17, 16, 15},
        .terms = {8, 8, 8, 8, 8, 8, 8, 8},
        .oe = {13, 13, 13, 13, 13, 13, 13, 13},
        .mode = {4, 4, 4, 4, 4, 4, 4, 4},
        .input = {2, 23, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 14},
        .ncfg = 1, .cfg = {2560}, .stride = {1},
        .nrules = 2, .rules = {{4, 0, 4}, {4, 1, 5}},
        .nspecial = 4,
        .special = {1, 12, 13, 24},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL20X10",
        .pins = 24, .outputs = 10, .width = 40, .qf = 1600,
        .pin = {23, 22, 21, 20, 19, 18, 17, 16, 15, 14},
        .terms = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4},
        .oe = {13, 13, 13, 13, 13, 13, 13, 13, 13, 13},
        .mode = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16},
        .input = {2, 23, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 14},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 4,
        .special = {1, 12, 13, 24},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL20X4",
        .pins = 24, .outputs = 10, .width = 40, .qf = 1600,
        .pin = {23, 22, 21, 20, 19, 18, 17, 16, 15, 14},
        .terms = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4},
        .oe = {0, 4, 8, 13, 13, 13, 13, 28, 32, 36},
        .mode = {30, 30, 30, 16, 16, 16, 16, 30, 30, 30},
        .input = {2, 23, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 14},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 4,
        .special = {1, 12, 13, 24},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
    {
        .name = "PAL20X8",
        .pins = 24, .outputs = 10, .width = 40, .qf = 1600,
        .pin = {23, 22, 21, 20, 19, 18, 17, 16, 15, 14},
        .terms = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4},
        .oe = {0, 13, 13, 13, 13, 13, 13, 13, 13, 36},
        .mode = {30, 16, 16, 16, 16, 16, 16, 16, 16, 30},
        .input = {2, 23, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 14},
        .ncfg = 0, .cfg = {0}, .stride = {0},
        .nrules = 0, .rules = {{0}},
        .nspecial = 4,
        .special = {1, 12, 13, 24},
        .special_name = {"CLK", "GND", "/OE", "VCC"},
    },
};
/* clang-format on */

static int mode_pair(int m)
{
    return m & ~1;
}

static int mode_is_active_low(int m)
{
    return !(m & 1);
}

static int mode_is_registered(int m)
{
    return m < MODE_FIXED;
}

static int mode_has_oe_term(int m)
{
    return m >= MODE_TRISTATE;
}

/* Whether NAME contains "NC" in any case. */
static int label_contains_nc(const char *name)
{
    for (; *name; name++)
        if (toupper((unsigned char)name[0]) == 'N' && toupper((unsigned char)name[1]) == 'C')
            return 1;
    return 0;
}

static const struct pal_device *find_pal_device(const char *name)
{
    if (!strcasecmp(name, "EC16P8"))
        name = "PAL1016P8";
    if (!strcasecmp(name, "G20RA10"))
        name = "GAL20RA10";
    for (size_t i = 0; i < sizeof(devices) / sizeof(*devices); i++)
        if (!strcasecmp(name, devices[i].name))
            return &devices[i];
    return NULL;
}

int pal_supports_device(const char *device)
{
    return find_pal_device(device) != NULL;
}

struct state
{
    const struct pal_device *d;
    const unsigned char *f;
    label_t name[MAX_PINS]; /* pin names without a polarity slash */
    int invert[MAX_PINS];   /* the pin is active low */
    int used[MAX_PINS];     /* the pin is an input to an active term */
    int owner[MAX_PINS];    /* output driving the pin, or -1 */
    int mode[16];           /* each output's mode after the configuration rules */
    int base[16];           /* each output's first product term */
    int has_terms[16];      /* the output has active terms of its own */
    int active[16];         /* the output gets an equation */
    int term[128];          /* design term of each active product term */
    int always;             /* design term that is constant true */
};

/* Only an entirely cleared term is inactive; mixed terms keep every literal. */
static int term_is_active(const struct state *s, int row)
{
    for (int c = 0; c < s->d->width; c++)
        if (s->f[row * s->d->width + c])
            return 1;
    return 0;
}

static int term_is_constant_high(const struct state *s, int row)
{
    for (int c = 0; c < s->d->width; c++)
        if (!s->f[row * s->d->width + c])
            return 0;
    return 1;
}

/* Apply the configuration rules and find the active terms and used pins. */
static void decode_outputs(struct state *s)
{
    const struct pal_device *d = s->d;
    int row                    = 0;
    for (int p = 0; p < MAX_PINS; p++)
        s->owner[p] = -1;
    for (int o = 0; o < d->outputs; o++) {
        int bits = 0, m = d->mode[o];
        s->owner[d->pin[o]] = o;
        s->base[o]          = row;
        for (int c = 0; c < d->ncfg; c++)
            bits = bits * 2 + s->f[d->cfg[c] + o * d->stride[c]];
        for (int c = 0; c < d->nrules; c++)
            if (d->rules[c].from == m && d->rules[c].bits == bits) {
                m = d->rules[c].to;
                break;
            }
        s->mode[o] = m;
        for (int t = 0; t < d->terms[o]; t++, row++)
            if (term_is_active(s, row)) {
                s->has_terms[o] = 1;
                for (int c = 0; c < d->width; c += 2)
                    if (!s->f[row * d->width + c] || !s->f[row * d->width + c + 1])
                        s->used[d->input[c / 2]] = 1;
            }
    }
    for (int o = 0; o < d->outputs; o++) {
        s->active[o] = s->has_terms[o];
        /* A registered or always enabled output is written when its pin
         * feeds the array, even without terms of its own. */
        if (s->used[d->pin[o]] && s->mode[o] < MODE_FIXED_IO)
            s->active[o] = 1;
        /* An output without terms shares those of the output before it. */
        if (!d->terms[o] && o)
            s->active[o] = s->active[o - 1];
    }
}

/* Name every pin from its label, its role or the device's fixed pins. */
static void name_pins(struct state *s, const label_t *labels)
{
    const struct pal_device *d = s->d;
    for (int p = 1; p <= d->pins; p++) {
        const char *n = labels[p];
        int owner     = s->owner[p];
        if (label_is_inverted(n)) {
            s->invert[p] = 1;
            n++;
        }
        if (*n)
            snprintf(s->name[p], MAX_LABEL, "%s", n);
        else if (owner >= 0 && (s->active[owner] || (s->used[p] && !mode_has_oe_term(s->mode[owner]))))
            snprintf(s->name[p], MAX_LABEL, "%s%c%d", mode_is_registered(s->mode[owner]) ? "r" : "",
                     s->used[p] ? 'f' : 'o', p);
        else if (s->used[p])
            snprintf(s->name[p], MAX_LABEL, "i%d", p);
    }
    for (int c = 0; c < d->nspecial; c++) {
        int p = d->special[c];
        if (!strcasecmp(d->special_name[c], "/OE"))
            s->invert[p] = 1;
        if (!*s->name[p]) {
            const char *n = d->special_name[c];
            if (label_is_inverted(n)) {
                s->invert[p] = 1;
                n++;
            }
            snprintf(s->name[p], MAX_LABEL, "%s", n);
        }
    }
}

static int check_duplicate_labels(const struct state *s, FILE *err)
{
    const struct pal_device *d = s->d;
    int failures = 0, present[MAX_PINS] = {0}, output_pin[MAX_PINS] = {0};
    int special_pin[MAX_PINS] = {0}, term_pin[MAX_PINS] = {0};
    int complementary =
        !strcasecmp(d->name, "PAL16C4") || !strcasecmp(d->name, "PAL16C1") || !strcasecmp(d->name, "PAL20C1");
    for (int p = 1; p <= d->pins; p++)
        present[p] = s->used[p];
    /* The clock and output enable inputs always take part in the duplicate
     * check, against outputs that have product terms; power, reset and
     * preload pins do not. */
    for (int c = 0; c < d->nspecial; c++) {
        const char *n = label_without_polarity(d->special_name[c]);
        if (!strncmp(n, "CLK", 3) || !strcmp(n, "OE")) {
            present[d->special[c]]     = 1;
            special_pin[d->special[c]] = 1;
        }
    }
    for (int o = 0; o < d->outputs; o++)
        if (d->pin[o] && s->has_terms[o])
            term_pin[d->pin[o]] = 1;
    for (int o = 0; o < d->outputs; o++)
        if (d->pin[o] && s->active[o]) {
            present[d->pin[o]]    = 1;
            output_pin[d->pin[o]] = 1;
        }
    for (int p = 1; p <= d->pins; p++)
        if (present[p])
            for (int c = 1; c < p; c++) {
                int exempt = 0;
                if (!present[c] || strcmp(s->name[p], s->name[c]) || (!output_pin[p] && !output_pin[c]))
                    continue;
                if ((special_pin[p] && !term_pin[c]) || (special_pin[c] && !term_pin[p]))
                    continue;
                /* The two pins of a complementary output may share a name, and
                 * a pin marked NC is not written. */
                if (complementary) {
                    if (label_contains_nc(s->name[p]))
                        exempt = 1;
                    for (int o = 0; o + 1 < d->outputs; o += 2)
                        if ((d->pin[o] == p && d->pin[o + 1] == c) || (d->pin[o] == c && d->pin[o + 1] == p))
                            exempt = 1;
                }
                if (!exempt) {
                    fprintf(err, "E1803: Duplicate label '%s' found on pins %d and %d.\n", s->name[p], p, c);
                    failures++;
                }
            }
    return failures == 0;
}

/* The ECL decoder keeps an inactive output when its final equation polarity
 * is inverted. Naming comes first, so such an output without a name cannot be
 * written. Returns 0 in that case. */
static int keep_inverted_ecl_outputs(struct state *s)
{
    const struct pal_device *d = s->d;
    if (strcasecmp(d->name, "PAL16PE8") && strcasecmp(d->name, "PAL16P4") && strcasecmp(d->name, "PAL1016P8"))
        return 1;
    for (int o = 0; o < d->outputs; o++) {
        int m = s->mode[o];
        if ((mode_pair(m) == MODE_FIXED || mode_pair(m) == MODE_FIXED_IO) &&
            (mode_is_active_low(m) ^ s->invert[d->pin[o]])) {
            if (!*s->name[d->pin[o]])
                return 0;
            s->active[o] = 1;
        }
    }
    return 1;
}

/* The active terms among COUNT terms from ROW. */
static struct sum sum_of_products(const struct state *s, int row, int count, struct design *d)
{
    struct sum sum = design_sum(d);
    for (int i = 0; i < count; i++)
        if (term_is_active(s, row + i))
            design_sum_add(d, &sum, s->term[row + i], -1);
    return sum;
}

static struct sum single_term(const struct state *s, int row, struct design *d)
{
    return design_single(d, term_is_active(s, row) ? s->term[row] : -1);
}

/* An output enable through the active-low pin OE, ANDed with SUM. FIXED when
 * the pin alone enables the output. */
static void gated_output_enable(struct design *d, int p, struct sum sum, int oe, int fixed)
{
    struct equation *e = design_equation(d, p, EQ_OUTPUT_ENABLE, 0);
    e->fixed           = fixed;
    e->sum             = sum;
    e->gate            = (struct literal){(unsigned char)oe, 1};
}

/* Registered XOR output: the first two terms XOR the next two. */
static void xor_output(const struct state *s, int o, int p, struct design *d)
{
    struct sum sum     = sum_of_products(s, s->base[o], 2, d);
    struct sum x       = sum_of_products(s, s->base[o] + 2, 2, d);
    struct equation *e = design_equation(d, p, EQ_REGISTERED, 1);
    e->sum             = sum;
    e->xor_sum         = x;
    gated_output_enable(d, p, design_single(d, s->always), s->d->oe[o], 1);
}

/* Asynchronous register: term 0 enables the output together with the /OE
 * pin, terms 1-3 are clock, reset and preset, and terms 4-7 the sum. Holding
 * both reset and preset bypasses the register. */
static void async_output(const struct state *s, int o, int p, int m, int oe_pin, struct design *d)
{
    static const enum equation_kind control[] = {EQ_CLOCK, EQ_RESET, EQ_PRESET};
    int base                                  = s->base[o];
    int bypass     = term_is_constant_high(s, base + 2) && term_is_constant_high(s, base + 3);
    struct sum sum = sum_of_products(s, base + 4, 4, d);
    design_equation(d, p, bypass ? EQ_COMBINATORIAL : EQ_REGISTERED, mode_is_active_low(m))->sum = sum;
    gated_output_enable(d, p, single_term(s, base, d), oe_pin, 0);
    for (int k = 0; k < 3; k++)
        if (term_is_active(s, base + 1 + k)) {
            sum                                       = single_term(s, base + 1 + k, d);
            design_equation(d, p, control[k], 0)->sum = sum;
        }
}

static void output_equations(const struct state *s, int o, int oe_pin, struct design *d)
{
    const struct pal_device *dev = s->d;
    int p = dev->pin[o], m = s->mode[o], first = mode_has_oe_term(m) ? 1 : 0;
    struct sum sum;
    if (!s->active[o] || !p || !dev->terms[o] || mode_pair(m) == MODE_COMPLEMENT)
        return;
    /* Complementary packages use the other pin when only the first is marked
     * NC. The pin list still keeps both labels. */
    if (o + 1 < dev->outputs && !dev->terms[o + 1] && dev->pin[o + 1] && label_contains_nc(s->name[p]) &&
        !label_contains_nc(s->name[dev->pin[o + 1]])) {
        p = dev->pin[o + 1];
        m = s->mode[o + 1];
    }
    if (m == MODE_XOR) {
        xor_output(s, o, p, d);
        return;
    }
    if (mode_pair(m) == MODE_ASYNC) {
        async_output(s, o, p, m, oe_pin, d);
        return;
    }
    sum = sum_of_products(s, s->base[o] + first, dev->terms[o] - first, d);
    design_equation(d, p, mode_is_registered(m) ? EQ_REGISTERED : EQ_COMBINATORIAL, mode_is_active_low(m))->sum = sum;
    if (mode_has_oe_term(m)) {
        sum                                             = single_term(s, s->base[o], d);
        design_equation(d, p, EQ_OUTPUT_ENABLE, 0)->sum = sum;
    } else if (mode_is_registered(m) && dev->oe[o])
        gated_output_enable(d, p, design_single(d, s->always), dev->oe[o], 1);
}

int pal_decode(const struct conversion *conv, struct design *d)
{
    struct state s               = {.d = find_pal_device(conv->device), .f = conv->fuses};
    const struct pal_device *dev = s.d;
    int oe_pin = 0, rows = 0;
    announce_output();
    decode_outputs(&s);
    name_pins(&s, conv->labels);
    if (!check_duplicate_labels(&s, conv->err))
        return 0;
    if (!keep_inverted_ecl_outputs(&s)) {
        fputs("E0100: Unknown internal error.\n", conv->err);
        return 0;
    }
    design_init(d, FAMILY_PAL, dev->name);
    d->npins = d->package_pins = dev->pins;
    for (int p = 1; p <= dev->pins; p++) {
        strcpy(d->name[p], s.name[p]);
        d->inverted[p] = s.invert[p];
    }
    if (!strcasecmp(dev->name, "GAL20RA10")) {
        d->ues      = s.f + 3210;
        d->ues_bits = 64;
    }
    for (int o = 0; o < dev->outputs; o++)
        rows += dev->terms[o];
    for (int r = 0; r < rows; r++)
        if (term_is_active(&s, r))
            s.term[r] = design_fuse_term(d, s.f + r * dev->width, dev->width, dev->input, NULL);
    s.always = design_term(d, 0, NULL);
    for (int k = 0; k < dev->nspecial; k++)
        if (!strcasecmp(dev->special_name[k], "/OE"))
            oe_pin = dev->special[k];
    for (int o = 0; o < dev->outputs; o++) {
        int m = s.mode[o];
        output_equations(&s, o, oe_pin, d);
        if (!s.active[o] && mode_pair(m) == MODE_XOR && s.used[dev->pin[o]])
            d->idle_register[dev->pin[o]] = (signed char)(mode_is_active_low(m) ? -1 : 1);
    }
    return 1;
}
