// kernel.c
#include "ken/printk.c"

// カーネルメイン関数
void kernel_main(void) {
    clear_screen();                     // 画面をクリア
    printken("Kernel started!");    // 文字列を表示

    while (1)                           // 無限ループ
    {
        __asm__ volatile ("cli; hlt");  // 停止
    }
}