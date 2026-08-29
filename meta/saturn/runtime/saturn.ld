/* SEGA Saturn linker script for Hatch Game Engine exports
 *
 * This defines the memory layout for the Saturn program.
 */

OUTPUT_FORMAT("elf32-littlesh")
OUTPUT_ARCH(sh)
ENTRY(_start)

MEMORY
{
    rom (rx) : ORIGIN = 0x00000000, LENGTH = 4M
    ram (rwx) : ORIGIN = 0x06000000, LENGTH = 512K
}

SECTIONS
{
    .header : {
        *(.header)
    } > rom

    .text : {
        _stext = .;
        *(.text)
        *(.text.*)
        _etext = .;
    } > rom

    .rodata : {
        _srodata = .;
        *(.rodata)
        *(.rodata.*)
        _erodata = .;
    } > rom

    .data : {
        _sdata = .;
        *(.data)
        *(.data.*)
        _edata = .;
    } > ram AT > rom

    _data_loadaddr = LOADADDR(.data);

    .bss : {
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _ebss = .;
    } > ram

    _end = .;
}
