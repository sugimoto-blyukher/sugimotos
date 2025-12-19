/* kernel.ld */

ENTRY(kernel_entry)         /* カーネルのエントリポイント */
OUTPUT_ARCH(i386)           /* ターゲット: i386 */

SECTIONS {
    . = 0x10000;            /* カーネルの開始アドレス */

    .text : {
        KEEP(*(.text.kernel_entry))  /* エントリポイントを保持 */
        *(.text)                     /* 実行可能コード */
    }

    .rodata ALIGN(4) : {    /* 読み取り専用データ */
        *(.rodata)          /* 定数データ */
    }

    .data ALIGN(4) : {      /* 初期化済みデータ */
        *(.data)            /* グローバル変数 */
    }

    .bss ALIGN(4) : {       /* 未初期化データ */
        *(.bss)             /* 未初期化変数 */
    }

    __end = .;              /* カーネルの終了アドレス */
}