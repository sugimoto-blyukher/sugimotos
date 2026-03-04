#include "kernel/wm_apps.h"
#include "kernel/wm.h"

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int wm_launch_builtin(const char *app)
{
    if (str_eq(app, "about")) {
        int id = wm_create("about", 44, 9);
        if (id < 0)
            return -1;
        wm_set_text(id, "miniwm desktop\nX11-like text GUI\ncommands: wm ls/move/resize/focus/close/tile");
        wm_focus(id);
        return id;
    }
    if (str_eq(app, "notes")) {
        int id = wm_create("notes", 36, 12);
        if (id < 0)
            return -1;
        wm_set_text(id, "TODO:\n- build app launcher\n- add keyboard shortcuts\n- add mouse someday");
        wm_focus(id);
        return id;
    }
    if (str_eq(app, "monitor")) {
        int id = wm_create("monitor", 40, 10);
        if (id < 0)
            return -1;
        wm_set_text(id, "system monitor\ncpu: cooperative scheduler\nram: static allocator\nfs: ext4 image");
        wm_focus(id);
        return id;
    }
    return -2;
}
