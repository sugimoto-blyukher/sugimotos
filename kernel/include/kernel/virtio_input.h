#pragma once

#include "common.h"

struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
};

int virtio_input_init(void);
int virtio_input_next_event(struct virtio_input_event *ev);
void virtio_input_handle_irq(void);

#define VI_EV_SYN 0
#define VI_EV_KEY 1
#define VI_EV_REL 2
#define VI_EV_ABS 3

#define VI_SYN_REPORT 0
#define VI_REL_X 0
#define VI_REL_Y 1
#define VI_REL_WHEEL 8
#define VI_ABS_X 0
#define VI_ABS_Y 1
#define VI_BTN_LEFT 272

#define VI_KEY_ESC 1
#define VI_KEY_1 2
#define VI_KEY_2 3
#define VI_KEY_3 4
#define VI_KEY_4 5
#define VI_KEY_5 6
#define VI_KEY_6 7
#define VI_KEY_7 8
#define VI_KEY_8 9
#define VI_KEY_9 10
#define VI_KEY_0 11
#define VI_KEY_MINUS 12
#define VI_KEY_EQUAL 13
#define VI_KEY_BACKSPACE 14
#define VI_KEY_TAB 15
#define VI_KEY_Q 16
#define VI_KEY_W 17
#define VI_KEY_E 18
#define VI_KEY_R 19
#define VI_KEY_T 20
#define VI_KEY_Y 21
#define VI_KEY_U 22
#define VI_KEY_I 23
#define VI_KEY_O 24
#define VI_KEY_P 25
#define VI_KEY_LEFTBRACE 26
#define VI_KEY_RIGHTBRACE 27
#define VI_KEY_ENTER 28
#define VI_KEY_LEFTCTRL 29
#define VI_KEY_A 30
#define VI_KEY_S 31
#define VI_KEY_D 32
#define VI_KEY_F 33
#define VI_KEY_G 34
#define VI_KEY_H 35
#define VI_KEY_J 36
#define VI_KEY_K 37
#define VI_KEY_L 38
#define VI_KEY_SEMICOLON 39
#define VI_KEY_APOSTROPHE 40
#define VI_KEY_GRAVE 41
#define VI_KEY_LEFTSHIFT 42
#define VI_KEY_BACKSLASH 43
#define VI_KEY_Z 44
#define VI_KEY_X 45
#define VI_KEY_C 46
#define VI_KEY_V 47
#define VI_KEY_B 48
#define VI_KEY_N 49
#define VI_KEY_M 50
#define VI_KEY_COMMA 51
#define VI_KEY_DOT 52
#define VI_KEY_SLASH 53
#define VI_KEY_RIGHTSHIFT 54
#define VI_KEY_SPACE 57
#define VI_KEY_UP 103
#define VI_KEY_DOWN 108
