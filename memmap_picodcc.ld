/* Custom linker script for PicoDCC
 * Reserves last 4KB of flash (0x101FF000-0x101FFFFF) for configuration storage
 * Firmware limited to 2044KB (0x10000000-0x101FEFFF)
 * 
 * Based on Pico SDK memmap_default.ld for RP2350
 */

MEMORY
{
    /* Firmware flash: 2044KB (2MB - 4KB reserved for config) */
    FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 2044k
    
    /* Configuration sector: 4KB at end of flash (RESERVED - not included in firmware) */
    /* CONFIG(r) : ORIGIN = 0x101FF000, LENGTH = 4k */
    /* Note: CONFIG region commented out - not used by firmware, just documented here */
    
    /* RAM regions - standard RP2350 layout */
    RAM(rwx)      : ORIGIN = 0x20000000, LENGTH = 512k
    SCRATCH_X(rwx): ORIGIN = 0x20080000, LENGTH = 4k
    SCRATCH_Y(rwx): ORIGIN = 0x20081000, LENGTH = 4k
}

/* Define entry point */
ENTRY(_entry_point)

SECTIONS
{
    /* Flash binary start marker */
    .flash_begin : {
        __flash_binary_start = .;
    } > FLASH

    /* Vector table must be first - aligned to 256 bytes minimum for Cortex-M33 */
    .vectors : {
        __vectors_start__ = .;
        KEEP(*(.vectors))
        __vectors_end__ = .;
        . = ALIGN(4);
    } > FLASH

    /* Boot2 bootloader (256 bytes max) */
    .boot2 : {
        __boot2_start__ = .;
        KEEP(*(.boot2))
        __boot2_end__ = .;
    } > FLASH

    ASSERT(__boot2_end__ - __boot2_start__ <= 256,
        "ERROR: boot2 must be <= 256 bytes in size")

    /* Main code section */
    .text : {
        __logical_binary_start = .;
        
        /* Reset and exception handlers */
        KEEP(*(.reset))
        KEEP(*(.time_critical*))
        
        /* Regular code */
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        
        /* C++ constructors/destructors */
        . = ALIGN(4);
        KEEP(*(.init))
        KEEP(*(.fini))
        
        /* Static constructors */
        . = ALIGN(4);
        __preinit_array_start = .;
        KEEP (*(.preinit_array))
        __preinit_array_end = .;
        
        . = ALIGN(4);
        __init_array_start = .;
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        __init_array_end = .;
        
        . = ALIGN(4);
        __fini_array_start = .;
        KEEP (*(.fini_array))
        KEEP (*(SORT(.fini_array.*)))
        __fini_array_end = .;
        
        /* Flashdata section for constants */
        . = ALIGN(4);
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.flashdata*)))
        . = ALIGN(4);
    } > FLASH

    /* ARM exception handling */
    .ARM.extab : {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    __exidx_start = .;
    .ARM.exidx : {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH
    __exidx_end = .;

    /* Binary info (Pico SDK metadata) */
    .binary_info : {
        __binary_info_start = .;
        KEEP(*(.binary_info.keep.*))
        *(.binary_info.*)
        __binary_info_end = .;
    } > FLASH

    /* End of text section */
    . = ALIGN(4);
    __etext = .;

    /* Initialized data section - loaded from flash, copied to RAM on boot */
    .data : {
        __data_start__ = .;
        *(vtable)
        *(.data*)
        . = ALIGN(4);
        *(.after_data.*)
        . = ALIGN(4);
        /* C++ exception handling */
        *(.jcr)
        . = ALIGN(4);
        __data_end__ = .;
    } > RAM AT> FLASH
    
    __data_source__ = LOADADDR(.data);
    __data_size__ = SIZEOF(.data);

    /* Thread-local storage */
    .tdata : {
        __tdata_start__ = .;
        *(.tdata*)
        __tdata_end__ = .;
    } > RAM AT> FLASH
    
    .tbss : {
        __tbss_start__ = .;
        *(.tbss*)
        __tbss_end__ = .;
    } > RAM

    PROVIDE(__data_end__ = .);

    /* Uninitialized data section - zero-initialized on boot */
    .uninitialized_data (NOLOAD): {
        . = ALIGN(4);
        __uninitialized_data_start__ = .;
        *(.uninitialized_data*)
        . = ALIGN(4);
        __uninitialized_data_end__ = .;
    } > RAM

    /* BSS section - zero-initialized on boot */
    .bss : {
        . = ALIGN(4);
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    /* Heap section */
    .heap (NOLOAD): {
        __end__ = .;
        end = __end__;
        __HeapBase = .;
        KEEP(*(.heap*))
        . = ALIGN(4);
    } > RAM

    /* Set heap limit to end of RAM (historically sbrk could grow to stack limit) */
    __HeapLimit = ORIGIN(RAM) + LENGTH(RAM);

    /* Scratch X RAM section (time-critical code copied to RAM) */
    .scratch_x : {
        __scratch_x_start__ = .;
        *(.scratch_x.*)
        . = ALIGN(4);
        __scratch_x_end__ = .;
    } > SCRATCH_X AT > FLASH
    __scratch_x_source__ = LOADADDR(.scratch_x);

    /* Scratch Y RAM section */
    .scratch_y : {
        __scratch_y_start__ = .;
        *(.scratch_y.*)
        . = ALIGN(4);
        __scratch_y_end__ = .;
    } > SCRATCH_Y AT > FLASH
    __scratch_y_source__ = LOADADDR(.scratch_y);

    /* Stack sections (dummy sections for size calculation) */
    .stack1_dummy (NOLOAD): {
        KEEP(*(.stack1*))
    } > SCRATCH_X

    .stack_dummy (NOLOAD): {
        KEEP(*(.stack*))
    } > SCRATCH_Y

    /* Flash binary end marker */
    .flash_end : {
        KEEP(*(.embedded_end_block*))
        PROVIDE(__flash_binary_end = .);
    } > FLASH = 0xaa

    /* Stack symbols */
    __StackLimit = ORIGIN(RAM) + LENGTH(RAM);
    __StackOneTop = ORIGIN(SCRATCH_X) + LENGTH(SCRATCH_X);
    __StackTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);
    __StackOneBottom = __StackOneTop - SIZEOF(.stack1_dummy);
    __StackBottom = __StackTop - SIZEOF(.stack_dummy);
    PROVIDE(__stack = __StackTop);

    /* Check for stack overflow into heap */
    ASSERT(__StackLimit >= __HeapLimit, "region RAM overflowed")

    /* Configuration sector protection - verify firmware doesn't overflow */
    ASSERT(__flash_binary_end <= 0x101FF000, 
        "ERROR: Firmware exceeds 2044KB and would overwrite config sector at 0x101FF000!")
}
