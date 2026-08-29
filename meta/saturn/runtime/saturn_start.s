/* SEGA Saturn startup code for Hatch Game Engine exports
 *
 * This provides the entry point and basic initialization.
 */

.section .header
#include "sat_header.inc"

.section .text
.global _start

_start:
    /* Initialize stack pointer */
    mov.l   stack_top, r15
    
    /* Clear BSS section */
    mov.l   sbss_addr, r0
    mov.l   ebss_addr, r1
    mov     #0, r2
clear_bss:
    cmp/ge  r0, r1
    bf      clear_bss_end
    mov.b   r2, @r0+
    bra     clear_bss
    nop
clear_bss_end:

    /* Jump to main */
    mov.l   main_addr, r0
    jsr     @r0
    nop
    
    /* Infinite loop if main returns */
halt_loop:
    sleep
    bra     halt_loop
    nop

.align 4
stack_top:    .long   0x06080000
sbss_addr:    .long   _sbss
ebss_addr:    .long   _ebss
main_addr:    .long   _main
