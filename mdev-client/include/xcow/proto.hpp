#pragma once

enum AuxBits {
    AUXBITS_VALID = 0x1,
    AUXBITS_WRITABLE = 0x2,
};
enum AuxCommand {
    AUXCMD_KEEP = 0x40000000,
    AUXCMD_FORWARD = 0x20000000,
};
#define AUX_CMD_MASK 0x70000000
#define AUX_MASK 0x3
// reserve 1 bit in the aux shift
// this way we have 3 aux bits per assoc and up to 8-way assoc
#define AUX_SHIFT 3
#define AUX_MAX_ASSOC 8
