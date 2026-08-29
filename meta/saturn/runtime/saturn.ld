/* Where a Saturn program's pieces go.
 *
 * The whole thing is one blob loaded to 0x06004000 in Work RAM High and jumped
 * to at its first byte, so there is no ROM to run from and no copying of
 * initialised data to do -- text, rodata and data all sit where they are
 * loaded. Only BSS is left to the startup code.
 *
 * elf32-sh, not elf32-littlesh: the Saturn's SH-2s are big endian.
 */

OUTPUT_FORMAT("elf32-sh")
OUTPUT_ARCH(sh)
ENTRY(_start)

MEMORY
{
    ram (rwx) : ORIGIN = 0x06004000, LENGTH = 0x000FC000
}

SECTIONS
{
    .text : {
        *(.text.start)
        *(.text)
        *(.text.*)
    } > ram

    .rodata ALIGN(4) : {
        *(.rodata)
        *(.rodata.*)
    } > ram

    .data ALIGN(4) : {
        *(.data)
        *(.data.*)
    } > ram

    .bss ALIGN(4) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > ram

    _end = .;

    /DISCARD/ : { *(.comment) *(.debug*) }
}
