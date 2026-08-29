! IP.BIN -- the first thing a Saturn reads off a disc.
!
! The first sixteen sectors of a Saturn CD are outside the filesystem, and this
! is what lives there. The console reads sector zero, checks it says
! "SEGA SEGASATURN", takes the numbers below out of the header, copies this
! whole thing to 0x06002000, loads the first file in the disc's root directory
! to the address at offset 0xF0, and then jumps to 0x06002E00 -- which is offset
! 0xE00 in this file. So the header is a fixed layout and the code has to be at
! exactly that offset, which is what the .org directives below are for.
!
! On a real disc, 0x100 to 0xDFF holds SEGA's security code -- a signed blob
! only they can produce. Nothing here forges it: it is left as zero, which an
! emulator accepts and a retail console will not.

    .section .ip, "ax"
    .global _ip_start

_ip_start:
    .org 0x000
    .include "ip_header.inc"

    ! The security code would be here. It is not ours to write.
    .org 0x100
    .space 0xE00 - 0x100, 0

    ! 0xE00: where the console jumps once the first program is in memory.
    .org 0xE00
    .align 2
ip_entry:
    mov.l   first_program, r0
    jmp     @r0
    nop

    .align 2
first_program:
    .long   0x06004000

    ! Pad to a whole 4 KiB, which is what the header declares as the IP size.
    .org 0x1000
