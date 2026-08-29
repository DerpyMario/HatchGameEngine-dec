/* The control pad, read through the SMPC.
 *
 * The SMPC is a microcontroller with a mailbox: fill in the input registers,
 * write a command, wait for it to clear the flag, read the output registers.
 * INTBACK is the command that fetches what the peripherals are doing. */

#include "saturn.h"

/* SMPC registers sit on odd bytes, two apart. */
#define SMPC_IREG(n)   SMPC_REG[0x01 + (n) * 2]
#define SMPC_COMREG    SMPC_REG[0x1F]
#define SMPC_OREG(n)   SMPC_REG[0x21 + (n) * 2]
#define SMPC_SR        SMPC_REG[0x61]
#define SMPC_SF        SMPC_REG[0x63]

void pad_init(void) {
    SMPC_SF = 0;
}

u16 pad_read(void) {
    u16 buttons;
    int guard;

    guard = 0x100000;
    while (SMPC_SF && --guard)
        ;

    SMPC_SF = 1;

    SMPC_IREG(0) = 0x00;   /* no SMPC status wanted */
    SMPC_IREG(1) = 0x0A;   /* peripheral data, 15-byte optimised */
    SMPC_IREG(2) = 0xF0;

    SMPC_COMREG = 0x10;    /* INTBACK */

    guard = 0x100000;
    while (SMPC_SF && --guard)
        ;

    /* OREG0 is the status, OREG1 the port, OREG2 the peripheral type, and the
     * two bytes after it are the buttons. A digital pad reports type 0x02. */
    if ((SMPC_OREG(2) & 0xF0) != 0x00 && SMPC_OREG(2) != 0x02) {
        /* Something that is not a digital pad, or nothing plugged in. */
    }

    buttons = (u16)((SMPC_OREG(3) << 8) | SMPC_OREG(4));

    /* The pad reports a pressed button as a zero. Everything above reads
     * better with them as ones. */
    return (u16)~buttons;
}
