#include <stddef.h>
#include <stdint.h>
#include "chos/user.h"

/* CHOS File Explorer 0.15.0-dev. Classic beveled-window theme, modern
 * navigation shortcuts, keyboard support, directory-aware drag-and-drop and
 * persistent CHFS operations. Keep redraws within the GUI's 32-op budget. */
#define FM_VISIBLE_ROWS 8u
#define FM_HISTORY_CAPACITY 12u
#define FM_ICON_COLUMNS 4u
#define FM_ICON_CELL_WIDTH 160
#define FM_ICON_CELL_HEIGHT 88
#define FM_PATH_CAPACITY CHOS_FILE_PATH_CAPACITY
#define FM_CHFS_PATH_LIMIT 48u
#define FM_NAME_CAPACITY 40u
#define GUI_WINDOW_MIN_ID 100L
#define GUI_WINDOW_MAX_ID 103L
#define FM_ROW_TOP 94
#define FM_ROW_STEP 28
#define FM_CLIENT_WIDTH 668
#define FM_CLIENT_HEIGHT 400
#define FM_MENU_WIDTH 184
#define FM_MENU_ROW_HEIGHT 22

static const char app_title[] = "CHOS File Explorer";
static chos_file_list_entry_t entries[CHOS_FILE_LIST_MAX];
static size_t entry_count;
static size_t selected;
static size_t scroll_offset;
static char navigation_history[FM_HISTORY_CAPACITY][FM_PATH_CAPACITY];
static size_t navigation_history_count;
static size_t navigation_history_index;
static int icon_view;
static uint32_t window_id;
static char status_line[96] = "Double-click or Enter to open. Right-click for actions.";
static char current_path[FM_PATH_CAPACITY] = "/DOCUMENTS";
static int menu_open;
static int menu_x, menu_y;
static int menu_target = -1;
static int menu_hover;
static int prompt_kind; /* 1 folder, 2 text file, 3 rename */
static char prompt_name[FM_NAME_CAPACITY];
static size_t prompt_length;
static int prompt_error;
static int pointer_down;
static int press_row = -1;
static int drag_active;
static int pointer_down_x, pointer_down_y;
static uint64_t loop_count;
static uint64_t last_click_loop;
static int last_click_row = -1;

static void message_begin(chos_gui_message_t *m, uint16_t type, size_t payload_size) {
    chos_memset(m, 0, sizeof(*m));
    m->header.version = CHOS_GUI_ABI_VERSION;
    m->header.type = type;
    m->header.size_bytes = (uint32_t)(offsetof(chos_gui_message_t, payload) + payload_size);
}
static long submit(chos_gui_message_t *m) { return chos_gui_submit(m, m->header.size_bytes); }
static int draw_text(uint32_t id, int32_t x, int32_t y, const char *value, size_t length, uint32_t color) {
    chos_gui_message_t m;
    if (length >= CHOS_GUI_MAX_TEXT) return 0;
    message_begin(&m, CHOS_GUI_MSG_DRAW_TEXT, sizeof(m.payload.draw_text));
    m.payload.draw_text.window_id = id; m.payload.draw_text.x = x; m.payload.draw_text.y = y;
    m.payload.draw_text.argb = color; m.payload.draw_text.text_length = (uint16_t)length;
    chos_memcpy(m.payload.draw_text.text, value, length);
    return submit(&m) == 0;
}
static int draw_cstr(uint32_t id, int32_t x, int32_t y, const char *value, uint32_t color) {
    return draw_text(id, x, y, value, chos_strlen(value), color);
}
static int fill_rect(uint32_t id, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    chos_gui_message_t m;
    message_begin(&m, CHOS_GUI_MSG_FILL_RECT, sizeof(m.payload.fill_rect));
    m.payload.fill_rect.window_id = id; m.payload.fill_rect.x = x; m.payload.fill_rect.y = y;
    m.payload.fill_rect.width = w; m.payload.fill_rect.height = h; m.payload.fill_rect.argb = color;
    return submit(&m) == 0;
}
static void set_status(const char *message) {
    (void)chos_strlcpy(status_line, message, sizeof(status_line));
}
static int path_equal(const char *a, const char *b) {
    size_t i = 0u;
    if (a == NULL || b == NULL) return 0;
    for (;;) {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca >= 'a' && ca <= 'z') ca = (unsigned char)(ca - ('a' - 'A'));
        if (cb >= 'a' && cb <= 'z') cb = (unsigned char)(cb - ('a' - 'A'));
        if (ca != cb) return 0;
        if (ca == '\0') return 1;
        ++i;
    }
}
static const char *path_leaf(const char *path) {
    const char *leaf = path;
    size_t i;
    if (path == NULL) return "";
    for (i = 0u; path[i] != '\0'; ++i) if (path[i] == '/') leaf = path + i + 1u;
    return leaf;
}
static int path_join(const char *directory, const char *leaf, char output[FM_PATH_CAPACITY]) {
    size_t d = chos_strlen(directory), l = chos_strlen(leaf), n = 0u;
    if (d == 0u || l == 0u || d + l + (directory[d - 1u] == '/' ? 0u : 1u) >= FM_CHFS_PATH_LIMIT) return 0;
    while (n < d) { output[n] = directory[n]; ++n; }
    if (output[n - 1u] != '/') output[n++] = '/';
    if (n + l >= FM_PATH_CAPACITY) return 0;
    chos_memcpy(output + n, leaf, l); output[n + l] = '\0';
    return 1;
}
static int is_direct_child(const char *directory, const char *path) {
    const char *tail;
    size_t n = 0u, i;
    if (directory == NULL || path == NULL) return 0;
    if (path_equal(directory, "/")) {
        if (path[0] != '/') return 0;
        tail = path + 1;
    } else {
        n = chos_strlen(directory);
        for (i = 0u; i < n; ++i) {
            char a = directory[i], b = path[i];
            if (a >= 'a' && a <= 'z') a = (char)(a - ('a' - 'A'));
            if (b >= 'a' && b <= 'z') b = (char)(b - ('a' - 'A'));
            if (a != b) return 0;
        }
        if (path[n] != '/') return 0;
        tail = path + n + 1u;
    }
    if (*tail == '\0') return 0;
    while (*tail != '\0') { if (*tail == '/') return 0; ++tail; }
    return 1;
}
static int path_under(const char *path, const char *root) {
    size_t i = 0u, n = chos_strlen(root);
    if (n == 0u) return 0;
    while (i < n && path[i] != '\0') {
        char a = path[i], b = root[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - ('a' - 'A'));
        if (b >= 'a' && b <= 'z') b = (char)(b - ('a' - 'A'));
        if (a != b) return 0;
        ++i;
    }
    return i == n && (path[i] == '/' || path[i] == '\0');
}
static int valid_name(const char *name) {
    size_t i, n = chos_strlen(name);
    if (n == 0u || n >= FM_NAME_CAPACITY || name[0] == ' ' || name[n - 1u] == ' ' ||
        path_equal(name, ".") || path_equal(name, "..")) return 0;
    for (i = 0u; i < n; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (ch < 32u || ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' ||
            ch == '"' || ch == '<' || ch == '>' || ch == '|') return 0;
    }
    return 1;
}
static void ensure_user_directories(void) {
    /* EEXIST is expected for an initialized disk. Other errors are surfaced by
     * later operations; this never formats the filesystem or touches files. */
    (void)chos_mkdir("/DESKTOP");
    (void)chos_mkdir("/DOCUMENTS");
    (void)chos_mkdir("/TRASH");
}
static long refresh_from_merged_catalog(void) {
    chos_file_list_entry_t all[CHOS_FILE_LIST_MAX];
    long total = chos_list_files(all, CHOS_FILE_LIST_MAX);
    size_t i;
    entry_count = 0u;
    if (total < 0) return total;
    if ((size_t)total > CHOS_FILE_LIST_MAX) total = CHOS_FILE_LIST_MAX;
    for (i = 0u; i < (size_t)total && entry_count < CHOS_FILE_LIST_MAX; ++i) {
        if (is_direct_child(current_path, all[i].path)) {
            chos_memcpy(&entries[entry_count], &all[i], sizeof(entries[entry_count]));
            ++entry_count;
        }
    }
    return (long)entry_count;
}
static int entry_sorts_before(const chos_file_list_entry_t *a, const chos_file_list_entry_t *b) {
    const int ad = (a->flags & CHOS_FILE_FLAG_DIRECTORY) != 0u;
    const int bd = (b->flags & CHOS_FILE_FLAG_DIRECTORY) != 0u;
    const char *an = path_leaf(a->path), *bn = path_leaf(b->path);
    size_t i = 0u;
    if (ad != bd) return ad > bd;
    for (;;) {
        unsigned char ac = (unsigned char)an[i], bc = (unsigned char)bn[i];
        if (ac >= 'a' && ac <= 'z') ac = (unsigned char)(ac - ('a' - 'A'));
        if (bc >= 'a' && bc <= 'z') bc = (unsigned char)(bc - ('a' - 'A'));
        if (ac != bc) return ac < bc;
        if (ac == '\0') return 0;
        ++i;
    }
}
static void sort_entries(void) {
    size_t i;
    for (i = 1u; i < entry_count; ++i) {
        chos_file_list_entry_t value;
        size_t j = i;
        chos_memcpy(&value, &entries[i], sizeof(value));
        while (j > 0u && entry_sorts_before(&value, &entries[j - 1u])) {
            chos_memcpy(&entries[j], &entries[j - 1u], sizeof(entries[j]));
            --j;
        }
        chos_memcpy(&entries[j], &value, sizeof(value));
    }
}
static void refresh_entries(void) {
    char previous_selection[FM_PATH_CAPACITY];
    int had_selection = selected < entry_count;
    long count;
    size_t i;
    if (had_selection) (void)chos_strlcpy(previous_selection, entries[selected].path, sizeof(previous_selection));
    else previous_selection[0] = '\0';
    count = chos_list_dir(current_path, entries, CHOS_FILE_LIST_MAX);
    if (count <= 0) {
        count = refresh_from_merged_catalog();
        if (count < 0) { entry_count = 0u; set_status("Directory unavailable or unreadable."); return; }
        if (count == 0) set_status("This folder is empty.");
        else set_status("Directory refreshed.");
    } else {
        entry_count = (size_t)count;
        set_status("Directory refreshed.");
    }
    sort_entries();
    if (had_selection) {
        for (i = 0u; i < entry_count; ++i) {
            if (path_equal(previous_selection, entries[i].path)) { selected = i; break; }
        }
    }
    if (selected >= entry_count) selected = entry_count == 0u ? 0u : entry_count - 1u;
    scroll_offset = selected >= FM_VISIBLE_ROWS ? selected - FM_VISIBLE_ROWS + 1u : 0u;
}
static int name_is_directory(size_t i) { return i < entry_count && (entries[i].flags & CHOS_FILE_FLAG_DIRECTORY) != 0u; }
static int is_trash_path(void) { return path_equal(current_path, "/TRASH"); }
static int is_program(const chos_file_list_entry_t *entry) {
    return entry != NULL && (entry->flags & CHOS_FILE_FLAG_EXECUTABLE) != 0u;
}
static int is_text(const chos_file_list_entry_t *entry) {
    return entry != NULL && (entry->flags & CHOS_FILE_FLAG_TEXT) != 0u;
}
static void history_reset(const char *path) {
    navigation_history_count = 1u;
    navigation_history_index = 0u;
    (void)chos_strlcpy(navigation_history[0], path, sizeof(navigation_history[0]));
}
static void history_append(const char *path) {
    size_t i;
    if (navigation_history_count == 0u) history_reset(current_path);
    if (path_equal(path, current_path)) return;
    if (navigation_history_index + 1u < navigation_history_count)
        navigation_history_count = navigation_history_index + 1u;
    if (navigation_history_count == FM_HISTORY_CAPACITY) {
        for (i = 1u; i < FM_HISTORY_CAPACITY; ++i)
            (void)chos_strlcpy(navigation_history[i - 1u], navigation_history[i], sizeof(navigation_history[0]));
        navigation_history_count = FM_HISTORY_CAPACITY - 1u;
        if (navigation_history_index != 0u) --navigation_history_index;
    }
    navigation_history_index = navigation_history_count;
    (void)chos_strlcpy(navigation_history[navigation_history_index], path,
                       sizeof(navigation_history[navigation_history_index]));
    ++navigation_history_count;
}
static void navigate_to(const char *path) {
    if (path == NULL || chos_strlen(path) >= FM_CHFS_PATH_LIMIT) { set_status("Path is too long."); return; }
    history_append(path);
    (void)chos_strlcpy(current_path, path, sizeof(current_path));
    selected = scroll_offset = 0u;
    menu_open = 0; prompt_kind = 0; drag_active = 0; press_row = -1;
    refresh_entries();
}
static void navigate_history_to(size_t index) {
    if (index >= navigation_history_count) return;
    navigation_history_index = index;
    (void)chos_strlcpy(current_path, navigation_history[index], sizeof(current_path));
    selected = scroll_offset = 0u; menu_open = 0; prompt_kind = 0; drag_active = 0; press_row = -1;
    refresh_entries();
}
static void navigate_back(void) {
    if (navigation_history_count == 0u || navigation_history_index == 0u) {
        set_status("No previous location."); return;
    }
    navigate_history_to(navigation_history_index - 1u);
    set_status("Previous location.");
}
static void navigate_forward(void) {
    if (navigation_history_index + 1u >= navigation_history_count) {
        set_status("No next location."); return;
    }
    navigate_history_to(navigation_history_index + 1u);
    set_status("Next location.");
}
static void navigate_parent(void) {
    char parent[FM_PATH_CAPACITY];
    size_t length = chos_strlen(current_path), i, slash = 0u;
    if (length <= 1u) { set_status("Already at the filesystem root."); return; }
    (void)chos_strlcpy(parent, current_path, sizeof(parent));
    for (i = 0u; i < length; ++i) if (parent[i] == '/') slash = i;
    if (slash == 0u) { parent[0] = '/'; parent[1] = '\0'; }
    else parent[slash] = '\0';
    navigate_to(parent);
}
static void format_size(uint32_t value, char output[18]) {
    char reverse[12]; size_t count = 0u, out = 0u;
    do { reverse[count++] = (char)('0' + value % 10u); value /= 10u; } while (value != 0u && count < sizeof(reverse));
    while (count != 0u) output[out++] = reverse[--count];
    output[out++] = ' '; output[out++] = 'B'; output[out] = '\0';
}
static void redraw(void);
static void open_selected(void) {
    long result;
    const chos_file_list_entry_t *entry;
    if (entry_count == 0u || selected >= entry_count) { set_status("No item is selected."); redraw(); return; }
    entry = &entries[selected];
    if (name_is_directory(selected)) {
        navigate_to(entry->path); redraw(); return;
    }
    if (is_program(entry)) {
        result = chos_spawn(entry->path);
        set_status(result < 0 ? "Could not launch this program." : "Program launched in a new process.");
    } else if (is_text(entry)) {
        result = chos_spawn_args("/BIN/TEXTVIEW.CHX", entry->path);
        set_status(result < 0 ? "Could not open the text editor." : "Opened text document.");
    } else set_status("No default application is registered for this file type.");
    redraw();
}
static int target_exists(const char *path) {
    chos_file_list_entry_t all[CHOS_FILE_LIST_MAX];
    long n = chos_list_files(all, CHOS_FILE_LIST_MAX);
    size_t i;
    if (n < 0) return 1; /* fail closed if we cannot check for a collision */
    if ((size_t)n > CHOS_FILE_LIST_MAX) n = CHOS_FILE_LIST_MAX;
    for (i = 0u; i < (size_t)n; ++i) if (path_equal(all[i].path, path)) return 1;
    return 0;
}
static int choose_unique_path(const char *directory, const char *leaf, char output[FM_PATH_CAPACITY]) {
    unsigned suffix;
    if (!path_join(directory, leaf, output)) return 0;
    if (!target_exists(output)) return 1;
    for (suffix = 2u; suffix < 100u; ++suffix) {
        char candidate[FM_NAME_CAPACITY];
        size_t n = 0u;
        while (leaf[n] != '\0' && n + 1u < sizeof(candidate)) { candidate[n] = leaf[n]; ++n; }
        if (n + 6u >= sizeof(candidate)) n = sizeof(candidate) - 7u;
        candidate[n++] = ' '; candidate[n++] = '(';
        if (suffix >= 10u) candidate[n++] = (char)('0' + suffix / 10u);
        candidate[n++] = (char)('0' + suffix % 10u); candidate[n++] = ')'; candidate[n] = '\0';
        if (path_join(directory, candidate, output) && !target_exists(output)) return 1;
    }
    return 0;
}
static int move_to_trash(const char *source) {
    char destination[FM_PATH_CAPACITY];
    if (path_equal(source, "/TRASH") || path_equal(source, "/DESKTOP") ||
        path_equal(source, "/DOCUMENTS") || path_equal(source, "/BIN") || path_equal(source, "/CFG") || path_equal(source, "/DOCS")) return 0;
    if (!choose_unique_path("/TRASH", path_leaf(source), destination)) return 0;
    return chos_rename(source, destination) == 0;
}
static int restore_to_desktop(const char *source) {
    char destination[FM_PATH_CAPACITY];
    if (!choose_unique_path("/DESKTOP", path_leaf(source), destination)) return 0;
    return chos_rename(source, destination) == 0;
}
static int create_named_item(int kind) {
    char destination[FM_PATH_CAPACITY];
    size_t n;
    if (!valid_name(prompt_name)) { prompt_error = 1; set_status("Enter a valid name."); return 0; }
    if (kind == 2 && !path_equal(path_leaf(prompt_name), prompt_name)) { prompt_error = 1; return 0; }
    if (kind == 2) {
        n = chos_strlen(prompt_name);
        if (n < 4u || !path_equal(prompt_name + n - 4u, ".TXT")) {
            if (n + 4u >= sizeof(prompt_name)) { prompt_error = 1; set_status("Text filename is too long."); return 0; }
            prompt_name[n++] = '.'; prompt_name[n++] = 'T'; prompt_name[n++] = 'X'; prompt_name[n++] = 'T'; prompt_name[n] = '\0';
        }
    }
    if (!path_join(current_path, prompt_name, destination)) { prompt_error = 1; set_status("Path is too long for CHFS."); return 0; }
    if (target_exists(destination)) { prompt_error = 1; set_status("An item with that name already exists."); return 0; }
    if (kind == 1) {
        if (chos_mkdir(destination) < 0) { prompt_error = 1; set_status("Could not create folder; storage may be full."); return 0; }
    } else {
        if (chos_write_file(destination, "", 0u) < 0) { prompt_error = 1; set_status("Could not create text document."); return 0; }
    }
    prompt_kind = 0; prompt_error = 0; selected = 0u; refresh_entries();
    if (kind == 2) set_status("Text document created."); else set_status("Folder created.");
    redraw(); return 1;
}
static void submit_prompt(void) {
    char destination[FM_PATH_CAPACITY];
    const char *source;
    if (prompt_kind == 1 || prompt_kind == 2) { (void)create_named_item(prompt_kind); return; }
    if (prompt_kind != 3 || selected >= entry_count) { prompt_kind = 0; return; }
    if (!valid_name(prompt_name) || !path_join(current_path, prompt_name, destination)) {
        prompt_error = 1; set_status("Invalid name or path too long."); redraw(); return;
    }
    source = entries[selected].path;
    if (target_exists(destination) && !path_equal(source, destination)) {
        prompt_error = 1; set_status("An item with that name already exists."); redraw(); return;
    }
    if (chos_rename(source, destination) < 0) {
        prompt_error = 1; set_status("Rename failed. Check destination and storage."); redraw(); return;
    }
    prompt_kind = 0; prompt_error = 0; selected = 0u; refresh_entries(); set_status("Item renamed."); redraw();
}
static void prompt_open(int kind) {
    prompt_kind = kind; prompt_error = 0; prompt_name[0] = '\0'; prompt_length = 0u;
    if (kind == 1) (void)chos_strlcpy(prompt_name, "New Folder", sizeof(prompt_name));
    else if (kind == 2) (void)chos_strlcpy(prompt_name, "New Text Document", sizeof(prompt_name));
    else if (kind == 3 && selected < entry_count) (void)chos_strlcpy(prompt_name, path_leaf(entries[selected].path), sizeof(prompt_name));
    prompt_length = chos_strlen(prompt_name); menu_open = 0;
}
static int remove_tree(const char *root) {
    chos_file_list_entry_t all[CHOS_FILE_LIST_MAX];
    size_t count = 0u, i, pass;
    long n = chos_list_files(all, CHOS_FILE_LIST_MAX);
    if (n < 0) return 0;
    if ((size_t)n > CHOS_FILE_LIST_MAX) n = CHOS_FILE_LIST_MAX;
    for (i = 0u; i < (size_t)n; ++i) {
        if ((all[i].flags & CHOS_FILE_FLAG_PERSISTENT) == 0u) continue;
        if (path_equal(all[i].path, root) || path_under(all[i].path, root)) {
            chos_memcpy(&entries[count++], &all[i], sizeof(entries[0]));
            if (count >= CHOS_FILE_LIST_MAX) break;
        }
    }
    /* Delete deepest paths first so non-empty directories are emptied safely. */
    for (pass = 0u; pass < count; ++pass) {
        size_t best = pass, j;
        for (j = pass + 1u; j < count; ++j)
            if (chos_strlen(entries[j].path) > chos_strlen(entries[best].path)) best = j;
        if (best != pass) {
            chos_file_list_entry_t tmp;
            chos_memcpy(&tmp, &entries[best], sizeof(tmp));
            chos_memcpy(&entries[best], &entries[pass], sizeof(tmp));
            chos_memcpy(&entries[pass], &tmp, sizeof(tmp));
        }
        if ((entries[pass].flags & CHOS_FILE_FLAG_DIRECTORY) != 0u) {
            if (chos_rmdir(entries[pass].path) < 0) return 0;
        } else if (chos_unlink(entries[pass].path) < 0) return 0;
    }
    return 1;
}
static int move_selected_to_folder(size_t source_index, const char *directory) {
    char destination[FM_PATH_CAPACITY];
    const char *source;
    if (source_index >= entry_count || directory == NULL || source_index >= CHOS_FILE_LIST_MAX) return 0;
    source = entries[source_index].path;
    if ((entries[source_index].flags & CHOS_FILE_FLAG_DIRECTORY) != 0u && path_under(directory, source)) {
        set_status("A folder cannot be moved inside itself."); return 0;
    }
    if (path_equal(source, directory)) return 0;
    if (!path_join(directory, path_leaf(source), destination)) { set_status("Destination path is too long."); return 0; }
    if (target_exists(destination)) { set_status("Move cancelled: destination already exists."); return 0; }
    if (chos_rename(source, destination) < 0) { set_status("Move failed; the source item was preserved."); return 0; }
    return 1;
}
static void activate_menu_item(int row) {
    const char *menu_rows[5];
    size_t n = 0u;
    int trash = is_trash_path();
    if (menu_target < 0) {
        menu_rows[n++] = "New Folder"; menu_rows[n++] = "New Text Document";
        menu_rows[n++] = "Refresh"; menu_rows[n++] = "Go Up";
    } else if (trash) {
        menu_rows[n++] = "Open"; menu_rows[n++] = "Restore to Desktop";
        menu_rows[n++] = "Delete Permanently";
    } else {
        menu_rows[n++] = "Open"; menu_rows[n++] = "New Folder";
        menu_rows[n++] = "New Text Document"; menu_rows[n++] = "Rename";
        menu_rows[n++] = "Move to Trash";
    }
    if (row < 0 || (size_t)row >= n) { menu_open = 0; redraw(); return; }
    menu_open = 0;
    if (menu_target < 0) {
        if (row == 0) prompt_open(1);
        else if (row == 1) prompt_open(2);
        else if (row == 2) { refresh_entries(); set_status("Directory refreshed."); }
        else if (row == 3) navigate_parent();
    } else if (trash) {
        if (row == 0) open_selected();
        else if (row == 1) {
            if (selected < entry_count && restore_to_desktop(entries[selected].path)) { refresh_entries(); set_status("Restored to Desktop."); }
            else set_status("Could not restore item.");
        } else if (row == 2) {
            if (selected < entry_count && remove_tree(entries[selected].path)) { refresh_entries(); set_status("Item permanently deleted."); }
            else set_status("Delete failed; some contents may remain.");
        }
    } else {
        if (row == 0) open_selected();
        else if (row == 1) {
            if (selected < entry_count && name_is_directory(selected)) {
                navigate_to(entries[selected].path);
            }
            prompt_open(1);
        } else if (row == 2) {
            if (selected < entry_count && name_is_directory(selected)) {
                navigate_to(entries[selected].path);
            }
            prompt_open(2);
        }
        else if (row == 3) prompt_open(3);
        else if (row == 4) {
            if (selected < entry_count && move_to_trash(entries[selected].path)) { refresh_entries(); set_status("Moved item to Trash."); }
            else set_status("Could not move item to Trash.");
        }
    }
    redraw();
}
static void redraw(void) {
    chos_gui_message_t m;
    size_t i, end;
    message_begin(&m, CHOS_GUI_MSG_CLEAR, sizeof(m.payload.clear));
    m.payload.clear.window_id = window_id; m.payload.clear.background_argb = 0xfff0f0f0u;
    if (submit(&m) != 0) return;
    {
        char location[FM_PATH_CAPACITY + 12u];
        const char prefix[] = "Location: ";
        size_t len = chos_strlen(current_path);
        chos_memcpy(location, prefix, sizeof(prefix) - 1u);
        chos_memcpy(location + sizeof(prefix) - 1u, current_path, len + 1u);
        (void)draw_cstr(window_id, 14, 7, location, 0xff000080u);
    }
    /* Compact 9x-style toolbar with modern location shortcuts. Click zones
     * are kept separate from item hit testing, including during drag/drop. */
    (void)draw_cstr(window_id, 14, 30, "Back", 0xff000080u);
    (void)draw_cstr(window_id, 52, 30, "Forward", 0xff000080u);
    (void)draw_cstr(window_id, 106, 30, "Up", 0xff000080u);
    (void)draw_cstr(window_id, 134, 30, "Refresh", 0xff000080u);
    (void)draw_cstr(window_id, 200, 30, "Desktop", 0xff000080u);
    (void)draw_cstr(window_id, 258, 30, "Documents", 0xff000080u);
    (void)draw_cstr(window_id, 334, 30, "System", 0xff000080u);
    (void)draw_cstr(window_id, 392, 30, "Trash", 0xff000080u);
    (void)draw_cstr(window_id, 448, 30, icon_view ? "Icons" : "List", 0xff000080u);
    if (!icon_view) {
        (void)draw_cstr(window_id, 16, 76, "Type    Name                                        Size", 0xff000000u);
        end = scroll_offset + FM_VISIBLE_ROWS; if (end > entry_count) end = entry_count;
        for (i = scroll_offset; i < end; ++i) {
            char line[CHOS_GUI_MAX_TEXT];
            char sizebuf[18];
            const char *leaf = path_leaf(entries[i].path);
            size_t pos = 0u, len = chos_strlen(leaf);
            uint32_t color = i == selected ? 0xffffffffu : 0xff202020u;
            const char *type = name_is_directory(i) ? "[DIR]  " : is_program(&entries[i]) ? "[APP]  " : is_text(&entries[i]) ? "[TXT]  " : "[FILE] ";
            size_t type_len = chos_strlen(type);
            chos_memcpy(line + pos, type, type_len); pos += type_len;
            if (len > 40u) len = 40u;
            chos_memcpy(line + pos, leaf, len); pos += len;
            /* A single draw operation per row keeps the full context menu
             * below the kernel's 32-operation window budget. */
            while (pos < 50u && pos + 1u < sizeof(line)) line[pos++] = ' ';
            format_size(entries[i].size_bytes, sizebuf);
            len = chos_strlen(sizebuf);
            if (pos + len >= sizeof(line)) len = sizeof(line) - pos - 1u;
            chos_memcpy(line + pos, sizebuf, len); pos += len; line[pos] = '\0';
            if (i == selected)
                (void)fill_rect(window_id, 12, FM_ROW_TOP + (int32_t)(i - scroll_offset) * FM_ROW_STEP - 4,
                                620u, FM_ROW_STEP - 5u, 0xff000080u);
            (void)draw_text(window_id, 16, FM_ROW_TOP + (int32_t)(i - scroll_offset) * FM_ROW_STEP, line, pos, color);
        }
    } else {
        end = scroll_offset + FM_VISIBLE_ROWS; if (end > entry_count) end = entry_count;
        for (i = scroll_offset; i < end; ++i) {
            char line[CHOS_GUI_MAX_TEXT];
            const char *leaf = path_leaf(entries[i].path);
            const char *type = name_is_directory(i) ? "[DIR] " : is_program(&entries[i]) ? "[APP] " : is_text(&entries[i]) ? "[TXT] " : "[FILE] ";
            size_t pos = chos_strlen(type), len = chos_strlen(leaf), visual = i - scroll_offset;
            int32_t x = 16 + (int32_t)(visual % FM_ICON_COLUMNS) * FM_ICON_CELL_WIDTH;
            int32_t y = FM_ROW_TOP + (int32_t)(visual / FM_ICON_COLUMNS) * FM_ICON_CELL_HEIGHT + 16;
            uint32_t color = i == selected ? 0xffffffffu : 0xff202020u;
            if (len > 15u) len = 15u;
            chos_memcpy(line, type, pos); chos_memcpy(line + pos, leaf, len); pos += len; line[pos] = '\0';
            if (i == selected) (void)fill_rect(window_id, x - 4, y - 5, 148u, 25u, 0xff000080u);
            (void)draw_text(window_id, x, y, line, pos, color);
        }
    }
    (void)draw_cstr(window_id, 16, 328, status_line, 0xff404040u);
    (void)draw_cstr(window_id, 16, 352, is_trash_path() ? "Trash: Restore to Desktop or Delete Permanently." : "F2 Rename   F5 Refresh   Backspace Up   Ctrl+N New Folder", 0xff606060u);
    if (prompt_kind != 0) {
        (void)fill_rect(window_id, 126, 118, 420u, 142u, 0xffc0c0c0u);
        (void)draw_cstr(window_id, 144, 132, prompt_kind == 1 ? "Create Folder" : prompt_kind == 2 ? "Create Text Document" : "Rename Item", 0xff000080u);
        (void)draw_cstr(window_id, 144, 161, prompt_name, 0xff000000u);
        (void)draw_cstr(window_id, 144, 192, prompt_error ? "Invalid name, existing item, or storage error." : "Type a name, then press Enter. Esc cancels.", prompt_error ? 0xff800000u : 0xff404040u);
        (void)draw_cstr(window_id, 144, 224, "Enter: confirm     Esc: cancel     Backspace: erase", 0xff000000u);
    }
    if (menu_open) {
        int row_count = menu_target < 0 ? 4 : (is_trash_path() ? 3 : 5);
        int x = menu_x, y = menu_y;
        if (x + FM_MENU_WIDTH > FM_CLIENT_WIDTH) x = FM_CLIENT_WIDTH - FM_MENU_WIDTH;
        if (y + row_count * FM_MENU_ROW_HEIGHT > FM_CLIENT_HEIGHT) y = FM_CLIENT_HEIGHT - row_count * FM_MENU_ROW_HEIGHT;
        (void)fill_rect(window_id, x, y, FM_MENU_WIDTH, (uint32_t)(row_count * FM_MENU_ROW_HEIGHT), 0xffe0e0e0u);
        for (i = 0u; i < (size_t)row_count; ++i) {
            const char *label;
            if (menu_target < 0) label = i == 0u ? "New Folder" : i == 1u ? "New Text Document" : i == 2u ? "Refresh" : "Go Up";
            else if (is_trash_path()) label = i == 0u ? "Open" : i == 1u ? "Restore to Desktop" : "Delete Permanently";
            else label = i == 0u ? "Open" : i == 1u ? "New Folder" : i == 2u ? "New Text Document" : i == 3u ? "Rename" : "Move to Trash";
            if ((int)i == menu_hover) (void)fill_rect(window_id, x + 2, y + (int)i * FM_MENU_ROW_HEIGHT + 1, FM_MENU_WIDTH - 4, FM_MENU_ROW_HEIGHT - 1, 0xff000080u);
            (void)draw_cstr(window_id, x + 8, y + (int)i * FM_MENU_ROW_HEIGHT + 5, label,
                            (int)i == menu_hover ? 0xffffffffu : 0xff000000u);
        }
    }
}
static void close_window(void) {
    chos_gui_message_t m; message_begin(&m, CHOS_GUI_MSG_DESTROY_WINDOW, sizeof(m.payload.window));
    m.payload.window.window_id = window_id; (void)submit(&m);
}
static char key_to_ascii(uint32_t keycode, uint32_t modifiers) {
    const int shift = (modifiers & 1u) != 0u;
    const int caps = (modifiers & 4u) != 0u;
    static const char digits[] = "1234567890", shifted[] = "!@#$%^&*()";
    char ch = '\0';
    if (keycode >= 2u && keycode <= 11u) return shift ? shifted[keycode - 2u] : digits[keycode - 2u];
    if (keycode >= 16u && keycode <= 25u) ch = "qwertyuiop"[keycode - 16u];
    else if (keycode >= 30u && keycode <= 38u) ch = "asdfghjkl"[keycode - 30u];
    else if (keycode >= 44u && keycode <= 50u) ch = "zxcvbnm"[keycode - 44u];
    else if (keycode == 57u) return ' ';
    else if (keycode == 12u) return shift ? '_' : '-';
    else if (keycode == 13u) return shift ? '+' : '=';
    else if (keycode == 26u) return shift ? '{' : '[';
    else if (keycode == 27u) return shift ? '}' : ']';
    else if (keycode == 39u) return shift ? ':' : ';';
    else if (keycode == 40u) return shift ? '"' : '\'';
    else if (keycode == 41u) return shift ? '~' : '`';
    else if (keycode == 43u || keycode == 86u) return shift ? '|' : '\\';
    else if (keycode == 51u) return shift ? '<' : ',';
    else if (keycode == 52u) return shift ? '>' : '.';
    else if (keycode == 53u) return shift ? '?' : '/';
    if (((shift != 0) != (caps != 0)) && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    return ch;
}
static int item_at_xy(int x, int y) {
    size_t offset;
    if (!icon_view) {
        int local = y - FM_ROW_TOP;
        if (local < 0 || local >= (int)FM_VISIBLE_ROWS * FM_ROW_STEP) return -1;
        offset = scroll_offset + (size_t)(local / FM_ROW_STEP);
    } else {
        int col, row;
        if (x < 12 || x >= 652 || y < FM_ROW_TOP || y >= FM_ROW_TOP + 2 * FM_ICON_CELL_HEIGHT) return -1;
        col = (x - 12) / FM_ICON_CELL_WIDTH;
        row = (y - FM_ROW_TOP) / FM_ICON_CELL_HEIGHT;
        if (col < 0 || col >= (int)FM_ICON_COLUMNS || row < 0 || row >= 2) return -1;
        offset = scroll_offset + (size_t)row * FM_ICON_COLUMNS + (size_t)col;
    }
    if (offset >= entry_count) return -1;
    return (int)offset;
}
static void handle_pointer(const chos_gui_pointer_event_t *p) {
    int row;
    if (p == NULL) return;
    if ((p->event_flags & CHOS_GUI_EVENT_POINTER_MOVE) != 0u) {
        if (menu_open) {
            int mx = p->x - menu_x, my = p->y - menu_y;
            int rows = menu_target < 0 ? 4 : (is_trash_path() ? 3 : 5);
            int next = mx >= 0 && mx < FM_MENU_WIDTH && my >= 0 && my < rows * FM_MENU_ROW_HEIGHT ? my / FM_MENU_ROW_HEIGHT : -1;
            if (next != menu_hover) { menu_hover = next; redraw(); }
        }
        if (pointer_down && press_row >= 0 && (p->buttons & 1u) != 0u) {
            int dx = p->x - pointer_down_x, dy = p->y - pointer_down_y;
            if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;
            if (dx > 4 || dy > 4) drag_active = 1;
            if (drag_active) { set_status("Dragging item: drop on a folder or shortcut."); redraw(); }
        }
        return;
    }
    if ((p->event_flags & CHOS_GUI_EVENT_POINTER_DOWN) == 0u) {
        if ((p->event_flags & CHOS_GUI_EVENT_POINTER_UP) != 0u && pointer_down) {
            const int up_row = item_at_xy(p->x, p->y);
            if (drag_active && press_row >= 0 && (size_t)press_row < entry_count) {
                const char *destination = NULL;
                if (p->y >= 28 && p->y < 51) {
                    if (p->x >= 200 && p->x < 256) destination = "/DESKTOP";
                    else if (p->x >= 258 && p->x < 332) destination = "/DOCUMENTS";
                    else if (p->x >= 334 && p->x < 390) destination = "/BIN";
                    else if (p->x >= 392 && p->x < 442) destination = "/TRASH";
                }
                if (destination == NULL && up_row >= 0 && name_is_directory((size_t)up_row) && up_row != press_row)
                    destination = entries[up_row].path;
                if (destination != NULL && !path_equal(entries[press_row].path, destination)) {
                    if (path_equal(destination, "/TRASH")) {
                        if (move_to_trash(entries[press_row].path)) set_status("Moved item to Trash.");
                        else set_status("Move to Trash failed; source preserved.");
                    } else if (move_selected_to_folder((size_t)press_row, destination)) set_status("Item moved to destination folder.");
                } else set_status("Drop onto a folder or Desktop/Documents shortcut to move the item.");
                selected = (size_t)press_row;
                refresh_entries();
            } else if (press_row >= 0 && (size_t)press_row < entry_count) {
                selected = (size_t)press_row;
                if (last_click_row == press_row && loop_count - last_click_loop < 45u) open_selected();
                else { last_click_row = press_row; last_click_loop = loop_count; set_status("Item selected."); }
            } else if (p->y >= 28 && p->y < 51) {
                if (p->x >= 12 && p->x < 46) navigate_back();
                else if (p->x >= 48 && p->x < 102) navigate_forward();
                else if (p->x >= 104 && p->x < 128) navigate_parent();
                else if (p->x >= 130 && p->x < 194) { refresh_entries(); set_status("Directory refreshed."); }
                else if (p->x >= 200 && p->x < 256) navigate_to("/DESKTOP");
                else if (p->x >= 258 && p->x < 332) navigate_to("/DOCUMENTS");
                else if (p->x >= 334 && p->x < 390) navigate_to("/BIN");
                else if (p->x >= 392 && p->x < 442) navigate_to("/TRASH");
                else if (p->x >= 448 && p->x < 510) { icon_view = !icon_view; scroll_offset = 0u; set_status(icon_view ? "Icon view selected." : "List view selected."); }
            }
            pointer_down = 0; press_row = -1; drag_active = 0; redraw();
        }
        return;
    }
    if ((p->buttons & 2u) != 0u) {
        row = item_at_xy(p->x, p->y);
        if (row >= 0) selected = (size_t)row;
        menu_target = row; menu_open = 1; menu_hover = -1;
        menu_x = p->x; menu_y = p->y;
        if (menu_x + FM_MENU_WIDTH > FM_CLIENT_WIDTH) menu_x = FM_CLIENT_WIDTH - FM_MENU_WIDTH;
        if (menu_y + (menu_target < 0 ? 4 : (is_trash_path() ? 3 : 5)) * FM_MENU_ROW_HEIGHT > FM_CLIENT_HEIGHT)
            menu_y = FM_CLIENT_HEIGHT - (menu_target < 0 ? 4 : (is_trash_path() ? 3 : 5)) * FM_MENU_ROW_HEIGHT;
        redraw(); return;
    }
    if ((p->buttons & 1u) == 0u) return;
    if (menu_open) {
        int rows = menu_target < 0 ? 4 : (is_trash_path() ? 3 : 5);
        if (p->x >= menu_x && p->x < menu_x + FM_MENU_WIDTH &&
            p->y >= menu_y && p->y < menu_y + rows * FM_MENU_ROW_HEIGHT)
            activate_menu_item((p->y - menu_y) / FM_MENU_ROW_HEIGHT);
        else { menu_open = 0; redraw(); }
        return;
    }
    row = item_at_xy(p->x, p->y);
    pointer_down = 1; press_row = row; drag_active = 0;
    pointer_down_x = p->x; pointer_down_y = p->y;
    if (row >= 0) { selected = (size_t)row; redraw(); }
}
static void handle_key(const chos_gui_key_event_t *key) {
    char ch;
    uint32_t code;
    if (key == NULL || (key->event_flags & CHOS_GUI_EVENT_KEY_DOWN) == 0u) return;
    code = key->keycode;
    if (prompt_kind != 0) {
        if (code == 1u) { prompt_kind = 0; prompt_error = 0; redraw(); return; }
        if (code == 28u) { submit_prompt(); return; }
        if (code == 14u) { if (prompt_length != 0u) prompt_name[--prompt_length] = '\0'; prompt_error = 0; redraw(); return; }
        ch = key_to_ascii(code, key->modifiers);
        if (ch >= 32 && ch <= 126 && prompt_length + 1u < sizeof(prompt_name)) {
            prompt_name[prompt_length++] = ch; prompt_name[prompt_length] = '\0'; prompt_error = 0; redraw();
        }
        return;
    }
    if (menu_open) {
        int count = menu_target < 0 ? 4 : is_trash_path() ? 3 : 5;
        if (code == 1u) menu_open = 0;
        else if (code == 103u || code == 72u) menu_hover = menu_hover <= 0 ? count - 1 : menu_hover - 1;
        else if (code == 108u || code == 80u) menu_hover = menu_hover + 1 >= count ? 0 : menu_hover + 1;
        else if (code == 28u) { activate_menu_item(menu_hover < 0 ? 0 : menu_hover); return; }
        redraw(); return;
    }
    if ((key->modifiers & 2u) != 0u && code == 49u) { prompt_open(1); redraw(); return; }
    if (code == 1u) { close_window(); chos_exit(0); }
    if (code == 14u) { navigate_parent(); redraw(); return; }
    if ((key->modifiers & 2u) != 0u && code == 37u) { navigate_back(); redraw(); return; }
    if ((key->modifiers & 2u) != 0u && code == 36u) { navigate_forward(); redraw(); return; }
    if (code == 63u) { refresh_entries(); set_status("Directory refreshed."); redraw(); return; }
    if (code == 60u) { if (entry_count != 0u) { prompt_open(3); redraw(); } return; }
    if (code == 111u || code == 211u) {
        if (selected < entry_count) {
            if (is_trash_path()) { if (remove_tree(entries[selected].path)) { refresh_entries(); set_status("Item permanently deleted."); } else set_status("Could not delete item."); }
            else if (move_to_trash(entries[selected].path)) { refresh_entries(); set_status("Moved item to Trash."); }
            else set_status("Could not delete or move item.");
            redraw();
        }
        return;
    }
    if (code == 103u || code == 72u) { if (selected > 0u) --selected; if (selected < scroll_offset) scroll_offset = selected; redraw(); return; }
    if (code == 108u || code == 80u) { if (selected + 1u < entry_count) ++selected; if (selected >= scroll_offset + FM_VISIBLE_ROWS) scroll_offset = selected - FM_VISIBLE_ROWS + 1u; redraw(); return; }
    if (code == 28u) { open_selected(); return; }
    if (code == 33u && (key->modifiers & 2u) != 0u) { refresh_entries(); redraw(); return; }
}
int main(int argc, char **argv) {
    chos_gui_message_t m, event;
    long result;
    ensure_user_directories();
    if (argc > 1 && argv != NULL && argv[1] != NULL && argv[1][0] == '/' &&
        chos_strlen(argv[1]) < FM_CHFS_PATH_LIMIT)
        (void)chos_strlcpy(current_path, argv[1], sizeof(current_path));
    history_reset(current_path);
    refresh_entries();
    message_begin(&m, CHOS_GUI_MSG_CREATE_WINDOW, sizeof(m.payload.create_window));
    m.payload.create_window.x = 55; m.payload.create_window.y = 48;
    m.payload.create_window.width = 680u; m.payload.create_window.height = 440u;
    m.payload.create_window.background_argb = 0xfff0f0f0u;
    result = submit(&m); if (result < GUI_WINDOW_MIN_ID || result > GUI_WINDOW_MAX_ID) chos_exit(1);
    window_id = (uint32_t)result;
    message_begin(&m, CHOS_GUI_MSG_SET_TITLE, sizeof(m.payload.set_title));
    m.payload.set_title.window_id = window_id; m.payload.set_title.title_length = (uint16_t)(sizeof(app_title) - 1u);
    chos_memcpy(m.payload.set_title.title, app_title, sizeof(app_title) - 1u);
    if (submit(&m) != 0) { close_window(); chos_exit(2); }
    message_begin(&m, CHOS_GUI_MSG_SHOW_WINDOW, sizeof(m.payload.window)); m.payload.window.window_id = window_id;
    if (submit(&m) != 0) { close_window(); chos_exit(3); }
    redraw();
    for (;;) {
        ++loop_count;
        result = chos_gui_poll_event(&event, sizeof(event));
        if (result == -(long)CHOS_ERRNO_EAGAIN) { (void)chos_sleep_ticks(1u); continue; }
        if (result < 0) break;
        if (event.header.type == CHOS_GUI_MSG_CLOSE_REQUEST) break;
        if (event.header.type == CHOS_GUI_MSG_KEY_EVENT) handle_key(&event.payload.key_event);
        else if (event.header.type == CHOS_GUI_MSG_POINTER_EVENT) handle_pointer(&event.payload.pointer_event);
    }
    close_window(); chos_exit(0);
}
