#include "video.h"
#include "printk.h"

typedef  unsigned char uint8_t;

void printken(const char* str) {{
    uint8_t* video = (uint8_t*)VIDEO_MEMORY;    // VGAメモリポインタ
    uint8_t attr = WHITE_ON_BLACK;  // 属性

    while (*str)
    {
        *video++ = *str++;  // 文字を書き込み
        *video++ = attr;    // 属性を書き込み
    }
}

// 画面クリア関数
void clear_screen(void) {
    uint8_t* video = (uint8_t*)VIDEO_MEMORY;    // VGAメモリポインタ
    uint8_t attr = WHITE_ON_BLACK;  // 属性

    for (int i = 0; i < 80 * 25; i++)
    {
        *video++ = ' ';     // 空白文字
        *video++ = attr;    // 属性を書き込み
    }
}