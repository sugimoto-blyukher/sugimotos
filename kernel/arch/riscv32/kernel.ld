ENTRY(boot)

SECTIONS {
    . = 0x80200000;

    .text : {
        __text_start = .;
        KEEP(*(.text.boot));
        *(.text .text.*);
        __user_text_start = .;
        *(.user.text .user.text.*);
        __user_text_end = .;
        __text_end = .;
    }

    .rodata : ALIGN(4096) {
        __rodata_start = .;
        *(.rodata .rodata.*);
        __user_rodata_start = .;
        *(.user.rodata .user.rodata.*);
        __user_rodata_end = .;
        __rodata_end = .;
    }

    .data ALIGN(4096) : {
        __data_start = .;
        *(.data .data.*);
        __user_data_start = .;
        *(.user.data .user.data.*);
        __user_data_end = .;
        __data_end = .;
    }

    .bss : ALIGN(4) {
        __bss = .;
        *(.bss .bss.* .sbss .sbss.*);
        __user_bss_start = .;
        *(.user.bss .user.bss.*);
        __user_bss_end = .;
        __bss_end = .;
    }

    . = ALIGN(4);
    . += 256 * 1024;
    __stack_top = .;

    . = ALIGN(4096);
    __free_ram = .;
    . += 32 * 1024 * 1024;
    __free_ram_end = .;
}
