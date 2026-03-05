#include "user_app.h"

#define U_FN static U_TEXT

U_FN void fm_set_status(struct file_manager *fm, const char *s)
{
    str_copy_lim(fm->status, s, sizeof(fm->status));
}

U_FN const char *path_name_part(const char *path)
{
    if (!path || !path[0])
        return path;
    if (path[0] == '/')
        return path + 1;
    return path;
}

U_FN int filedb_find(const char *name)
{
    if (!name || !name[0])
        return -1;
    for (int i = 0; i < g_file_db_count; i++) {
        if (str_eq(g_file_db[i], name))
            return i;
    }
    return -1;
}

void filedb_add(const char *path)
{
    const char *name = path_name_part(path);
    if (!name || !name[0] || filedb_find(name) >= 0)
        return;
    if (g_file_db_count >= FILE_DB_MAX)
        return;
    str_copy_lim(g_file_db[g_file_db_count], name, FM_NAME_MAX);
    g_file_db_count++;
}

void filedb_remove(const char *path)
{
    const char *name = path_name_part(path);
    int idx = filedb_find(name);
    if (idx < 0)
        return;
    for (int i = idx; i + 1 < g_file_db_count; i++)
        str_copy_lim(g_file_db[i], g_file_db[i + 1], FM_NAME_MAX);
    g_file_db_count--;
}

void filedb_rename(const char *old_path, const char *new_path)
{
    const char *old_name = path_name_part(old_path);
    const char *new_name = path_name_part(new_path);
    int idx = filedb_find(old_name);
    if (idx < 0) {
        filedb_add(new_path);
        return;
    }
    str_copy_lim(g_file_db[idx], new_name, FM_NAME_MAX);
}

U_FN const char *fm_selected_name(const struct file_manager *fm)
{
    if (fm->count <= 0 || fm->selected < 0 || fm->selected >= fm->count)
        return NULL;
    if (!fm->names[fm->selected][0] || fm->names[fm->selected][0] == '(')
        return NULL;
    return fm->names[fm->selected];
}

U_FN void fm_make_path(const char *name, char *path, int path_sz)
{
    if (path_sz <= 1)
        return;
    path[0] = '/';
    str_copy_lim(path + 1, name, path_sz - 1);
}

U_FN int fm_is_image_name(const char *name)
{
    return str_ends_with_ci(name, ".ppm") ||
           str_ends_with_ci(name, ".png") ||
           str_ends_with_ci(name, ".jpg") ||
           str_ends_with_ci(name, ".jpeg");
}

U_FN void fm_reload_entries(struct file_manager *fm)
{
    if (g_file_db_count <= 0) {
        fm->count = 1;
        fm->selected = 0;
        str_copy_lim(fm->names[0], "(empty)", FM_NAME_MAX);
        return;
    }
    fm->count = g_file_db_count;
    if (fm->count > FM_MAX_ENTRIES)
        fm->count = FM_MAX_ENTRIES;
    for (int i = 0; i < fm->count; i++)
        str_copy_lim(fm->names[i], g_file_db[i], FM_NAME_MAX);
    if (fm->selected < 0)
        fm->selected = 0;
    if (fm->selected >= fm->count)
        fm->selected = fm->count - 1;
}

U_FN void fm_load_preview(struct file_manager *fm)
{
    fm->preview[0] = '\0';
    const char *name = fm_selected_name(fm);
    if (!name)
        return;
    char path[FM_NAME_MAX + 2];
    fm_make_path(name, path, sizeof(path));
    int fd = u_open(path, O_RDONLY);
    if (fd < 0) {
        str_copy_lim(fm->preview, "preview: open failed", sizeof(fm->preview));
        return;
    }
    int n = u_read(fd, g_iobuf, 256);
    u_close(fd);
    if (n <= 0) {
        str_copy_lim(fm->preview, "preview: empty/unreadable", sizeof(fm->preview));
        return;
    }
    int p = 0;
    for (int i = 0; i < n && p + 1 < (int) sizeof(fm->preview); i++) {
        char c = g_iobuf[i];
        if (c == '\n' || c == '\r' || c == '\t')
            fm->preview[p++] = ' ';
        else if (c >= 32 && c <= 126)
            fm->preview[p++] = c;
        else
            fm->preview[p++] = '.';
    }
    fm->preview[p] = '\0';
}

U_FN int fm_name_is_valid(const char *name)
{
    if (!name || !name[0])
        return 0;
    for (int i = 0; name[i]; i++) {
        if (name[i] == '/')
            return 0;
    }
    return 1;
}

U_FN void fm_render(struct file_manager *fm)
{
    if (!fm->active || fm->win_id <= 0)
        return;
    int p = 0;
    g_textbuf[0] = '\0';
    append_str("File Manager\n", &p);
    append_str("Up/Down: select  Enter: open-image/preview  r:rename  d:delete  l:reload  q/Esc:quit\n", &p);

    if (fm->rename_mode) {
        append_str("rename> ", &p);
        append_str(fm->rename_buf, &p);
        append_str("\nEnter:apply Esc:cancel Backspace:edit\n\n", &p);
    } else {
        append_char('\n', &p);
    }

    int shown = (fm->count < 14) ? fm->count : 14;
    for (int i = 0; i < shown; i++) {
        append_char((i == fm->selected) ? '>' : ' ', &p);
        append_char(' ', &p);
        append_str(fm->names[i], &p);
        append_char('\n', &p);
    }
    append_str("\npreview: ", &p);
    append_str(fm->preview, &p);
    append_str("\nstatus: ", &p);
    append_str(fm->status, &p);

    (void) u_wm_set_text(fm->win_id, g_textbuf);
    u_wm_render();
}

U_FN int fm_poll_key(int win_id)
{
    struct wm_event ev;
    while (1) {
        int rc = u_wm_poll_event(win_id, &ev);
        if (rc < 0)
            return -9;
        if (rc == 0)
            return -1;
        if (ev.type != WM_EV_KEY)
            continue;
        if ((int) ev.b == VI_KEY_UP)
            return -2;
        if ((int) ev.b == VI_KEY_DOWN)
            return -3;
        if ((int) ev.b == VI_KEY_ESC)
            return -4;
        if ((int) ev.b == VI_KEY_ENTER)
            return '\n';
        if ((int) ev.b == VI_KEY_BACKSPACE)
            return 127;
        if (ev.a != 0)
            return (int) ev.a;
    }
}

U_FN void fm_begin_rename(struct file_manager *fm)
{
    const char *name = fm_selected_name(fm);
    if (!name) {
        fm_set_status(fm, "rename: no file selected");
        return;
    }
    fm->rename_mode = 1;
    str_copy_lim(fm->rename_buf, name, sizeof(fm->rename_buf));
    fm->rename_len = str_len(fm->rename_buf);
    fm_set_status(fm, "rename: edit name and press Enter");
}

U_FN void fm_apply_rename(struct file_manager *fm)
{
    const char *old_name = fm_selected_name(fm);
    if (!old_name) {
        fm_set_status(fm, "rename: no file selected");
        return;
    }
    if (!fm_name_is_valid(fm->rename_buf)) {
        fm_set_status(fm, "rename: invalid name");
        return;
    }
    if (str_eq(old_name, fm->rename_buf)) {
        fm->rename_mode = 0;
        fm_set_status(fm, "rename: unchanged");
        return;
    }
    char old_path[FM_NAME_MAX + 2];
    char new_path[FM_NAME_MAX + 2];
    fm_make_path(old_name, old_path, sizeof(old_path));
    fm_make_path(fm->rename_buf, new_path, sizeof(new_path));
    if (u_rename(old_path, new_path) < 0) {
        fm_set_status(fm, "rename: failed");
        return;
    }
    fm->rename_mode = 0;
    fm_set_status(fm, "rename: done");
    fm_reload_entries(fm);
    fm_load_preview(fm);
}

void fm_run(void)
{
    memset(&g_fm, 0, sizeof(g_fm));
    g_fm.win_id = u_wm_create("file-manager", 560, 360);
    if (g_fm.win_id < 0) {
        u_puts("fm: no window slot\n");
        return;
    }
    g_fm.active = 1;
    g_fm.selected = 0;
    fm_set_status(&g_fm, "ready");
    fm_reload_entries(&g_fm);
    fm_load_preview(&g_fm);
    (void) u_wm_focus(g_fm.win_id);
    fm_render(&g_fm);

    while (g_fm.active) {
        (void) u_wm_poll_mouse();
        int key = fm_poll_key(g_fm.win_id);
        if (key == -9) {
            g_fm.active = 0;
            break;
        }
        if (key == -1) {
            u_yield();
            continue;
        }

        if (g_fm.rename_mode) {
            if (key == '\n' || key == '\r') {
                fm_apply_rename(&g_fm);
                fm_render(&g_fm);
                continue;
            }
            if (key == -4) {
                g_fm.rename_mode = 0;
                fm_set_status(&g_fm, "rename: canceled");
                fm_render(&g_fm);
                continue;
            }
            if (key == 8 || key == 127) {
                if (g_fm.rename_len > 0) {
                    g_fm.rename_len--;
                    g_fm.rename_buf[g_fm.rename_len] = '\0';
                }
                fm_render(&g_fm);
                continue;
            }
            if (key >= 32 && key <= 126) {
                if (g_fm.rename_len + 1 < FM_NAME_MAX) {
                    g_fm.rename_buf[g_fm.rename_len++] = (char) key;
                    g_fm.rename_buf[g_fm.rename_len] = '\0';
                }
                fm_render(&g_fm);
            }
            continue;
        }

        if (key == -4 || key == 'q' || key == 'Q') {
            g_fm.active = 0;
            break;
        }
        if (key == -2) {
            if (g_fm.selected > 0)
                g_fm.selected--;
            fm_load_preview(&g_fm);
            fm_render(&g_fm);
            continue;
        }
        if (key == -3) {
            if (g_fm.selected + 1 < g_fm.count)
                g_fm.selected++;
            fm_load_preview(&g_fm);
            fm_render(&g_fm);
            continue;
        }
        if (key == 'l' || key == 'L') {
            fm_reload_entries(&g_fm);
            fm_load_preview(&g_fm);
            fm_set_status(&g_fm, "reload: done");
            fm_render(&g_fm);
            continue;
        }
        if (key == 'd' || key == 'D') {
            const char *name = fm_selected_name(&g_fm);
            if (!name) {
                fm_set_status(&g_fm, "delete: no file selected");
            } else {
                char path[FM_NAME_MAX + 2];
                fm_make_path(name, path, sizeof(path));
                if (u_unlink(path) < 0)
                    fm_set_status(&g_fm, "delete: failed");
                else
                    fm_set_status(&g_fm, "delete: done");
                fm_reload_entries(&g_fm);
                fm_load_preview(&g_fm);
            }
            fm_render(&g_fm);
            continue;
        }
        if (key == 'r' || key == 'R') {
            fm_begin_rename(&g_fm);
            fm_render(&g_fm);
            continue;
        }
        if (key == '\n' || key == '\r') {
            const char *name = fm_selected_name(&g_fm);
            if (!name) {
                fm_set_status(&g_fm, "open: no file selected");
            } else if (fm_is_image_name(name)) {
                char path[FM_NAME_MAX + 2];
                fm_make_path(name, path, sizeof(path));
                if (img_open_path(path) == 0)
                    fm_set_status(&g_fm, "open: image viewer launched");
                else
                    fm_set_status(&g_fm, "open: image launch failed");
            } else {
                fm_load_preview(&g_fm);
                fm_set_status(&g_fm, "open: preview refreshed");
            }
            fm_render(&g_fm);
            continue;
        }
    }

    (void) u_wm_close(g_fm.win_id);
    u_wm_render();
    memset(&g_fm, 0, sizeof(g_fm));
}
