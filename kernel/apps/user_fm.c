#include "user_app.h"

void fm_run(void) {
    if (g_fm.active) {
        if (u_wm_focus(g_fm.win_id) >= 0) return;
    }
    g_fm.active = 1;
    g_fm.win_id = u_wm_create("FileManager", 400, 300);
    u_wm_set_text(g_fm.win_id, "File Manager (Direct Kernel Access)\nClick to select files.");
    u_wm_render();
}

void filedb_add(const char *path) {
    if (g_file_db_count >= FILE_DB_MAX) return;
    str_copy_lim(g_file_db[g_file_db_count++], path, FM_NAME_MAX);
}

void filedb_remove(const char *path) {
    for (int i = 0; i < g_file_db_count; i++) {
        if (str_eq(g_file_db[i], path)) {
            for (int j = i; j < g_file_db_count - 1; j++)
                str_copy_lim(g_file_db[j], g_file_db[j+1], FM_NAME_MAX);
            g_file_db_count--;
            break;
        }
    }
}

void filedb_rename(const char *old_path, const char *new_path) {
    for (int i = 0; i < g_file_db_count; i++) {
        if (str_eq(g_file_db[i], old_path)) {
            str_copy_lim(g_file_db[i], new_path, FM_NAME_MAX);
            break;
        }
    }
}
