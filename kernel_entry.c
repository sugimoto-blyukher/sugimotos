// kernel_entry.c

extern void kernel_main(void); // カーネルメイン関数

__attribute__((section(".text.kernel_entry")))

// カーネルのエントリポイント
void kernel_entry(void)
{
    // カーネルメインを呼び出し
    kernel_main();
}