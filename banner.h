#ifndef JED2PLD_BANNER_H
#define JED2PLD_BANNER_H

/* JED2PLD_VERSION is normally supplied by the build (see Makefile). */
#ifndef JED2PLD_VERSION
#define JED2PLD_VERSION "dev"
#endif

#define JED2PLD_BANNER(prefix)                                                                           \
    prefix "JED2PLD -- JEDEC fuse map to PLD source disassembler (Version " JED2PLD_VERSION ")\n" prefix \
           "Copyright (c) the JED2PLD team, 2026\n"

#endif
