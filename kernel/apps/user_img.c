#include "user_app.h"

int img_open_path(const char *path) {
    int win = u_wm_create("ImageViewer", 320, 240);
    if (win < 0) return -1;
    u_wm_set_text(win, path);
    u_wm_render();
    return win;
}
