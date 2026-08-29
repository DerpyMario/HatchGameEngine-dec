! Where a Saturn program starts.
!
! The BIOS -- or an emulator standing in for it -- has already copied this to
! 0x06004000 and jumped to the first byte of it, so _start has to be the first
! thing in the binary. The linker script puts .text.start ahead of everything
! for that reason.
!
! SH-2 branches have a delay slot: the instruction written after a branch runs
! before the branch takes effect. The nops below are that slot, not padding.

    .section .text.start, "ax"
    .global _start
    .align 2

_start:
    ! A stack of our own, at the top of Work RAM High.
    mov.l   stack_top, r15

    ! Zero the BSS. The loader copied only what was in the file, and C expects
    ! everything else to start at zero.
    mov.l   bss_start, r0
    mov.l   bss_end, r1
    mov     #0, r2

bss_loop:
    cmp/hs  r1, r0
    bt      bss_done
    mov.b   r2, @r0
    add     #1, r0
    bra     bss_loop
    nop

bss_done:
    mov.l   main_addr, r0
    jsr     @r0
    nop

    ! main is not supposed to come back. If it does, stop here rather than
    ! running off into whatever follows.
halt:
    bra     halt
    nop

    .align 2
stack_top:  .long 0x06100000
bss_start:  .long __bss_start
bss_end:    .long __bss_end
main_addr:  .long _main
