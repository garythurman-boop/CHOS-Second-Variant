#include <stddef.h>
#include <stdint.h>
#include "chos/gui.h"
#include "chos/taskbar_text.h"
#include "chos/syscall.h"
#include "chos/virtio_gpu.h"
#include "chos/virtio_input.h"
#include "chos/storage.h"
#include "chos/chfs.h"
#include "chos/process.h"
#include "chos/romfs.h"
#include "chos/window_manager.h"
#include "chos/timer.h"
#include "chos/display_modes.h"
#include "chos/power.h"
#include "chos/uart.h"
#include "chos/audio.h"
#include "chos/platform.h"

typedef struct { uint32_t width, height; const uint32_t *pixels; } gui_embedded_image_t;
typedef struct { const char *name; const gui_embedded_image_t *image; } gui_embedded_named_image_t;
#include "embedded_assets.h"
#include "font8x12.h"
#include "font_ui_dejavu.h"

#define SCREEN_W ((int32_t)screen.width)
#define SCREEN_H ((int32_t)screen.height)
#define GUI_LOGICAL_WIDTH 800
#define GUI_LOGICAL_HEIGHT 600
#define TASKBAR_BASE_HEIGHT 30
#define TASKBAR_APP_BUTTON_WIDTH 88
#define TASKBAR_APP_BUTTON_STEP 92
#define TASKBAR_APP_TEXT_PADDING 14
#define TASKBAR_TOOLTIP_DELAY_TICKS 24u
#define TASKBAR_TOOLTIP_FADE_STEP_TICKS 2u
#define TASKBAR_TOOLTIP_FADE_STEPS 6u
#define TASKBAR_TOOLTIP_MAX_WIDTH 520
#define TASKBAR_TOOLTIP_MAX_LINES 8u
#define TASKBAR_HEIGHT (TASKBAR_BASE_HEIGHT * (int)gui_scale_factor())
#define TASKBAR_Y (SCREEN_H - TASKBAR_HEIGHT)
#define START_MENU_BASE_HEIGHT 302
#define START_MENU_HEIGHT (START_MENU_BASE_HEIGHT * (int)gui_scale_factor())
#define VIEW_WELCOME 0
#define VIEW_CONSOLE 1
#define VIEW_FILES 2
#define VIEW_ABOUT 3
#define VIEW_DIAGNOSTICS 4
#define VIEW_SETTINGS 5
#define VIEW_FILE_VIEWER 6
#define VIEW_TRASH 7
#define GUI_VIEW_COUNT 8u
#define GUI_DESKTOP_ICON_CAP (CHOS_CHFS_MAX_FILES + 7u)
#define GUI_DESKTOP_GRID_COLUMNS 8u
#define GUI_DESKTOP_GRID_ROWS 6u
#define GUI_DESKTOP_GRID_CELLS (GUI_DESKTOP_GRID_COLUMNS * GUI_DESKTOP_GRID_ROWS)
#define GUI_DESKTOP_ICON_X 18
#define GUI_DESKTOP_ICON_Y 20
#define GUI_DESKTOP_CELL_W 90
#define GUI_DESKTOP_CELL_H 82
#define GUI_DESKTOP_PATH "/DESKTOP"
#define GUI_DOCUMENTS_PATH "/DOCUMENTS"
#define GUI_TRASH_PATH "/TRASH"
#define GUI_DESKTOP_CONFIG_PATH "/CFG/DESKTOP.CFG"
#define GUI_DESKTOP_NAME_CAPACITY 39u
#define GUI_CONTEXT_ROW_HEIGHT 24
#define GUI_CONTEXT_WIDTH 164
#define GUI_TEXT_EDITOR_CAPACITY TERMINAL_FILE_CAPACITY
#define TERMINAL_COLUMNS 68u
#define TERMINAL_ROWS 29u
#define TERMINAL_COMMAND_CAPACITY 96u
#define TERMINAL_HISTORY_CAPACITY 8u
#define FILE_VIEW_ROWS 11u
#define TERMINAL_FILE_CAPACITY 1024u
#define DISPLAY_MODES_PER_PAGE 5u
#define SETTINGS_PATH "/CFG/CHOS.CFG"
#define VIEWER_LINE_CAPACITY 192u
#define VIEWER_VISIBLE_LINES 21u
#define VIEWER_TEXT_WIDTH 542u
#define SPLASH_DURATION_TICKS (3u * CHOS_TIMER_HZ)
#define SPLASH_MINIMUM_SECONDS 3u
#define SPLASH_ANIMATION_TICKS 3u
#define SPLASH_FRAME_RATE 30u
#define SPLASH_SEGMENT_SPEED 120u
#define GUI_WINDOW_BASE_WIDTH 595
#define GUI_WINDOW_BASE_HEIGHT 465
#define GUI_WINDOW_WIDTH (GUI_WINDOW_BASE_WIDTH * (int)gui_scale_factor())
#define GUI_WINDOW_HEIGHT (GUI_WINDOW_BASE_HEIGHT * (int)gui_scale_factor())
#define GUI_WINDOW_BASE_X 180
#define GUI_WINDOW_BASE_Y 70

#define C_DESKTOP_TOP     0xff008080u /* Windows 9x/2000-era solid teal desktop */
#define C_DESKTOP_BOTTOM  0xff008080u
#define C_DESKTOP_ALT_TOP 0xff008080u
#define C_DESKTOP_ALT_BOT 0xff008080u
#define C_WHITE           0xffffffffu
#define C_BLACK           0xff000000u
#define C_TEXT            0xff000000u
#define C_MUTED           0xff404040u
#define C_PANEL           0xffc0c0c0u /* classic 3-D control face */
#define C_PANEL_LIGHT     0xffffffffu
#define C_PANEL_DARK      0xff808080u
#define C_BLUE            0xff0a246au /* classic active-title-bar navy */
#define C_BLUE_DARK       0xff0a246au
#define C_BLUE_LIGHT      0xff3a6ea5u
#define C_GREEN           0xff008000u
#define C_ORANGE          0xffa05a00u
#define C_PURPLE          0xff800080u
#define C_RED             0xff800000u
#define C_TASKBAR         0xffc0c0c0u
#define C_TOOLTIP_BG      0xffffffe1u
#define C_SELECTION       0xff0a246au
#define C_DESKTOP_GLOW    0xff008080u
#define C_WINDOW_EDGE     0xff003399u
#define C_TERMINAL_BG     0xff000000u
#define C_TERMINAL_FG     0xfff0f0f0u
#define C_TERMINAL_GREEN  0xff80ff80u

#define GUI_APP_WINDOW_BASE 100
#define GUI_APP_MAX_CLIENTS 4u
/* Browser frames need >64 ops for six beveled controls, status, page text, and footer. */
#define GUI_APP_MAX_DRAW_OPS 80u
typedef struct {
    uint16_t type;
    int32_t x, y;
    uint32_t width, height, argb;
    uint16_t text_length;
    char text[CHOS_GUI_MAX_TEXT + 1u];
} gui_app_draw_op_t;
typedef struct {
    int active;
    int window_id;
    uint32_t owner_pid;
    char title[CHOS_GUI_MAX_TEXT];
    uint32_t background_argb;
    gui_app_draw_op_t ops[GUI_APP_MAX_DRAW_OPS];
    size_t op_count;
    chos_gui_message_t events[CHOS_GUI_EVENT_QUEUE_CAPACITY];
    size_t event_head;
    size_t event_count;
} gui_app_client_t;
static gui_app_client_t gui_app_clients[GUI_APP_MAX_CLIENTS];

static chos_surface_t screen;
/* The image background is prepared once per display-mode/wallpaper change.
 * This removes the previous expensive per-frame geometric landscape painter. */
static uint32_t wallpaper_cache[CHOS_DISPLAY_MAX_WIDTH * CHOS_DISPLAY_MAX_HEIGHT];
static int wallpaper_cache_valid;
static size_t wallpaper_cache_mode;
static int wallpaper_cache_kind;
static int startup_chime_pending;
static int startup_chime_queued;
static uint64_t startup_chime_started_tick;
static uint64_t startup_chime_started_counter;
static uint64_t startup_chime_counter_frequency;
static unsigned startup_chime_attempts;
static unsigned startup_chime_init_attempts;
static uint64_t startup_chime_next_probe_ms;
static int physical_coordinates;
static unsigned gui_scale_factor(void) {
    /* Scale the logical 800x600 desktop in discrete steps. A 2x window is
     * 1190x930; a 3x window is 1785x1395, leaving room for the taskbar. */
    if (screen.width >= 2400u && screen.height >= 1530u) return 3u;
    if (screen.width >= 1600u && screen.height >= 1024u) return 2u;
    return 1u;
}
static int gui_active;
static int view = VIEW_WELCOME;
static chos_window_manager_t window_manager;
static int window_manager_initialized;
static int draw_offset_x;
static int draw_offset_y;
static int drawing_active_window;
static int drawing_maximized_window;
static int dragging_window_id = CHOS_WM_NO_WINDOW;
static int32_t drag_offset_x;
static int32_t drag_offset_y;
static int selected_icon;
static int selected_desktop_icon = -1;
static int start_menu_selected_item = -1;
static int start_menu_open;
static int desktop_icons_dirty = 1;
static int desktop_layout_loaded;
static int desktop_directories_initialized;
static int desktop_drag_index = -1;
static int desktop_drag_moved;
static int desktop_drag_current_cell;
static int32_t desktop_drag_start_x, desktop_drag_start_y;
static int last_desktop_click_index = -1;
static uint64_t last_desktop_click_tick;
static unsigned desktop_position_cells[GUI_DESKTOP_ICON_CAP];
static char desktop_layout_data[2048];
static size_t desktop_layout_size;
static size_t desktop_icon_count;
static size_t trash_selected_index;
static size_t trash_scroll_offset;
enum { GUI_CONTEXT_DESKTOP = 0, GUI_CONTEXT_APP_ICON = 1, GUI_CONTEXT_FILE_ICON = 2, GUI_CONTEXT_TRASH_ICON = 3, GUI_CONTEXT_DOCUMENTS_ICON = 4 };
enum { GUI_PROMPT_NEW_FOLDER = 1, GUI_PROMPT_NEW_TEXT = 2, GUI_PROMPT_RENAME = 3, GUI_PROMPT_CONFIRM_DELETE = 4, GUI_PROMPT_EMPTY_TRASH = 5 };
static char desktop_status_message[96];
static uint64_t desktop_status_until_tick;
static int context_menu_open;
static int context_menu_kind;
static int context_menu_submenu;
static int context_menu_icon_index = -1;
static int context_menu_hover_index = -1;
static int32_t context_menu_x, context_menu_y;
static int prompt_kind;
static char prompt_name[GUI_DESKTOP_NAME_CAPACITY];
static size_t prompt_name_length;
static int prompt_error;
static char prompt_target_path[CHOS_CHFS_PATH_MAX];
static int viewer_editable;
static int viewer_unsaved;
static int viewer_save_error;

/* Fresh profiles boot into the classic flat teal desktop. The included
 * landscape remains available from Settings, rather than being the default. */
static int wallpaper_alt = 0;
static int dirty;
static uint64_t dirty_generation, presented_generation;
static void gui_mark_dirty(void) {
    (void)__atomic_fetch_add(&dirty_generation,1u,__ATOMIC_RELAXED);
    dirty=1;
}
static int32_t mouse_x = 400;
static int32_t mouse_y = 300;
static int left_button_down;
static int app_pointer_capture_window_id = CHOS_WM_NO_WINDOW;
static int shift_down;
static char unicode_key_override;
static int control_down;
static int caps_lock_down;
static int terminal_initialized;
static uint32_t gui_console_pid; /* owned by the EL0 Console launched from the desktop */
static uint32_t gui_file_manager_pid; /* owned by the EL0 File Manager launched from the desktop */
static uint32_t gui_browser_pid; /* owned by the EL0 CHOS Browser launched from the Start menu */
#define GUI_CONSOLE_OUTPUT_RING_CAPACITY 4096u
static char gui_console_output_ring[GUI_CONSOLE_OUTPUT_RING_CAPACITY];
static size_t gui_console_output_head;
static size_t gui_console_output_count;
static int terminal_history_position;
static size_t terminal_history_count;
static size_t terminal_row;
static size_t terminal_column;
static char terminal_lines[TERMINAL_ROWS][TERMINAL_COLUMNS + 1u];
static char terminal_command[TERMINAL_COMMAND_CAPACITY];
static char terminal_history[TERMINAL_HISTORY_CAPACITY][TERMINAL_COMMAND_CAPACITY];
static char terminal_cwd[CHOS_CHFS_PATH_MAX] = "/";
static size_t file_selected_index;
static size_t file_scroll_offset;
static char viewer_path[CHOS_CHFS_PATH_MAX];
static char viewer_content[TERMINAL_FILE_CAPACITY];
static size_t viewer_content_size;
static size_t viewer_scroll_offset;
static int viewer_load_error;
static int viewer_empty_file;
static int splash_active;
static uint64_t splash_started_tick;
static uint64_t splash_last_animation_tick;
static uint64_t splash_last_animation_counter;
static uint64_t splash_started_counter;
static uint64_t splash_counter_frequency;
static int splash_startup_complete;
static int power_confirmation_action;
static int display_mode_error;
static size_t display_mode_page;
static int settings_save_error;
static int startup_sound_enabled = 1;
static int settings_loading;
static uint32_t frames_drawn;
static uint32_t platform_pci_device_count;
static uint32_t platform_xhci_count;
static int platform_pci_status_valid;
static char platform_xhci_id[12] = "not found";
static char platform_xhci_bars[20] = "not available";
#define GUI_CURSOR_BOX_WIDTH 16
#define GUI_CURSOR_BOX_HEIGHT 24
#define GUI_CURSOR_SAVE_STRIDE 96
#define GUI_DRAG_BORDER_MAX_THICKNESS 6
#define GUI_DRAG_STRIP_COUNT 4u
typedef struct { int32_t x, y, width, height; } gui_drag_rect_t;
static uint32_t cursor_under[GUI_CURSOR_SAVE_STRIDE * GUI_CURSOR_SAVE_STRIDE];
/* Dragging uses a lightweight rubber-band outline instead of repainting the
 * entire high-resolution desktop on every pointer event. Save-under is limited
 * to the four thin border strips, keeping the working set small at 1440p. */
static uint32_t drag_under_top[CHOS_DISPLAY_MAX_WIDTH * GUI_DRAG_BORDER_MAX_THICKNESS];
static uint32_t drag_under_bottom[CHOS_DISPLAY_MAX_WIDTH * GUI_DRAG_BORDER_MAX_THICKNESS];
static uint32_t drag_under_left[CHOS_DISPLAY_MAX_HEIGHT * GUI_DRAG_BORDER_MAX_THICKNESS];
static uint32_t drag_under_right[CHOS_DISPLAY_MAX_HEIGHT * GUI_DRAG_BORDER_MAX_THICKNESS];
static gui_drag_rect_t drag_outline_rects[GUI_DRAG_STRIP_COUNT];
static int drag_outline_visible;
static int drag_preview_dirty;
static int32_t drag_preview_x, drag_preview_y;
static uint64_t clock_last_minute;
static uint64_t clock_last_poll_tick;
static int clock_initialized;
static unsigned timezone_index = 5u;
static int32_t cursor_under_x, cursor_under_y;
static int32_t cursor_under_width, cursor_under_height;
static int cursor_under_valid;
static int cursor_move_pending;
static int taskbar_tooltip_window_id = CHOS_WM_NO_WINDOW;
static uint64_t taskbar_tooltip_started_tick;
static unsigned taskbar_tooltip_fade_step;
static int32_t taskbar_tooltip_anchor_x, taskbar_tooltip_anchor_y;
/* The high-contrast cursor is a replaceable transparent PNG embedded at build time.
 * Its 1x1 upper-left tip is the input hotspot. */

/* Freestanding structure-copy helper: do not let the compiler introduce a
 * libc memcpy dependency into the kernel for small GUI protocol envelopes. */
static void gui_copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    size_t i;
    for (i = 0u; i < length; ++i) dst[i] = src[i];
}


static const char *const view_names[] = {
    "Welcome", "Console", "File Manager", "About CHOS", "Diagnostics", "Settings", "Text Viewer", "Trash"
};
static const char *const icon_names[] = {
    "Console", "File Manager", "About CHOS", "Diagnostics", "Settings"
};
static const int icon_views[] = { VIEW_CONSOLE, VIEW_FILES, VIEW_ABOUT, VIEW_DIAGNOSTICS, VIEW_SETTINGS };

enum {
    GUI_DESKTOP_APP = 0,
    GUI_DESKTOP_TRASH = 1,
    GUI_DESKTOP_DIRECTORY = 2,
    GUI_DESKTOP_TEXT = 3,
    GUI_DESKTOP_FILE = 4,
    GUI_DESKTOP_DOCUMENTS = 5
};
typedef struct {
    int kind;
    int app_view;
    unsigned cell;
    char title[CHOS_CHFS_PATH_MAX];
    char path[CHOS_CHFS_PATH_MAX];
    char layout_key[CHOS_CHFS_PATH_MAX + 8u];
} gui_desktop_icon_t;
static gui_desktop_icon_t desktop_icons[GUI_DESKTOP_ICON_CAP];

static const int8_t gui_timezone_offsets[] = { -8, -7, -6, -5, -4, 0, 1, 5, 8, 9 };
static const char *const gui_timezone_labels[] = {
    "UTC-08", "UTC-07", "UTC-06", "UTC-05", "UTC-04",
    "UTC", "UTC+01", "UTC+05", "UTC+08", "UTC+09"
};
#define GUI_TIMEZONE_COUNT (sizeof(gui_timezone_offsets) / sizeof(gui_timezone_offsets[0]))

static const char *gui_timezone_label(void) {
    if (timezone_index >= GUI_TIMEZONE_COUNT) timezone_index = 5u;
    return gui_timezone_labels[timezone_index];
}

static void gui_format_clock(char output[12]) {
    uint64_t epoch = platform_rtc_unix_seconds();
    int64_t local_seconds;
    unsigned hour24, minute, hour12;
    size_t pos = 0u;
    if (epoch == 0u) {
        const char unavailable[] = "--:-- --";
        size_t i; for (i = 0u; i < sizeof(unavailable); ++i) output[i] = unavailable[i];
        return;
    }
    local_seconds = (int64_t)(epoch % 86400u) + (int64_t)gui_timezone_offsets[timezone_index] * 3600;
    while (local_seconds < 0) local_seconds += 86400;
    while (local_seconds >= 86400) local_seconds -= 86400;
    hour24 = (unsigned)(local_seconds / 3600);
    minute = (unsigned)((local_seconds / 60) % 60);
    hour12 = hour24 % 12u; if (hour12 == 0u) hour12 = 12u;
    if (hour12 >= 10u) output[pos++] = (char)('0' + hour12 / 10u);
    output[pos++] = (char)('0' + hour12 % 10u);
    output[pos++] = ':';
    output[pos++] = (char)('0' + minute / 10u);
    output[pos++] = (char)('0' + minute % 10u);
    output[pos++] = ' ';
    output[pos++] = hour24 < 12u ? 'A' : 'P';
    output[pos++] = 'M';
    output[pos] = '\0';
}

static void gui_clock_poll(void) {
    uint64_t ticks, epoch, minute;
    if (!timer_is_ready()) return;
    ticks = timer_ticks();
    if (ticks == clock_last_poll_tick || (ticks % CHOS_TIMER_HZ) != 0u) return;
    clock_last_poll_tick = ticks;
    epoch = platform_rtc_unix_seconds();
    if (epoch == 0u) return;
    minute = epoch / 60u;
    if (!clock_initialized || minute != clock_last_minute) {
        clock_last_minute = minute; clock_initialized = 1; gui_mark_dirty();
    }
}

static void open_view(int new_view);
static int gui_open_window(int id);
static void terminal_initialize(void);
static void terminal_newline(void);
static void terminal_puts(const char *value);
static void terminal_prompt(void);
static void terminal_run_path(const char *argument);
static int file_is_program(const char *path);
static int file_viewer_load(const char *path);
static void gui_render(void);
static void gui_drag_overlay_update(void);
static void gui_drag_overlay_erase(void);
static void click_at(int32_t x, int32_t y, int in_window);
static int gui_apply_display_mode(size_t mode_index);
static void gui_taskbar_tooltip_update_hover(void);
static void gui_taskbar_tooltip_tick(void);
static void gui_draw_taskbar_tooltip(void);
static const char *display_mode_label(size_t mode_index);
static const char *display_mode_full_label(size_t mode_index);
static gui_app_client_t *gui_app_find_window(int window_id);
static gui_app_client_t *gui_app_find_owner(uint32_t owner_pid);
static void gui_app_queue_pointer(int window_id, uint32_t event_flags, uint32_t buttons);
static void gui_app_queue_key(int window_id, uint32_t keycode, int pressed);
static void gui_app_queue_focus(int window_id, uint32_t event_flags);
static void gui_app_queue_message(gui_app_client_t *client, const chos_gui_message_t *event);
static void gui_set_active_view(int next_view);
static void gui_desktop_refresh_icons(void);
static void gui_desktop_save_layout(void);
static void gui_start_menu_update_hover(void);
static void gui_context_menu_open_at(int32_t x, int32_t y);
static void gui_context_menu_click(int32_t x, int32_t y);
static void gui_context_menu_update_hover(void);
static void gui_draw_context_menu(void);
static void gui_draw_prompt(void);
static void gui_start_menu_activate_item(int index);
static int gui_launch_or_focus_app(uint32_t *pid_slot, const char *path);
static void gui_trash_draw_content(void);
static void gui_draw_desktop_status(void);
static void gui_desktop_status(const char *message);
static int gui_desktop_icon_at(int32_t physical_x, int32_t physical_y);
static void gui_prompt_open(int kind, const char *target_path);
static void gui_prompt_submit(void);
static void gui_desktop_activate(int index);
static int gui_ensure_desktop_directory(void);
static int gui_ensure_user_directories(void);
static int gui_desktop_move_into_folder(int source_index, int target_index);
static void gui_trash_open(void);
static int gui_text_editor_save(void);
static int string_ends_with_fold(const char *value, const char *suffix);

static int gui_coord_x(int x) {
    return physical_coordinates ? x : (int)((int64_t)x * gui_scale_factor() + draw_offset_x);
}
static int gui_coord_y(int y) {
    return physical_coordinates ? y : (int)((int64_t)y * gui_scale_factor() + draw_offset_y);
}
static int gui_size(int size) {
    return physical_coordinates ? size : (int)((int64_t)size * gui_scale_factor());
}
static void text(int x, int y, const char *s, uint32_t fg) {
    unsigned scale = gui_scale_factor();
    chos_gfx_ui_text_scaled(&screen, gui_coord_x(x), gui_coord_y(y), s, fg, 0, scale);
}
static void text_bold(int x, int y, const char *s, uint32_t fg) {
    unsigned scale = gui_scale_factor();
    chos_gfx_ui_text_scaled(&screen, gui_coord_x(x), gui_coord_y(y), s, fg, 1, scale);
}
static void mono_text(int x, int y, const char *s, uint32_t fg) {
    unsigned scale = gui_scale_factor();
    chos_gfx_text_scaled(&screen, gui_coord_x(x), gui_coord_y(y), s, fg, 0u, 0, scale);
}
static void rect(int x, int y, int w, int h, uint32_t c) {
    chos_gfx_fill_rect(&screen, gui_coord_x(x), gui_coord_y(y), gui_size(w), gui_size(h), c);
}
static void outline(int x, int y, int w, int h, uint32_t c) {
    chos_gfx_outline_rect(&screen, gui_coord_x(x), gui_coord_y(y), gui_size(w), gui_size(h), c);
}
static void draw_hline(int x, int y, int w, uint32_t c) {
    chos_gfx_hline(&screen, gui_coord_x(x), gui_coord_y(y), gui_size(w), c);
}
static void draw_vline(int x, int y, int h, uint32_t c) {
    chos_gfx_vline(&screen, gui_coord_x(x), gui_coord_y(y), gui_size(h), c);
}
static void draw_bevel(int x, int y, int w, int h, uint32_t light, uint32_t dark, int raised) {
    chos_gfx_bevel(&screen, gui_coord_x(x), gui_coord_y(y), gui_size(w), gui_size(h), light, dark, raised);
}
static void draw_glyph(int x, int y, char ch, uint32_t fg, uint32_t bg, int opaque) {
    chos_gfx_char_scaled(&screen, gui_coord_x(x), gui_coord_y(y), ch, fg, bg, opaque, gui_scale_factor());
}
static int gui_strings_equal(const char *left, const char *right) {
    size_t i = 0u;
    if (left == NULL || right == NULL) return 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) return 0;
        ++i;
    }
    return left[i] == right[i];
}

static int gui_prefix_equal(const char *text, const char *prefix) {
    size_t i = 0u;
    if (text == NULL || prefix == NULL) return 0;
    while (prefix[i] != '\0') { if (text[i] != prefix[i]) return 0; ++i; }
    return 1;
}

static const gui_embedded_image_t *gui_asset_find(const char *name) {
    size_t i;
    if (name == NULL) return NULL;
    for (i = 0u; i < CHOS_ICON_ASSET_COUNT; ++i) {
        if (gui_strings_equal(chos_icon_assets[i].name, name)) return chos_icon_assets[i].image;
    }
    return NULL;
}

static uint32_t gui_alpha_over(uint32_t source, uint32_t destination) {
    unsigned a = (source >> 24) & 255u;
    unsigned ia = 255u - a;
    unsigned r = (((source >> 16) & 255u) * a + ((destination >> 16) & 255u) * ia + 127u) / 255u;
    unsigned g = (((source >> 8) & 255u) * a + ((destination >> 8) & 255u) * ia + 127u) / 255u;
    unsigned b = ((source & 255u) * a + (destination & 255u) * ia + 127u) / 255u;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

static void gui_draw_image(const gui_embedded_image_t *image, int x, int y, int width, int height) {
    int px, py;
    int dst_x, dst_y, dst_w, dst_h;
    if (image == NULL || image->pixels == NULL || image->width == 0u || image->height == 0u ||
        width <= 0 || height <= 0) return;
    dst_x = gui_coord_x(x); dst_y = gui_coord_y(y);
    dst_w = gui_size(width); dst_h = gui_size(height);
    if (dst_w <= 0 || dst_h <= 0) return;
    for (py = 0; py < dst_h; ++py) {
        uint32_t sy = (uint32_t)(((uint64_t)(uint32_t)py * image->height) / (uint32_t)dst_h);
        if (sy >= image->height) sy = image->height - 1u;
        for (px = 0; px < dst_w; ++px) {
            uint32_t sx = (uint32_t)(((uint64_t)(uint32_t)px * image->width) / (uint32_t)dst_w);
            uint32_t source, destination;
            int64_t dx = (int64_t)dst_x + px, dy = (int64_t)dst_y + py;
            if (sx >= image->width) sx = image->width - 1u;
            source = image->pixels[(size_t)sy * image->width + sx];
            if ((source >> 24) == 0u || dx < 0 || dy < 0 || (uint64_t)dx >= screen.width || (uint64_t)dy >= screen.height) continue;
            if ((source >> 24) == 255u) {
                chos_gfx_pixel(&screen, (int32_t)dx, (int32_t)dy, source);
            } else {
                destination = screen.pixels[(size_t)dy * screen.stride_pixels + (size_t)dx];
                chos_gfx_pixel(&screen, (int32_t)dx, (int32_t)dy, gui_alpha_over(source, destination));
            }
        }
    }
}

static void gui_draw_named_icon(const char *name, int x, int y, int width, int height) {
    gui_draw_image(gui_asset_find(name), x, y, width, height);
}

static int gui_config_value(const char *config, size_t config_size, const char *key,
                            unsigned *value_out) {
    size_t i, key_len = 0u;
    if (config == NULL || key == NULL || value_out == NULL) return 0;
    while (key[key_len] != '\0') ++key_len;
    for (i = 0u; i + key_len < config_size; ++i) {
        size_t j;
        unsigned value = 0u;
        int have_digit = 0;
        if (i != 0u && config[i - 1u] != '\n') continue;
        for (j = 0u; j < key_len; ++j) if (config[i + j] != key[j]) break;
        if (j != key_len) continue;
        j = i + key_len;
        while (j < config_size && config[j] >= '0' && config[j] <= '9') {
            have_digit = 1;
            if (value > 100000u) return 0;
            value = value * 10u + (unsigned)(config[j] - '0');
            ++j;
        }
        if (have_digit && (j == config_size || config[j] == '\n' || config[j] == '\r')) {
            *value_out = value;
            return 1;
        }
        return 0;
    }
    return 0;
}

static void gui_save_settings(void) {
    static const char prefix[] = "CHOSCFG1\nmode=";
    static const char wallpaper_line[] = "\nwallpaper=";
    static const char audio_line[] = "\naudio=";
    static const char timezone_line[] = "\ntimezone=";
    char config[128];
    size_t pos = 0u;
    unsigned mode = (unsigned)virtio_gpu_current_mode_index();
    if (settings_loading) return;
    if (!storage_mounted()) { settings_save_error = 1; return; }
#define CFG_APPEND_BYTES(src, count) do { size_t cfg_i; for (cfg_i = 0u; cfg_i < (count) && pos < sizeof(config); ++cfg_i) config[pos++] = (src)[cfg_i]; } while (0)
    CFG_APPEND_BYTES(prefix, sizeof(prefix) - 1u);
    config[pos++] = (char)('0' + ((mode / 10u) % 10u));
    config[pos++] = (char)('0' + (mode % 10u));
    CFG_APPEND_BYTES(wallpaper_line, sizeof(wallpaper_line) - 1u);
    config[pos++] = wallpaper_alt ? '1' : '0';
    CFG_APPEND_BYTES(audio_line, sizeof(audio_line) - 1u);
    config[pos++] = startup_sound_enabled ? '1' : '0';
    CFG_APPEND_BYTES(timezone_line, sizeof(timezone_line) - 1u);
    config[pos++] = (char)('0' + (timezone_index / 10u));
    config[pos++] = (char)('0' + (timezone_index % 10u));
    config[pos++] = '\n';
#undef CFG_APPEND_BYTES
    /* Older disk images may not have a CFG directory yet. Runtime CHFS writes
       deliberately do not create missing parent directories, so provision it
       before the first settings save instead of silently losing mode changes. */
    if (!storage_is_directory("/CFG") && !storage_make_directory("/CFG")) {
        settings_save_error = 1;
        return;
    }
    settings_save_error = storage_write_file(SETTINGS_PATH, config, pos) ? 0 : 1;
}

static unsigned gui_load_settings(void) {
    char config[128];
    size_t read_size = 0u;
    unsigned mode = 0u, wallpaper = 0u, audio = 1u, timezone = 5u;
    wallpaper_alt = 0;
    startup_sound_enabled = 1;
    timezone_index = 5u;
    if (!storage_mounted() || !storage_read_file(SETTINGS_PATH, config, sizeof(config) - 1u, &read_size) ||
        read_size < 9u || !gui_prefix_equal(config, "CHOSCFG1\n")) return 0u;
    config[read_size] = '\0';
    (void)gui_config_value(config, read_size, "mode=", &mode);
    (void)gui_config_value(config, read_size, "wallpaper=", &wallpaper);
    (void)gui_config_value(config, read_size, "audio=", &audio);
    (void)gui_config_value(config, read_size, "timezone=", &timezone);
    if (mode >= chos_display_mode_count()) mode = 0u;
    if (timezone >= GUI_TIMEZONE_COUNT) timezone = 5u;
    timezone_index = timezone;
    wallpaper_alt = wallpaper != 0u;
    startup_sound_enabled = audio != 0u;
    return mode;
}

static void gui_chfs_entry_copy(chos_chfs_entry_t *destination,
                                const chos_chfs_entry_t *source) {
    size_t i;
    if (destination == NULL || source == NULL) return;
    for (i = 0u; i < CHOS_CHFS_PATH_MAX; ++i) destination->path[i] = source->path[i];
    destination->first_sector = source->first_sector;
    destination->sector_count = source->sector_count;
    destination->size_bytes = source->size_bytes;
    destination->crc32 = source->crc32;
}

static size_t gui_string_length(const char *value) {
    size_t length = 0u;
    if (value != NULL) while (value[length] != '\0') ++length;
    return length;
}

static void gui_copy_string(char *destination, size_t capacity, const char *source) {
    size_t i = 0u;
    if (destination == NULL || capacity == 0u) return;
    if (source != NULL) while (source[i] != '\0' && i + 1u < capacity) {
        destination[i] = source[i]; ++i;
    }
    destination[i] = '\0';
}

static const char *gui_path_leaf(const char *path) {
    const char *leaf = path;
    size_t i;
    if (path == NULL) return "";
    for (i = 0u; path[i] != '\0'; ++i) if (path[i] == '/') leaf = path + i + 1u;
    return leaf;
}

static int gui_path_is_direct_child(const char *directory, const char *path) {
    size_t n, i;
    if (directory == NULL || path == NULL) return 0;
    n = gui_string_length(directory);
    if (n == 1u && directory[0] == '/') {
        if (path[0] != '/' || path[1] == '\0') return 0;
        for (i = 1u; path[i] != '\0'; ++i) if (path[i] == '/') return 0;
        return 1;
    }
    for (i = 0u; i < n; ++i) {
        char a = directory[i], b = path[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    if (path[n] != '/') return 0;
    if (path[n + 1u] == '\0') return 0;
    for (i = n + 1u; path[i] != '\0'; ++i) if (path[i] == '/') return 0;
    return 1;
}

static int gui_desktop_layout_cell(const char *key, unsigned fallback) {
    unsigned value = fallback;
    if (desktop_layout_size != 0u && key != NULL)
        (void)gui_config_value(desktop_layout_data, desktop_layout_size, key, &value);
    return value < GUI_DESKTOP_GRID_CELLS ? (int)value : (int)fallback;
}

static void gui_desktop_add_icon(int kind, int app_view, const char *title,
                                 const char *path, const char *layout_key) {
    gui_desktop_icon_t *icon;
    size_t n;
    unsigned fallback;
    int candidate;
    if (desktop_icon_count >= GUI_DESKTOP_ICON_CAP) return;
    icon = &desktop_icons[desktop_icon_count];
    for (n = 0u; n < sizeof(*icon); ++n) ((uint8_t *)icon)[n] = 0u;
    icon->kind = kind;
    icon->app_view = app_view;
    gui_copy_string(icon->title, sizeof(icon->title), title);
    gui_copy_string(icon->path, sizeof(icon->path), path);
    gui_copy_string(icon->layout_key, sizeof(icon->layout_key), layout_key);
    fallback = (unsigned)(desktop_icon_count % GUI_DESKTOP_GRID_CELLS);
    candidate = gui_desktop_layout_cell(icon->layout_key, fallback);
    icon->cell = (unsigned)candidate;
    ++desktop_icon_count;
}

static void gui_desktop_load_layout(void) {
    size_t read_size = 0u;
    static const char header[] = "CHOSDESK1\n";
    if (desktop_layout_loaded || !storage_mounted()) return;
    desktop_layout_loaded = 1;
    desktop_layout_size = 0u;
    desktop_layout_data[0] = '\0';
    if (!storage_read_file(GUI_DESKTOP_CONFIG_PATH,
            desktop_layout_data, sizeof(desktop_layout_data) - 1u, &read_size)) return;
    if (read_size < sizeof(header) - 1u) return;
    desktop_layout_data[read_size] = '\0';
    for (size_t i = 0u; i < sizeof(header) - 1u; ++i)
        if (desktop_layout_data[i] != header[i]) return;
    desktop_layout_size = read_size;
}

static int gui_desktop_ensure_config_directory(void) {
    if (!storage_mounted()) return 0;
    if (storage_is_directory("/CFG")) return 1;
    return storage_make_directory("/CFG");
}

static int gui_ensure_desktop_directory(void) {
    if (!storage_mounted()) return 0;
    if (storage_is_directory(GUI_DESKTOP_PATH)) return 1;
    if (storage_make_directory(GUI_DESKTOP_PATH)) {
        desktop_icons_dirty = 1;
        return 1;
    }
    return 0;
}

static int gui_ensure_trash_directory(void) {
    if (!storage_mounted()) return 0;
    if (storage_is_directory(GUI_TRASH_PATH)) return 1;
    if (storage_make_directory(GUI_TRASH_PATH)) return 1;
    return 0;
}

static int gui_ensure_user_directories(void) {
    static const char *const directories[] = {
        GUI_DESKTOP_PATH, GUI_DOCUMENTS_PATH, GUI_TRASH_PATH, "/CFG"
    };
    size_t i;
    if (!storage_mounted()) return 0;
    if (desktop_directories_initialized) return 1;
    for (i = 0u; i < sizeof(directories) / sizeof(directories[0]); ++i) {
        if (!storage_is_directory(directories[i]) && !storage_make_directory(directories[i])) {
            /* CHFS has a deliberately small metadata table; expose an explicit
             * status rather than repeatedly attempting the same failed migration. */
            gui_desktop_status("Some user folders could not be initialized; storage may be full.");
            return 0;
        }
    }
    desktop_directories_initialized = 1;
    return 1;
}

static void gui_desktop_refresh_icons(void) {
    static const char *const app_keys[] = { "app0=", "app1=", "app2=", "app3=", "app4=" };
    uint8_t occupied[GUI_DESKTOP_GRID_CELLS];
    size_t i;
    chos_chfs_entry_t entry;
    if (!desktop_icons_dirty) return;
    (void)gui_ensure_user_directories();
    gui_desktop_load_layout();
    desktop_icon_count = 0u;
    for (i = 0u; i < 5u; ++i) {
        char layout_key[16];
        gui_copy_string(layout_key, sizeof(layout_key), app_keys[i]);
        gui_desktop_add_icon(GUI_DESKTOP_APP, icon_views[i], icon_names[i], "", layout_key);
    }
    gui_desktop_add_icon(GUI_DESKTOP_TRASH, VIEW_TRASH, "Trash", GUI_TRASH_PATH, "trash=");
    gui_desktop_add_icon(GUI_DESKTOP_DOCUMENTS, -1, "Documents", GUI_DOCUMENTS_PATH, "documents=");
    if (storage_mounted() && storage_is_directory(GUI_DESKTOP_PATH)) {
        for (i = 0u; i < CHOS_CHFS_MAX_FILES; ++i) {
            char key[CHOS_CHFS_PATH_MAX + 8u];
            const char *leaf;
            int kind;
            size_t j, key_pos = 0u;
            if (!storage_get_at(i, &entry)) break;
            if (!gui_path_is_direct_child(GUI_DESKTOP_PATH, entry.path)) continue;
            leaf = gui_path_leaf(entry.path);
            if (leaf[0] == '\0') continue;
            kind = storage_is_directory(entry.path) ? GUI_DESKTOP_DIRECTORY :
                   (string_ends_with_fold(leaf, ".TXT") ? GUI_DESKTOP_TEXT : GUI_DESKTOP_FILE);
            key[key_pos++] = 'p'; key[key_pos++] = 'a'; key[key_pos++] = 't';
            key[key_pos++] = 'h'; key[key_pos++] = ':';
            for (j = 0u; entry.path[j] != '\0' && key_pos + 2u < sizeof(key); ++j)
                key[key_pos++] = entry.path[j];
            key[key_pos++] = '='; key[key_pos] = '\0';
            gui_desktop_add_icon(kind, -1, leaf, entry.path, key);
        }
    }
    for (i = 0u; i < GUI_DESKTOP_GRID_CELLS; ++i) occupied[i] = 0u;
    for (i = 0u; i < desktop_icon_count; ++i) {
        unsigned cell = desktop_icons[i].cell;
        if (cell >= GUI_DESKTOP_GRID_CELLS || occupied[cell]) {
            unsigned replacement;
            for (replacement = 0u; replacement < GUI_DESKTOP_GRID_CELLS && occupied[replacement]; ++replacement) {}
            if (replacement < GUI_DESKTOP_GRID_CELLS) cell = replacement;
        }
        desktop_icons[i].cell = cell;
        desktop_position_cells[i] = cell;
        if (cell < GUI_DESKTOP_GRID_CELLS) occupied[cell] = 1u;
    }
    if (selected_desktop_icon >= (int)desktop_icon_count) selected_desktop_icon = -1;
    desktop_icons_dirty = 0;
}

static void gui_buffer_append(char *buffer, size_t capacity, size_t *length, const char *value) {
    size_t i = 0u;
    if (buffer == NULL || length == NULL || value == NULL) return;
    while (value[i] != '\0' && *length + 1u < capacity) buffer[(*length)++] = value[i++];
    if (capacity != 0u) buffer[*length < capacity ? *length : capacity - 1u] = '\0';
}

static void gui_buffer_append_uint(char *buffer, size_t capacity, size_t *length, unsigned value) {
    char reverse[12];
    size_t count = 0u;
    do { reverse[count++] = (char)('0' + value % 10u); value /= 10u; } while (value != 0u && count < sizeof(reverse));
    while (count != 0u && *length + 1u < capacity) buffer[(*length)++] = reverse[--count];
    if (capacity != 0u) buffer[*length < capacity ? *length : capacity - 1u] = '\0';
}

static void gui_desktop_save_layout(void) {
    static const char header[] = "CHOSDESK1\n";
    char config[2048];
    size_t length = 0u, i;
    if (!storage_mounted() || !gui_desktop_ensure_config_directory()) return;
    config[0] = '\0';
    gui_buffer_append(config, sizeof(config), &length, header);
    for (i = 0u; i < desktop_icon_count; ++i) {
        gui_buffer_append(config, sizeof(config), &length, desktop_icons[i].layout_key);
        gui_buffer_append_uint(config, sizeof(config), &length, desktop_icons[i].cell);
        if (length + 1u < sizeof(config)) config[length++] = '\n';
        config[length] = '\0';
    }
    if (length != 0u && storage_write_file(GUI_DESKTOP_CONFIG_PATH, config, length)) {
        /* Future rebuilds of the icon model must read the just-saved layout
           rather than keep using a stale pre-save cache. */
        desktop_layout_loaded = 0;
        desktop_layout_size = 0u;
    }
}

static void gui_desktop_cell_xy(unsigned cell, int *x, int *y) {
    if (cell >= GUI_DESKTOP_GRID_CELLS) cell = 0u;
    if (x != NULL) *x = GUI_DESKTOP_ICON_X + (int)(cell / GUI_DESKTOP_GRID_ROWS) * GUI_DESKTOP_CELL_W;
    if (y != NULL) *y = GUI_DESKTOP_ICON_Y + (int)(cell % GUI_DESKTOP_GRID_ROWS) * GUI_DESKTOP_CELL_H;
}

static unsigned gui_desktop_cell_at(int32_t physical_x, int32_t physical_y) {
    int scale = (int)gui_scale_factor();
    int x = physical_x / scale, y = physical_y / scale;
    int col = (x - GUI_DESKTOP_ICON_X + GUI_DESKTOP_CELL_W / 2) / GUI_DESKTOP_CELL_W;
    int row = (y - GUI_DESKTOP_ICON_Y + GUI_DESKTOP_CELL_H / 2) / GUI_DESKTOP_CELL_H;
    if (col < 0) col = 0;
    if (col >= (int)GUI_DESKTOP_GRID_COLUMNS) col = (int)GUI_DESKTOP_GRID_COLUMNS - 1;
    if (row < 0) row = 0;
    if (row >= (int)GUI_DESKTOP_GRID_ROWS) row = (int)GUI_DESKTOP_GRID_ROWS - 1;
    return (unsigned)(col * (int)GUI_DESKTOP_GRID_ROWS + row);
}

static void gui_desktop_icon_xy(size_t index, int *x, int *y) {
    unsigned cell;
    if (index >= desktop_icon_count) { if (x != NULL) *x = 0; if (y != NULL) *y = 0; return; }
    cell = desktop_icons[index].cell;
    if ((int)index == desktop_drag_index && desktop_drag_moved) cell = (unsigned)desktop_drag_current_cell;
    gui_desktop_cell_xy(cell, x, y);
}

static int gui_desktop_icon_at(int32_t physical_x, int32_t physical_y) {
    int scale = (int)gui_scale_factor();
    int x = physical_x / scale, y = physical_y / scale;
    size_t i;
    gui_desktop_refresh_icons();
    for (i = 0u; i < desktop_icon_count; ++i) {
        int ix, iy;
        gui_desktop_icon_xy(i, &ix, &iy);
        if (x >= ix - 8 && x < ix + 60 && y >= iy - 4 && y < iy + 62) return (int)i;
    }
    return -1;
}

static void gui_desktop_label_fit(const char *source, char output[CHOS_CHFS_PATH_MAX]) {
    size_t source_length = gui_string_length(source);
    size_t prefix_length = source_length;
    gui_copy_string(output, CHOS_CHFS_PATH_MAX, source);
    if (chos_gfx_ui_text_width(output, 0) <= 82) return;
    if (source_length > CHOS_CHFS_PATH_MAX - 1u) source_length = CHOS_CHFS_PATH_MAX - 1u;
    prefix_length = source_length;
    while (prefix_length > 0u) {
        size_t i;
        output[prefix_length] = '\0';
        if (prefix_length + 3u < CHOS_CHFS_PATH_MAX) {
            output[prefix_length] = '.';
            output[prefix_length + 1u] = '.';
            output[prefix_length + 2u] = '.';
            output[prefix_length + 3u] = '\0';
        } else {
            output[CHOS_CHFS_PATH_MAX - 4u] = '.';
            output[CHOS_CHFS_PATH_MAX - 3u] = '.';
            output[CHOS_CHFS_PATH_MAX - 2u] = '.';
            output[CHOS_CHFS_PATH_MAX - 1u] = '\0';
        }
        if (chos_gfx_ui_text_width(output, 0) <= 82) return;
        --prefix_length;
        for (i = prefix_length; i < CHOS_CHFS_PATH_MAX; ++i) output[i] = '\0';
    }
    gui_copy_string(output, CHOS_CHFS_PATH_MAX, "...");
}


static void gui_desktop_status(const char *message) {
    gui_copy_string(desktop_status_message, sizeof(desktop_status_message), message);
    desktop_status_until_tick = timer_is_ready() ? timer_ticks() + 5u * CHOS_TIMER_HZ : 0u;
    gui_mark_dirty();
}

static int gui_path_join(const char *directory, const char *leaf, char output[CHOS_CHFS_PATH_MAX]) {
    size_t n = 0u, i = 0u;
    if (directory == NULL || leaf == NULL || leaf[0] == '\0') return 0;
    while (directory[n] != '\0') ++n;
    if (n + (n == 1u && directory[0] == '/' ? 0u : 1u) >= CHOS_CHFS_PATH_MAX) return 0;
    for (i = 0u; i < n; ++i) output[i] = directory[i];
    if (!(n == 1u && directory[0] == '/')) output[n++] = '/';
    for (i = 0u; leaf[i] != '\0'; ++i) {
        if (n + 1u >= CHOS_CHFS_PATH_MAX) return 0;
        output[n++] = leaf[i];
    }
    output[n] = '\0';
    return 1;
}

static int gui_name_valid(const char *name) {
    size_t i, n = gui_string_length(name);
    if (n == 0u || n >= GUI_DESKTOP_NAME_CAPACITY || name[0] == ' ' || name[n - 1u] == ' ' ||
        gui_strings_equal(name, ".") || gui_strings_equal(name, "..")) return 0;
    for (i = 0u; i < n; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (ch < 32u || ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' ||
            ch == '"' || ch == '<' || ch == '>' || ch == '|') return 0;
    }
    return 1;
}

static void gui_prompt_open(int kind, const char *target_path) {
    prompt_kind = kind;
    prompt_error = 0;
    prompt_name_length = 0u;
    prompt_name[0] = '\0';
    prompt_target_path[0] = '\0';
    if (target_path != NULL) gui_copy_string(prompt_target_path, sizeof(prompt_target_path), target_path);
    if (kind == GUI_PROMPT_RENAME && target_path != NULL) {
        gui_copy_string(prompt_name, sizeof(prompt_name), gui_path_leaf(target_path));
        prompt_name_length = gui_string_length(prompt_name);
    }
    context_menu_open = 0;
    context_menu_submenu = 0;
    gui_mark_dirty();
}

static int gui_text_editor_save(void) {
    if (!viewer_editable || viewer_load_error || !storage_mounted()) {
        viewer_save_error = 1;
        gui_desktop_status("Text document could not be saved.");
        gui_mark_dirty();
        return 0;
    }
    if (!storage_write_file(viewer_path, viewer_content, viewer_content_size)) {
        viewer_save_error = 1;
        gui_desktop_status("Text document could not be saved.");
        gui_mark_dirty();
        return 0;
    }
    viewer_unsaved = 0;
    viewer_save_error = 0;
    viewer_empty_file = viewer_content_size == 0u;
    gui_desktop_status("Text document saved.");
    gui_mark_dirty();
    return 1;
}

static size_t gui_trash_count(void) {
    size_t count = 0u, i;
    chos_chfs_entry_t entry;
    if (!storage_mounted()) return 0u;
    for (i = 0u; i < CHOS_CHFS_MAX_FILES; ++i) {
        if (!storage_get_at(i, &entry)) break;
        if (gui_path_is_direct_child(GUI_TRASH_PATH, entry.path)) ++count;
    }
    return count;
}

static int gui_trash_entry_at(size_t wanted, chos_chfs_entry_t *out) {
    size_t count = 0u, i;
    chos_chfs_entry_t entry;
    if (out == NULL || !storage_mounted()) return 0;
    for (i = 0u; i < CHOS_CHFS_MAX_FILES; ++i) {
        if (!storage_get_at(i, &entry)) break;
        if (!gui_path_is_direct_child(GUI_TRASH_PATH, entry.path)) continue;
        if (count++ == wanted) { gui_chfs_entry_copy(out, &entry); return 1; }
    }
    return 0;
}

static int gui_path_has_prefix_component(const char *path, const char *prefix) {
    size_t i = 0u, n;
    if (path == NULL || prefix == NULL) return 0;
    n = gui_string_length(prefix);
    if (n == 0u) return 0;
    while (i < n && path[i] != '\0') {
        char a = path[i], b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
        ++i;
    }
    return i == n && (path[i] == '\0' || path[i] == '/');
}

static int gui_remove_tree(const char *root_path) {
    size_t pass;
    chos_chfs_entry_t root_entry;
    if (root_path == NULL || !storage_mounted()) return 0;
    /* Remove the deepest descendant first; CHFS refuses to remove a directory
       while it still contains entries. Each successful call updates metadata. */
    for (pass = 0u; pass < CHOS_CHFS_MAX_FILES + 1u; ++pass) {
        chos_chfs_entry_t entry, deepest;
        size_t i, deepest_length = 0u;
        int found = 0;
        if (!storage_lookup(root_path, &root_entry) && !storage_is_directory(root_path)) return 1;
        for (i = 0u; i < CHOS_CHFS_MAX_FILES; ++i) {
            size_t length;
            if (!storage_get_at(i, &entry)) break;
            if (!gui_path_has_prefix_component(entry.path, root_path)) continue;
            length = gui_string_length(entry.path);
            if (length >= deepest_length) { gui_chfs_entry_copy(&deepest, &entry); deepest_length = length; found = 1; }
        }
        if (!found) return 1;
        if (storage_is_directory(deepest.path)) {
            if (!storage_remove_directory(deepest.path)) return 0;
        } else if (!storage_remove_file(deepest.path)) return 0;
    }
    return !storage_lookup(root_path, &root_entry) && !storage_is_directory(root_path);
}

static int gui_make_unique_trash_path(const char *source_path, char output[CHOS_CHFS_PATH_MAX]) {
    const char *leaf = gui_path_leaf(source_path);
    unsigned suffix = 2u;
    char name[CHOS_CHFS_PATH_MAX];
    chos_chfs_entry_t entry;
    gui_copy_string(name, sizeof(name), leaf);
    if (!gui_path_join(GUI_TRASH_PATH, name, output)) return 0;
    if (!storage_lookup(output, &entry) && !storage_is_directory(output)) return 1;
    while (suffix < 100u) {
        char numbered[GUI_DESKTOP_NAME_CAPACITY];
        size_t length = 0u;
        gui_copy_string(numbered, sizeof(numbered), leaf);
        length = gui_string_length(numbered);
        if (length + 5u >= sizeof(numbered)) length = sizeof(numbered) - 6u;
        numbered[length++] = ' ';
        numbered[length++] = '(';
        gui_buffer_append_uint(numbered, sizeof(numbered), &length, suffix);
        if (length + 2u < sizeof(numbered)) { numbered[length++] = ')'; numbered[length] = '\0'; }
        if (gui_path_join(GUI_TRASH_PATH, numbered, output) &&
            !storage_lookup(output, &entry) && !storage_is_directory(output)) return 1;
        ++suffix;
    }
    return 0;
}

static int gui_move_to_trash(const char *path) {
    char destination[CHOS_CHFS_PATH_MAX];
    int result;
    if (path == NULL || !gui_ensure_trash_directory() || !gui_make_unique_trash_path(path, destination)) return 0;
    if (storage_is_directory(path)) result = storage_rename_directory(path, destination);
    else result = storage_rename_file(path, destination);
    if (result) {
        desktop_icons_dirty = 1;
        gui_desktop_refresh_icons();
        gui_desktop_save_layout();
        gui_desktop_status("Moved item to Trash.");
    }
    return result;
}

static int gui_desktop_move_into_folder(int source_index, int target_index) {
    char destination[CHOS_CHFS_PATH_MAX];
    chos_chfs_entry_t entry;
    gui_desktop_icon_t *source, *target;
    int is_directory, result;
    if (source_index < 0 || target_index < 0 ||
        (size_t)source_index >= desktop_icon_count || (size_t)target_index >= desktop_icon_count ||
        source_index == target_index) return 0;
    source = &desktop_icons[source_index];
    target = &desktop_icons[target_index];
    if (source->kind == GUI_DESKTOP_APP || source->kind == GUI_DESKTOP_TRASH ||
        (target->kind != GUI_DESKTOP_DIRECTORY && target->kind != GUI_DESKTOP_DOCUMENTS)) return 0;
    if (!storage_mounted() || source->path[0] == '\0' || target->path[0] == '\0' ||
        !storage_is_directory(target->path)) return 0;
    if (!gui_path_join(target->path, gui_path_leaf(source->path), destination)) {
        gui_desktop_status("That folder path is too long.");
        return -1;
    }
    if (gui_strings_equal(destination, source->path)) {
        gui_desktop_status("That item is already in this folder.");
        return -1;
    }
    if (storage_lookup(destination, &entry) || storage_is_directory(destination)) {
        gui_desktop_status("Move cancelled: an item with that name already exists.");
        return -1;
    }
    is_directory = storage_is_directory(source->path);
    result = is_directory ? storage_rename_directory(source->path, destination) :
                            storage_rename_file(source->path, destination);
    if (!result) {
        gui_desktop_status(is_directory ? "Folder could not be moved." : "File could not be moved.");
        return -1;
    }
    desktop_icons_dirty = 1;
    gui_desktop_refresh_icons();
    gui_desktop_save_layout();
    gui_desktop_status("Item moved into folder.");
    gui_mark_dirty();
    return 1;
}

static int gui_restore_from_trash(const char *path) {
    char destination[CHOS_CHFS_PATH_MAX];
    chos_chfs_entry_t entry;
    int result;
    if (path == NULL || !gui_ensure_desktop_directory() ||
        !gui_path_join(GUI_DESKTOP_PATH, gui_path_leaf(path), destination)) return 0;
    if (storage_lookup(destination, &entry) || storage_is_directory(destination)) return 0;
    if (storage_is_directory(path)) result = storage_rename_directory(path, destination);
    else result = storage_rename_file(path, destination);
    if (result) {
        desktop_icons_dirty = 1;
        gui_desktop_refresh_icons();
        gui_desktop_save_layout();
        gui_desktop_status("Restored item to Desktop.");
    }
    return result;
}

static void gui_trash_open(void) {
    if (!gui_ensure_trash_directory()) {
        gui_desktop_status("Trash is unavailable without mounted storage.");
        return;
    }
    trash_selected_index = 0u;
    (void)gui_open_window(VIEW_TRASH);
    gui_mark_dirty();
}

static void gui_desktop_open_path(const char *path, int is_directory) {
    if (path == NULL) return;
    if (is_directory) {
        uint32_t pid = 0u;
        if (process_spawn_path_args("/BIN/FILEMAN.CHX", path, &pid) == PROCESS_OK && pid != 0u) {
            gui_file_manager_pid = pid;
            start_menu_open = 0; context_menu_open = 0; gui_mark_dirty();
            return;
        }
        (void)gui_open_window(VIEW_FILES);
        gui_desktop_status("Folder selected; File Manager path arguments unavailable.");
    } else if (file_is_program(path)) {
        (void)gui_open_window(VIEW_CONSOLE);
        terminal_puts("run "); terminal_puts(path); terminal_newline();
        terminal_run_path(path); terminal_prompt(); gui_mark_dirty();
    } else {
        (void)file_viewer_load(path);
        viewer_editable = string_ends_with_fold(path, ".TXT") && storage_lookup(path, &(chos_chfs_entry_t){0});
        viewer_unsaved = 0; viewer_save_error = 0;
        if (!gui_open_window(VIEW_FILE_VIEWER)) gui_desktop_status("No window slot is available.");
    }
}

static void gui_desktop_activate(int index) {
    gui_desktop_icon_t *icon;
    if (index < 0 || (size_t)index >= desktop_icon_count) return;
    icon = &desktop_icons[index];
    if (icon->kind == GUI_DESKTOP_APP) {
        selected_icon = icon->app_view - 1;
        open_view(icon->app_view);
    } else if (icon->kind == GUI_DESKTOP_TRASH) gui_trash_open();
    else gui_desktop_open_path(icon->path, icon->kind == GUI_DESKTOP_DIRECTORY ||
                                        icon->kind == GUI_DESKTOP_DOCUMENTS);
    context_menu_open = 0;
    gui_mark_dirty();
}

static void gui_start_menu_update_hover(void) {
    int scale = (int)gui_scale_factor();
    int top = TASKBAR_Y - START_MENU_HEIGHT;
    int relative_y, index;
    if (!start_menu_open || mouse_x < 54 * scale || mouse_x >= 225 * scale ||
        mouse_y < top + 42 * scale || mouse_y >= top + 228 * scale) return;
    relative_y = mouse_y - top - 42 * scale;
    index = relative_y / (31 * scale);
    if (index >= 0 && index < 6 && index != start_menu_selected_item) {
        start_menu_selected_item = index;
        gui_mark_dirty();
    }
}

static void gui_start_menu_activate_item(int index) {
    if (index < 0 || index >= 6) return;
    start_menu_selected_item = index;
    start_menu_open = 0;
    start_menu_selected_item = -1;
    context_menu_open = 0;
    if (index == 5) {
        if (!gui_launch_or_focus_app(&gui_browser_pid, "/BIN/BROWSER.CHX"))
            gui_desktop_status("Could not launch CHOS Web Browser.");
        gui_mark_dirty();
        return;
    }
    open_view(icon_views[index]);
    gui_mark_dirty();
}

static void gui_context_menu_open_at(int32_t x, int32_t y) {
    int icon_index, scale = (int)gui_scale_factor();
    gui_desktop_refresh_icons();
    icon_index = gui_desktop_icon_at(x, y);
    context_menu_icon_index = icon_index;
    context_menu_submenu = 0;
    context_menu_hover_index = -1;
    if (icon_index < 0) context_menu_kind = GUI_CONTEXT_DESKTOP;
    else {
        selected_desktop_icon = icon_index;
        if (desktop_icons[icon_index].kind == GUI_DESKTOP_APP) context_menu_kind = GUI_CONTEXT_APP_ICON;
        else if (desktop_icons[icon_index].kind == GUI_DESKTOP_TRASH) context_menu_kind = GUI_CONTEXT_TRASH_ICON;
        else if (desktop_icons[icon_index].kind == GUI_DESKTOP_DOCUMENTS) context_menu_kind = GUI_CONTEXT_DOCUMENTS_ICON;
        else context_menu_kind = GUI_CONTEXT_FILE_ICON;
    }
    context_menu_x = x; context_menu_y = y;
    if (context_menu_x + GUI_CONTEXT_WIDTH * scale > SCREEN_W) context_menu_x = SCREEN_W - GUI_CONTEXT_WIDTH * scale;
    if (context_menu_y + 6 * GUI_CONTEXT_ROW_HEIGHT * scale > TASKBAR_Y) context_menu_y = TASKBAR_Y - 6 * GUI_CONTEXT_ROW_HEIGHT * scale;
    if (context_menu_x < 0) context_menu_x = 0;
    if (context_menu_y < 0) context_menu_y = 0;
    context_menu_open = 1;
    start_menu_open = 0;
    gui_mark_dirty();
}

static size_t gui_context_row_count(void) {
    if (context_menu_submenu) return 2u;
    switch (context_menu_kind) {
        case GUI_CONTEXT_APP_ICON: return 3u;
        case GUI_CONTEXT_FILE_ICON: return 3u;
        case GUI_CONTEXT_TRASH_ICON: return 2u;
        case GUI_CONTEXT_DOCUMENTS_ICON: return 1u;
        default: return 4u;
    }
}

static void gui_context_menu_update_hover(void) {
    const int scale = (int)gui_scale_factor();
    int x = mouse_x - context_menu_x, y = mouse_y - context_menu_y - 3 * scale;
    int index = -1;
    if (!context_menu_open) return;
    if (x >= 2 * scale && x < (GUI_CONTEXT_WIDTH - 2) * scale && y >= 0 &&
        y < (int)gui_context_row_count() * GUI_CONTEXT_ROW_HEIGHT * scale)
        index = y / (GUI_CONTEXT_ROW_HEIGHT * scale);
    if (index != context_menu_hover_index) { context_menu_hover_index = index; gui_mark_dirty(); }
}

static void gui_prompt_submit(void) {
    chos_chfs_entry_t entry;
    char destination[CHOS_CHFS_PATH_MAX], parent[CHOS_CHFS_PATH_MAX];
    int result = 0;
    size_t i, parent_length;
    if (prompt_kind == GUI_PROMPT_CONFIRM_DELETE) {
        result = gui_remove_tree(prompt_target_path);
        if (result) { desktop_icons_dirty = 1; gui_desktop_status("Item permanently deleted."); }
        else gui_desktop_status("Could not delete item.");
        prompt_kind = 0; gui_mark_dirty(); return;
    }
    if (prompt_kind == GUI_PROMPT_EMPTY_TRASH) {
        size_t count = 0u;
        while (count++ < CHOS_CHFS_MAX_FILES) {
            if (gui_trash_count() == 0u) { result = 1; break; }
            if (!gui_trash_entry_at(0u, &entry) || !gui_remove_tree(entry.path)) break;
        }
        if (gui_trash_count() == 0u) { result = 1; desktop_icons_dirty = 1; gui_desktop_status("Trash emptied."); }
        else gui_desktop_status("Some Trash items could not be removed.");
        prompt_kind = 0; gui_mark_dirty(); return;
    }
    if (!gui_name_valid(prompt_name)) { prompt_error = 1; gui_mark_dirty(); return; }
    if (prompt_kind == GUI_PROMPT_NEW_FOLDER || prompt_kind == GUI_PROMPT_NEW_TEXT) {
        char filename[GUI_DESKTOP_NAME_CAPACITY];
        gui_copy_string(filename, sizeof(filename), prompt_name);
        if (prompt_kind == GUI_PROMPT_NEW_TEXT && !string_ends_with_fold(filename, ".TXT")) {
            size_t length = gui_string_length(filename);
            if (length + 4u >= sizeof(filename)) { prompt_error = 1; gui_mark_dirty(); return; }
            filename[length++] = '.'; filename[length++] = 'T'; filename[length++] = 'X'; filename[length++] = 'T'; filename[length] = '\0';
        }
        if (!gui_ensure_desktop_directory() || !gui_path_join(GUI_DESKTOP_PATH, filename, destination)) {
            prompt_error = 1; gui_mark_dirty(); return;
        }
        if (storage_lookup(destination, &entry) || storage_is_directory(destination)) {
            prompt_error = 1; gui_mark_dirty(); return;
        }
        if (prompt_kind == GUI_PROMPT_NEW_FOLDER) result = storage_make_directory(destination);
        else result = storage_write_file(destination, "", 0u);
        if (!result) { prompt_error = 1; gui_mark_dirty(); return; }
        {
            int created_text = prompt_kind == GUI_PROMPT_NEW_TEXT;
            desktop_icons_dirty = 1;
            gui_desktop_refresh_icons();
            gui_desktop_save_layout();
            prompt_kind = 0;
            gui_desktop_status(created_text ? "Text document created." : "Folder created.");
        }
        if (string_ends_with_fold(destination, ".TXT")) {
            (void)file_viewer_load(destination);
            viewer_editable = 1; viewer_unsaved = 0; viewer_save_error = 0;
            (void)gui_open_window(VIEW_FILE_VIEWER);
        }
        gui_mark_dirty();
        return;
    }
    if (prompt_kind == GUI_PROMPT_RENAME) {
        parent_length = gui_string_length(prompt_target_path);
        while (parent_length > 0u && prompt_target_path[parent_length - 1u] != '/') --parent_length;
        if (parent_length == 0u || parent_length >= sizeof(parent)) { prompt_error = 1; gui_mark_dirty(); return; }
        for (i = 0u; i < parent_length; ++i) parent[i] = prompt_target_path[i];
        if (parent_length == 1u) parent[1] = '\0'; else parent[parent_length - 1u] = '\0';
        if (!gui_path_join(parent, prompt_name, destination)) {
            prompt_error = 1; gui_mark_dirty(); return;
        }
        if (storage_lookup(destination, &entry) || storage_is_directory(destination)) {
            /* A case-insensitive no-op rename is permitted by CHFS itself. */
            if (!gui_strings_equal(destination, prompt_target_path)) { prompt_error = 1; gui_mark_dirty(); return; }
        }
        if (storage_is_directory(prompt_target_path)) result = storage_rename_directory(prompt_target_path, destination);
        else result = storage_rename_file(prompt_target_path, destination);
        if (!result) { prompt_error = 1; gui_mark_dirty(); return; }
        desktop_icons_dirty = 1;
        gui_desktop_refresh_icons();
        gui_desktop_save_layout();
        prompt_kind = 0; gui_desktop_status("Item renamed."); gui_mark_dirty(); return;
    }
}

static void gui_context_menu_click(int32_t x, int32_t y) {
    static const char *const desktop_rows[] = { "Arrange Icons", "Refresh", "New...", "Settings" };
    static const char *const app_rows[] = { "Open", "Arrange Icons", "Refresh" };
    static const char *const file_rows[] = { "Open", "Rename...", "Move to Trash" };
    static const char *const trash_rows[] = { "Open", "Empty Trash..." };
    static const char *const documents_rows[] = { "Open" };
    static const char *const new_rows[] = { "Folder", "Text Document" };
    const char *const *rows;
    size_t count, row;
    const int scale = (int)gui_scale_factor();
    int local_x = x - context_menu_x, local_y = y - context_menu_y - 3 * scale;
    if (!context_menu_open) return;
    count = gui_context_row_count();
    if (local_x < 2 * scale || local_x >= GUI_CONTEXT_WIDTH * scale || local_y < 0 ||
        local_y >= (int)count * GUI_CONTEXT_ROW_HEIGHT * scale) { context_menu_open = 0; context_menu_submenu = 0; gui_mark_dirty(); return; }
    row = (size_t)(local_y / (GUI_CONTEXT_ROW_HEIGHT * scale));
    if (context_menu_submenu) rows = new_rows;
    else if (context_menu_kind == GUI_CONTEXT_APP_ICON) rows = app_rows;
    else if (context_menu_kind == GUI_CONTEXT_FILE_ICON) rows = file_rows;
    else if (context_menu_kind == GUI_CONTEXT_TRASH_ICON) rows = trash_rows;
    else if (context_menu_kind == GUI_CONTEXT_DOCUMENTS_ICON) rows = documents_rows;
    else rows = desktop_rows;
    if (row >= count) return;
    if (!context_menu_submenu && context_menu_kind == GUI_CONTEXT_DESKTOP && row == 2u) {
        context_menu_submenu = 1;
        context_menu_x += GUI_CONTEXT_WIDTH * scale - 2 * scale;
        if (context_menu_x + GUI_CONTEXT_WIDTH * scale > SCREEN_W) context_menu_x = SCREEN_W - GUI_CONTEXT_WIDTH * scale;
        context_menu_hover_index = -1; gui_mark_dirty(); return;
    }
    if (context_menu_submenu) {
        context_menu_open = 0; context_menu_submenu = 0;
        if (row == 0u) gui_prompt_open(GUI_PROMPT_NEW_FOLDER, NULL);
        else gui_prompt_open(GUI_PROMPT_NEW_TEXT, NULL);
        return;
    }
    if (gui_strings_equal(rows[row], "Arrange Icons")) {
        size_t i;
        gui_desktop_refresh_icons();
        for (i = 0u; i < desktop_icon_count; ++i) desktop_icons[i].cell = (unsigned)(i % GUI_DESKTOP_GRID_CELLS);
        gui_desktop_save_layout(); desktop_icons_dirty = 1; gui_desktop_status("Icons arranged to the grid.");
    } else if (gui_strings_equal(rows[row], "Refresh")) {
        desktop_icons_dirty = 1; gui_desktop_status("Desktop refreshed.");
    } else if (gui_strings_equal(rows[row], "Settings")) {
        context_menu_open = 0; open_view(VIEW_SETTINGS); return;
    } else if (gui_strings_equal(rows[row], "Open")) {
        int index = context_menu_icon_index;
        context_menu_open = 0;
        if (context_menu_kind == GUI_CONTEXT_TRASH_ICON) gui_trash_open();
        else if (context_menu_kind == GUI_CONTEXT_DOCUMENTS_ICON && index >= 0) gui_desktop_activate(index);
        else if (context_menu_kind == GUI_CONTEXT_FILE_ICON && index >= 0) gui_desktop_activate(index);
        else if (index >= 0) gui_desktop_activate(index);
        return;
    } else if (gui_strings_equal(rows[row], "Rename...")) {
        int index = context_menu_icon_index;
        if (index >= 0) gui_prompt_open(GUI_PROMPT_RENAME, desktop_icons[index].path);
    } else if (gui_strings_equal(rows[row], "Move to Trash")) {
        int index = context_menu_icon_index;
        if (index >= 0 && gui_move_to_trash(desktop_icons[index].path)) desktop_icons_dirty = 1;
        else gui_desktop_status("Could not move item to Trash.");
        context_menu_open = 0;
    } else if (gui_strings_equal(rows[row], "Empty Trash...")) {
        gui_prompt_open(GUI_PROMPT_EMPTY_TRASH, NULL);
    }
    context_menu_open = 0;
    context_menu_submenu = 0;
    desktop_icons_dirty = 1;
    gui_mark_dirty();
}

static void draw_number(int x, int y, uint32_t value, uint32_t color) {
    char digits[11];
    size_t count = 0u;
    if (value == 0u) { text(x, y, "0", color); return; }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    {
        size_t i;
        for (i = 0u; i < count; ++i) {
            draw_glyph(x + (int)(i * 8u), y, digits[count - i - 1u], color, 0u, 0);
        }
    }
}
static void draw_button(int x, int y, int w, int h, const char *label, int pressed) {
    /* Classic raised/recessed gray controls; the default UI has no glossy gradients. */
    rect(x, y, w, h, C_PANEL);
    draw_bevel(x, y, w, h, pressed ? C_PANEL_DARK : C_WHITE,
               pressed ? C_WHITE : C_BLACK, !pressed);
    if (w >= 8 && h >= 8) {
        draw_hline(x + 2, y + 2, w - 4, pressed ? C_PANEL_DARK : C_PANEL_LIGHT);
        draw_vline(x + 2, y + 2, h - 4, pressed ? C_PANEL_DARK : C_PANEL_LIGHT);
    }
    if (label != NULL) {
        int metric_scale = physical_coordinates ? (int)gui_scale_factor() : 1;
        int label_width = chos_gfx_ui_text_width(label, 1) * metric_scale;
        int label_height = (int)CHOS_GFX_UI_LINE_HEIGHT * metric_scale;
        int label_x = x + (w - label_width) / 2;
        text_bold(label_x, y + (h - label_height) / 2, label, C_TEXT);
    }
}
static void draw_icon_art(int x, int y, int kind) {
    static const char *const assets[] = {
        "console", "file_manager", "about", "diagnostics", "settings",
        "text_viewer", "display", "network", "help", "shutdown", "restart", "computer", "system_monitor"
    };
    const char *name = (kind >= 0 && (size_t)kind < sizeof(assets) / sizeof(assets[0])) ? assets[kind] : "computer";
    int width = 40, height = 38;
    /* PNGs live in assets/icons and are embedded at build time. Replacing a PNG
       changes the artwork without touching the GUI renderer. */
    /* Transparent PNG pixels must leave the desktop visible. The old shadow
       rectangle used an alpha color in a non-alpha-aware fill primitive, which
       painted dark opaque boxes behind otherwise transparent icons. */
    gui_draw_named_icon(name, x, y, width, height);
}

static void draw_desktop_special_icon(int x, int y, int kind) {
    if (kind == GUI_DESKTOP_TRASH) {
        /* Original CHOS recycle bin: clean pixel art with a metal body and lid. */
        rect(x + 8, y + 7, 24, 4, C_PANEL_DARK);
        rect(x + 10, y + 11, 20, 24, C_PANEL_LIGHT);
        draw_bevel(x + 10, y + 11, 20, 24, C_WHITE, C_PANEL_DARK, 1);
        rect(x + 7, y + 5, 26, 3, C_PANEL);
        draw_bevel(x + 7, y + 5, 26, 3, C_WHITE, C_BLACK, 1);
        rect(x + 15, y + 1, 10, 4, C_PANEL_DARK);
        rect(x + 15, y + 15, 2, 14, C_PANEL_DARK);
        rect(x + 19, y + 15, 2, 14, C_PANEL_DARK);
        rect(x + 23, y + 15, 2, 14, C_PANEL_DARK);
    } else if (kind == GUI_DESKTOP_DIRECTORY || kind == GUI_DESKTOP_DOCUMENTS) {
        rect(x + 4, y + 9, 20, 8, 0xffffd653u);
        rect(x + 4, y + 15, 32, 20, 0xffffd653u);
        draw_bevel(x + 4, y + 15, 32, 20, 0xfffff0a0u, 0xff886000u, 1);
        rect(x + 6, y + 18, 28, 14, 0xffffd45cu);
    } else {
        rect(x + 7, y + 2, 26, 34, C_WHITE);
        draw_bevel(x + 7, y + 2, 26, 34, C_WHITE, C_PANEL_DARK, 1);
        rect(x + 12, y + 10, 15, 2, C_BLUE_DARK);
        rect(x + 12, y + 16, 15, 2, C_PANEL_DARK);
        rect(x + 12, y + 22, 15, 2, C_PANEL_DARK);
        rect(x + 12, y + 28, 11, 2, C_PANEL_DARK);
    }
}

static void draw_desktop_icon(size_t index) {
    gui_desktop_icon_t *icon;
    char label[CHOS_CHFS_PATH_MAX];
    int x, y, label_x;
    if (index >= desktop_icon_count) return;
    icon = &desktop_icons[index];
    gui_desktop_icon_xy(index, &x, &y);
    gui_desktop_label_fit(icon->title, label);
    label_x = x + 20 - chos_gfx_ui_text_width(label, 0) / 2;
    if (label_x < 1) label_x = 1;
    if (label_x + chos_gfx_ui_text_width(label, 0) > GUI_LOGICAL_WIDTH - 1)
        label_x = GUI_LOGICAL_WIDTH - 1 - chos_gfx_ui_text_width(label, 0);
    if ((int)index == selected_desktop_icon) {
        rect(x - 8, y - 4, 64, 62, C_SELECTION);
        outline(x - 8, y - 4, 64, 62, 0xffc4e5ffu);
    }
    if (icon->kind == GUI_DESKTOP_APP) draw_icon_art(x, y, icon->app_view - 1);
    else draw_desktop_special_icon(x, y, icon->kind);
    text(label_x + 1, y + 42, label, C_BLACK);
    text(label_x, y + 41, label, C_WHITE);
}

static void draw_titlebar(int x, int y, int w, const char *title) {
    int control_y = y + 4;
    rect(x + 2, y + 2, w - 4, 24, drawing_active_window ? C_BLUE_DARK : C_PANEL_DARK);
    text_bold(x + 7, y + 7, title, C_WHITE);
    /* Conventional Windows 9x order: minimize, maximize/restore, close. */
    draw_button(x + w - 68, control_y, 18, 18, NULL, 0);
    draw_hline(x + w - 63, control_y + 12, 8, C_BLACK);
    draw_button(x + w - 47, control_y, 18, 18, NULL, 0);
    outline(x + w - 42, control_y + 5, 8, 8, C_BLACK);
    draw_hline(x + w - 41, control_y + 6, 6, C_BLACK);
    draw_button(x + w - 26, control_y, 18, 18, NULL, 0);
    text_bold(x + w - 22, control_y + 2, "x", C_BLACK);
}

static void draw_window_shell(const char *title, int x, int y, int w, int h) {
    if (drawing_maximized_window) {
        unsigned scale = gui_scale_factor();
        w = (int)(screen.width / scale);
        h = (int)((size_t)TASKBAR_Y / scale);
    }
    rect(x + 4, y + 4, w, h, C_PANEL_DARK);
    rect(x, y, w, h, C_PANEL);
    draw_bevel(x, y, w, h, C_WHITE, C_BLACK, 1);
    draw_titlebar(x, y, w, title);
    rect(x + 3, y + 28, w - 6, h - 31, C_PANEL);
}

static void draw_status_line(const char *left, const char *right) {
    rect(190, 511, 578, 19, C_PANEL);
    draw_hline(190, 511, 578, C_PANEL_DARK);
    text(198, 515, left, C_MUTED);
    if (right != NULL) text(605, 515, right, C_MUTED);
}

static const int32_t window_x[GUI_VIEW_COUNT] = {180, 190, 105, 150, 135, 165, 200, 165};
static const int32_t window_y[GUI_VIEW_COUNT] = {70, 58, 65, 82, 62, 76, 60, 60};

static int window_title_for_view(int id, const char **title, const char **short_title) {
    static const char *const titles[GUI_VIEW_COUNT] = {"Welcome to CHOS", "Console - CHOS", "File Manager - CHOS", "About CHOS", "CHOS Diagnostics", "Desktop Settings", "Text Viewer - CHOS", "Trash - CHOS"};
    static const char *const short_titles[GUI_VIEW_COUNT] = {"Home", "Console", "Files", "About", "Diag", "Settings", "Viewer", "Trash"};
    if (id < VIEW_WELCOME || id >= (int)GUI_VIEW_COUNT) return 0;
    if (title != NULL) *title = titles[id];
    if (short_title != NULL) *short_title = short_titles[id];
    return 1;
}

static int32_t gui_default_window_x(int id) {
    int32_t x = (int32_t)((int64_t)window_x[id] * gui_scale_factor()) +
                (SCREEN_W - (int32_t)GUI_LOGICAL_WIDTH * (int32_t)gui_scale_factor()) / 2;
    int32_t maximum = SCREEN_W - GUI_WINDOW_WIDTH;
    if (maximum < 0) maximum = 0;
    if (x < 0) x = 0;
    if (x > maximum) x = maximum;
    return x;
}
static int32_t gui_default_window_y(int id) {
    int32_t y = (int32_t)((int64_t)window_y[id] * gui_scale_factor()) +
                (SCREEN_H - (int32_t)GUI_LOGICAL_HEIGHT * (int32_t)gui_scale_factor()) / 2;
    int32_t maximum = TASKBAR_Y - GUI_WINDOW_HEIGHT;
    if (maximum < 0) maximum = 0;
    if (y < 0) y = 0;
    if (y > maximum) y = maximum;
    return y;
}
static int gui_window_open(int id) { return chos_wm_get_const(&window_manager, id) != NULL; }
static void gui_set_active_view(int next_view) {
    int old_view = view;
    if (old_view != next_view) {
        if (gui_app_find_window(old_view) != NULL)
            gui_app_queue_focus(old_view, CHOS_GUI_EVENT_FOCUS_LOST);
        view = next_view;
        if (gui_app_find_window(next_view) != NULL)
            gui_app_queue_focus(next_view, CHOS_GUI_EVENT_FOCUS_GAINED);
    }
    gui_mark_dirty();
}
static void gui_sync_active_view(void) {
    int active = chos_wm_active_id(&window_manager);
    gui_set_active_view(active != CHOS_WM_NO_WINDOW ? active : VIEW_WELCOME);
}
static int gui_open_window(int id) {
    const char *title = NULL;
    if (!window_manager_initialized) { chos_wm_init(&window_manager); window_manager_initialized = 1; }
    if (!window_title_for_view(id, &title, NULL)) return 0;
    if (!chos_wm_open(&window_manager, id, title, gui_default_window_x(id),
                      gui_default_window_y(id), GUI_WINDOW_WIDTH, GUI_WINDOW_HEIGHT)) return 0;
    if (id == VIEW_CONSOLE) terminal_initialize();
    gui_sync_active_view(); start_menu_open = 0;
    return 1;
}
static void gui_minimize_window(int id) {
    if (chos_wm_minimize(&window_manager, id)) gui_sync_active_view();
}
static void gui_toggle_maximize_window(int id) {
    chos_wm_window_t *window = chos_wm_get(&window_manager, id);
    if (window == NULL) return;
    if (chos_wm_toggle_maximized(&window_manager, id, 0, 0, SCREEN_W, TASKBAR_Y)) {
        gui_sync_active_view();
        cursor_under_valid = 0; cursor_move_pending = 0;
    }
}
static void gui_close_window(int id) {
    gui_app_client_t *client = gui_app_find_window(id);
    if (client != NULL) {
        chos_gui_message_t event;
        for (size_t i = 0u; i < sizeof(event); ++i) ((uint8_t *)&event)[i] = 0u;
        event.header.version = CHOS_GUI_ABI_VERSION;
        event.header.type = CHOS_GUI_MSG_CLOSE_REQUEST;
        event.header.size_bytes = (uint32_t)(offsetof(chos_gui_message_t, payload) + sizeof(event.payload.window));
        event.payload.window.window_id = (uint32_t)id;
        gui_app_queue_message(client, &event);
        gui_mark_dirty();
        return;
    }
    if (id == VIEW_CONSOLE) terminal_command[0] = '\0';
    if (id == VIEW_FILE_VIEWER && viewer_editable && viewer_unsaved && !gui_text_editor_save()) return;
    if (chos_wm_close(&window_manager, id)) gui_sync_active_view();
}

static int gui_apply_display_mode(size_t mode_index) {
    chos_surface_t updated_surface;
    const int32_t old_width = SCREEN_W;
    const int32_t old_height = SCREEN_H;
    size_t i;
    if (mode_index >= chos_display_mode_count()) return 0;
    display_mode_error = 0;
    if (!virtio_gpu_set_mode(mode_index, &updated_surface)) {
        display_mode_error = 1;
        gui_mark_dirty();
        return 0;
    }
    screen = updated_surface;
    taskbar_tooltip_window_id = CHOS_WM_NO_WINDOW;
    taskbar_tooltip_fade_step = 0u;
    wallpaper_cache_valid = 0;
    display_mode_page = mode_index / DISPLAY_MODES_PER_PAGE;
    for (i = 0u; i < GUI_VIEW_COUNT; ++i) {
        chos_wm_window_t *window = chos_wm_get(&window_manager, (int)i);
        int32_t max_x, max_y, new_x, new_y;
        if (window == NULL) continue;
        if (window->maximized) {
            window->x = 0; window->y = 0;
            window->width = SCREEN_W; window->height = TASKBAR_Y;
            window->restore_width = GUI_WINDOW_WIDTH; window->restore_height = GUI_WINDOW_HEIGHT;
            max_x = SCREEN_W - window->restore_width; max_y = TASKBAR_Y - window->restore_height;
            if (max_x < 0) max_x = 0;
            if (max_y < 0) max_y = 0;
            window->restore_x = gui_default_window_x((int)i);
            window->restore_y = gui_default_window_y((int)i);
        } else {
            window->width = GUI_WINDOW_WIDTH;
            window->height = GUI_WINDOW_HEIGHT;
            max_x = SCREEN_W - window->width;
            max_y = TASKBAR_Y - window->height;
            if (max_x < 0) max_x = 0;
            if (max_y < 0) max_y = 0;
            new_x = gui_default_window_x((int)i);
            new_y = gui_default_window_y((int)i);
            (void)chos_wm_move(&window_manager, (int)i, new_x, new_y, 0, 0, max_x, max_y);
        }
    }
    mouse_x = (int32_t)(((int64_t)mouse_x * SCREEN_W) / (old_width > 0 ? old_width : 1));
    mouse_y = (int32_t)(((int64_t)mouse_y * SCREEN_H) / (old_height > 0 ? old_height : 1));
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= SCREEN_W) mouse_x = SCREEN_W - 1;
    if (mouse_y >= SCREEN_H) mouse_y = SCREEN_H - 1;
    gui_mark_dirty();
    gui_save_settings();
    return 1;
}

static int gui_taskbar_view_at(size_t wanted) {
    size_t i, index = 0u;
    for (i = 0u; i < GUI_VIEW_COUNT; ++i) {
        if (!gui_window_open((int)i)) continue;
        if (index == wanted) return (int)i;
        ++index;
    }
    for (i = 0u; i < GUI_APP_MAX_CLIENTS; ++i) {
        if (!gui_app_clients[i].active || !gui_window_open(gui_app_clients[i].window_id)) continue;
        if (index == wanted) return gui_app_clients[i].window_id;
        ++index;
    }
    return CHOS_WM_NO_WINDOW;
}

static int gui_taskbar_tooltip_target_at(int32_t x, int32_t y) {
    const int scale = (int)gui_scale_factor();
    const int button_height = 22 * scale;
    const int button_y = TASKBAR_Y + (TASKBAR_HEIGHT - button_height) / 2;
    const int clock_x = SCREEN_W - 82 * scale;
    int relative_x, slot_width, slot_offset, slot_x;
    size_t task;
    if (!gui_active || splash_active || start_menu_open || power_confirmation_action != 0 || left_button_down)
        return CHOS_WM_NO_WINDOW;
    if (y < button_y || y >= button_y + button_height || x < 70 * scale || x >= clock_x - 4 * scale)
        return CHOS_WM_NO_WINDOW;
    relative_x = x - 70 * scale;
    slot_width = TASKBAR_APP_BUTTON_STEP * scale;
    task = (size_t)(relative_x / slot_width);
    slot_offset = relative_x % slot_width;
    slot_x = 70 * scale + (int)task * slot_width;
    if (slot_offset < 0 || slot_offset >= TASKBAR_APP_BUTTON_WIDTH * scale ||
        slot_x + TASKBAR_APP_BUTTON_WIDTH * scale > clock_x - 4 * scale)
        return CHOS_WM_NO_WINDOW;
    return gui_taskbar_view_at(task);
}

static const char *gui_taskbar_full_title(int target) {
    gui_app_client_t *client;
    switch (target) {
        case VIEW_WELCOME: return "Welcome to CHOS";
        case VIEW_CONSOLE: return "Console";
        case VIEW_FILES: return "File Manager";
        case VIEW_ABOUT: return "About CHOS";
        case VIEW_DIAGNOSTICS: return "Diagnostics";
        case VIEW_SETTINGS: return "Settings";
        case VIEW_FILE_VIEWER: return "Text Viewer";
        case VIEW_TRASH: return "Trash";
        default: break;
    }
    client = gui_app_find_window(target);
    return client != NULL ? client->title : NULL;
}

static void gui_taskbar_tooltip_update_hover(void) {
    const int target = gui_taskbar_tooltip_target_at(mouse_x, mouse_y);
    if (target == taskbar_tooltip_window_id) return;
    if (taskbar_tooltip_fade_step != 0u) gui_mark_dirty();
    taskbar_tooltip_window_id = target;
    taskbar_tooltip_fade_step = 0u;
    taskbar_tooltip_started_tick = timer_is_ready() ? timer_ticks() : 0u;
    taskbar_tooltip_anchor_x = mouse_x;
    taskbar_tooltip_anchor_y = mouse_y;
}

static void gui_taskbar_tooltip_tick(void) {
    unsigned next_step;
    uint64_t now, elapsed, requested_step;
    gui_taskbar_tooltip_update_hover();
    if (taskbar_tooltip_window_id == CHOS_WM_NO_WINDOW || taskbar_tooltip_fade_step >= TASKBAR_TOOLTIP_FADE_STEPS)
        return;
    if (!timer_is_ready()) {
        taskbar_tooltip_fade_step = TASKBAR_TOOLTIP_FADE_STEPS;
        gui_mark_dirty();
        return;
    }
    now = timer_ticks();
    elapsed = now - taskbar_tooltip_started_tick;
    if (elapsed < TASKBAR_TOOLTIP_DELAY_TICKS) return;
    requested_step = 1u + (elapsed - TASKBAR_TOOLTIP_DELAY_TICKS) / TASKBAR_TOOLTIP_FADE_STEP_TICKS;
    next_step = requested_step > TASKBAR_TOOLTIP_FADE_STEPS ? TASKBAR_TOOLTIP_FADE_STEPS : (unsigned)requested_step;
    if (next_step > taskbar_tooltip_fade_step) {
        taskbar_tooltip_fade_step = next_step;
        gui_mark_dirty();
    }
}

static void terminal_clear_lines(void) {
    size_t row, col;
    for (row = 0u; row < TERMINAL_ROWS; ++row) {
        for (col = 0u; col <= TERMINAL_COLUMNS; ++col) terminal_lines[row][col] = '\0';
    }
    terminal_row = 0u;
    terminal_column = 0u;
    gui_mark_dirty();
}

static void terminal_newline(void) {
    size_t row, col;
    if (terminal_row + 1u < TERMINAL_ROWS) {
        ++terminal_row;
    } else {
        for (row = 1u; row < TERMINAL_ROWS; ++row) {
            for (col = 0u; col <= TERMINAL_COLUMNS; ++col)
                terminal_lines[row - 1u][col] = terminal_lines[row][col];
        }
        for (col = 0u; col <= TERMINAL_COLUMNS; ++col)
            terminal_lines[TERMINAL_ROWS - 1u][col] = '\0';
    }
    terminal_column = 0u;
    terminal_lines[terminal_row][0] = '\0';
    gui_mark_dirty();
}

static void terminal_putc(char ch) {
    if (ch == '\r') return;
    if (ch == '\n') { terminal_newline(); return; }
    if (ch == '\b') {
        if (terminal_column > 0u) {
            --terminal_column;
            terminal_lines[terminal_row][terminal_column] = '\0';
            gui_mark_dirty();
        }
        return;
    }
    if ((unsigned char)ch < 0x20u && ch != '\t') return;
    if (ch == '\t') {
        unsigned spaces = 4u - (unsigned)(terminal_column & 3u);
        while (spaces-- != 0u) terminal_putc(' ');
        return;
    }
    if (terminal_column >= TERMINAL_COLUMNS - 1u) terminal_newline();
    terminal_lines[terminal_row][terminal_column++] = ch;
    terminal_lines[terminal_row][terminal_column] = '\0';
    gui_mark_dirty();
}

static void terminal_puts(const char *value) {
    if (value == NULL) return;
    while (*value != '\0') terminal_putc(*value++);
}

static void terminal_put_uint(uint64_t value) {
    char digits[21];
    size_t count = 0u;
    if (value == 0u) { terminal_putc('0'); return; }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count != 0u) terminal_putc(digits[--count]);
}

static void terminal_put_int(int64_t value) {
    uint64_t magnitude;
    if (value < 0) {
        terminal_putc('-');
        magnitude = (uint64_t)(-(value + 1)) + 1u;
    } else magnitude = (uint64_t)value;
    terminal_put_uint(magnitude);
}

static void terminal_prompt(void) {
    if (terminal_column != 0u) terminal_newline();
    terminal_puts("CHOS> ");
    terminal_command[0] = '\0';
    terminal_history_position = (int)terminal_history_count;
}

static void terminal_initialize(void) {
    if (terminal_initialized) return;
    terminal_clear_lines();
    terminal_puts("CHOS Console 0.30.0\n");
    terminal_puts("Type 'help' to list commands. Serial recovery remains available.\n\n");
    terminal_prompt();
    terminal_initialized = 1;
}

int chos_gui_console_output_enabled(void) {
    return gui_active && window_manager_initialized &&
           (gui_console_pid != 0u || gui_window_open(VIEW_CONSOLE));
}

void chos_gui_console_write(const char *bytes, size_t length) {
    size_t i;
    if (bytes == NULL || !chos_gui_console_output_enabled()) return;
    if (gui_console_pid != 0u) {
        /* The active EL0 Console consumes this bounded stream through
         * CHOS_SYS_CONSOLE_READ. Drop the oldest byte under pressure so output
         * cannot block kernel/process progress. */
        for (i = 0u; i < length; ++i) {
            size_t tail;
            if (gui_console_output_count == GUI_CONSOLE_OUTPUT_RING_CAPACITY) {
                gui_console_output_head = (gui_console_output_head + 1u) % GUI_CONSOLE_OUTPUT_RING_CAPACITY;
                --gui_console_output_count;
            }
            tail = (gui_console_output_head + gui_console_output_count) % GUI_CONSOLE_OUTPUT_RING_CAPACITY;
            gui_console_output_ring[tail] = bytes[i];
            ++gui_console_output_count;
        }
        return;
    }
    if (!gui_window_open(VIEW_CONSOLE)) return;
    for (i = 0u; i < length; ++i) terminal_putc(bytes[i]);
}

int64_t chos_gui_console_session_read(uint32_t owner_pid, char *destination, size_t capacity) {
    size_t amount, i;
    if (owner_pid == 0u || owner_pid != gui_console_pid || destination == NULL || capacity == 0u)
        return -1;
    amount = capacity < gui_console_output_count ? capacity : gui_console_output_count;
    for (i = 0u; i < amount; ++i) {
        destination[i] = gui_console_output_ring[gui_console_output_head];
        gui_console_output_head = (gui_console_output_head + 1u) % GUI_CONSOLE_OUTPUT_RING_CAPACITY;
    }
    gui_console_output_count -= amount;
    return (int64_t)amount;
}

static int ascii_equal_fold(char left, char right) {
    if (left >= 'a' && left <= 'z') left = (char)(left - 'a' + 'A');
    if (right >= 'a' && right <= 'z') right = (char)(right - 'a' + 'A');
    return left == right;
}

static int string_ends_with_fold(const char *value, const char *suffix) {
    size_t n = 0u, m = 0u, i;
    if (value == NULL || suffix == NULL) return 0;
    while (value[n] != '\0') ++n;
    while (suffix[m] != '\0') ++m;
    if (m > n) return 0;
    for (i = 0u; i < m; ++i) if (!ascii_equal_fold(value[n - m + i], suffix[i])) return 0;
    return 1;
}

static int terminal_resolve_path(const char *argument, char out[CHOS_CHFS_PATH_MAX]) {
    char raw[CHOS_CHFS_PATH_MAX * 2u];
    size_t length = 0u, i = 0u, out_length = 1u;
    if (argument == NULL || argument[0] == '\0') return 0;
    if (argument[0] == '/') {
        while (argument[length] != '\0' && length + 1u < sizeof(raw)) {
            raw[length] = argument[length]; ++length;
        }
        if (argument[length] != '\0') return 0;
    } else {
        size_t cwd_len = 0u;
        while (terminal_cwd[cwd_len] != '\0') ++cwd_len;
        for (i = 0u; i < cwd_len; ++i) raw[length++] = terminal_cwd[i];
        if (cwd_len > 1u) raw[length++] = '/';
        while (argument[i] != '\0' && length + 1u < sizeof(raw)) raw[length++] = argument[i++];
        if (argument[i] != '\0') return 0;
    }
    raw[length] = '\0';
    out[0] = '/'; out[1] = '\0';
    i = 0u;
    while (i < length) {
        size_t segment_start, segment_len;
        while (i < length && raw[i] == '/') ++i;
        segment_start = i;
        while (i < length && raw[i] != '/') ++i;
        segment_len = i - segment_start;
        if (segment_len == 0u || (segment_len == 1u && raw[segment_start] == '.')) continue;
        if (segment_len == 2u && raw[segment_start] == '.' && raw[segment_start + 1u] == '.') {
            if (out_length > 1u) {
                --out_length;
                while (out_length > 1u && out[out_length - 1u] != '/') --out_length;
                if (out_length > 1u) --out_length;
                out[out_length] = '\0';
            }
            continue;
        }
        if (out_length > 1u) {
            if (out_length + 1u >= CHOS_CHFS_PATH_MAX) return 0;
            out[out_length++] = '/';
        }
        if (segment_len >= CHOS_CHFS_PATH_MAX - out_length) return 0;
        {
            size_t j;
            for (j = 0u; j < segment_len; ++j) out[out_length++] = raw[segment_start + j];
        }
        out[out_length] = '\0';
    }
    return 1;
}

static size_t storage_entry_count(void) {
    size_t count = 0u;
    chos_chfs_entry_t entry;
    if (!storage_mounted()) return 0u;
    while (count < CHOS_CHFS_MAX_FILES && storage_get_at(count, &entry)) ++count;
    return count;
}

static size_t file_manager_entry_count(void) {
    size_t count = storage_mounted() ? storage_entry_count() : 0u;
    size_t i;
    for (i = 0u; i < romfs_file_count(); ++i) {
        chos_romfs_file_t file;
        chos_chfs_entry_t existing;
        if (!romfs_get_at(i, &file) || file.path == NULL) continue;
        /* CHFS takes precedence for duplicate names, but embedded recovery and
         * demo applications remain visible when a persistent volume is mounted. */
        if (storage_mounted() && storage_lookup(file.path, &existing)) continue;
        ++count;
    }
    return count;
}

static int file_manager_get_entry(size_t index, chos_chfs_entry_t *out) {
    size_t i;
    if (out == NULL) return 0;
    if (storage_mounted()) {
        size_t persistent_count = storage_entry_count();
        if (index < persistent_count) return storage_get_at(index, out);
        index -= persistent_count;
    }
    for (i = 0u; i < romfs_file_count(); ++i) {
        chos_romfs_file_t file;
        chos_chfs_entry_t existing;
        size_t j;
        if (!romfs_get_at(i, &file) || file.path == NULL || file.size > UINT32_MAX) continue;
        if (storage_mounted() && storage_lookup(file.path, &existing)) continue;
        if (index != 0u) { --index; continue; }
        for (j = 0u; j + 1u < sizeof(out->path) && file.path[j] != '\0'; ++j)
            out->path[j] = file.path[j];
        out->path[j] = '\0';
        out->size_bytes = (uint32_t)file.size;
        out->first_sector = 0u; out->sector_count = 0u; out->crc32 = 0u;
        return 1;
    }
    return 0;
}

static int terminal_path_has_entries(const char *directory) {
    size_t i, count = storage_entry_count(), len = 0u;
    chos_chfs_entry_t entry;
    while (directory[len] != '\0') ++len;
    if (len == 1u && directory[0] == '/') return 1;
    for (i = 0u; i < count; ++i) {
        size_t j = 0u;
        if (!storage_get_at(i, &entry)) continue;
        while (j < len && entry.path[j] == directory[j]) ++j;
        if (j == len && (entry.path[j] == '/' || entry.path[j] == '\0')) return 1;
    }
    return 0;
}

static void terminal_command_dir(void) {
    size_t i, count = storage_entry_count(), shown = 0u, cwd_len = 0u;
    chos_chfs_entry_t entry;
    while (terminal_cwd[cwd_len] != '\0') ++cwd_len;
    terminal_puts("Directory: "); terminal_puts(terminal_cwd); terminal_puts("\n");
    if (!storage_mounted()) {
        terminal_puts("Persistent storage is unavailable. Embedded programs remain launchable.\n");
        return;
    }
    for (i = 0u; i < count; ++i) {
        size_t pos;
        const char *display;
        if (!storage_get_at(i, &entry)) continue;
        if (cwd_len > 1u) {
            size_t j = 0u;
            while (j < cwd_len && entry.path[j] == terminal_cwd[j]) ++j;
            if (j != cwd_len || (entry.path[j] != '/' && entry.path[j] != '\0')) continue;
        }
        display = entry.path;
        if (cwd_len > 1u) display = entry.path + cwd_len + (entry.path[cwd_len] == '/' ? 1u : 0u);
        else if (display[0] == '/') ++display;
        terminal_puts(display);
        /* Align sizes without depending on libc formatting. */
        pos = 0u; while (display[pos] != '\0') ++pos;
        while (pos++ < 34u) terminal_putc(' ');
        terminal_put_uint(entry.size_bytes); terminal_puts(" bytes\n");
        ++shown;
    }
    if (shown == 0u) terminal_puts("No entries.\n");
    terminal_put_uint(shown); terminal_puts(" file(s)\n");
}

static void terminal_command_type(const char *argument) {
    char path[CHOS_CHFS_PATH_MAX];
    chos_chfs_entry_t entry;
    size_t bytes = 0u, i;
    chos_romfs_file_t romfs_file;
    static uint8_t file_buffer[TERMINAL_FILE_CAPACITY];
    if (argument == NULL || argument[0] == '\0' || !terminal_resolve_path(argument, path)) {
        terminal_puts("Usage: type <path>\n"); return;
    }
    if (storage_lookup(path, &entry)) {
        if (entry.size_bytes > sizeof(file_buffer)) {
            terminal_puts("File exceeds the terminal read limit (1024 bytes).\n"); return;
        }
        if (!storage_read_file(path, file_buffer, sizeof(file_buffer), &bytes)) {
            terminal_puts("Unable to read file from CHFS.\n"); return;
        }
    } else if (romfs_lookup(path, &romfs_file)) {
        bytes = romfs_file.size < sizeof(file_buffer) ? romfs_file.size : sizeof(file_buffer);
        for (i = 0u; i < bytes; ++i) file_buffer[i] = romfs_file.data[i];
        if (romfs_file.size > sizeof(file_buffer)) terminal_puts("[Showing first 1024 bytes]\n");
    } else {
        terminal_puts("File not found: "); terminal_puts(path); terminal_putc('\n'); return;
    }
    for (i = 0u; i < bytes; ++i) {
        uint8_t ch = file_buffer[i];
        if (ch == '\r') continue;
        if (ch == '\n' || ch == '\t' || ch >= 0x20u) terminal_putc((char)ch);
        else terminal_putc('.');
    }
    if (bytes != 0u && file_buffer[bytes - 1u] != '\n') terminal_putc('\n');
}

static int terminal_parse_pid(const char *argument, uint32_t *pid_out) {
    uint64_t value = 0u; size_t i = 0u;
    if (argument == NULL || pid_out == NULL || argument[0] == '\0') return 0;
    while (argument[i] >= '0' && argument[i] <= '9') {
        value = value * 10u + (uint64_t)(argument[i] - '0');
        if (value > UINT32_MAX) return 0;
        ++i;
    }
    while (argument[i] == ' ' || argument[i] == '\t') ++i;
    if (i == 0u || argument[i] != '\0' || value == 0u) return 0;
    *pid_out = (uint32_t)value; return 1;
}
static void terminal_run_path(const char *argument) {
    char path[CHOS_CHFS_PATH_MAX]; uint32_t pid = 0u; chos_process_error_t error;
    if (argument == NULL || argument[0] == '\0' || !terminal_resolve_path(argument, path)) { terminal_puts("Usage: run <program.chx>\n"); return; }
    terminal_puts("Launching "); terminal_puts(path); terminal_puts("\n");
    error = process_spawn_path(path, &pid);
    if (error != PROCESS_OK) {
        terminal_puts("Launch failed: "); terminal_puts(process_error_string(error)); terminal_putc('\n');
        if (error == PROCESS_ERR_CONTEXT) terminal_puts("The scheduled runtime is required for asynchronous applications.\n");
        return;
    }
    terminal_puts("[queued process "); terminal_put_uint(pid); terminal_puts("]\n");
}
static void terminal_list_processes(void) {
    size_t i; int shown = 0; chos_process_info_t info;
    terminal_puts("PID   STATE       EXIT   NAME\n");
    for (i = 0u; i < CHOS_PROCESS_MAX; ++i) {
        if (!process_get_info_at(i, &info)) continue;
        terminal_put_uint(info.pid); terminal_puts("   "); terminal_puts(process_state_name(info.state)); terminal_puts("   ");
        terminal_put_int(info.exit_status); terminal_puts("   "); terminal_puts(info.name); terminal_putc('\n'); shown = 1;
    }
    if (!shown) terminal_puts("No process records.\n");
}
static void terminal_process_status(const char *argument) {
    uint32_t pid; chos_process_state_t state; int64_t status = 0; int query;
    if (!terminal_parse_pid(argument, &pid)) { terminal_puts("Usage: status <pid>\n"); return; }
    query = process_query_pid(pid, &state, &status);
    if (query < 0) { terminal_puts("Unknown process ID.\n"); return; }
    terminal_puts("PID "); terminal_put_uint(pid); terminal_puts(": "); terminal_puts(process_state_name(state));
    if (query == 0) { terminal_puts(", exit status "); terminal_put_int(status); }
    terminal_putc('\n');
}

static void terminal_execute(char *input) {
    char command_copy[TERMINAL_COMMAND_CAPACITY];
    char *command, *argument, *split;
    size_t i = 0u;
    while (input[i] == ' ' || input[i] == '\t') ++i;
    if (input[i] == '\0') return;
    for (i = 0u; i + 1u < sizeof(command_copy) && input[i] != '\0'; ++i) command_copy[i] = input[i];
    command_copy[i] = '\0';
    command = command_copy;
    while (*command == ' ' || *command == '\t') ++command;
    split = command;
    while (*split != '\0' && *split != ' ' && *split != '\t') ++split;
    if (*split != '\0') { *split++ = '\0'; }
    argument = split;
    while (*argument == ' ' || *argument == '\t') ++argument;
    if (gui_strings_equal(command, "help")) {
        terminal_puts("Commands:\n  help       Show this help\n  clear      Clear the terminal\n  pwd        Show current directory\n  dir / ls   List CHFS files\n  cd PATH    Change directory prefix\n  type PATH  Display a text file\n  run PATH   Launch a CHCX program asynchronously\n  tasklist   List process states\n  status PID Show a process status\n  sysinfo    Show system status\n  version    Show CHOS version\n  echo TEXT  Print text\n  exit       Close this Console window\n");
    } else if (gui_strings_equal(command, "clear") || gui_strings_equal(command, "cls")) {
        terminal_clear_lines();
    } else if (gui_strings_equal(command, "pwd")) {
        terminal_puts(terminal_cwd); terminal_putc('\n');
    } else if (gui_strings_equal(command, "dir") || gui_strings_equal(command, "ls")) {
        terminal_command_dir();
    } else if (gui_strings_equal(command, "cd")) {
        char path[CHOS_CHFS_PATH_MAX];
        if (argument[0] == '\0') terminal_puts("Usage: cd <path>\n");
        else if (!terminal_resolve_path(argument, path)) terminal_puts("Path is too long or invalid.\n");
        else if (!terminal_path_has_entries(path)) { terminal_puts("Directory not found: "); terminal_puts(path); terminal_putc('\n'); }
        else {
            size_t j = 0u; while (path[j] != '\0') { terminal_cwd[j] = path[j]; ++j; }
            terminal_cwd[j] = '\0'; terminal_puts(terminal_cwd); terminal_putc('\n');
        }
    } else if (gui_strings_equal(command, "type") || gui_strings_equal(command, "cat")) {
        terminal_command_type(argument);
    } else if (gui_strings_equal(command, "run") || gui_strings_equal(command, "spawn")) {
        terminal_run_path(argument);
    } else if (gui_strings_equal(command, "tasklist") || gui_strings_equal(command, "ps")) {
        terminal_list_processes();
    } else if (gui_strings_equal(command, "status")) {
        terminal_process_status(argument);
    } else if (gui_strings_equal(command, "sysinfo")) {
        terminal_puts("CHOS 0.30.0\nArchitecture: AArch64\nKernel mode: EL1\nDisplay: ");
        if (virtio_gpu_ready()) {
            terminal_puts("VirtIO-GPU ");
            terminal_puts(display_mode_full_label(virtio_gpu_current_mode_index()));
            terminal_putc('\n');
        } else terminal_puts("unavailable\n");
        terminal_puts("Input devices: "); terminal_put_uint(chos_input_device_count()); terminal_putc('\n');
        terminal_puts("Persistent storage: "); terminal_puts(storage_mounted() ? "CHFS mounted\n" : "unavailable (ROMFS fallback)\n");
    } else if (gui_strings_equal(command, "version") || gui_strings_equal(command, "ver")) {
        terminal_puts("CHOS ARM64 0.30.0\n");
    } else if (gui_strings_equal(command, "echo")) {
        terminal_puts(argument); terminal_putc('\n');
    } else if (gui_strings_equal(command, "exit")) {
        gui_close_window(VIEW_CONSOLE);
        return;
    } else {
        terminal_puts("Unknown command: "); terminal_puts(command); terminal_puts("\nType 'help' for available commands.\n");
    }
}

static void terminal_history_add(const char *command) {
    size_t i, len = 0u;
    if (command == NULL || command[0] == '\0') return;
    if (terminal_history_count == TERMINAL_HISTORY_CAPACITY) {
        for (i = 1u; i < TERMINAL_HISTORY_CAPACITY; ++i) {
            size_t j = 0u;
            while ((terminal_history[i][j] != '\0' || terminal_history[i - 1u][j] != '\0') && j + 1u < TERMINAL_COMMAND_CAPACITY) {
                terminal_history[i - 1u][j] = terminal_history[i][j]; ++j;
            }
            terminal_history[i - 1u][j] = '\0';
        }
        --terminal_history_count;
    }
    while (command[len] != '\0' && len + 1u < TERMINAL_COMMAND_CAPACITY) ++len;
    for (i = 0u; i < len; ++i) terminal_history[terminal_history_count][i] = command[i];
    terminal_history[terminal_history_count][len] = '\0';
    ++terminal_history_count;
    terminal_history_position = (int)terminal_history_count;
}

static void terminal_set_current_input(const char *value) {
    size_t i, length = 0u;
    for (i = 0u; i <= TERMINAL_COLUMNS; ++i) terminal_lines[terminal_row][i] = '\0';
    terminal_column = 0u;
    terminal_puts("CHOS> ");
    terminal_command[0] = '\0';
    while (*value != '\0' && length < TERMINAL_COLUMNS - 8u) {
        char ch = *value++;
        terminal_putc(ch);
        terminal_command[length++] = ch;
    }
    terminal_command[length] = '\0';
    gui_mark_dirty();
}

static void terminal_submit(void) {
    char command[TERMINAL_COMMAND_CAPACITY];
    size_t i;
    for (i = 0u; i < sizeof(command); ++i) command[i] = terminal_command[i];
    i = 0u;
    while (command[i] != '\0') ++i;
    while (i > 0u && (command[i - 1u] == ' ' || command[i - 1u] == '\t')) command[--i] = '\0';
    terminal_history_add(command);
    terminal_newline();
    terminal_command[0] = '\0';
    terminal_execute(command);
    if (view == VIEW_CONSOLE) terminal_prompt();
    gui_mark_dirty();
}

static char terminal_key_to_ascii(uint16_t keycode) {
    static const char plain_digits[] = "1234567890";
    static const char shifted_digits[] = "!@#$%^&*()";
    char ch = '\0';
    if (unicode_key_override != '\0') return unicode_key_override;
    if (keycode >= 2u && keycode <= 11u) return shift_down ? shifted_digits[keycode - 2u] : plain_digits[keycode - 2u];
    if (keycode >= 16u && keycode <= 25u) {
        static const char keys[] = "qwertyuiop"; ch = keys[keycode - 16u];
    } else if (keycode >= 30u && keycode <= 38u) {
        static const char keys[] = "asdfghjkl"; ch = keys[keycode - 30u];
    } else if (keycode >= 44u && keycode <= 50u) {
        static const char keys[] = "zxcvbnm"; ch = keys[keycode - 44u];
    } else if (keycode == 57u) return ' ';
    else if (keycode == 12u) return shift_down ? '_' : '-';
    else if (keycode == 13u) return shift_down ? '+' : '=';
    else if (keycode == 26u) return shift_down ? '{' : '[';
    else if (keycode == 27u) return shift_down ? '}' : ']';
    else if (keycode == 39u) return shift_down ? ':' : ';';
    else if (keycode == 40u) return shift_down ? '"' : '\'';
    else if (keycode == 41u) return shift_down ? '~' : '`';
    else if (keycode == 43u || keycode == 86u) return shift_down ? '|' : '\\';
    else if (keycode == 51u) return shift_down ? '<' : ',';
    else if (keycode == 52u) return shift_down ? '>' : '.';
    else if (keycode == 53u) return shift_down ? '?' : '/';
    if ((shift_down != 0) != (caps_lock_down != 0) && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    return ch;
}

static int terminal_handle_key(uint16_t keycode, int pressed) {
    if (keycode == 42u || keycode == 54u) { shift_down = pressed; return 1; }
    if (keycode == 29u || keycode == 97u) { control_down = pressed; return 1; }
    if (!pressed) return 1;
    if (keycode == 1u) { open_view(VIEW_WELCOME); return 1; }
    if (keycode == 28u) { terminal_submit(); return 1; }
    if (keycode == 14u) {
        if (terminal_command[0] != '\0' && terminal_column > 6u) {
            size_t len = 0u; while (terminal_command[len] != '\0') ++len;
            if (len != 0u) terminal_command[len - 1u] = '\0';
            terminal_putc('\b');
        }
        return 1;
    }
    if (control_down && (keycode == 38u || keycode == 46u)) {
        if (keycode == 38u) {
            terminal_clear_lines();
            terminal_prompt();
        } else {
            terminal_command[0] = '\0';
            terminal_set_current_input("");
        }
        return 1;
    }
    if (keycode == 103u) {
        if (terminal_history_position > 0) {
            --terminal_history_position;
            terminal_set_current_input(terminal_history[terminal_history_position]);
        }
        return 1;
    }
    if (keycode == 108u) {
        if (terminal_history_position < (int)terminal_history_count) ++terminal_history_position;
        terminal_set_current_input(terminal_history_position == (int)terminal_history_count ? "" : terminal_history[terminal_history_position]);
        return 1;
    }
    {
        char ch = terminal_key_to_ascii(keycode);
        size_t length = 0u;
        while (terminal_command[length] != '\0') ++length;
        if (ch >= 0x20 && ch <= 0x7e && length + 1u < sizeof(terminal_command) && length < TERMINAL_COLUMNS - 8u) {
            terminal_command[length] = ch;
            terminal_command[length + 1u] = '\0';
            terminal_putc(ch);
        }
    }
    return 1;
}

static int file_is_program(const char *path) { return string_ends_with_fold(path, ".CHX"); }

static void viewer_set_message(const char *message, int is_error) {
    size_t i = 0u;
    while (message[i] != '\0' && i + 1u < sizeof(viewer_content)) {
        viewer_content[i] = message[i];
        ++i;
    }
    viewer_content[i] = '\0';
    viewer_content_size = i;
    viewer_load_error = is_error;
    viewer_empty_file = 0;
    viewer_scroll_offset = 0u;
}

static int file_viewer_load(const char *path) {
    chos_chfs_entry_t entry;
    chos_romfs_file_t romfs_file;
    size_t bytes = 0u, i;
    viewer_content[0] = '\0';
    viewer_content_size = 0u;
    viewer_load_error = 0;
    viewer_empty_file = 0;
    viewer_scroll_offset = 0u;
    viewer_editable = 0;
    viewer_unsaved = 0;
    viewer_save_error = 0;
    for (i = 0u; i + 1u < sizeof(viewer_path) && path[i] != '\0'; ++i) viewer_path[i] = path[i];
    viewer_path[i] = '\0';

    if (storage_lookup(path, &entry)) {
        if (entry.size_bytes >= sizeof(viewer_content)) {
            viewer_set_message("This file is larger than the built-in viewer limit (1023 bytes).", 1);
            return 0;
        }
        if (!storage_read_file(path, viewer_content, sizeof(viewer_content) - 1u, &bytes)) {
            viewer_set_message("Read failed. Check the volume and retry opening this file.", 1);
            return 0;
        }
        if (bytes > sizeof(viewer_content) - 1u) {
            viewer_set_message("The filesystem returned an invalid length for this file.", 1);
            return 0;
        }
    } else if (romfs_lookup(path, &romfs_file)) {
        if (romfs_file.size >= sizeof(viewer_content)) {
            viewer_set_message("This embedded file is larger than the viewer limit (1023 bytes).", 1);
            return 0;
        }
        if (romfs_file.size != 0u && romfs_file.data == NULL) {
            viewer_set_message("Embedded file data is unavailable.", 1);
            return 0;
        }
        bytes = romfs_file.size;
        for (i = 0u; i < bytes; ++i) viewer_content[i] = (char)romfs_file.data[i];
    } else {
        viewer_set_message("File not found in CHFS or embedded ROMFS.", 1);
        return 0;
    }

    viewer_content[bytes] = '\0';
    viewer_content_size = bytes;
    viewer_empty_file = bytes == 0u;
    return 1;
}

static size_t viewer_next_wrapped_line(size_t *offset, char *out, size_t capacity) {
    size_t pos, count = 0u, last_space_out = (size_t)-1, last_space_after = 0u;
    unsigned line_width = 0u;
    if (offset == NULL || out == NULL || capacity < 2u || *offset >= viewer_content_size) {
        if (out != NULL && capacity > 0u) out[0] = '\0';
        return 0u;
    }
    pos = *offset;
    while (pos < viewer_content_size) {
        char ch = viewer_content[pos];
        unsigned char_width;
        if (ch == '\r') { ++pos; continue; }
        if (ch == '\n') {
            ++pos;
            out[count] = '\0';
            *offset = pos;
            return 1u;
        }
        if (ch == '\t') ch = ' ';
        char_width = chos_gfx_ui_char_width(ch, 0);
        if (line_width + char_width > VIEWER_TEXT_WIDTH && count > 0u) {
            if (last_space_out != (size_t)-1 && last_space_out > 0u) {
                count = last_space_out;
                pos = last_space_after;
                while (pos < viewer_content_size && viewer_content[pos] == ' ') ++pos;
            }
            out[count] = '\0';
            *offset = pos;
            return 1u;
        }
        if (count + 1u >= capacity) {
            out[count] = '\0';
            *offset = pos;
            return 1u;
        }
        if (ch == ' ') { last_space_out = count; last_space_after = pos + 1u; }
        out[count++] = ch;
        line_width += char_width;
        ++pos;
    }
    out[count] = '\0';
    *offset = pos;
    return 1u;
}

static size_t viewer_visual_line_count(void) {
    size_t offset = 0u, count = 0u;
    char line[VIEWER_LINE_CAPACITY];
    while (offset < viewer_content_size && count < sizeof(viewer_content) + 1u) {
        if (viewer_next_wrapped_line(&offset, line, sizeof(line)) == 0u) break;
        ++count;
    }
    return count;
}

static size_t viewer_max_scroll(void) {
    size_t lines = viewer_visual_line_count();
    return lines > VIEWER_VISIBLE_LINES ? lines - VIEWER_VISIBLE_LINES : 0u;
}

static void viewer_scroll_by(int direction, size_t amount) {
    size_t max_scroll = viewer_max_scroll();
    if (direction < 0) {
        if (viewer_scroll_offset > amount) viewer_scroll_offset -= amount;
        else viewer_scroll_offset = 0u;
    } else {
        if (viewer_scroll_offset < max_scroll) {
            size_t remaining = max_scroll - viewer_scroll_offset;
            viewer_scroll_offset += amount < remaining ? amount : remaining;
        }
    }
    gui_mark_dirty();
}

static void file_manager_open_selected(int run_program) {
    chos_chfs_entry_t entry;
    if (!file_manager_get_entry(file_selected_index, &entry)) return;
    if (run_program || file_is_program(entry.path)) {
        if (!file_is_program(entry.path)) {
            (void)gui_open_window(VIEW_CONSOLE);
            terminal_puts("Run requires a .CHX application: ");
            terminal_puts(entry.path);
            terminal_putc('\n');
            terminal_prompt();
            gui_mark_dirty();
            return;
        }
        if (!gui_open_window(VIEW_CONSOLE)) return;
        terminal_puts("run "); terminal_puts(entry.path); terminal_newline();
        terminal_run_path(entry.path);
        terminal_prompt();
        gui_mark_dirty();
    } else {
        /* Register the viewer with the window manager before activating it.
         * Merely assigning `view` updates the reported active view, but the
         * compositor only renders registered windows; that made Open appear
         * to do nothing on the actual desktop even though host tests observed
         * the VIEW_FILE_VIEWER enum. */
        (void)file_viewer_load(entry.path);
        viewer_editable = string_ends_with_fold(entry.path, ".TXT") && storage_lookup(entry.path, &(chos_chfs_entry_t){0});
        viewer_unsaved = 0; viewer_save_error = 0;
        if (!gui_open_window(VIEW_FILE_VIEWER)) {
            /* Keep the File Manager active if no window slot is available. */
            (void)gui_open_window(VIEW_FILES);
            return;
        }
    }
}


static void draw_welcome_content(void) {
    rect(199, 137, 562, 72, C_PANEL);
    draw_bevel(199, 137, 562, 72, C_WHITE, C_PANEL_DARK, 1);
    rect(207, 145, 56, 56, C_PANEL);
    draw_bevel(207, 145, 56, 56, C_WHITE, C_BLACK, 1);
    gui_draw_named_icon("computer", 215, 151, 40, 42);
    text_bold(277, 149, "Welcome to CHOS", C_BLUE_DARK);
    text(277, 169, "A small, modular ARM64 operating environment.", C_TEXT);
    text(204, 225, "Choose a tool to get started:", C_TEXT);

    /* Four soft beveled launch tiles. Mouse clicks map to these rectangles. */
    draw_button(205, 250, 260, 77, NULL, 0);
    draw_icon_art(217, 261, 0);
    text_bold(265, 263, "Console", C_BLUE_DARK);
    text(265, 281, "Interactive terminal", C_MUTED);
    text(265, 297, "Commands + app output", C_MUTED);

    draw_button(480, 250, 270, 77, NULL, 0);
    draw_icon_art(492, 261, 1);
    text_bold(540, 263, "File Manager", C_BLUE_DARK);
    text(540, 281, "Browse persistent files", C_MUTED);
    text(540, 297, "CHFS / ROMFS", C_MUTED);

    draw_button(205, 339, 260, 77, NULL, 0);
    draw_icon_art(217, 350, 3);
    text_bold(265, 352, "Diagnostics", C_BLUE_DARK);
    text(265, 370, "Inspect system services", C_MUTED);
    text(265, 386, "Memory, input, display", C_MUTED);

    draw_button(480, 339, 270, 77, NULL, 0);
    draw_icon_art(492, 350, 4);
    text_bold(540, 352, "Settings", C_BLUE_DARK);
    text(540, 370, "Desktop appearance", C_MUTED);
    text(540, 386, "Resolution:", C_MUTED);
    text(607, 386, display_mode_label(virtio_gpu_current_mode_index()), C_MUTED);

    draw_hline(204, 438, 545, 0xffc1cbd5u);
    text(204, 453, "CHOS 0.30.0", C_BLUE_DARK);
    if (platform_pci_status_valid) {
        text(204, 472, "PCI functions detected:", C_MUTED);
        draw_number(354, 472, platform_pci_device_count, C_MUTED);
        text(204, 491, "USB xHCI controller:", C_MUTED);
        text(339, 491, platform_xhci_id, platform_xhci_count != 0u ? C_GREEN : C_ORANGE);
        text(204, 510, "xHCI BAR0 / BAR1:", C_MUTED);
        text(326, 510, platform_xhci_bars, platform_xhci_count != 0u ? C_GREEN : C_ORANGE);
    } else {
        text(204, 472, "Platform input:", C_MUTED);
        text(302, 472, "QEMU VirtIO", C_GREEN);
        text(204, 491, "Input backend:", C_MUTED);
        text(302, 491, "VirtIO event queues", C_GREEN);
    }
    draw_status_line("Welcome", "Ready");
}

static void draw_files_content(void) {
    size_t count = file_manager_entry_count();
    size_t visible, row_index;
    draw_window_shell("File Manager - CHOS", 180, 70, 595, 465);
    rect(188, 101, 579, 29, C_PANEL);
    text(198, 110, "Location:", C_TEXT);
    rect(260, 105, 495, 20, C_WHITE);
    outline(260, 105, 495, 20, C_PANEL_DARK);
    text(268, 109, storage_mounted() ? "/" : "Embedded ROMFS", C_TEXT);
    rect(188, 134, 579, 24, C_PANEL);
    draw_bevel(188, 134, 579, 24, C_WHITE, C_PANEL_DARK, 1);
    text(199, 140, "Name / path", C_TEXT);
    text(524, 140, "Bytes", C_TEXT);
    text(626, 140, "Type", C_TEXT);
    draw_hline(188, 158, 579, C_PANEL_DARK);
    if (file_selected_index >= count && count != 0u) file_selected_index = count - 1u;
    if (file_scroll_offset > file_selected_index) file_scroll_offset = file_selected_index;
    if (count > FILE_VIEW_ROWS && file_scroll_offset + FILE_VIEW_ROWS > count) file_scroll_offset = count - FILE_VIEW_ROWS;
    visible = count - file_scroll_offset;
    if (visible > FILE_VIEW_ROWS) visible = FILE_VIEW_ROWS;
    if (count == 0u) {
        text(201, 172, storage_mounted() ? "This volume is empty." : "No files are available; check storage diagnostics.", C_MUTED);
    } else {
        for (row_index = 0u; row_index < visible; ++row_index) {
            chos_chfs_entry_t entry;
            size_t i, path_len = 0u;
            int y = 164 + (int)row_index * 21;
            size_t absolute_index = file_scroll_offset + row_index;
            if (!file_manager_get_entry(absolute_index, &entry)) continue;
            if (absolute_index == file_selected_index) {
                rect(190, y - 2, 575, 19, C_BLUE_DARK);
                draw_hline(190, y - 2, 575, C_BLUE_DARK);
            } else if ((row_index & 1u) == 0u) rect(190, y - 2, 575, 19, C_WHITE);
            while (entry.path[path_len] != '\0') ++path_len;
            if (path_len > 39u) { for (i = 0u; i < 36u; ++i) viewer_path[i] = entry.path[i]; viewer_path[36] = '.'; viewer_path[37] = '.'; viewer_path[38] = '.'; viewer_path[39] = '\0'; text(201, y, viewer_path, absolute_index == file_selected_index ? C_WHITE : C_TEXT); }
            else text(201, y, entry.path, absolute_index == file_selected_index ? C_WHITE : C_TEXT);
            draw_number(524, y, entry.size_bytes, absolute_index == file_selected_index ? C_WHITE : C_MUTED);
            text(626, y, file_is_program(entry.path) ? "Program" : "File", absolute_index == file_selected_index ? C_WHITE : C_MUTED);
        }
    }
    draw_button(198, 410, 78, 28, "Open", 0);
    draw_button(282, 410, 78, 28, "Run", 0);
    draw_button(490, 410, 74, 28, "Prev", 0);
    draw_button(570, 410, 74, 28, "Next", 0);
    text(654, 419, count != 0u ? "Select file" : "Empty", C_MUTED);
    draw_hline(188, 493, 579, C_PANEL_DARK);
    text(199, 501, "Open text files; run .CHX programs in Console", C_MUTED);
    draw_status_line(storage_mounted() ? "File Manager" : "Embedded Programs", storage_mounted() ? "CHFS mounted" : "ROMFS fallback");
}

static void draw_console_content(void) {
    size_t row;
    draw_window_shell("Console - CHOS", 180, 70, 595, 465);
    rect(194, 108, 567, 379, C_TERMINAL_BG);
    outline(194, 108, 567, 379, 0xff6c7f90u);
    for (row = 0u; row < TERMINAL_ROWS; ++row) {
        uint32_t color = (row == terminal_row) ? C_TERMINAL_FG : 0xffd4e0eeu;
        if (terminal_lines[row][0] != '\0') mono_text(202, 114 + (int)row * 12, terminal_lines[row], color);
    }
    draw_status_line("Console", "help  |  dir  |  type PATH  |  run PATH");
}

static size_t viewer_append_decimal(char *out, size_t pos, size_t capacity, size_t value) {
    char reversed[12];
    size_t count = 0u;
    do {
        reversed[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u && count < sizeof(reversed));
    while (count > 0u && pos + 1u < capacity) out[pos++] = reversed[--count];
    return pos;
}

static void draw_file_viewer_content(void) {
    size_t offset = 0u, line = 0u, skipped = 0u;
    char output[VIEWER_LINE_CAPACITY];
    char status_right[32];
    size_t total_lines = viewer_visual_line_count();
    draw_window_shell(viewer_editable ? "Text Editor - CHOS" : "Text Viewer - CHOS", 180, 70, 595, 465);
    rect(194, 108, 567, 355, C_WHITE);
    outline(194, 108, 567, 355, C_PANEL_DARK);
    text_bold(204, 115, viewer_path, C_BLUE_DARK);
    draw_hline(202, 135, 550, C_PANEL_DARK);
    if (viewer_empty_file && !viewer_editable) {
        text(204, 151, "This file is empty (0 bytes).", C_MUTED);
    } else {
        while (skipped < viewer_scroll_offset && offset < viewer_content_size) {
            if (viewer_next_wrapped_line(&offset, output, sizeof(output)) == 0u) break;
            ++skipped;
        }
        offset = (viewer_scroll_offset == 0u) ? 0u : offset;
        while (offset < viewer_content_size && line < VIEWER_VISIBLE_LINES) {
            if (viewer_next_wrapped_line(&offset, output, sizeof(output)) == 0u) break;
            if (output[0] != '\0') text(204, 145 + (int)line * (int)CHOS_GFX_UI_LINE_HEIGHT,
                                       output, viewer_load_error ? C_RED : C_TEXT);
            ++line;
        }
        if (viewer_content_size == 0u && !viewer_load_error)
            text(204, 151, viewer_editable ? "Start typing; Ctrl+S saves your document." : "No text to display.", C_MUTED);
    }
    draw_button(204, 475, 174, 27, "Back to File Manager", 0);
    if (viewer_editable) draw_button(390, 475, 80, 27, "Save", 0);
    if (viewer_load_error) gui_copy_string(status_right, sizeof(status_right), "Read error");
    else if (viewer_save_error) gui_copy_string(status_right, sizeof(status_right), "Save failed");
    else if (viewer_unsaved) gui_copy_string(status_right, sizeof(status_right), "Modified");
    else if (viewer_editable) gui_copy_string(status_right, sizeof(status_right), "Ctrl+S saves");
    else if (viewer_empty_file) gui_copy_string(status_right, sizeof(status_right), "Empty file");
    else if (total_lines > VIEWER_VISIBLE_LINES) {
        size_t p = 0u;
        const char prefix[] = "Line ";
        size_t i;
        for (i = 0u; prefix[i] != '\0' && p + 1u < sizeof(status_right); ++i) status_right[p++] = prefix[i];
        p = viewer_append_decimal(status_right, p, sizeof(status_right), viewer_scroll_offset + 1u);
        if (p + 1u < sizeof(status_right)) status_right[p++] = '/';
        p = viewer_append_decimal(status_right, p, sizeof(status_right), total_lines);
        status_right[p] = '\0';
    } else gui_copy_string(status_right, sizeof(status_right), "Read-only");
    draw_status_line(viewer_load_error ? "Text Viewer - read error" : (viewer_editable ? "Text Editor" : "Text Viewer"), status_right);
}

static void gui_trash_draw_content(void) {
    size_t count = gui_trash_count(), i, row = 0u;
    chos_chfs_entry_t entry;
    draw_window_shell("Trash - CHOS", 180, 70, 595, 465);
    text_bold(204, 111, "Trash", C_BLUE_DARK);
    text(204, 133, "Items can be restored to the Desktop or deleted permanently.", C_TEXT);
    rect(198, 156, 560, 244, C_WHITE);
    outline(198, 156, 560, 244, C_PANEL_DARK);
    if (trash_scroll_offset > count) trash_scroll_offset = count;
    if (trash_scroll_offset > 0u && trash_selected_index < trash_scroll_offset) trash_scroll_offset = trash_selected_index;
    for (i = trash_scroll_offset; i < count && row < 10u; ++i) {
        char label[CHOS_CHFS_PATH_MAX + 8u];
        if (!gui_trash_entry_at(i, &entry)) break;
        if (i == trash_selected_index) {
            rect(201, 160 + (int)row * 23, 554, 22, C_SELECTION);
            text_bold(207, 164 + (int)row * 23, storage_is_directory(entry.path) ? "[DIR]" : "[FILE]",
                      C_WHITE);
            text(252, 164 + (int)row * 23, gui_path_leaf(entry.path), C_WHITE);
        } else {
            text(207, 164 + (int)row * 23, storage_is_directory(entry.path) ? "[DIR]" : "[FILE]", C_TEXT);
            gui_copy_string(label, sizeof(label), gui_path_leaf(entry.path));
            if (chos_gfx_ui_text_width(label, 0) > 440) gui_desktop_label_fit(label, label);
            text(252, 164 + (int)row * 23, label, C_TEXT);
        }
        ++row;
    }
    if (count == 0u) text(210, 173, "Trash is empty.", C_MUTED);
    if (count > 10u) {
        text(207, 385, "Use Up/Down or the wheel to see more items.", C_MUTED);
        text(680, 385, trash_scroll_offset == 0u ? "v" : "^", C_MUTED);
    }
    draw_button(204, 475, 82, 27, "Restore", 0);
    draw_button(291, 475, 91, 27, "Delete", 0);
    draw_button(387, 475, 100, 27, "Empty Trash", 0);
    draw_button(492, 475, 94, 27, "Close", 0);
    draw_status_line("Trash", count == 0u ? "Empty" : "CHFS storage");
}

static void draw_about_content(void) {
    draw_window_shell("About CHOS", 180, 70, 595, 465);
    rect(200, 112, 555, 95, C_BLUE_DARK);
    draw_bevel(200, 112, 555, 95, C_WHITE, C_BLACK, 1);
    gui_draw_named_icon("computer", 214, 136, 48, 43);
    text_bold(278, 131, "CHOS", C_WHITE);
    text(278, 154, "ARM64 Operating Environment", C_WHITE);
    text(278, 176, "Development build 0.30.0", C_WHITE);
    text(204, 230, "Design goals", C_BLUE_DARK);
    text(204, 253, "Lightweight core   |   Modular drivers", C_TEXT);
    text(204, 276, "C and AArch64 assembly", C_TEXT);
    text(204, 299, "EL0 applications with CHCX v2 images", C_TEXT);
    text(204, 322, "Persistent CHFS volume and embedded ROMFS", C_TEXT);
    text(204, 345, "GICv2 and ARM Generic Timer on QEMU virt", C_TEXT);
    text(204, 391, "Console commands and CHCX app output are available", C_MUTED);
    text(204, 409, "in the desktop; a user-space window manager is next.", C_MUTED);
    draw_status_line("About CHOS", "AArch64 / EL1");
}

static void draw_diagnostics_content(void) {
    draw_window_shell("CHOS Diagnostics", 180, 70, 595, 465);
    text(201, 110, "SERVICE STATUS", C_BLUE_DARK);
    draw_hline(200, 129, 551, C_PANEL_DARK);
    text(203, 147, "VirtIO-GPU display", C_TEXT);
    rect(410, 147, 110, 14, C_PANEL_LIGHT);
    rect(410, 147, virtio_gpu_ready() ? 110 : 18, 14, virtio_gpu_ready() ? C_GREEN : C_ORANGE);
    text(532, 148, virtio_gpu_ready() ? "Ready" : "Unavailable", C_TEXT);
    text(203, 177, "VirtIO input devices", C_TEXT);
    rect(410, 177, 110, 14, C_PANEL_LIGHT);
    rect(410, 177, chos_input_device_count() ? 110 : 18, 14, chos_input_device_count() ? C_GREEN : C_ORANGE);
    text(532, 178, chos_input_device_count() ? "Ready" : "Serial only", C_TEXT);
    text(203, 207, "Persistent storage", C_TEXT);
    rect(410, 207, 110, 14, C_PANEL_LIGHT);
    rect(410, 207, storage_mounted() ? 110 : 18, 14, storage_mounted() ? C_GREEN : C_ORANGE);
    text(532, 208, storage_mounted() ? "Mounted" : "ROMFS", C_TEXT);
    text(203, 250, "Display mode", C_MUTED);
    text(410, 250, display_mode_full_label(virtio_gpu_current_mode_index()), C_TEXT);
    text(203, 276, "Color format", C_MUTED);
    text(410, 276, "BGRA8 / little-endian", C_TEXT);
    text(203, 302, "Input path", C_MUTED);
    text(410, 302, "VirtIO event queues (polled)", C_TEXT);
    text(203, 328, "Recovery console", C_MUTED);
    text(410, 328, "PL011 serial / always on", C_TEXT);
    text(203, 370, "Display driver", C_MUTED);
    text(203, 388, virtio_gpu_last_error(), C_TEXT);
    text(203, 416, "Input driver", C_MUTED);
    text(203, 434, chos_input_last_error(), C_TEXT);
    draw_status_line("Diagnostics", "Read-only view");
}

static const char *display_mode_label(size_t mode_index) {
    static const char *const labels[CHOS_DISPLAY_MODE_COUNT] = {
        "800 x 600  (4:3)", "1024 x 640  (16:10)", "1024 x 768  (4:3)",
        "1280 x 720  (16:9)", "1280 x 800  (16:10)", "1280 x 960  (4:3)",
        "1366 x 768  (16:9 class)", "1440 x 900  (16:10)", "1440 x 1080  (4:3)",
        "1600 x 900  (16:9)", "1600 x 1200  (4:3)", "1680 x 1050  (16:10)",
        "1920 x 1080  (16:9)", "1920 x 1200  (16:10)", "1920 x 1440  (4:3)",
        "2048 x 1152  (16:9)", "2048 x 1280  (16:10)", "2048 x 1536  (4:3)",
        "2304 x 1296  (16:9)", "2304 x 1440  (16:10)", "2560 x 1080  (21:9)",
        "2560 x 1440  (16:9)", "2560 x 1600  (16:10)", "2560 x 1920  (4:3)",
        "1152 x 864  (4:3)", "1200 x 900  (4:3)"
    };
    return mode_index < CHOS_DISPLAY_MODE_COUNT ? labels[mode_index] : "Unknown";
}

static const char *display_mode_full_label(size_t mode_index) {
    static const char *const labels[CHOS_DISPLAY_MODE_COUNT] = {
        "800 x 600 x 32 bpp", "1024 x 640 x 32 bpp", "1024 x 768 x 32 bpp",
        "1280 x 720 x 32 bpp", "1280 x 800 x 32 bpp", "1280 x 960 x 32 bpp",
        "1366 x 768 x 32 bpp", "1440 x 900 x 32 bpp", "1440 x 1080 x 32 bpp",
        "1600 x 900 x 32 bpp", "1600 x 1200 x 32 bpp", "1680 x 1050 x 32 bpp",
        "1920 x 1080 x 32 bpp", "1920 x 1200 x 32 bpp", "1920 x 1440 x 32 bpp",
        "2048 x 1152 x 32 bpp", "2048 x 1280 x 32 bpp", "2048 x 1536 x 32 bpp",
        "2304 x 1296 x 32 bpp", "2304 x 1440 x 32 bpp", "2560 x 1080 x 32 bpp",
        "2560 x 1440 x 32 bpp", "2560 x 1600 x 32 bpp", "2560 x 1920 x 32 bpp",
        "1152 x 864 x 32 bpp", "1200 x 900 x 32 bpp"
    };
    return mode_index < CHOS_DISPLAY_MODE_COUNT ? labels[mode_index] : "Unknown mode";
}

static void draw_settings_content(void) {
    size_t current_mode = virtio_gpu_current_mode_index();
    size_t row;
    draw_window_shell("Desktop Settings", 180, 70, GUI_WINDOW_BASE_WIDTH, GUI_WINDOW_BASE_HEIGHT);
    text_bold(204, 109, "Appearance", C_BLUE_DARK);
    text(204, 132, "Wallpaper", C_TEXT);
    draw_button(205, 151, 138, 43, "Classic Teal", 0);
    draw_button(354, 151, 138, 43, "CHOS Horizon", 0);
    if (wallpaper_alt) outline(354, 151, 138, 43, C_BLUE_DARK);
    else outline(205, 151, 138, 43, C_BLUE_DARK);
    text(204, 205, wallpaper_alt ? "Original 1024 x 768 landscape" : "Classic solid teal desktop", C_MUTED);

    text(510, 132, "Sound / time zone", C_TEXT);
    draw_button(510, 151, 105, 43, startup_sound_enabled ? "Sound: On" : "Sound: Off", 0);
    draw_button(620, 151, 114, 43, gui_timezone_label(), 0);
    if (!chos_audio_available()) text(510, 199, "No audio device", C_MUTED);
    else text(510, 199, "VirtIO-Sound ready", C_MUTED);
    text(620, 199, "Click to cycle", C_MUTED);

    draw_hline(204, 225, 546, C_PANEL_DARK);
    text_bold(204, 236, "Display resolution", C_BLUE_DARK);
    text(405, 236, "Available framebuffer modes", C_MUTED);
    for (row = 0u; row < DISPLAY_MODES_PER_PAGE; ++row) {
        size_t index = display_mode_page * DISPLAY_MODES_PER_PAGE + row;
        int y = 261 + (int)row * 28;
        if (index >= chos_display_mode_count()) break;
        draw_button(205, y, 334, 24, display_mode_label(index), index == current_mode);
        if (index == current_mode) {
            rect(209, y + 4, 3, 16, C_BLUE);
            outline(205, y, 334, 24, C_BLUE_DARK);
        }
    }
    draw_button(548, 261, 85, 24, "Previous", display_mode_page == 0u);
    draw_button(640, 261, 90, 24, "Next", (display_mode_page + 1u) * DISPLAY_MODES_PER_PAGE >= chos_display_mode_count());
    {
        size_t pages = (chos_display_mode_count() + DISPLAY_MODES_PER_PAGE - 1u) / DISPLAY_MODES_PER_PAGE;
        text(548, 293, "Page", C_MUTED);
        draw_number(590, 293, (uint32_t)(display_mode_page + 1u), C_TEXT);
        text(600, 293, "/", C_MUTED);
        draw_number(610, 293, (uint32_t)pages, C_TEXT);
    }
    if (display_mode_error) {
        text(205, 420, "Mode change failed; previous resolution retained.", C_RED);
    } else if (settings_save_error) {
        text(205, 420, "Settings could not be saved to persistent storage.", C_ORANGE);
    } else {
        text(205, 420, "Changes apply immediately and are saved when storage is mounted.", C_MUTED);
    }
    text(205, 443, "Higher resolutions use more memory and may be slower.", C_MUTED);
    text(205, 464, "Mode, wallpaper and audio preference persist on CHFS.", C_MUTED);
    draw_status_line("Settings", display_mode_full_label(current_mode));
}

static void draw_start_menu(void) {
    static const char *const entries[] = { "Console", "File Manager", "About CHOS", "Diagnostics", "Settings", "Web Browser" };
    static const char *const icons[] = { "console", "file_manager", "about", "diagnostics", "settings", "network" };
    int old_physical = physical_coordinates;
    int scale = (int)gui_scale_factor();
    int top = TASKBAR_Y - START_MENU_HEIGHT;
    int i;
    physical_coordinates = 1;
    rect(7 * scale, top, 226 * scale, START_MENU_HEIGHT, C_PANEL);
    draw_bevel(7 * scale, top, 226 * scale, START_MENU_HEIGHT, C_WHITE, C_BLACK, 1);
    /* The original CHOS sidebar fades from classic navy at the top to black
       at the bottom. The vertical brand is rotated and reads upward from C. */
    chos_gfx_gradient_v(&screen, 10 * scale, top + 3 * scale,
                        44 * scale, START_MENU_HEIGHT - 6 * scale, C_BLUE_DARK, C_BLACK);
    {
        static const char brand[] = "CHOS";
        const int strip_x = 10 * scale;
        const int strip_y = top + 3 * scale;
        const int strip_w = 44 * scale;
        const int strip_h = START_MENU_HEIGHT - 6 * scale;
        const int glyph_scale = 3 * scale;
        const int glyph_w = (int)CHOS_UI_FONT_HEIGHT * glyph_scale;
        const int brand_x = strip_x + (strip_w - glyph_w) / 2;
        const int brand_bottom = strip_y + strip_h - 7 * scale;
        const int letter_gap = 3 * scale;
        const int brand_pitch = (int)CHOS_UI_FONT_WIDTH * glyph_scale + letter_gap;
        size_t letter;
        for (letter = 0u; letter < 4u; ++letter) {
            unsigned code = (unsigned char)brand[letter];
            unsigned glyph_index, row, col, advance;
            const uint8_t (*bitmap)[CHOS_UI_FONT_ROW_BYTES];
            int glyph_y;
            if (code < CHOS_UI_GLYPH_FIRST || code >= CHOS_UI_GLYPH_FIRST + CHOS_UI_GLYPH_COUNT) continue;
            glyph_index = code - CHOS_UI_GLYPH_FIRST;
            bitmap = chos_ui_bold_bitmap[glyph_index];
            advance = chos_ui_bold_advance[glyph_index];
            /* Center each rotated glyph inside an equal-height cell. The
             * per-glyph advance changes ink width, never letter-to-letter pitch. */
            glyph_y = brand_bottom - (int)letter * brand_pitch -
                (brand_pitch + (int)advance * glyph_scale) / 2;
            for (row = 0u; row < CHOS_UI_FONT_HEIGHT; ++row) {
                for (col = 0u; col < advance && col < CHOS_UI_FONT_WIDTH; ++col) {
                    uint8_t packed = bitmap[row][col / 2u];
                    unsigned coverage = (col & 1u) == 0u ? (unsigned)(packed >> 4) : (unsigned)(packed & 0x0fu);
                    int px = brand_x + (int)row * glyph_scale;
                    int py = glyph_y + ((int)advance - 1 - (int)col) * glyph_scale;
                    unsigned sy, sx;
                    if (coverage == 0u) continue;
                    for (sy = 0u; sy < (unsigned)glyph_scale; ++sy) {
                        for (sx = 0u; sx < (unsigned)glyph_scale; ++sx) {
                            int dx = px + (int)sx, dy = py + (int)sy;
                            uint32_t *pixel;
                            if (dx < 0 || dy < 0 || (uint64_t)dx >= screen.width || (uint64_t)dy >= screen.height) continue;
                            pixel = &screen.pixels[(size_t)dy * screen.stride_pixels + (size_t)dx];
                            if (coverage == 15u) *pixel = C_WHITE;
                            else {
                                unsigned inv = 15u - coverage;
                                unsigned r = ((((C_WHITE >> 16) & 255u) * coverage) + (((*pixel >> 16) & 255u) * inv) + 7u) / 15u;
                                unsigned g = ((((C_WHITE >> 8) & 255u) * coverage) + (((*pixel >> 8) & 255u) * inv) + 7u) / 15u;
                                unsigned b = (((C_WHITE & 255u) * coverage) + ((*pixel & 255u) * inv) + 7u) / 15u;
                                *pixel = 0xff000000u | (r << 16) | (g << 8) | b;
                            }
                        }
                    }
                }
            }
        }
    }
    for (i = 0; i < 6; ++i) {
        int y = top + (42 + i * 31) * scale;
        if (i == start_menu_selected_item) {
            rect(54 * scale, y, 171 * scale, 28 * scale, C_BLUE_DARK);
            outline(54 * scale, y, 171 * scale, 28 * scale, C_WHITE);
        }
        gui_draw_named_icon(icons[i], 58 * scale, y + 3 * scale, 22 * scale, 22 * scale);
        text(84 * scale, y + 7 * scale, entries[i], i == start_menu_selected_item ? C_WHITE : C_TEXT);
        draw_hline(56 * scale, y + 29 * scale, 166 * scale, C_PANEL_DARK);
    }
    draw_hline(54 * scale, top + 232 * scale, 171 * scale, C_PANEL_DARK);
    draw_button(54 * scale, top + 242 * scale, 81 * scale, 32 * scale, "Shutdown", 0);
    draw_button(140 * scale, top + 242 * scale, 85 * scale, 32 * scale, "Restart", 0);
    text(54 * scale, top + 279 * scale, "CHOS ARM64", C_TEXT);
    physical_coordinates = old_physical;
}

static void gui_build_wallpaper_cache(void) {
    size_t x, y;
    const uint32_t teal = 0xff008080u;
    const gui_embedded_image_t *image = &chos_asset_wallpaper;
    uint32_t out_w = (uint32_t)screen.width, out_h = (uint32_t)screen.height;
    uint32_t src_x0 = 0u, src_y0 = 0u, src_w = image->width, src_h = image->height;
    size_t pixels = screen.width * screen.height;
    if (out_w == 0u || out_h == 0u || out_w > CHOS_DISPLAY_MAX_WIDTH || out_h > CHOS_DISPLAY_MAX_HEIGHT ||
        pixels > CHOS_DISPLAY_MAX_WIDTH * CHOS_DISPLAY_MAX_HEIGHT) return;
    if (wallpaper_alt) {
        /* cover: crop the wider dimension, never distort the supplied PNG. */
        if ((uint64_t)image->width * out_h > (uint64_t)image->height * out_w) {
            src_w = (uint32_t)(((uint64_t)image->height * out_w) / out_h);
            if (src_w == 0u) src_w = 1u;
            src_x0 = (image->width - src_w) / 2u;
        } else if ((uint64_t)image->width * out_h < (uint64_t)image->height * out_w) {
            src_h = (uint32_t)(((uint64_t)image->width * out_h) / out_w);
            if (src_h == 0u) src_h = 1u;
            src_y0 = (image->height - src_h) / 2u;
        }
    }
    for (y = 0u; y < screen.height; ++y) {
        uint32_t sy = wallpaper_alt ? src_y0 + (uint32_t)(((uint64_t)y * src_h) / out_h) : 0u;
        if (sy >= image->height) sy = image->height - 1u;
        for (x = 0u; x < screen.width; ++x) {
            uint32_t c = teal;
            if (wallpaper_alt) {
                uint32_t sx = src_x0 + (uint32_t)(((uint64_t)x * src_w) / out_w);
                if (sx >= image->width) sx = image->width - 1u;
                c = image->pixels[(size_t)sy * image->width + sx];
                if ((c >> 24) != 255u) c = gui_alpha_over(c, teal);
                c |= 0xff000000u;
            }
            wallpaper_cache[y * screen.width + x] = c;
        }
    }
    wallpaper_cache_valid = 1;
    wallpaper_cache_mode = virtio_gpu_current_mode_index();
    wallpaper_cache_kind = wallpaper_alt;
}

static void draw_desktop_wallpaper(void) {
    size_t y;
    if (!wallpaper_alt) {
        /* The classic default is a single flat color, not a per-frame landscape. */
        chos_gfx_clear(&screen, 0xff008080u);
        return;
    }
    if (!wallpaper_cache_valid || wallpaper_cache_mode != virtio_gpu_current_mode_index() ||
        wallpaper_cache_kind != wallpaper_alt) gui_build_wallpaper_cache();
    if (!wallpaper_cache_valid) { chos_gfx_clear(&screen, 0xff008080u); return; }
    for (y = 0u; y < screen.height; ++y) {
        uint32_t *dst = screen.pixels + y * screen.stride_pixels;
        const uint32_t *src = wallpaper_cache + y * screen.width;
        size_t x;
        for (x = 0u; x < screen.width; ++x) dst[x] = src[x];
    }
}

static void draw_taskbar(void) {
    size_t i, task = 0u;
    int old_physical = physical_coordinates;
    int scale = (int)gui_scale_factor();
    const int start_x = 3 * scale;
    const int start_width = 62 * scale;
    const int button_height = 22 * scale;
    const int button_y = TASKBAR_Y + (TASKBAR_HEIGHT - button_height) / 2;
    char clock_text[12];
    int clock_x = SCREEN_W - 82 * scale;
    physical_coordinates = 1;
    rect(0, TASKBAR_Y, SCREEN_W, SCREEN_H - TASKBAR_Y, C_TASKBAR);
    draw_hline(0, TASKBAR_Y, SCREEN_W, C_WHITE);
    draw_hline(0, TASKBAR_Y + scale, SCREEN_W, C_PANEL_LIGHT);
    draw_hline(0, SCREEN_H - scale, SCREEN_W, C_PANEL_DARK);
    /* A restrained four-pane CHOS mark and raised Start button evoke the
     * 1990s/2000-era shell without copying Microsoft artwork. */
    draw_button(start_x, button_y, start_width, button_height, NULL, 0);
    rect(start_x + 6 * scale, button_y + 6 * scale, 4 * scale, 4 * scale, 0xffd23b32u);
    rect(start_x + 11 * scale, button_y + 6 * scale, 4 * scale, 4 * scale, 0xffe5c641u);
    rect(start_x + 6 * scale, button_y + 11 * scale, 4 * scale, 4 * scale, 0xff4b9b52u);
    rect(start_x + 11 * scale, button_y + 11 * scale, 4 * scale, 4 * scale, 0xff3b73c5u);
    text_bold(start_x + 20 * scale, button_y + (button_height - 12 * scale) / 2,
              "Start", C_TEXT);
    for (i = 0u; i < GUI_VIEW_COUNT; ++i) {
        const chos_wm_window_t *w = chos_wm_get_const(&window_manager, (int)i);
        const char *short_title = NULL;
        int x;
        if (w == NULL) continue;
        (void)window_title_for_view((int)i, NULL, &short_title);
        x = (70 + (int)task * TASKBAR_APP_BUTTON_STEP) * scale;
        if (x + TASKBAR_APP_BUTTON_WIDTH * scale <= clock_x - 4 * scale) {
            char taskbar_title[CHOS_GUI_MAX_TEXT];
            (void)chos_taskbar_truncate_title(short_title, taskbar_title, sizeof(taskbar_title),
                                              TASKBAR_APP_BUTTON_WIDTH - TASKBAR_APP_TEXT_PADDING);
            draw_button(x, button_y, TASKBAR_APP_BUTTON_WIDTH * scale, button_height,
                        taskbar_title, (int)i == view && !w->minimized);
            if (w->minimized) rect(x + 3 * scale, button_y + 16 * scale, 3 * scale, 2 * scale, C_BLUE_DARK);
        }
        ++task;
    }
    for (i = 0u; i < GUI_APP_MAX_CLIENTS; ++i) {
        const gui_app_client_t *client = &gui_app_clients[i];
        const chos_wm_window_t *w;
        int x;
        if (!client->active) continue;
        w = chos_wm_get_const(&window_manager, client->window_id);
        if (w == NULL) continue;
        x = (70 + (int)task * TASKBAR_APP_BUTTON_STEP) * scale;
        if (x + TASKBAR_APP_BUTTON_WIDTH * scale <= clock_x - 4 * scale) {
            char taskbar_title[CHOS_GUI_MAX_TEXT];
            (void)chos_taskbar_truncate_title(client->title, taskbar_title, sizeof(taskbar_title),
                                              TASKBAR_APP_BUTTON_WIDTH - TASKBAR_APP_TEXT_PADDING);
            draw_button(x, button_y, TASKBAR_APP_BUTTON_WIDTH * scale, button_height,
                        taskbar_title, client->window_id == view && !w->minimized);
            if (w->minimized) rect(x + 3 * scale, button_y + 16 * scale, 3 * scale, 2 * scale, C_BLUE_DARK);
        }
        ++task;
    }
    rect(clock_x, button_y, 78 * scale, button_height, C_PANEL);
    draw_bevel(clock_x, button_y, 78 * scale, button_height, C_PANEL_DARK, C_WHITE, 0);
    gui_format_clock(clock_text);
    {
        size_t clock_len = 0u;
        int width;
        int tx;
        int ty;
        while (clock_text[clock_len] != '\0') ++clock_len;
        width = (int)clock_len * 8 * scale;
        tx = clock_x + (78 * scale - width) / 2;
        ty = button_y + (button_height - 12 * scale) / 2;
        mono_text(tx, ty, clock_text, C_TEXT);
    }
    physical_coordinates = old_physical;
}

static uint32_t gui_color_mix(uint32_t base, uint32_t overlay, unsigned alpha) {
    unsigned inv, r, g, b;
    if (alpha > 255u) alpha = 255u;
    inv = 255u - alpha;
    r = ((((base >> 16) & 0xffu) * inv) + (((overlay >> 16) & 0xffu) * alpha) + 127u) / 255u;
    g = ((((base >> 8) & 0xffu) * inv) + (((overlay >> 8) & 0xffu) * alpha) + 127u) / 255u;
    b = (((base & 0xffu) * inv) + ((overlay & 0xffu) * alpha) + 127u) / 255u;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

static void gui_blend_rect_physical(int32_t x, int32_t y, int32_t width, int32_t height,
                                    uint32_t color, unsigned alpha) {
    int32_t x0 = x, y0 = y, x1, y1, row;
    if (width <= 0 || height <= 0 || alpha == 0u) return;
    x1 = x + width; y1 = y + height;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    if (x0 >= x1 || y0 >= y1) return;
    for (row = y0; row < y1; ++row) {
        uint32_t *pixels = screen.pixels + (size_t)row * screen.stride_pixels;
        int32_t col;
        for (col = x0; col < x1; ++col) pixels[col] = gui_color_mix(pixels[col], color, alpha);
    }
}

static size_t gui_tooltip_wrap(const char *title, int max_width,
                               char lines[TASKBAR_TOOLTIP_MAX_LINES][CHOS_GUI_MAX_TEXT],
                               int *max_line_width) {
    size_t count = 1u, i = 0u, col = 0u;
    int width = 0, widest = 0;
    size_t row;
    for (row = 0u; row < TASKBAR_TOOLTIP_MAX_LINES; ++row) lines[row][0] = '\0';
    if (max_line_width != NULL) *max_line_width = 0;
    if (title == NULL || title[0] == '\0' || max_width < 1) return 0u;
    while (title[i] != '\0' && count <= TASKBAR_TOOLTIP_MAX_LINES) {
        char ch = title[i++];
        int cw;
        if (ch == '\r') continue;
        if (ch == '\n') {
            if (width > widest) widest = width;
            if (count == TASKBAR_TOOLTIP_MAX_LINES) break;
            ++count; col = 0u; width = 0;
            continue;
        }
        cw = (int)chos_gfx_ui_char_width(ch, 0);
        if (col != 0u && width + cw > max_width) {
            /* Prefer a word boundary when the current line contains a space. */
            size_t space = col;
            while (space > 0u && lines[count - 1u][space - 1u] != ' ') --space;
            if (space > 0u) {
                size_t move_from = space;
                if (count == TASKBAR_TOOLTIP_MAX_LINES) break;
                size_t new_col = 0u;
                int new_width = 0;
                while (lines[count - 1u][move_from] != '\0') {
                    char moved = lines[count - 1u][move_from];
                    int moved_width = (int)chos_gfx_ui_char_width(moved, 0);
                    if (new_col + 1u >= CHOS_GUI_MAX_TEXT) break;
                    lines[count][new_col++] = moved;
                    new_width += moved_width;
                    ++move_from;
                }
                lines[count - 1u][space - 1u] = '\0';
                col = space - 1u;
                width = chos_gfx_ui_text_width(lines[count - 1u], 0);
                lines[count][new_col] = '\0';
                ++count;
                col = new_col;
                width = new_width;
            } else {
                if (width > widest) widest = width;
                if (count == TASKBAR_TOOLTIP_MAX_LINES) break;
                ++count; col = 0u; width = 0;
            }
            if (ch == ' ') continue;
        }
        if (col + 1u >= CHOS_GUI_MAX_TEXT) break;
        lines[count - 1u][col++] = ch;
        lines[count - 1u][col] = '\0';
        width += cw;
        if (width > widest) widest = width;
    }
    if (max_line_width != NULL) *max_line_width = widest;
    return count;
}

static void gui_draw_taskbar_tooltip(void) {
    const char *title;
    char lines[TASKBAR_TOOLTIP_MAX_LINES][CHOS_GUI_MAX_TEXT];
    int scale, max_width, text_width = 0, box_width, box_height, x, y, line;
    int text_height;
    unsigned alpha;
    size_t line_count;
    if (taskbar_tooltip_window_id == CHOS_WM_NO_WINDOW || taskbar_tooltip_fade_step == 0u ||
        start_menu_open || power_confirmation_action != 0 || left_button_down) return;
    title = gui_taskbar_full_title(taskbar_tooltip_window_id);
    if (title == NULL || title[0] == '\0') return;
    scale = (int)gui_scale_factor();
    max_width = SCREEN_W / scale - 32;
    if (max_width > TASKBAR_TOOLTIP_MAX_WIDTH) max_width = TASKBAR_TOOLTIP_MAX_WIDTH;
    if (max_width < 80) max_width = 80;
    line_count = gui_tooltip_wrap(title, max_width, lines, &text_width);
    if (line_count == 0u) return;
    if (line_count > TASKBAR_TOOLTIP_MAX_LINES) line_count = TASKBAR_TOOLTIP_MAX_LINES;
    text_height = (int)line_count * (int)CHOS_GFX_UI_LINE_HEIGHT;
    box_width = (text_width + 14) * scale;
    if (box_width < 54 * scale) box_width = 54 * scale;
    box_height = (text_height + 8) * scale;
    if (box_width > SCREEN_W - 8 * scale) box_width = SCREEN_W - 8 * scale;
    if (box_height > SCREEN_H - 8 * scale) box_height = SCREEN_H - 8 * scale;
    x = taskbar_tooltip_anchor_x + 18 * scale;
    y = taskbar_tooltip_anchor_y - box_height - 10 * scale;
    if (x + box_width > SCREEN_W - 4 * scale) x = taskbar_tooltip_anchor_x - box_width - 12 * scale;
    if (x < 4 * scale) x = 4 * scale;
    if (x + box_width > SCREEN_W - 4 * scale) x = SCREEN_W - box_width - 4 * scale;
    if (y < 4 * scale) y = taskbar_tooltip_anchor_y + 20 * scale;
    if (y + box_height > SCREEN_H - 4 * scale) y = SCREEN_H - box_height - 4 * scale;
    alpha = taskbar_tooltip_fade_step * 255u / TASKBAR_TOOLTIP_FADE_STEPS;

    gui_blend_rect_physical(x + 2 * scale, y + 2 * scale, box_width, box_height, C_PANEL_DARK, alpha / 2u);
    gui_blend_rect_physical(x, y, box_width, box_height, C_TOOLTIP_BG, alpha);
    gui_blend_rect_physical(x, y, box_width, scale, C_WHITE, alpha);
    gui_blend_rect_physical(x, y, scale, box_height, C_WHITE, alpha);
    gui_blend_rect_physical(x, y + box_height - scale, box_width, scale, C_PANEL_DARK, alpha);
    gui_blend_rect_physical(x + box_width - scale, y, scale, box_height, C_PANEL_DARK, alpha);
    for (line = 0; line < (int)line_count; ++line) {
        int text_x = x + 7 * scale;
        int text_y = y + (4 + line * (int)CHOS_GFX_UI_LINE_HEIGHT) * scale;
        uint32_t faded_text = gui_color_mix(C_TOOLTIP_BG, C_BLACK, alpha);
        chos_gfx_ui_text_scaled(&screen, text_x, text_y, lines[line], faded_text, 0, (unsigned)scale);
    }
}

static void gui_dim_screen(void) {
    int32_t y;
    for (y = 0; y < SCREEN_H; ++y) {
        uint32_t *row = screen.pixels + (size_t)y * screen.stride_pixels;
        int32_t x;
        for (x = 0; x < SCREEN_W; ++x) {
            uint32_t c = row[x];
            uint32_t r = ((c >> 16) & 0xffu) * 54u / 100u;
            uint32_t g = ((c >> 8) & 0xffu) * 58u / 100u;
            uint32_t b = (c & 0xffu) * 64u / 100u + 12u;
            if (b > 255u) b = 255u;
            row[x] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    }
}

static void draw_power_confirmation(void) {
    const int scale = (int)gui_scale_factor();
    const int dialog_w = 390 * scale;
    const int dialog_h = 176 * scale;
    const int x = (SCREEN_W - dialog_w) / 2;
    const int y = (SCREEN_H - dialog_h) / 2;
    const int shutdown = power_confirmation_action == CHOS_POWER_SHUTDOWN;
    int old_physical = physical_coordinates;
    physical_coordinates = 1;
    gui_dim_screen();
    rect(x + 3 * scale, y + 3 * scale, dialog_w, dialog_h, C_PANEL_DARK);
    rect(x, y, dialog_w, dialog_h, C_PANEL);
    draw_bevel(x, y, dialog_w, dialog_h, C_WHITE, C_BLACK, 1);
    rect(x + 3 * scale, y + 3 * scale, dialog_w - 6 * scale, 25 * scale, C_BLUE_DARK);
    text_bold(x + 9 * scale, y + 8 * scale, shutdown ? "Shut Down CHOS" : "Restart CHOS", C_WHITE);
    gui_draw_named_icon(shutdown ? "shutdown" : "restart", x + 18 * scale, y + 43 * scale, 40 * scale, 40 * scale);
    text_bold(x + 70 * scale, y + 45 * scale, shutdown ? "Close CHOS?" : "Restart CHOS?", C_TEXT);
    if (shutdown) {
        text(x + 70 * scale, y + 69 * scale, "The guest will halt safely.", C_TEXT);
        text(x + 70 * scale, y + 88 * scale, "Close the QEMU window to exit.", C_TEXT);
    } else {
        text(x + 70 * scale, y + 69 * scale, "The kernel and devices will restart.", C_TEXT);
        text(x + 70 * scale, y + 88 * scale, "Saved settings will be retained.", C_TEXT);
    }
    draw_hline(x + 10 * scale, y + 124 * scale, dialog_w - 20 * scale, C_PANEL_DARK);
    draw_button(x + 172 * scale, y + 137 * scale, 94 * scale, 28 * scale, shutdown ? "Shut Down" : "Restart", 0);
    draw_button(x + 276 * scale, y + 137 * scale, 92 * scale, 28 * scale, "Cancel", 0);
    physical_coordinates = old_physical;
}

static void gui_draw_context_menu(void) {
    static const char *const desktop_rows[] = { "Arrange Icons", "Refresh", "New...", "Settings" };
    static const char *const app_rows[] = { "Open", "Arrange Icons", "Refresh" };
    static const char *const file_rows[] = { "Open", "Rename...", "Move to Trash" };
    static const char *const trash_rows[] = { "Open", "Empty Trash..." };
    static const char *const documents_rows[] = { "Open" };
    static const char *const new_rows[] = { "Folder", "Text Document" };
    const char *const *rows;
    const int scale = (int)gui_scale_factor();
    size_t count = gui_context_row_count(), i;
    int old_physical = physical_coordinates;
    if (!context_menu_open || count == 0u) return;
    if (context_menu_submenu) rows = new_rows;
    else if (context_menu_kind == GUI_CONTEXT_APP_ICON) rows = app_rows;
    else if (context_menu_kind == GUI_CONTEXT_FILE_ICON) rows = file_rows;
    else if (context_menu_kind == GUI_CONTEXT_TRASH_ICON) rows = trash_rows;
    else if (context_menu_kind == GUI_CONTEXT_DOCUMENTS_ICON) rows = documents_rows;
    else rows = desktop_rows;
    physical_coordinates = 1;
    rect(context_menu_x, context_menu_y, GUI_CONTEXT_WIDTH * scale,
         ((int)count * GUI_CONTEXT_ROW_HEIGHT + 6) * scale, C_PANEL);
    draw_bevel(context_menu_x, context_menu_y, GUI_CONTEXT_WIDTH * scale,
               ((int)count * GUI_CONTEXT_ROW_HEIGHT + 6) * scale, C_WHITE, C_BLACK, 1);
    for (i = 0u; i < count; ++i) {
        int row_y = context_menu_y + (3 + (int)i * GUI_CONTEXT_ROW_HEIGHT) * scale;
        if ((int)i == context_menu_hover_index) {
            rect(context_menu_x + 3 * scale, row_y, (GUI_CONTEXT_WIDTH - 6) * scale,
                 GUI_CONTEXT_ROW_HEIGHT * scale, C_SELECTION);
            text(context_menu_x + 11 * scale, row_y + 5 * scale, rows[i], C_WHITE);
        } else {
            text(context_menu_x + 11 * scale, row_y + 5 * scale, rows[i], C_TEXT);
        }
        if (!context_menu_submenu && context_menu_kind == GUI_CONTEXT_DESKTOP && i == 2u)
            text(context_menu_x + 141 * scale, row_y + 4 * scale, ">", C_TEXT);
    }
    physical_coordinates = old_physical;
}

static void gui_draw_prompt(void) {
    const int scale = (int)gui_scale_factor();
    const int width = 420 * scale, height = 186 * scale;
    const int x = (SCREEN_W - width) / 2, y = (TASKBAR_Y - height) / 2;
    int old_physical = physical_coordinates;
    const char *title = "New Folder";
    const char *message = "Enter a name for the new folder:";
    const char *primary = "Create";
    int confirm_only = 0;
    if (prompt_kind == GUI_PROMPT_NEW_TEXT) { title = "New Text Document"; message = "Enter a name for the text document:"; }
    else if (prompt_kind == GUI_PROMPT_RENAME) { title = "Rename Item"; message = "Enter the new name:"; primary = "Rename"; }
    else if (prompt_kind == GUI_PROMPT_CONFIRM_DELETE) { title = "Delete Permanently"; message = "Permanently delete this item? This cannot be undone."; primary = "Delete"; confirm_only = 1; }
    else if (prompt_kind == GUI_PROMPT_EMPTY_TRASH) { title = "Empty Trash"; message = "Permanently delete every item in Trash?"; primary = "Empty Trash"; confirm_only = 1; }
    if (prompt_kind == 0) return;
    physical_coordinates = 1;
    gui_dim_screen();
    rect(x + 3 * scale, y + 3 * scale, width, height, C_PANEL_DARK);
    rect(x, y, width, height, C_PANEL);
    draw_bevel(x, y, width, height, C_WHITE, C_BLACK, 1);
    rect(x + 3 * scale, y + 3 * scale, width - 6 * scale, 26 * scale, C_BLUE_DARK);
    text_bold(x + 10 * scale, y + 8 * scale, title, C_WHITE);
    gui_draw_named_icon(confirm_only ? "help" : "text_viewer", x + 17 * scale, y + 48 * scale, 34 * scale, 34 * scale);
    text(x + 62 * scale, y + 48 * scale, message, C_TEXT);
    if (!confirm_only) {
        rect(x + 62 * scale, y + 76 * scale, width - 82 * scale, 28 * scale, C_WHITE);
        draw_bevel(x + 62 * scale, y + 76 * scale, width - 82 * scale, 28 * scale, C_PANEL_DARK, C_WHITE, 0);
        text(x + 68 * scale, y + 83 * scale, prompt_name, C_TEXT);
        if (prompt_error) text(x + 62 * scale, y + 111 * scale, "Invalid name, duplicate item, or storage error.", C_RED);
        else text(x + 62 * scale, y + 111 * scale, "Enter accepts; Esc cancels.", C_MUTED);
    }
    draw_hline(x + 10 * scale, y + 135 * scale, width - 20 * scale, C_PANEL_DARK);
    draw_button(x + 214 * scale, y + 146 * scale, 92 * scale, 28 * scale, primary, 0);
    draw_button(x + 314 * scale, y + 146 * scale, 92 * scale, 28 * scale, "Cancel", 0);
    physical_coordinates = old_physical;
}

static void gui_draw_desktop_status(void) {
    int scale = (int)gui_scale_factor();
    int old_physical = physical_coordinates;
    if (desktop_status_message[0] == '\0') return;
    if (timer_is_ready() && timer_ticks() > desktop_status_until_tick) { desktop_status_message[0] = '\0'; return; }
    physical_coordinates = 1;
    rect(6 * scale, TASKBAR_Y - 28 * scale, 360 * scale, 24 * scale, C_PANEL);
    draw_bevel(6 * scale, TASKBAR_Y - 28 * scale, 360 * scale, 24 * scale, C_WHITE, C_PANEL_DARK, 1);
    text(12 * scale, TASKBAR_Y - 21 * scale, desktop_status_message, C_TEXT);
    physical_coordinates = old_physical;
}

static void draw_pointer(void) {
    int scale = (int)gui_scale_factor();
    if (virtio_gpu_cursor_available()) return;
    int old_physical = physical_coordinates;
    physical_coordinates = 1;
    gui_draw_image(&chos_asset_cursor, mouse_x, mouse_y,
                   GUI_CURSOR_BOX_WIDTH * scale, GUI_CURSOR_BOX_HEIGHT * scale);
    physical_coordinates = old_physical;
}

static void gui_cursor_capture_and_draw(void) {
    int scale = (int)gui_scale_factor();
    if (virtio_gpu_cursor_available()) {
        /* Hardware cursor is a separate scanout plane; never write it into the
         * framebuffer or maintain save-under pixels for it. */
        cursor_under_valid = 0;
        return;
    }
    int32_t x = mouse_x, y = mouse_y;
    int32_t w = GUI_CURSOR_BOX_WIDTH * scale;
    int32_t h = GUI_CURSOR_BOX_HEIGHT * scale;
    int32_t row;
    if (x < 0 || y < 0 || (size_t)x >= screen.width || (size_t)y >= screen.height) {
        cursor_under_valid = 0;
        draw_pointer();
        return;
    }
    if ((size_t)(x + w) > screen.width) w = (int32_t)screen.width - x;
    if ((size_t)(y + h) > screen.height) h = (int32_t)screen.height - y;
    if (w <= 0 || h <= 0 || w > GUI_CURSOR_SAVE_STRIDE || h > GUI_CURSOR_SAVE_STRIDE) {
        cursor_under_valid = 0;
        draw_pointer();
        return;
    }
    for (row = 0; row < h; ++row) {
        const uint32_t *src = screen.pixels + (size_t)(y + row) * screen.stride_pixels + (size_t)x;
        uint32_t *dst = cursor_under + (size_t)row * GUI_CURSOR_SAVE_STRIDE;
        int32_t col;
        for (col = 0; col < w; ++col) dst[col] = src[col];
    }
    cursor_under_x = x; cursor_under_y = y;
    cursor_under_width = w; cursor_under_height = h;
    cursor_under_valid = 1;
    draw_pointer();
}

static void gui_cursor_restore_under(void) {
    int32_t row;
    if (virtio_gpu_cursor_available() || !cursor_under_valid) return;
    for (row = 0; row < cursor_under_height; ++row) {
        uint32_t *dst = screen.pixels + (size_t)(cursor_under_y + row) * screen.stride_pixels + (size_t)cursor_under_x;
        const uint32_t *src = cursor_under + (size_t)row * GUI_CURSOR_SAVE_STRIDE;
        int32_t col;
        for (col = 0; col < cursor_under_width; ++col) dst[col] = src[col];
    }
}

static int gui_cursor_move_only(void) {
    int32_t old_x, old_y, old_w, old_h;
    int32_t new_x, new_y, new_w, new_h;
    uint32_t left, top, right, bottom;
    chos_gpu_rect_t changed;
    if (!gui_active) return 0;
    if (virtio_gpu_cursor_available()) {
        if (virtio_gpu_cursor_move(mouse_x, mouse_y, !splash_active)) return 1;
        /* Cursorq failure disables the hardware cursor in the driver. Redraw now
         * so the software bitmap fallback takes over immediately. */
        gui_mark_dirty();
        return 0;
    }
    if (!cursor_under_valid || splash_active) return 0;

    old_x = cursor_under_x; old_y = cursor_under_y;
    old_w = cursor_under_width; old_h = cursor_under_height;
    gui_cursor_restore_under();
    cursor_under_valid = 0;
    gui_cursor_capture_and_draw();
    if (!cursor_under_valid) { gui_active = 0; return 0; }

    new_x = cursor_under_x; new_y = cursor_under_y;
    new_w = cursor_under_width; new_h = cursor_under_height;
    if (old_w <= 0 || old_h <= 0 || new_w <= 0 || new_h <= 0) {
        gui_active = 0; cursor_under_valid = 0; return 0;
    }

    /* Publish the old and new cursor locations as ONE framebuffer update.
       Two separate VirtIO transfers can expose an intermediate frame where
       the cursor is absent, particularly under fast pointer input. Relative
       input is clamped per event, so this bounding rectangle stays small. */
    left = (uint32_t)(old_x < new_x ? old_x : new_x);
    top = (uint32_t)(old_y < new_y ? old_y : new_y);
    right = (uint32_t)((old_x + old_w) > (new_x + new_w) ?
                       (old_x + old_w) : (new_x + new_w));
    bottom = (uint32_t)((old_y + old_h) > (new_y + new_h) ?
                        (old_y + old_h) : (new_y + new_h));
    changed = (chos_gpu_rect_t){left, top, right - left, bottom - top};
    if (!virtio_gpu_present_rect(changed.x, changed.y, changed.width, changed.height)) {
        gui_active = 0; cursor_under_valid = 0; return 0;
    }
    ++frames_drawn;
    return 1;
}

static void gui_drag_get_rects(int32_t x, int32_t y, int32_t width, int32_t height,
                               int32_t thickness, gui_drag_rect_t rects[GUI_DRAG_STRIP_COUNT]) {
    rects[0] = (gui_drag_rect_t){x, y, width, thickness};
    rects[1] = (gui_drag_rect_t){x, y + height - thickness, width, thickness};
    rects[2] = (gui_drag_rect_t){x, y + thickness, thickness, height - 2 * thickness};
    rects[3] = (gui_drag_rect_t){x + width - thickness, y + thickness,
                                  thickness, height - 2 * thickness};
}

static void gui_drag_save_under(const gui_drag_rect_t rects[GUI_DRAG_STRIP_COUNT]) {
    size_t row, col;
    for (row = 0u; row < (size_t)rects[0].height; ++row) {
        const uint32_t *src_top = screen.pixels + (size_t)(rects[0].y + (int32_t)row) * screen.stride_pixels + (size_t)rects[0].x;
        const uint32_t *src_bottom = screen.pixels + (size_t)(rects[1].y + (int32_t)row) * screen.stride_pixels + (size_t)rects[1].x;
        for (col = 0u; col < (size_t)rects[0].width; ++col) {
            drag_under_top[row * CHOS_DISPLAY_MAX_WIDTH + col] = src_top[col];
            drag_under_bottom[row * CHOS_DISPLAY_MAX_WIDTH + col] = src_bottom[col];
        }
    }
    for (row = 0u; row < (size_t)rects[2].height; ++row) {
        const uint32_t *src_left = screen.pixels + (size_t)(rects[2].y + (int32_t)row) * screen.stride_pixels + (size_t)rects[2].x;
        const uint32_t *src_right = screen.pixels + (size_t)(rects[3].y + (int32_t)row) * screen.stride_pixels + (size_t)rects[3].x;
        for (col = 0u; col < (size_t)rects[2].width; ++col) {
            drag_under_left[row * GUI_DRAG_BORDER_MAX_THICKNESS + col] = src_left[col];
            drag_under_right[row * GUI_DRAG_BORDER_MAX_THICKNESS + col] = src_right[col];
        }
    }
}

static void gui_drag_restore_under(void) {
    size_t row, col;
    if (!drag_outline_visible) return;
    for (row = 0u; row < (size_t)drag_outline_rects[0].height; ++row) {
        uint32_t *dst_top = screen.pixels + (size_t)(drag_outline_rects[0].y + (int32_t)row) * screen.stride_pixels + (size_t)drag_outline_rects[0].x;
        uint32_t *dst_bottom = screen.pixels + (size_t)(drag_outline_rects[1].y + (int32_t)row) * screen.stride_pixels + (size_t)drag_outline_rects[1].x;
        for (col = 0u; col < (size_t)drag_outline_rects[0].width; ++col) {
            dst_top[col] = drag_under_top[row * CHOS_DISPLAY_MAX_WIDTH + col];
            dst_bottom[col] = drag_under_bottom[row * CHOS_DISPLAY_MAX_WIDTH + col];
        }
    }
    for (row = 0u; row < (size_t)drag_outline_rects[2].height; ++row) {
        uint32_t *dst_left = screen.pixels + (size_t)(drag_outline_rects[2].y + (int32_t)row) * screen.stride_pixels + (size_t)drag_outline_rects[2].x;
        uint32_t *dst_right = screen.pixels + (size_t)(drag_outline_rects[3].y + (int32_t)row) * screen.stride_pixels + (size_t)drag_outline_rects[3].x;
        for (col = 0u; col < (size_t)drag_outline_rects[2].width; ++col) {
            dst_left[col] = drag_under_left[row * GUI_DRAG_BORDER_MAX_THICKNESS + col];
            dst_right[col] = drag_under_right[row * GUI_DRAG_BORDER_MAX_THICKNESS + col];
        }
    }
    drag_outline_visible = 0;
}

static void gui_drag_fill(const gui_drag_rect_t *rect, uint32_t color) {
    int32_t row, col;
    if (rect == NULL) return;
    for (row = 0; row < rect->height; ++row) {
        uint32_t *dst = screen.pixels + (size_t)(rect->y + row) * screen.stride_pixels + (size_t)rect->x;
        for (col = 0; col < rect->width; ++col) {
            /* White/black dash pattern reads clearly on both classic teal and
             * gray windows while touching only the narrow outline itself. */
            dst[col] = (((rect->x + col + rect->y + row) & 3) < 2) ? color : C_WHITE;
        }
    }
}

static void gui_drag_overlay_update(void) {
    chos_wm_window_t *window;
    gui_drag_rect_t next_rects[GUI_DRAG_STRIP_COUNT];
    gui_drag_rect_t old_rects[GUI_DRAG_STRIP_COUNT];
    int had_old_outline;
    int32_t old_cursor_x = 0, old_cursor_y = 0, old_cursor_w = 0, old_cursor_h = 0;
    int32_t thickness, max_x, max_y;
    size_t i;
    int present_ok = 1;
    if (!gui_active || !left_button_down || dragging_window_id == CHOS_WM_NO_WINDOW) {
        drag_preview_dirty = 0;
        return;
    }
    window = chos_wm_get(&window_manager, dragging_window_id);
    if (window == NULL) {
        drag_preview_dirty = 0;
        dragging_window_id = CHOS_WM_NO_WINDOW;
        return;
    }
    if (!drag_preview_dirty && drag_outline_visible) return;

    if (cursor_under_valid) {
        old_cursor_x = cursor_under_x; old_cursor_y = cursor_under_y;
        old_cursor_w = cursor_under_width; old_cursor_h = cursor_under_height;
        gui_cursor_restore_under();
        cursor_under_valid = 0;
    }
    had_old_outline = drag_outline_visible;
    if (had_old_outline) {
        for (i = 0u; i < GUI_DRAG_STRIP_COUNT; ++i) old_rects[i] = drag_outline_rects[i];
        gui_drag_restore_under();
    }

    max_x = SCREEN_W - window->width;
    max_y = TASKBAR_Y - window->height;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (drag_preview_x < 0) drag_preview_x = 0;
    if (drag_preview_y < 0) drag_preview_y = 0;
    if (drag_preview_x > max_x) drag_preview_x = max_x;
    if (drag_preview_y > max_y) drag_preview_y = max_y;
    thickness = (int32_t)(2u * gui_scale_factor());
    if (thickness < 2) thickness = 2;
    if (thickness > GUI_DRAG_BORDER_MAX_THICKNESS) thickness = GUI_DRAG_BORDER_MAX_THICKNESS;
    if (window->width <= 2 * thickness || window->height <= 2 * thickness ||
        window->width > (int32_t)CHOS_DISPLAY_MAX_WIDTH ||
        window->height > (int32_t)CHOS_DISPLAY_MAX_HEIGHT) {
        gui_mark_dirty();
        drag_preview_dirty = 0;
        return;
    }
    gui_drag_get_rects(drag_preview_x, drag_preview_y, window->width, window->height,
                       thickness, next_rects);
    gui_drag_save_under(next_rects);
    for (i = 0u; i < GUI_DRAG_STRIP_COUNT; ++i) gui_drag_fill(&next_rects[i], C_BLACK);
    for (i = 0u; i < GUI_DRAG_STRIP_COUNT; ++i) drag_outline_rects[i] = next_rects[i];
    drag_outline_visible = 1;

    /* Transfer each edge independently (old/new edge pair) so the host never
     * flushes the large rectangle spanning the entire moved window. The data
     * copied is proportional to perimeter, not area, even at 1440p. */
    for (i = 0u; i < GUI_DRAG_STRIP_COUNT && present_ok; ++i) {
        if (had_old_outline) {
            present_ok = virtio_gpu_present_rect_pair((uint32_t)old_rects[i].x,
                (uint32_t)old_rects[i].y, (uint32_t)old_rects[i].width, (uint32_t)old_rects[i].height,
                (uint32_t)next_rects[i].x, (uint32_t)next_rects[i].y,
                (uint32_t)next_rects[i].width, (uint32_t)next_rects[i].height);
        } else {
            present_ok = virtio_gpu_present_rect((uint32_t)next_rects[i].x,
                (uint32_t)next_rects[i].y, (uint32_t)next_rects[i].width, (uint32_t)next_rects[i].height);
        }
    }

    gui_cursor_capture_and_draw();
    if (present_ok) {
        if (old_cursor_w > 0 && old_cursor_h > 0 && cursor_under_valid) {
            present_ok = virtio_gpu_present_rect_pair((uint32_t)old_cursor_x, (uint32_t)old_cursor_y,
                (uint32_t)old_cursor_w, (uint32_t)old_cursor_h,
                (uint32_t)cursor_under_x, (uint32_t)cursor_under_y,
                (uint32_t)cursor_under_width, (uint32_t)cursor_under_height);
        } else if (cursor_under_valid) {
            present_ok = virtio_gpu_present_rect((uint32_t)cursor_under_x, (uint32_t)cursor_under_y,
                (uint32_t)cursor_under_width, (uint32_t)cursor_under_height);
        }
    }
    if (!present_ok) gui_active = 0;
    else ++frames_drawn;
    drag_preview_dirty = 0;
}

static void gui_drag_overlay_erase(void) {
    if (cursor_under_valid) gui_cursor_restore_under();
    cursor_under_valid = 0;
    if (drag_outline_visible) gui_drag_restore_under();
    drag_preview_dirty = 0;
}

static int splash_minimum_elapsed(void) {
    if (splash_counter_frequency != 0u) {
        return timer_counter() - splash_started_counter >=
               splash_counter_frequency * (uint64_t)SPLASH_MINIMUM_SECONDS;
    }
    if (timer_is_ready()) return timer_ticks() - splash_started_tick >= SPLASH_DURATION_TICKS;
    /* A timer-less platform must not get stuck forever on a cosmetic splash. */
    return 1;
}

static void gui_render_splash_bar(void) {
    uint64_t elapsed_ms = 0u;
    const int scale = (int)gui_scale_factor();
    const int track_x = 164, track_y = 373, track_w = 468, track_h = 12;
    const int block_w = 20, block_gap = 6, segment_w = block_w * 3 + block_gap * 2;
    const int cycle = track_w + segment_w;
    int segment_x, i;
    draw_offset_x = (SCREEN_W - GUI_LOGICAL_WIDTH * scale) / 2;
    draw_offset_y = (SCREEN_H - GUI_LOGICAL_HEIGHT * scale) / 2;
    physical_coordinates = 0;
    if (splash_counter_frequency != 0u) {
        uint64_t elapsed_counter = timer_counter() - splash_started_counter;
        elapsed_ms = (elapsed_counter * 1000u) / splash_counter_frequency;
    } else if (timer_is_ready()) {
        elapsed_ms = ((timer_ticks() - splash_started_tick) * 1000u) / CHOS_TIMER_HZ;
    }
    segment_x = track_x + (int)((elapsed_ms * SPLASH_SEGMENT_SPEED / 1000u) % (uint64_t)cycle) - segment_w;

    /* Repaint only the progress track. Static splash artwork is drawn once at
     * boot, so the animation is not delayed by regenerating the wallpaper. */
    rect(track_x, track_y, track_w, track_h, C_WHITE);
    draw_bevel(track_x, track_y, track_w, track_h, C_PANEL_DARK, C_WHITE, 0);
    chos_gfx_set_clip(&screen, gui_coord_x(track_x + 1), gui_coord_y(track_y + 1),
                      (track_w - 2) * scale, (track_h - 2) * scale);
    for (i = 0; i < 3; ++i) {
        int bx = segment_x + i * (block_w + block_gap);
        rect(bx, track_y + 1, block_w, track_h - 2, C_BLUE_DARK);
        draw_hline(bx, track_y + 1, block_w, C_WHITE);
    }
    chos_gfx_reset_clip(&screen);
    draw_offset_x = 0;
    draw_offset_y = 0;
    if (virtio_gpu_present()) ++frames_drawn;
    else gui_active = 0;
    dirty = 0;
}

static void gui_render_splash(void) {
    const int scale = (int)gui_scale_factor();
    if (virtio_gpu_cursor_available()) (void)virtio_gpu_cursor_move(mouse_x, mouse_y, 0);
    int old_physical = physical_coordinates;
    chos_gfx_reset_clip(&screen);
    draw_offset_x = 0; draw_offset_y = 0; drawing_active_window = 0;
    physical_coordinates = 1;
    draw_desktop_wallpaper();
    rect(0, TASKBAR_Y, SCREEN_W, TASKBAR_HEIGHT, C_PANEL);
    draw_hline(0, TASKBAR_Y, SCREEN_W, C_WHITE);
    draw_hline(0, TASKBAR_Y + scale, SCREEN_W, C_PANEL_DARK);
    physical_coordinates = old_physical;

    /* Classic desktop-style startup panel: flat gray client area, blue title bar,
       raised border and original CHOS branding. */
    draw_offset_x = (SCREEN_W - GUI_LOGICAL_WIDTH * scale) / 2;
    draw_offset_y = (SCREEN_H - GUI_LOGICAL_HEIGHT * scale) / 2;
    rect(118, 117, 562, 365, C_PANEL);
    draw_bevel(118, 117, 562, 365, C_WHITE, C_BLACK, 1);
    rect(122, 121, 554, 31, C_BLUE_DARK);
    text_bold(132, 129, "Welcome to CHOS", C_WHITE);
    rect(145, 166, 48, 48, C_PANEL);
    draw_bevel(145, 166, 48, 48, C_WHITE, C_PANEL_DARK, 1);
    gui_draw_named_icon("computer", 151, 169, 36, 40);
    text_bold(210, 174, "CHOS", C_TEXT);
    text(210, 197, "Just enough OS.", C_TEXT);
    draw_hline(145, 231, 505, C_PANEL_DARK);
    text_bold(164, 252, "Preparing your desktop", C_TEXT);
    text(164, 276, "Loading the graphical shell and system tools.", C_TEXT);
    draw_hline(164, 309, 468, C_PANEL_DARK);
    text(164, 405, "Starting CHOS...", C_TEXT);
    text(164, 442, "CHOS ARM64  |  Development build 0.30.0", C_MUTED);
    draw_offset_x = 0; draw_offset_y = 0;
    gui_render_splash_bar();
}

static gui_app_client_t *gui_app_find_window(int window_id) {
    size_t i;
    for (i = 0u; i < GUI_APP_MAX_CLIENTS; ++i)
        if (gui_app_clients[i].active && gui_app_clients[i].window_id == window_id) return &gui_app_clients[i];
    return NULL;
}

static gui_app_client_t *gui_app_find_owner(uint32_t owner_pid) {
    size_t i;
    for (i = 0u; i < GUI_APP_MAX_CLIENTS; ++i)
        if (gui_app_clients[i].active && gui_app_clients[i].owner_pid == owner_pid) return &gui_app_clients[i];
    return NULL;
}

static int gui_event_is_pointer_move(const chos_gui_message_t *event) {
    return event != NULL && event->header.type == CHOS_GUI_MSG_POINTER_EVENT &&
           (event->payload.pointer_event.event_flags & CHOS_GUI_EVENT_POINTER_MOVE) != 0u;
}

static void gui_app_queue_message(gui_app_client_t *client, const chos_gui_message_t *event) {
    size_t tail;
    if (client == NULL || event == NULL) return;
    if (client->event_count == CHOS_GUI_EVENT_QUEUE_CAPACITY) {
        size_t offset;
        if (gui_event_is_pointer_move(event)) {
            /* Pointer movement is replaceable state. Update the newest queued
             * motion sample, or drop this sample if the queue only has discrete
             * key/button/focus/close transitions. Never evict a transition for
             * a high-rate pointer move. */
            for (offset = client->event_count; offset > 0u; --offset) {
                size_t index = (client->event_head + offset - 1u) % CHOS_GUI_EVENT_QUEUE_CAPACITY;
                if (gui_event_is_pointer_move(&client->events[index])) {
                    gui_copy_bytes(&client->events[index], event, sizeof(*event));
                    return;
                }
            }
            return;
        }

        /* Discrete events get priority over pointer motion. Remove the oldest
         * queued motion sample first; if the queue contains only discrete
         * events, fall back to bounded FIFO eviction rather than blocking the
         * input path. */
        for (offset = 0u; offset < client->event_count; ++offset) {
            size_t index = (client->event_head + offset) % CHOS_GUI_EVENT_QUEUE_CAPACITY;
            if (gui_event_is_pointer_move(&client->events[index])) {
                size_t shift;
                for (shift = offset; shift + 1u < client->event_count; ++shift) {
                    size_t dst = (client->event_head + shift) % CHOS_GUI_EVENT_QUEUE_CAPACITY;
                    size_t src = (client->event_head + shift + 1u) % CHOS_GUI_EVENT_QUEUE_CAPACITY;
                    gui_copy_bytes(&client->events[dst], &client->events[src], sizeof(*event));
                }
                --client->event_count;
                break;
            }
        }
        if (client->event_count == CHOS_GUI_EVENT_QUEUE_CAPACITY) {
            client->event_head = (client->event_head + 1u) % CHOS_GUI_EVENT_QUEUE_CAPACITY;
            --client->event_count;
        }
    }
    tail = (client->event_head + client->event_count) % CHOS_GUI_EVENT_QUEUE_CAPACITY;
    gui_copy_bytes(&client->events[tail], event, sizeof(*event));
    ++client->event_count;
}

static void gui_app_queue_pointer(int window_id, uint32_t event_flags, uint32_t buttons) {
    gui_app_client_t *client = gui_app_find_window(window_id);
    const chos_wm_window_t *window = chos_wm_get_const(&window_manager, window_id);
    chos_gui_message_t event;
    unsigned scale = gui_scale_factor();
    int32_t local_x, local_y;
    size_t i;
    if (client == NULL || window == NULL || scale == 0u) return;
    for (i = 0u; i < sizeof(event); ++i) ((uint8_t *)&event)[i] = 0u;
    event.header.version = CHOS_GUI_ABI_VERSION;
    event.header.type = CHOS_GUI_MSG_POINTER_EVENT;
    event.header.size_bytes = (uint32_t)(offsetof(chos_gui_message_t, payload) + sizeof(event.payload.pointer_event));
    local_x = (mouse_x - window->x) / (int32_t)scale;
    local_y = (mouse_y - window->y) / (int32_t)scale - 31;
    event.payload.pointer_event.window_id = (uint32_t)window_id;
    event.payload.pointer_event.x = local_x;
    event.payload.pointer_event.y = local_y;
    event.payload.pointer_event.delta_x = 0;
    event.payload.pointer_event.delta_y = 0;
    event.payload.pointer_event.buttons = buttons;
    event.payload.pointer_event.event_flags = event_flags;
    gui_app_queue_message(client, &event);
}

static void gui_app_queue_key(int window_id, uint32_t keycode, int pressed) {
    gui_app_client_t *client = gui_app_find_window(window_id);
    chos_gui_message_t event;
    size_t i;
    if (client == NULL) return;
    for (i = 0u; i < sizeof(event); ++i) ((uint8_t *)&event)[i] = 0u;
    event.header.version = CHOS_GUI_ABI_VERSION;
    event.header.type = CHOS_GUI_MSG_KEY_EVENT;
    event.header.size_bytes = (uint32_t)(offsetof(chos_gui_message_t, payload) + sizeof(event.payload.key_event));
    event.payload.key_event.window_id = (uint32_t)window_id;
    event.payload.key_event.event_flags = pressed ? CHOS_GUI_EVENT_KEY_DOWN : CHOS_GUI_EVENT_KEY_UP;
    event.payload.key_event.keycode = keycode;
    event.payload.key_event.modifiers = (shift_down ? 1u : 0u) | (control_down ? 2u : 0u) | (caps_lock_down ? 4u : 0u);
    gui_app_queue_message(client, &event);
}

static void gui_app_queue_focus(int window_id, uint32_t event_flags) {
    gui_app_client_t *client = gui_app_find_window(window_id);
    chos_gui_message_t event;
    size_t i;
    if (client == NULL) return;
    for (i = 0u; i < sizeof(event); ++i) ((uint8_t *)&event)[i] = 0u;
    event.header.version = CHOS_GUI_ABI_VERSION;
    event.header.type = CHOS_GUI_MSG_FOCUS_EVENT;
    event.header.size_bytes = (uint32_t)(offsetof(chos_gui_message_t, payload) + sizeof(event.payload.key_event));
    event.payload.key_event.window_id = (uint32_t)window_id;
    event.payload.key_event.event_flags = event_flags;
    gui_app_queue_message(client, &event);
}

static size_t gui_expected_message_size(uint16_t type) {
    size_t payload_size;
    switch (type) {
        case CHOS_GUI_MSG_CREATE_WINDOW: payload_size = sizeof(chos_gui_create_window_t); break;
        case CHOS_GUI_MSG_DESTROY_WINDOW:
        case CHOS_GUI_MSG_SHOW_WINDOW: payload_size = sizeof(((chos_gui_message_t *)0)->payload.window); break;
        case CHOS_GUI_MSG_SET_TITLE: payload_size = sizeof(((chos_gui_message_t *)0)->payload.set_title); break;
        case CHOS_GUI_MSG_FILL_RECT: payload_size = sizeof(chos_gui_fill_rect_t); break;
        case CHOS_GUI_MSG_DRAW_TEXT: payload_size = sizeof(chos_gui_draw_text_t); break;
        case CHOS_GUI_MSG_CLEAR: payload_size = sizeof(chos_gui_clear_t); break;
        default: return 0u;
    }
    return offsetof(chos_gui_message_t, payload) + payload_size;
}

int64_t chos_gui_submit_message(uint32_t owner_pid, const chos_gui_message_t *message, size_t supplied_bytes) {
    size_t expected, i;
    gui_app_client_t *client;
    const chos_wm_window_t *window;
    unsigned scale;
    if (!gui_active || message == NULL || owner_pid == 0u) return -(int64_t)CHOS_ERRNO_EINVAL;
    if (!chos_gui_message_header_valid(&message->header, supplied_bytes) || message->header.flags != 0u)
        return -(int64_t)CHOS_ERRNO_EINVAL;
    expected = gui_expected_message_size(message->header.type);
    if (expected == 0u || message->header.size_bytes != expected || supplied_bytes != expected)
        return -(int64_t)CHOS_ERRNO_EINVAL;
    if (message->header.type == CHOS_GUI_MSG_CREATE_WINDOW) {
        const chos_gui_create_window_t *request = &message->payload.create_window;
        int slot = -1;
        int32_t x, y, width, height, max_x, max_y;
        static const char default_title[] = "CHOS Application";
        if (request->window_id != CHOS_GUI_WINDOW_ID_NONE || request->options != 0u ||
            request->x < 0 || request->y < 0 || request->width < 260u || request->height < 160u ||
            request->width > (uint32_t)GUI_LOGICAL_WIDTH || request->height > (uint32_t)(GUI_LOGICAL_HEIGHT - 40) ||
            (uint64_t)(uint32_t)request->x + request->width > (uint32_t)GUI_LOGICAL_WIDTH ||
            (uint64_t)(uint32_t)request->y + request->height > (uint32_t)(GUI_LOGICAL_HEIGHT - TASKBAR_BASE_HEIGHT))
            return -(int64_t)CHOS_ERRNO_EINVAL;
        if (gui_app_find_owner(owner_pid) != NULL) return -(int64_t)CHOS_ERRNO_EBUSY;
        for (i = 0u; i < GUI_APP_MAX_CLIENTS; ++i) if (!gui_app_clients[i].active) { slot = (int)i; break; }
        if (slot < 0) return -(int64_t)CHOS_ERRNO_ENOMEM;
        client = &gui_app_clients[(size_t)slot];
        for (i = 0u; i < sizeof(*client); ++i) ((uint8_t *)client)[i] = 0u;
        client->active = 1;
        client->owner_pid = owner_pid;
        client->window_id = GUI_APP_WINDOW_BASE + slot;
        for (i = 0u; i + 1u < sizeof(client->title) && default_title[i] != '\0'; ++i) client->title[i] = default_title[i];
        client->title[i] = '\0';
        client->background_argb = request->background_argb != 0u ? request->background_argb : C_PANEL;
        scale = gui_scale_factor(); if (scale == 0u) scale = 1u;
        x = (SCREEN_W - GUI_LOGICAL_WIDTH * (int32_t)scale) / 2 + request->x * (int32_t)scale;
        y = (SCREEN_H - GUI_LOGICAL_HEIGHT * (int32_t)scale) / 2 + request->y * (int32_t)scale;
        width = (int32_t)request->width * (int32_t)scale;
        height = (int32_t)request->height * (int32_t)scale;
        max_x = SCREEN_W - width; max_y = TASKBAR_Y - height;
        if (max_x < 0) max_x = 0;
        if (max_y < 0) max_y = 0;
        if (x < 0) x = 0;
        if (x > max_x) x = max_x;
        if (y < 0) y = 0;
        if (y > max_y) y = max_y;
        if (!chos_wm_open(&window_manager, client->window_id, client->title, x, y, width, height)) {
            for (i = 0u; i < sizeof(*client); ++i) ((uint8_t *)client)[i] = 0u;
            return -(int64_t)CHOS_ERRNO_ENOMEM;
        }
        gui_sync_active_view();
        return client->window_id;
    }

    if (message->header.type == CHOS_GUI_MSG_REPLY ||
        message->header.type >= CHOS_GUI_MSG_KEY_EVENT) return -(int64_t)CHOS_ERRNO_EINVAL;
    client = gui_app_find_owner(owner_pid);
    if (client == NULL) return -(int64_t)CHOS_ERRNO_EPERM;
    if (message->header.type == CHOS_GUI_MSG_DESTROY_WINDOW ||
        message->header.type == CHOS_GUI_MSG_SHOW_WINDOW ||
        message->header.type == CHOS_GUI_MSG_SET_TITLE ||
        message->header.type == CHOS_GUI_MSG_FILL_RECT ||
        message->header.type == CHOS_GUI_MSG_DRAW_TEXT ||
        message->header.type == CHOS_GUI_MSG_CLEAR) {
        uint32_t requested_id;
        switch (message->header.type) {
            case CHOS_GUI_MSG_DESTROY_WINDOW: requested_id = message->payload.window.window_id; break;
            case CHOS_GUI_MSG_SHOW_WINDOW: requested_id = message->payload.window.window_id; break;
            case CHOS_GUI_MSG_SET_TITLE: requested_id = message->payload.set_title.window_id; break;
            case CHOS_GUI_MSG_FILL_RECT: requested_id = message->payload.fill_rect.window_id; break;
            case CHOS_GUI_MSG_DRAW_TEXT: requested_id = message->payload.draw_text.window_id; break;
            case CHOS_GUI_MSG_CLEAR: requested_id = message->payload.clear.window_id; break;
            default: return -(int64_t)CHOS_ERRNO_EINVAL;
        }
        if (requested_id != (uint32_t)client->window_id) return -(int64_t)CHOS_ERRNO_EPERM;
    } else return -(int64_t)CHOS_ERRNO_EINVAL;

    if (message->header.type == CHOS_GUI_MSG_DESTROY_WINDOW) {
        int id = client->window_id;
        if (view == id) gui_app_queue_focus(id, CHOS_GUI_EVENT_FOCUS_LOST);
        (void)chos_wm_close(&window_manager, id);
        for (i = 0u; i < sizeof(*client); ++i) ((uint8_t *)client)[i] = 0u;
        gui_sync_active_view();
        return 0;
    }
    if (message->header.type == CHOS_GUI_MSG_SHOW_WINDOW) {
        if (!chos_wm_activate(&window_manager, client->window_id)) return -(int64_t)CHOS_ERRNO_ENOENT;
        gui_sync_active_view();
        start_menu_open = 0;
        return 0;
    }
    if (message->header.type == CHOS_GUI_MSG_SET_TITLE) {
        const chos_gui_message_t *m = message;
        size_t length = m->payload.set_title.title_length;
        if (length == 0u || length >= sizeof(client->title)) return -(int64_t)CHOS_ERRNO_EINVAL;
        for (i = 0u; i < length; ++i) client->title[i] = m->payload.set_title.title[i];
        client->title[length] = '\0';
        {
            chos_wm_window_t *mutable_window = chos_wm_get(&window_manager, client->window_id);
            if (mutable_window == NULL) return -(int64_t)CHOS_ERRNO_ENOENT;
            mutable_window->title = client->title;
        }
        gui_mark_dirty();
        return 0;
    }
    window = chos_wm_get_const(&window_manager, client->window_id);
    if (window == NULL) return -(int64_t)CHOS_ERRNO_ENOENT;
    scale = gui_scale_factor(); if (scale == 0u) scale = 1u;
    if (message->header.type == CHOS_GUI_MSG_CLEAR) {
        client->background_argb = message->payload.clear.background_argb != 0u
            ? message->payload.clear.background_argb : C_PANEL;
        client->op_count = 0u;
        gui_mark_dirty();
        return 0;
    }
    if (message->header.type == CHOS_GUI_MSG_FILL_RECT) {
        const chos_gui_fill_rect_t *r = &message->payload.fill_rect;
        int32_t logical_w = (int32_t)(window->width / (int32_t)scale) - 12;
        int32_t logical_h = (int32_t)(window->height / (int32_t)scale) - 40;
        gui_app_draw_op_t *op;
        if (r->x < 0 || r->y < 0 || r->width == 0u || r->height == 0u ||
            (int64_t)r->x + r->width > logical_w || (int64_t)r->y + r->height > logical_h)
            return -(int64_t)CHOS_ERRNO_EINVAL;
        if (client->op_count >= GUI_APP_MAX_DRAW_OPS) return -(int64_t)CHOS_ERRNO_ENOMEM;
        op = &client->ops[client->op_count++];
        op->type = CHOS_GUI_MSG_FILL_RECT; op->x = r->x; op->y = r->y;
        op->width = r->width; op->height = r->height; op->argb = r->argb | 0xff000000u;
        gui_mark_dirty();
        return 0;
    }
    if (message->header.type == CHOS_GUI_MSG_DRAW_TEXT) {
        const chos_gui_draw_text_t *r = &message->payload.draw_text;
        int32_t logical_w = (int32_t)(window->width / (int32_t)scale) - 12;
        int32_t logical_h = (int32_t)(window->height / (int32_t)scale) - 40;
        gui_app_draw_op_t *op;
        if (r->x < 0 || r->y < 0 || r->x >= logical_w || r->y >= logical_h ||
            r->text_length >= CHOS_GUI_MAX_TEXT || r->font_id > 1u) return -(int64_t)CHOS_ERRNO_EINVAL;
        if (client->op_count >= GUI_APP_MAX_DRAW_OPS) return -(int64_t)CHOS_ERRNO_ENOMEM;
        op = &client->ops[client->op_count++];
        op->type = CHOS_GUI_MSG_DRAW_TEXT; op->x = r->x; op->y = r->y;
        op->argb = r->argb | 0xff000000u; op->text_length = r->text_length;
        for (i = 0u; i < r->text_length; ++i) op->text[i] = r->text[i];
        op->text[r->text_length] = '\0';
        gui_mark_dirty();
        return 0;
    }
    return -(int64_t)CHOS_ERRNO_EINVAL;
}

int chos_gui_pop_event(uint32_t owner_pid, chos_gui_message_t *event_out) {
    gui_app_client_t *client;
    if (owner_pid == 0u || event_out == NULL) return -1;
    client = gui_app_find_owner(owner_pid);
    if (client == NULL || client->event_count == 0u) return 0;
    gui_copy_bytes(event_out, &client->events[client->event_head], sizeof(*event_out));
    client->event_head = (client->event_head + 1u) % CHOS_GUI_EVENT_QUEUE_CAPACITY;
    --client->event_count;
    return 1;
}

void chos_gui_process_exited(uint32_t owner_pid) {
    size_t i;
    int changed = 0;
    if (owner_pid == 0u) return;
    if (gui_console_pid == owner_pid) {
        gui_console_pid = 0u;
        gui_console_output_head = 0u;
        gui_console_output_count = 0u;
    }
    if (gui_file_manager_pid == owner_pid) gui_file_manager_pid = 0u;
    if (gui_browser_pid == owner_pid) gui_browser_pid = 0u;
    for (i = 0u; i < GUI_APP_MAX_CLIENTS; ++i) {
        gui_app_client_t *client = &gui_app_clients[i];
        if (client->active && client->owner_pid == owner_pid) {
            if (dragging_window_id == client->window_id) {
                gui_drag_overlay_erase();
                dragging_window_id = CHOS_WM_NO_WINDOW;
                left_button_down = 0;
            }
            if (app_pointer_capture_window_id == client->window_id)
                app_pointer_capture_window_id = CHOS_WM_NO_WINDOW;
            if (view == client->window_id)
                gui_app_queue_focus(client->window_id, CHOS_GUI_EVENT_FOCUS_LOST);
            (void)chos_wm_close(&window_manager, client->window_id);
            for (size_t byte_index = 0u; byte_index < sizeof(*client); ++byte_index)
                ((uint8_t *)client)[byte_index] = 0u;
            changed = 1;
        }
    }
    if (changed) gui_sync_active_view();
}

static void gui_draw_app_window(int id, const chos_wm_window_t *window) {
    gui_app_client_t *client = gui_app_find_window(id);
    unsigned scale = gui_scale_factor();
    int logical_w, logical_h;
    size_t i;
    int old_physical = physical_coordinates;
    if (client == NULL || window == NULL) return;
    if (scale == 0u) scale = 1u;
    logical_w = (int)(window->width / (int32_t)scale);
    logical_h = (int)(window->height / (int32_t)scale);
    if (logical_w < 160 || logical_h < 100) return;
    physical_coordinates = 0;
    draw_offset_x = window->x - GUI_WINDOW_BASE_X * (int32_t)scale;
    draw_offset_y = window->y - GUI_WINDOW_BASE_Y * (int32_t)scale;
    drawing_maximized_window = window->maximized;
    draw_window_shell(client->title, GUI_WINDOW_BASE_X, GUI_WINDOW_BASE_Y, logical_w, logical_h);
    rect(GUI_WINDOW_BASE_X + 4, GUI_WINDOW_BASE_Y + 30, logical_w - 8, logical_h - 34,
         client->background_argb);
    for (i = 0u; i < client->op_count; ++i) {
        const gui_app_draw_op_t *op = &client->ops[i];
        if (op->type == CHOS_GUI_MSG_FILL_RECT) {
            rect(GUI_WINDOW_BASE_X + 6 + op->x, GUI_WINDOW_BASE_Y + 34 + op->y,
                 (int)op->width, (int)op->height, op->argb);
        } else if (op->type == CHOS_GUI_MSG_DRAW_TEXT) {
            text(GUI_WINDOW_BASE_X + 7 + op->x, GUI_WINDOW_BASE_Y + 35 + op->y,
                 op->text, op->argb);
        }
    }
    physical_coordinates = old_physical;
}

static void gui_render(void) {
    uint64_t frame_generation=__atomic_load_n(&dirty_generation,__ATOMIC_RELAXED);
    const chos_wm_window_t *window;
    uint32_t z_order = 0u;
    int cursor_move_failed = 0;
    if (!gui_active) return;
    cursor_under_valid = 0; /* The whole scene is about to replace the save-under pixels. */
    cursor_move_pending = 0;
    drag_outline_visible = 0;
    drag_preview_dirty = 0;
    chos_gfx_reset_clip(&screen);
    draw_offset_x = 0; draw_offset_y = 0; drawing_active_window = 0; drawing_maximized_window = 0;
    draw_desktop_wallpaper();
    gui_desktop_refresh_icons();
    for (size_t desktop_index = 0u; desktop_index < desktop_icon_count; ++desktop_index)
        draw_desktop_icon(desktop_index);
    gui_draw_desktop_status();

    while ((window = chos_wm_next_in_z_order(&window_manager, z_order)) != NULL) {
        int current_view = window->id;
        z_order = window->z_order;
        drawing_maximized_window = window->maximized;
        if (window->maximized) {
            draw_offset_x = -GUI_WINDOW_BASE_X * (int32_t)gui_scale_factor();
            draw_offset_y = -GUI_WINDOW_BASE_Y * (int32_t)gui_scale_factor();
        } else {
            draw_offset_x = window->x - GUI_WINDOW_BASE_X * (int32_t)gui_scale_factor();
            draw_offset_y = window->y - GUI_WINDOW_BASE_Y * (int32_t)gui_scale_factor();
        }
        drawing_active_window = current_view == view && !window->minimized;
        chos_gfx_set_clip(&screen, window->x, window->y, window->width,
                          window->y < TASKBAR_Y ? TASKBAR_Y - window->y : 0);
        switch (current_view) {
            case VIEW_WELCOME: draw_window_shell("Welcome to CHOS", 180, 70, GUI_WINDOW_BASE_WIDTH, GUI_WINDOW_BASE_HEIGHT); draw_welcome_content(); break;
            case VIEW_CONSOLE: draw_console_content(); break;
            case VIEW_FILES: draw_files_content(); break;
            case VIEW_ABOUT: draw_about_content(); break;
            case VIEW_DIAGNOSTICS: draw_diagnostics_content(); break;
            case VIEW_SETTINGS: draw_settings_content(); break;
            case VIEW_FILE_VIEWER: draw_file_viewer_content(); break;
            case VIEW_TRASH: gui_trash_draw_content(); break;
            default: gui_draw_app_window(current_view, window); break;
        }
    }
    chos_gfx_reset_clip(&screen);
    draw_offset_x = 0; draw_offset_y = 0; drawing_active_window = 0; drawing_maximized_window = 0;
    draw_taskbar();
    if (start_menu_open) draw_start_menu();
    if (context_menu_open) gui_draw_context_menu();
    gui_draw_taskbar_tooltip();
    if (power_confirmation_action != 0) draw_power_confirmation();
    if (prompt_kind != 0) gui_draw_prompt();
    if (!virtio_gpu_cursor_available()) gui_cursor_capture_and_draw();
    if (virtio_gpu_present()) {
        ++frames_drawn;
        if (virtio_gpu_cursor_available() && !virtio_gpu_cursor_move(mouse_x, mouse_y, !splash_active))
            cursor_move_failed = 1;
    } else gui_active = 0;
    /* A hardware cursor failure disables cursorq in the driver. Keep one redraw
     * pending so the software bitmap immediately takes over instead of leaving
     * the screen cursorless until an unrelated UI action occurs. */
    presented_generation=frame_generation;
    dirty=0;
    if(cursor_move_failed) gui_mark_dirty();
}

static int gui_launch_or_focus_app(uint32_t *pid_slot, const char *path) {
    if (pid_slot == NULL || path == NULL) return 0;
    if (*pid_slot != 0u) {
        chos_process_state_t state;
        if (process_query_pid(*pid_slot, &state, NULL) == 1) {
            gui_app_client_t *client = gui_app_find_owner(*pid_slot);
            if (client != NULL && chos_wm_activate(&window_manager, client->window_id))
                gui_sync_active_view();
            return 1; /* queued process may not have created its window yet */
        }
        *pid_slot = 0u;
    }
    {
        uint32_t pid = 0u;
        if (process_spawn_path(path, &pid) == PROCESS_OK && pid != 0u) {
            *pid_slot = pid;
            start_menu_open = 0;
            gui_mark_dirty();
            return 1;
        }
    }
    return 0;
}

static void open_view(int new_view) {
    if (new_view < VIEW_WELCOME || new_view >= (int)GUI_VIEW_COUNT) new_view = VIEW_WELCOME;
    if (new_view == VIEW_CONSOLE && gui_launch_or_focus_app(&gui_console_pid, "/BIN/CONSOLE.CHX")) return;
    if (new_view == VIEW_FILES && gui_launch_or_focus_app(&gui_file_manager_pid, "/BIN/FILEMAN.CHX")) return;
    (void)gui_open_window(new_view);
}

static void activate_selected(void) {
    if (selected_icon < 0 || selected_icon >= 5) selected_icon = 0;
    open_view(icon_views[selected_icon]);
}

static void gui_render_power_status(const char *title, const char *detail) {
    int scale = (int)gui_scale_factor();
    int title_w = chos_gfx_ui_text_width(title, 1) * scale;
    int detail_w = chos_gfx_ui_text_width(detail, 0) * scale;
    int old_physical = physical_coordinates;
    physical_coordinates = 1;
    chos_gfx_reset_clip(&screen);
    draw_offset_x = 0; draw_offset_y = 0; drawing_active_window = 0;
    chos_gfx_clear(&screen, C_PANEL);
    draw_bevel(80 * scale, SCREEN_H / 2 - 75 * scale, SCREEN_W - 160 * scale, 150 * scale, C_WHITE, C_BLACK, 1);
    rect(84 * scale, SCREEN_H / 2 - 71 * scale, SCREEN_W - 168 * scale, 39 * scale, C_BLUE_DARK);
    text_bold((SCREEN_W - title_w) / 2, SCREEN_H / 2 - 61 * scale, title, C_WHITE);
    text((SCREEN_W - detail_w) / 2, SCREEN_H / 2 - 4 * scale, detail, C_TEXT);
    physical_coordinates = old_physical;
    if (virtio_gpu_present()) ++frames_drawn;
}

static void gui_confirm_power_action(void) {
    int action = power_confirmation_action;
    power_confirmation_action = 0;
    start_menu_open = 0;
    start_menu_selected_item = -1;
    if (action == CHOS_POWER_RESTART) {
        gui_render_power_status("Restarting CHOS...", "Please wait while the kernel and devices restart.");
        uart_puts("[POWER] Restart requested from the graphical desktop.\n");
        chos_power_execute(CHOS_POWER_RESTART);
    }
    gui_render_power_status("CHOS has halted", "Close the QEMU window to exit CHOS.");
    chos_power_execute(CHOS_POWER_SHUTDOWN);
}

static void gui_handle_power_confirmation_click(int32_t x, int32_t y) {
    const int scale = (int)gui_scale_factor();
    const int dialog_w = 390 * scale;
    const int dialog_h = 176 * scale;
    const int dialog_x = (SCREEN_W - dialog_w) / 2;
    const int dialog_y = (SCREEN_H - dialog_h) / 2;
    if (y >= dialog_y + 137 * scale && y < dialog_y + 165 * scale) {
        if (x >= dialog_x + 172 * scale && x < dialog_x + 266 * scale) gui_confirm_power_action();
        else if (x >= dialog_x + 276 * scale && x < dialog_x + 368 * scale) {
            power_confirmation_action = 0;
            gui_mark_dirty();
        }
    }
}

static void click_at(int32_t x, int32_t y, int in_window) {
    if (!in_window && start_menu_open) {
        int scale = (int)gui_scale_factor();
        int menu_top = TASKBAR_Y - START_MENU_HEIGHT;
        if (x >= 7 * scale && x < 233 * scale && y >= menu_top && y < TASKBAR_Y) {
            int relative_y = y - menu_top;
            if (x >= 54 * scale && x < 225 * scale && relative_y >= 42 * scale && relative_y < 228 * scale) {
                int idx = (relative_y - 42 * scale) / (31 * scale);
                if (idx >= 0 && idx < 6) {
                    gui_start_menu_activate_item(idx);
                    return;
                }
            }
            if (relative_y >= 242 * scale && relative_y < 276 * scale) {
                if (x >= 54 * scale && x < 135 * scale) power_confirmation_action = CHOS_POWER_SHUTDOWN;
                else if (x >= 140 * scale && x < 225 * scale) power_confirmation_action = CHOS_POWER_RESTART;
                if (power_confirmation_action != 0) {
                    start_menu_open = 0;
                    start_menu_selected_item = -1;
                    gui_mark_dirty();
                    return;
                }
            }
            start_menu_open = 0;
            start_menu_selected_item = -1;
            gui_mark_dirty();
            return;
        }
        start_menu_open = 0;
        start_menu_selected_item = -1;
        gui_mark_dirty();
        return;
    }
    if (!in_window && y >= TASKBAR_Y && x >= 0 && x < 66 * (int)gui_scale_factor()) {
        start_menu_open = 1;
        context_menu_open = 0;
        if (start_menu_selected_item < 0 || start_menu_selected_item >= 6) start_menu_selected_item = 0;
        gui_start_menu_update_hover(); gui_mark_dirty(); return;
    }
    if (!in_window && y >= TASKBAR_Y && y < SCREEN_H && x >= 70 * (int)gui_scale_factor() &&
        x < SCREEN_W - 84 * (int)gui_scale_factor()) {
        int scale = (int)gui_scale_factor();
        int relative_x = x - 70 * scale;
        int slot_width = TASKBAR_APP_BUTTON_STEP * scale;
        int slot_offset = relative_x % slot_width;
        size_t task = (size_t)(relative_x / slot_width);
        int slot_x = 70 * scale + (int)task * slot_width;
        int clock_x = SCREEN_W - 82 * scale;
        if (slot_offset < TASKBAR_APP_BUTTON_WIDTH * scale &&
            slot_x + TASKBAR_APP_BUTTON_WIDTH * scale <= clock_x - 4 * scale) {
            int target = gui_taskbar_view_at(task);
            if (target != CHOS_WM_NO_WINDOW) {
                if (target >= GUI_APP_WINDOW_BASE) {
                    if (chos_wm_activate(&window_manager, target)) gui_sync_active_view();
                } else {
                    (void)gui_open_window(target);
                }
            }
        }
        return;
    }
    if (view == VIEW_WELCOME && y >= 248 && y < 420) {
        if (x >= 205 && x < 466 && y < 330) open_view(VIEW_CONSOLE);
        else if (x >= 480 && x < 752 && y < 330) open_view(VIEW_FILES);
        else if (x >= 205 && x < 466 && y >= 339) open_view(VIEW_DIAGNOSTICS);
        else if (x >= 480 && x < 752 && y >= 339) open_view(VIEW_SETTINGS);
        return;
    }
    if (view == VIEW_TRASH) {
        size_t count = gui_trash_count();
        if (y >= 160 && y < 390) {
            size_t row = (size_t)(y - 160) / 23u;
            size_t index = trash_scroll_offset + row;
            if (row < 10u && index < count) { trash_selected_index = index; gui_mark_dirty(); return; }
        }
        if (y >= 475 && y < 503) {
            chos_chfs_entry_t entry;
            if (x >= 204 && x < 286) {
                if (gui_trash_entry_at(trash_selected_index, &entry)) {
                    if (gui_restore_from_trash(entry.path)) {
                        desktop_icons_dirty = 1; gui_desktop_refresh_icons(); gui_desktop_save_layout();
                        if (trash_selected_index >= gui_trash_count() && trash_selected_index > 0u) --trash_selected_index;
                    } else gui_desktop_status("Could not restore item; a name may already exist.");
                }
                return;
            }
            if (x >= 291 && x < 382) {
                if (gui_trash_entry_at(trash_selected_index, &entry)) gui_prompt_open(GUI_PROMPT_CONFIRM_DELETE, entry.path);
                return;
            }
            if (x >= 387 && x < 487) { gui_prompt_open(GUI_PROMPT_EMPTY_TRASH, NULL); return; }
            if (x >= 492 && x < 586) { gui_close_window(VIEW_TRASH); return; }
        }
        return;
    }
    if (view == VIEW_FILES) {
        size_t count = file_manager_entry_count();
        if (y >= 162 && y < 395) {
            size_t row = (size_t)((y - 164) / 21);
            size_t index = file_scroll_offset + row;
            if (y >= 164 && index < count && row < FILE_VIEW_ROWS) { file_selected_index = index; gui_mark_dirty(); return; }
        }
        if (y >= 408 && y < 441) {
            if (x >= 198 && x < 276) { file_manager_open_selected(0); return; }
            if (x >= 282 && x < 360) { file_manager_open_selected(1); return; }
            if (x >= 490 && x < 564 && file_scroll_offset > 0u) { --file_scroll_offset; gui_mark_dirty(); return; }
            if (x >= 570 && x < 644 && file_scroll_offset + FILE_VIEW_ROWS < count) { ++file_scroll_offset; gui_mark_dirty(); return; }
        }
    }
    if (view == VIEW_FILE_VIEWER && x >= 204 && x < 378 && y >= 475 && y < 503) { open_view(VIEW_FILES); return; }
    if (view == VIEW_FILE_VIEWER && viewer_editable && x >= 390 && x < 470 && y >= 475 && y < 503) { gui_text_editor_save(); return; }
    if (view == VIEW_SETTINGS) {
        if (y >= 151 && y < 194 && x >= 205 && x < 343) {
            wallpaper_alt = 0; wallpaper_cache_valid = 0; gui_save_settings(); gui_mark_dirty();
        } else if (y >= 151 && y < 194 && x >= 354 && x < 492) {
            wallpaper_alt = 1; wallpaper_cache_valid = 0; gui_save_settings(); gui_mark_dirty();
        } else if (y >= 151 && y < 194 && x >= 510 && x < 615) {
            startup_sound_enabled = !startup_sound_enabled; gui_save_settings(); gui_mark_dirty();
        } else if (y >= 151 && y < 194 && x >= 620 && x < 734) {
            timezone_index = (timezone_index + 1u) % GUI_TIMEZONE_COUNT;
            gui_save_settings(); gui_mark_dirty();
        } else if (y >= 261 && y < 399 && x >= 205 && x < 539) {
            size_t row = (size_t)(y - 261) / 28u;
            size_t index = display_mode_page * DISPLAY_MODES_PER_PAGE + row;
            if (row < DISPLAY_MODES_PER_PAGE && index < chos_display_mode_count())
                (void)gui_apply_display_mode(index);
        } else if (y >= 261 && y < 288 && x >= 548 && x < 633 && display_mode_page > 0u) {
            --display_mode_page; gui_mark_dirty();
        } else if (y >= 261 && y < 288 && x >= 640 && x < 730 &&
                   (display_mode_page + 1u) * DISPLAY_MODES_PER_PAGE < chos_display_mode_count()) {
            ++display_mode_page; gui_mark_dirty();
        }
    }
}

static void gui_status_hex4(char *out, uint16_t value) {
    static const char digits[] = "0123456789ABCDEF";
    out[0] = digits[(value >> 12u) & 0x0fu];
    out[1] = digits[(value >> 8u) & 0x0fu];
    out[2] = digits[(value >> 4u) & 0x0fu];
    out[3] = digits[value & 0x0fu];
    out[4] = '\0';
}

static void gui_status_hex8(char *out, uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    for (uint32_t i = 0u; i < 8u; ++i)
        out[i] = digits[(value >> ((7u - i) * 4u)) & 0x0fu];
    out[8] = '\0';
}

void chos_gui_set_pci_input_status(uint32_t pci_devices, uint32_t xhci_count,
                                   uint16_t xhci_vendor, uint16_t xhci_device,
                                   uint32_t xhci_bar0, uint32_t xhci_bar1) {
    platform_pci_status_valid = 1;
    platform_pci_device_count = pci_devices;
    platform_xhci_count = xhci_count;
    if (xhci_count != 0u) {
        gui_status_hex4(platform_xhci_id, xhci_vendor);
        platform_xhci_id[4] = ':';
        gui_status_hex4(platform_xhci_id + 5, xhci_device);
        gui_status_hex8(platform_xhci_bars, xhci_bar0);
        platform_xhci_bars[8] = ' ';
        platform_xhci_bars[9] = '/';
        platform_xhci_bars[10] = ' ';
        gui_status_hex8(platform_xhci_bars + 11, xhci_bar1);
    } else {
        const char missing[] = "not found";
        const char no_bars[] = "not available";
        size_t i;
        for (i = 0u; i < sizeof(missing); ++i) platform_xhci_id[i] = missing[i];
        for (i = 0u; i < sizeof(no_bars); ++i) platform_xhci_bars[i] = no_bars[i];
    }
    if (gui_active) gui_mark_dirty();
}

int chos_gui_init(const chos_surface_t *surface) {
    if (surface == NULL || !chos_surface_valid(surface) ||
        chos_display_mode_index((uint32_t)surface->width, (uint32_t)surface->height) < 0 ||
        surface->stride_pixels < surface->width) return 0;
    screen = *surface;
    gui_active = 1;
    /* Prefer the VirtIO-GPU cursorq plane. If queue 1 is unavailable or the
     * host rejects cursor resources, the old software bitmap remains a fallback. */
    (void)virtio_gpu_cursor_init(chos_asset_cursor_pixels, chos_asset_cursor.width,
                                chos_asset_cursor.height, 0u, 0u);
    view = VIEW_WELCOME;
    for (size_t client_index = 0u; client_index < GUI_APP_MAX_CLIENTS; ++client_index) {
        for (size_t byte_index = 0u; byte_index < sizeof(gui_app_clients[client_index]); ++byte_index)
            ((uint8_t *)&gui_app_clients[client_index])[byte_index] = 0u;
    }
    chos_wm_init(&window_manager);
    window_manager_initialized = 1;
    (void)chos_wm_open(&window_manager, VIEW_WELCOME, "Welcome to CHOS",
                       gui_default_window_x(VIEW_WELCOME), gui_default_window_y(VIEW_WELCOME),
                       GUI_WINDOW_WIDTH, GUI_WINDOW_HEIGHT);
    draw_offset_x = 0; draw_offset_y = 0; drawing_active_window = 0;
    dragging_window_id = CHOS_WM_NO_WINDOW;
    drag_preview_dirty = 0; drag_outline_visible = 0;
    drag_preview_x = drag_preview_y = 0;
    left_button_down = 0;
    taskbar_tooltip_window_id = CHOS_WM_NO_WINDOW;
    taskbar_tooltip_started_tick = 0u;
    taskbar_tooltip_fade_step = 0u;
    taskbar_tooltip_anchor_x = mouse_x;
    taskbar_tooltip_anchor_y = mouse_y;
    app_pointer_capture_window_id = CHOS_WM_NO_WINDOW;
    cursor_under_valid = 0;
    cursor_move_pending = 0;
    clock_last_minute = 0u; clock_last_poll_tick = 0u; clock_initialized = 0;
    selected_icon = 0;
    selected_desktop_icon = -1;
    start_menu_selected_item = -1;
    start_menu_open = 0;
    desktop_icons_dirty = 1;
    desktop_layout_loaded = 0;
    desktop_icon_count = 0u;
    desktop_drag_index = -1; desktop_drag_moved = 0; desktop_drag_current_cell = 0;
    last_desktop_click_index = -1; last_desktop_click_tick = 0u;
    context_menu_open = 0; context_menu_kind = GUI_CONTEXT_DESKTOP; context_menu_submenu = 0;
    context_menu_icon_index = -1; context_menu_hover_index = -1;
    prompt_kind = 0; prompt_error = 0; prompt_name[0] = '\0'; prompt_name_length = 0u;
    viewer_editable = 0; viewer_unsaved = 0; viewer_save_error = 0;
    trash_selected_index = 0u; trash_scroll_offset = 0u;
    desktop_status_message[0] = '\0'; desktop_status_until_tick = 0u;
    power_confirmation_action = 0;
    display_mode_error = 0;
    splash_startup_complete = 0;
    wallpaper_alt = 0;
    wallpaper_cache_valid = 0;
    startup_sound_enabled = 1;
    startup_chime_pending = 0; startup_chime_queued = 0; startup_chime_attempts = 0u;
    startup_chime_init_attempts = 0u;
    startup_chime_next_probe_ms = 0u;
    settings_save_error = 0;
    settings_loading = 1;
    {
        unsigned saved_mode = gui_load_settings();
        display_mode_page = saved_mode / DISPLAY_MODES_PER_PAGE;
        if (saved_mode != virtio_gpu_current_mode_index()) (void)gui_apply_display_mode(saved_mode);
    }
    settings_loading = 0;
    terminal_initialized = 0;
    gui_console_pid = 0u;
    gui_file_manager_pid = 0u;
    gui_browser_pid = 0u;
    gui_console_output_head = 0u;
    gui_console_output_count = 0u;
    terminal_history_count = 0u;
    terminal_history_position = 0;
    terminal_command[0] = '\0';
    terminal_cwd[0] = '/'; terminal_cwd[1] = '\0';
    file_selected_index = 0u;
    file_scroll_offset = 0u;
    shift_down = 0; control_down = 0; caps_lock_down = 0;
    frames_drawn = 0u;
    viewer_scroll_offset = 0u;
    viewer_load_error = 0;
    viewer_empty_file = 0;
    splash_active = 1;
    splash_started_tick = timer_is_ready() ? timer_ticks() : 0u;
    splash_last_animation_tick = splash_started_tick;
    splash_started_counter = timer_counter();
    splash_last_animation_counter = splash_started_counter;
    splash_counter_frequency = timer_frequency_hz();
    /* Keep the first cursor out of the welcome text and entirely on-screen. */
    {
        int cursor_scale = (int)gui_scale_factor();
        mouse_x = SCREEN_W - GUI_CURSOR_BOX_WIDTH * cursor_scale - 2;
    }
    mouse_y = 10;
    left_button_down = 0;
    gui_mark_dirty();
    gui_render_splash();
    return gui_active && frames_drawn > 0u && virtio_gpu_ready();
}

void chos_gui_show(void) {
    if (!gui_active) return;
    gui_clock_poll();
    gui_taskbar_tooltip_tick();
    if (splash_active) {
        gui_mark_dirty();
        chos_gui_pump();
        return;
    }
    (void)gui_open_window(VIEW_WELCOME);
    gui_mark_dirty();
    gui_render();
}

void chos_gui_startup_complete(void) {
    splash_startup_complete = 1;
    gui_mark_dirty();
}

void chos_gui_input_key_unicode(uint16_t keycode, uint16_t unicode_char, int pressed) {
    char previous = unicode_key_override;
    unicode_key_override = (pressed && unicode_char >= 0x20u && unicode_char <= 0x7eu)
                         ? (char)unicode_char : '\0';
    chos_gui_input_key(keycode, pressed);
    unicode_key_override = previous;
}

void chos_gui_input_key(uint16_t keycode, int pressed) {
    /* Keep modifier state current even while the splash intentionally ignores
     * input. Otherwise a Shift release during startup can leave Shift latched. */
    if (keycode == 42u || keycode == 54u) shift_down = pressed != 0;
    else if (keycode == 29u || keycode == 97u) control_down = pressed != 0;
    else if (keycode == 58u && pressed) caps_lock_down = !caps_lock_down;
    if (!gui_active) return;
    if (splash_active) return; /* Input must not bypass the minimum splash interval. */
    if (power_confirmation_action != 0 && pressed) {
        if (keycode == 1u) { power_confirmation_action = 0; gui_mark_dirty(); }
        else if (keycode == 28u) gui_confirm_power_action();
        return;
    }
    if (prompt_kind != 0) {
        char ch;
        if (!pressed) return;
        if (keycode == 1u) { prompt_kind = 0; prompt_error = 0; gui_mark_dirty(); return; }
        if (keycode == 28u) { gui_prompt_submit(); return; }
        if (prompt_kind >= GUI_PROMPT_CONFIRM_DELETE) return;
        if (keycode == 14u) {
            if (prompt_name_length > 0u) prompt_name[--prompt_name_length] = '\0';
            prompt_error = 0; gui_mark_dirty(); return;
        }
        ch = terminal_key_to_ascii(keycode);
        if (ch >= 32 && ch <= 126 && prompt_name_length + 1u < sizeof(prompt_name)) {
            prompt_name[prompt_name_length++] = ch; prompt_name[prompt_name_length] = '\0';
            prompt_error = 0; gui_mark_dirty();
        }
        return;
    }
    if (start_menu_open && pressed) {
        if (keycode == 1u) { start_menu_open = 0; start_menu_selected_item = -1; gui_mark_dirty(); return; }
        if (keycode == 103u || keycode == 105u) {
            start_menu_selected_item = (start_menu_selected_item + 5) % 6; gui_mark_dirty(); return;
        }
        if (keycode == 108u || keycode == 106u) {
            start_menu_selected_item = (start_menu_selected_item + 1) % 6; gui_mark_dirty(); return;
        }
        if (keycode == 28u || keycode == 57u) { gui_start_menu_activate_item(start_menu_selected_item); return; }
    }
    if (view >= GUI_APP_WINDOW_BASE && gui_app_find_window(view) != NULL) {
        gui_app_queue_key(view, keycode, pressed);
        return;
    }
    if (view == VIEW_CONSOLE) { (void)terminal_handle_key(keycode, pressed); gui_mark_dirty(); return; }
    if (!pressed) return;
    if (view == VIEW_FILE_VIEWER && viewer_editable) {
        char ch;
        if (keycode == 1u) { if (viewer_unsaved && !gui_text_editor_save()) return; gui_close_window(VIEW_FILE_VIEWER); return; }
        if (control_down && keycode == 31u) { gui_text_editor_save(); return; }
        /* Preserve normal document navigation while editing, including the
           keyboard scroll behavior shared with the read-only viewer. */
        if (keycode == 103u) { viewer_scroll_by(-1, 1u); return; }
        if (keycode == 108u) { viewer_scroll_by(1, 1u); return; }
        if (keycode == 104u) { viewer_scroll_by(-1, VIEWER_VISIBLE_LINES > 1u ? VIEWER_VISIBLE_LINES - 1u : 1u); return; }
        if (keycode == 109u) { viewer_scroll_by(1, VIEWER_VISIBLE_LINES > 1u ? VIEWER_VISIBLE_LINES - 1u : 1u); return; }
        if (keycode == 102u) { viewer_scroll_offset = 0u; gui_mark_dirty(); return; }
        if (keycode == 107u) { viewer_scroll_offset = viewer_max_scroll(); gui_mark_dirty(); return; }
        if (keycode == 14u) {
            if (viewer_content_size > 0u) {
                viewer_content[--viewer_content_size] = '\0';
                viewer_unsaved = 1; viewer_empty_file = viewer_content_size == 0u; viewer_scroll_offset = viewer_max_scroll(); gui_mark_dirty();
            }
            return;
        }
        if (keycode == 28u) ch = '\n';
        else ch = terminal_key_to_ascii(keycode);
        if ((ch >= 32 && ch <= 126) || ch == '\n') {
            if (viewer_content_size + 1u < sizeof(viewer_content)) {
                viewer_content[viewer_content_size++] = ch;
                viewer_content[viewer_content_size] = '\0';
                viewer_unsaved = 1; viewer_empty_file = 0; viewer_scroll_offset = viewer_max_scroll(); gui_mark_dirty();
            } else { viewer_save_error = 1; gui_mark_dirty(); }
        }
        return;
    }
    if (view == VIEW_FILE_VIEWER) {
        if (keycode == 1u) { gui_close_window(VIEW_FILE_VIEWER); return; }
        if (keycode == 103u) { viewer_scroll_by(-1, 1u); return; } /* Up */
        if (keycode == 108u) { viewer_scroll_by(1, 1u); return; }  /* Down */
        if (keycode == 104u) { viewer_scroll_by(-1, VIEWER_VISIBLE_LINES > 1u ? VIEWER_VISIBLE_LINES - 1u : 1u); return; }
        if (keycode == 109u) { viewer_scroll_by(1, VIEWER_VISIBLE_LINES > 1u ? VIEWER_VISIBLE_LINES - 1u : 1u); return; }
        if (keycode == 102u) { viewer_scroll_offset = 0u; gui_mark_dirty(); return; } /* Home */
        if (keycode == 107u) { viewer_scroll_offset = viewer_max_scroll(); gui_mark_dirty(); return; } /* End */
    }
    if (view == VIEW_FILES) {
        size_t count = file_manager_entry_count();
        if (keycode == 103u && file_selected_index > 0u) { --file_selected_index; if (file_selected_index < file_scroll_offset) file_scroll_offset = file_selected_index; gui_mark_dirty(); return; }
        if (keycode == 108u && file_selected_index + 1u < count) { ++file_selected_index; if (file_selected_index >= file_scroll_offset + FILE_VIEW_ROWS) file_scroll_offset = file_selected_index - FILE_VIEW_ROWS + 1u; gui_mark_dirty(); return; }
        if (keycode == 28u) { file_manager_open_selected(0); return; }
    }
    if (keycode == 15u) { selected_icon = (selected_icon + 1) % 5; gui_mark_dirty(); }
    else if (keycode == 28u || keycode == 57u) activate_selected();
    else if (keycode == 1u) { if (start_menu_open) { start_menu_open = 0; gui_mark_dirty(); } else gui_minimize_window(view); }
    else if (keycode == 59u) open_view(VIEW_DIAGNOSTICS);
    else if (keycode == 46u) open_view(VIEW_CONSOLE);
    else if (keycode == 33u) open_view(VIEW_FILES);
    else if (keycode == 32u) open_view(VIEW_DIAGNOSTICS);
    else if (keycode == 31u) open_view(VIEW_SETTINGS);
    else if (keycode == 30u) open_view(VIEW_ABOUT);
    else if (keycode == 103u || keycode == 105u) { selected_icon = (selected_icon + 4) % 5; gui_mark_dirty(); }
    else if (keycode == 108u || keycode == 106u) { selected_icon = (selected_icon + 1) % 5; gui_mark_dirty(); }
}

static void update_drag_position(void) {
    const chos_wm_window_t *window;
    int32_t x, y, max_x, max_y;
    if (!left_button_down) {
        dragging_window_id = CHOS_WM_NO_WINDOW;
        drag_preview_dirty = 0;
        return;
    }
    if (dragging_window_id == CHOS_WM_NO_WINDOW) return;
    window = chos_wm_get_const(&window_manager, dragging_window_id);
    if (window == NULL) {
        dragging_window_id = CHOS_WM_NO_WINDOW;
        drag_preview_dirty = 0;
        return;
    }
    x = mouse_x - drag_offset_x;
    y = mouse_y - drag_offset_y;
    max_x = SCREEN_W - window->width;
    max_y = TASKBAR_Y - window->height;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (x < 0) x = 0;
    if (x > max_x) x = max_x;
    if (y < 0) y = 0;
    if (y > max_y) y = max_y;
    if (x != drag_preview_x || y != drag_preview_y) {
        drag_preview_x = x;
        drag_preview_y = y;
        drag_preview_dirty = 1;
    }
}

void chos_gui_input_pointer_relative(int32_t dx, int32_t dy) {
    int was_dirty, old_x, old_y;
    if (!gui_active || splash_active) return;
    if (dx > 160) dx = 160;
    if (dx < -160) dx = -160;
    if (dy > 160) dy = 160;
    if (dy < -160) dy = -160;
    was_dirty = dirty; old_x = mouse_x; old_y = mouse_y;
    mouse_x += dx; mouse_y += dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x >= SCREEN_W) mouse_x = SCREEN_W - 1;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= SCREEN_H) mouse_y = SCREEN_H - 1;
    if (mouse_x == old_x && mouse_y == old_y) return;
    gui_taskbar_tooltip_update_hover();
    gui_start_menu_update_hover();
    gui_context_menu_update_hover();
    if (desktop_drag_index >= 0 && left_button_down) {
        int32_t dx = mouse_x - desktop_drag_start_x, dy = mouse_y - desktop_drag_start_y;
        int threshold = 5 * (int)gui_scale_factor();
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx > threshold || dy > threshold) desktop_drag_moved = 1;
        if (desktop_drag_moved) {
            int next_cell = (int)gui_desktop_cell_at(mouse_x, mouse_y);
            if (next_cell != desktop_drag_current_cell) { desktop_drag_current_cell = next_cell; gui_mark_dirty(); }
        }
    }
    update_drag_position();
    if (left_button_down && dragging_window_id != CHOS_WM_NO_WINDOW) {
        cursor_move_pending = virtio_gpu_cursor_available() ? 1 : 0;
        return; /* Outline updates remain small; hardware cursor is moved by the pump. */
    }
    {
        int hit = chos_wm_hit_test(&window_manager, mouse_x, mouse_y);
        if (hit >= GUI_APP_WINDOW_BASE && gui_app_find_window(hit) != NULL)
            gui_app_queue_pointer(hit, CHOS_GUI_EVENT_POINTER_MOVE, left_button_down ? 1u : 0u);
    }
    if (!was_dirty && !dirty && !left_button_down && dragging_window_id == CHOS_WM_NO_WINDOW && !splash_active) {
        cursor_move_pending = 1; /* one cursor update after the whole VirtIO input batch */
        return;
    }
    cursor_move_pending = 0;
    gui_mark_dirty();
}
void chos_gui_input_pointer_absolute(int32_t x, int32_t y) {
    int was_dirty, old_x, old_y;
    if (!gui_active || splash_active) return;
    was_dirty = dirty; old_x = mouse_x; old_y = mouse_y;
    if (x >= 0) mouse_x = (int32_t)(((uint64_t)(uint32_t)x * (uint32_t)(SCREEN_W - 1)) / 32767u);
    if (y >= 0) mouse_y = (int32_t)(((uint64_t)(uint32_t)y * (uint32_t)(SCREEN_H - 1)) / 32767u);
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x >= SCREEN_W) mouse_x = SCREEN_W - 1;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= SCREEN_H) mouse_y = SCREEN_H - 1;
    if (mouse_x == old_x && mouse_y == old_y) return;
    gui_taskbar_tooltip_update_hover();
    gui_start_menu_update_hover();
    gui_context_menu_update_hover();
    if (desktop_drag_index >= 0 && left_button_down) {
        int32_t dx = mouse_x - desktop_drag_start_x, dy = mouse_y - desktop_drag_start_y;
        int threshold = 5 * (int)gui_scale_factor();
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx > threshold || dy > threshold) desktop_drag_moved = 1;
        if (desktop_drag_moved) {
            int next_cell = (int)gui_desktop_cell_at(mouse_x, mouse_y);
            if (next_cell != desktop_drag_current_cell) { desktop_drag_current_cell = next_cell; gui_mark_dirty(); }
        }
    }
    update_drag_position();
    if (left_button_down && dragging_window_id != CHOS_WM_NO_WINDOW) {
        cursor_move_pending = virtio_gpu_cursor_available() ? 1 : 0;
        return; /* Outline updates remain small; hardware cursor is moved by the pump. */
    }
    {
        int hit = chos_wm_hit_test(&window_manager, mouse_x, mouse_y);
        if (hit >= GUI_APP_WINDOW_BASE && gui_app_find_window(hit) != NULL)
            gui_app_queue_pointer(hit, CHOS_GUI_EVENT_POINTER_MOVE, left_button_down ? 1u : 0u);
    }
    if (!was_dirty && !dirty && !left_button_down && dragging_window_id == CHOS_WM_NO_WINDOW && !splash_active) {
        cursor_move_pending = 1; /* one cursor update after the whole VirtIO input batch */
        return;
    }
    cursor_move_pending = 0;
    gui_mark_dirty();
}

void chos_gui_input_scroll(int32_t lines) {
    size_t amount;
    if (!gui_active || splash_active || lines == 0) return;
    if (view == VIEW_TRASH) {
        size_t count = gui_trash_count();
        if (lines > 20) lines = 20;
        if (lines < -20) lines = -20;
        if (lines < 0) {
            amount = (size_t)(-lines);
            if (trash_selected_index > amount) trash_selected_index -= amount; else trash_selected_index = 0u;
        } else {
            amount = (size_t)lines;
            if (trash_selected_index + amount < count) trash_selected_index += amount;
            else if (count != 0u) trash_selected_index = count - 1u;
        }
        if (trash_selected_index < trash_scroll_offset) trash_scroll_offset = trash_selected_index;
        if (trash_selected_index >= trash_scroll_offset + 10u) trash_scroll_offset = trash_selected_index - 9u;
        gui_mark_dirty(); return;
    }
    if (view != VIEW_FILE_VIEWER) return;
    if (lines > 20) lines = 20;
    if (lines < -20) lines = -20;
    amount = (size_t)(lines < 0 ? -lines : lines) * 3u;
    viewer_scroll_by(lines < 0 ? -1 : 1, amount);
}

void chos_gui_input_button(uint16_t button, int pressed) {
    int hit;
    if (!gui_active) return;
    if (button == 0x111u) {
        if (pressed && !splash_active && prompt_kind == 0 && power_confirmation_action == 0 && !start_menu_open && mouse_y < TASKBAR_Y) {
            int right_hit = chos_wm_hit_test(&window_manager, mouse_x, mouse_y);
            if (right_hit == CHOS_WM_NO_WINDOW) gui_context_menu_open_at(mouse_x, mouse_y);
            else if (right_hit >= GUI_APP_WINDOW_BASE && gui_app_find_window(right_hit) != NULL) {
                (void)chos_wm_activate(&window_manager, right_hit);
                gui_set_active_view(right_hit);
                gui_app_queue_pointer(right_hit, CHOS_GUI_EVENT_POINTER_DOWN, 2u);
                gui_mark_dirty();
            }
        }
        return;
    }
    if (button != 0x110u) return;
    if (!pressed) {
        if (desktop_drag_index >= 0 && left_button_down) {
            int index = desktop_drag_index;
            if (desktop_drag_moved && index >= 0 && (size_t)index < desktop_icon_count) {
                unsigned old_cell = desktop_icons[index].cell;
                unsigned target_cell = (unsigned)desktop_drag_current_cell;
                int target_index = gui_desktop_icon_at(mouse_x, mouse_y);
                int moved_into_folder = 0;
                if (target_index >= 0) target_cell = desktop_icons[target_index].cell;
                if (target_index >= 0 && target_index != index &&
                    (desktop_icons[target_index].kind == GUI_DESKTOP_DIRECTORY ||
                     desktop_icons[target_index].kind == GUI_DESKTOP_DOCUMENTS) &&
                    desktop_icons[index].kind != GUI_DESKTOP_APP &&
                    desktop_icons[index].kind != GUI_DESKTOP_TRASH) {
                    moved_into_folder = gui_desktop_move_into_folder(index, target_index);
                }
                if (moved_into_folder == 0) {
                    size_t j;
                    int has_target = 0;
                    for (j = 0u; j < desktop_icon_count; ++j)
                        if ((int)j != index && desktop_icons[j].cell == target_cell) { has_target = 1; break; }
                    /* A folder drop that was rejected must not silently turn into
                     * a positional swap; only empty-space/app/file drops reposition. */
                    if (moved_into_folder == -1) {
                        /* The helper already supplied the actionable error. */
                    } else if (has_target && target_index >= 0 &&
                               (desktop_icons[target_index].kind == GUI_DESKTOP_DIRECTORY ||
                                desktop_icons[target_index].kind == GUI_DESKTOP_DOCUMENTS) &&
                               desktop_icons[index].kind != GUI_DESKTOP_APP &&
                               desktop_icons[index].kind != GUI_DESKTOP_TRASH) {
                        /* Dropped directly on a folder: never treat it as a grid swap. */
                    } else {
                        for (j = 0u; j < desktop_icon_count; ++j) {
                            if ((int)j != index && desktop_icons[j].cell == target_cell) {
                                desktop_icons[j].cell = old_cell;
                                break;
                            }
                        }
                        desktop_icons[index].cell = target_cell;
                        selected_desktop_icon = index;
                        gui_desktop_save_layout();
                        gui_desktop_status("Desktop icon position saved.");
                    }
                }
            } else if (index >= 0 && (size_t)index < desktop_icon_count) {
                uint64_t now = timer_is_ready() ? timer_ticks() : 0u;
                if (index == last_desktop_click_index && now >= last_desktop_click_tick &&
                    now - last_desktop_click_tick <= 45u) {
                    last_desktop_click_index = -1;
                    gui_desktop_activate(index);
                } else {
                    last_desktop_click_index = index;
                    last_desktop_click_tick = now;
                }
            }
            desktop_drag_index = -1;
            desktop_drag_moved = 0;
            left_button_down = 0;
            gui_mark_dirty();
            return;
        }
        /* Commit the preview once on release. During the drag only the outline
         * and pointer regions are transferred; the full scene redraw happens
         * once now, after geometry is committed. */
        if (dragging_window_id != CHOS_WM_NO_WINDOW && left_button_down) {
            int released_id = dragging_window_id;
            chos_wm_window_t *window = chos_wm_get(&window_manager, released_id);
            gui_drag_overlay_erase();
            if (window != NULL) {
                (void)chos_wm_move(&window_manager, released_id, drag_preview_x, drag_preview_y,
                                   0, 0, SCREEN_W - window->width, TASKBAR_Y - window->height);
            }
            dragging_window_id = CHOS_WM_NO_WINDOW;
            drag_outline_visible = 0;
            drag_preview_dirty = 0;
            left_button_down = 0;
            if (gui_active) gui_mark_dirty();
            return;
        }
        /* Deliver release to the app that received the matching press, even if
         * the pointer was moved outside its client area in the meantime. */
        if (app_pointer_capture_window_id != CHOS_WM_NO_WINDOW) {
            gui_app_queue_pointer(app_pointer_capture_window_id, CHOS_GUI_EVENT_POINTER_UP, 0u);
            app_pointer_capture_window_id = CHOS_WM_NO_WINDOW;
        }
        /* Always clear capture if the device emits a stray/repeated release. */
        dragging_window_id = CHOS_WM_NO_WINDOW;
        drag_outline_visible = 0;
        drag_preview_dirty = 0;
        left_button_down = 0;
        if (gui_active) gui_mark_dirty();
        return;
    }
    if (splash_active || left_button_down) return;
    if (prompt_kind != 0) {
        const int scale = (int)gui_scale_factor();
        const int width = 420 * scale, height = 186 * scale;
        const int x = (SCREEN_W - width) / 2, y = (TASKBAR_Y - height) / 2;
        if (mouse_y >= y + 146 * scale && mouse_y < y + 174 * scale) {
            if (mouse_x >= x + 214 * scale && mouse_x < x + 306 * scale) gui_prompt_submit();
            else if (mouse_x >= x + 314 * scale && mouse_x < x + 406 * scale) { prompt_kind = 0; prompt_error = 0; gui_mark_dirty(); }
        }
        left_button_down = 1; gui_mark_dirty(); return;
    }
    if (context_menu_open) {
        gui_context_menu_click(mouse_x, mouse_y);
        left_button_down = 1; gui_mark_dirty(); return;
    }
    if (power_confirmation_action != 0) {
        gui_handle_power_confirmation_click(mouse_x, mouse_y);
        left_button_down = 1;
        gui_mark_dirty();
        return;
    }
    if (start_menu_open) {
        click_at(mouse_x, mouse_y, 0);
        left_button_down = 1;
        gui_mark_dirty();
        return;
    }
    hit = chos_wm_hit_test(&window_manager, mouse_x, mouse_y);
    if (hit != CHOS_WM_NO_WINDOW) {
        const chos_wm_window_t *window = chos_wm_get_const(&window_manager, hit);
        if (window != NULL) {
            int32_t scale = (int32_t)gui_scale_factor();
            int32_t local_x = (mouse_x - window->x) / scale + GUI_WINDOW_BASE_X;
            int32_t local_y = (mouse_y - window->y) / scale + GUI_WINDOW_BASE_Y;
            chos_wm_title_control_t control = chos_wm_hit_test_title_control(window, mouse_x, mouse_y,
                                                                              (uint32_t)scale);
            if (chos_wm_activate(&window_manager, hit)) gui_set_active_view(hit);
            {
                if (control == CHOS_WM_CONTROL_MINIMIZE) gui_minimize_window(hit);
                else if (control == CHOS_WM_CONTROL_MAXIMIZE) gui_toggle_maximize_window(hit);
                else if (control == CHOS_WM_CONTROL_CLOSE) gui_close_window(hit);
                else if (local_y >= GUI_WINDOW_BASE_Y && local_y < GUI_WINDOW_BASE_Y + 29) {
                    if (!window->maximized) {
                        dragging_window_id = hit;
                        drag_offset_x = mouse_x - window->x;
                        drag_offset_y = mouse_y - window->y;
                        drag_preview_x = window->x;
                        drag_preview_y = window->y;
                        drag_preview_dirty = 0;
                        drag_outline_visible = 0;
                    }
                    gui_mark_dirty();
                } else if (gui_app_find_window(hit) != NULL) {
                    gui_app_queue_pointer(hit, CHOS_GUI_EVENT_POINTER_DOWN, 1u);
                    app_pointer_capture_window_id = hit;
                } else click_at(local_x, local_y, 1);
            }
        }
    } else {
        int desktop_index = mouse_y < TASKBAR_Y ? gui_desktop_icon_at(mouse_x, mouse_y) : -1;
        if (desktop_index >= 0) {
            selected_desktop_icon = desktop_index;
            desktop_drag_index = desktop_index;
            desktop_drag_start_x = mouse_x; desktop_drag_start_y = mouse_y;
            desktop_drag_current_cell = (int)desktop_icons[desktop_index].cell;
            desktop_drag_moved = 0;
            left_button_down = 1; gui_mark_dirty(); return;
        }
        selected_desktop_icon = -1;
        click_at(mouse_x, mouse_y, 0);
    }
    left_button_down = 1;
    gui_mark_dirty();
}

void chos_gui_pump(void) {
    uint64_t now;
    chos_audio_pump();
    if (!gui_active) return;
    gui_clock_poll();
    gui_taskbar_tooltip_tick();
    if (splash_active) {
        if (splash_startup_complete && splash_minimum_elapsed()) {
            splash_active = 0;
            if (startup_sound_enabled) {
                startup_chime_pending = 1; startup_chime_queued = 0; startup_chime_attempts = 0u;
                startup_chime_init_attempts = 0u;
                startup_chime_started_tick = timer_is_ready() ? timer_ticks() : 0u;
                startup_chime_started_counter = timer_counter();
                startup_chime_counter_frequency = timer_frequency_hz();
                /* Give the host audio backend a short warm-up interval after
                   the desktop becomes visible; immediate first-boot submits
                   can be lost while a newly created backend starts. */
                startup_chime_next_probe_ms = 250u;
            }
            gui_mark_dirty();
        } else {
            if (splash_counter_frequency != 0u) {
                uint64_t counter_now = timer_counter();
                uint64_t frame_interval = splash_counter_frequency / SPLASH_FRAME_RATE;
                if (frame_interval == 0u) frame_interval = 1u;
                if (counter_now - splash_last_animation_counter >= frame_interval) {
                    splash_last_animation_counter = counter_now;
                    gui_render_splash_bar();
                }
            } else {
                now = timer_is_ready() ? timer_ticks() : 0u;
                if (!timer_is_ready() || now - splash_last_animation_tick >= SPLASH_ANIMATION_TICKS) {
                    splash_last_animation_tick = now;
                    gui_render_splash_bar();
                }
            }
        }
        if (splash_active) return;
    }
    if (startup_chime_pending) {
        uint64_t elapsed_ms = 0u;
        if (startup_chime_counter_frequency != 0u) {
            uint64_t elapsed = timer_counter() - startup_chime_started_counter;
            elapsed_ms = (elapsed * 1000u) / startup_chime_counter_frequency;
        } else if (timer_is_ready()) {
            elapsed_ms = ((timer_ticks() - startup_chime_started_tick) * 1000u) / CHOS_TIMER_HZ;
        }
        if (startup_chime_queued) {
            chos_audio_chime_state_t state = chos_audio_startup_chime_state();
            if (state == CHOS_AUDIO_CHIME_PLAYED) {
                startup_chime_pending = 0;
            } else if (state == CHOS_AUDIO_CHIME_FAILED || state == CHOS_AUDIO_CHIME_IDLE) {
                /* Queue acceptance is not the same as audible completion. If
                   VirtIO reports a failed transfer, permit a bounded resubmit. */
                startup_chime_queued = 0;
                startup_chime_next_probe_ms = elapsed_ms + 250u;
            }
        }
        if (startup_chime_pending && !startup_chime_queued &&
            elapsed_ms >= startup_chime_next_probe_ms) {
            uint64_t next_delay_ms = 250u;
            if (chos_audio_available()) {
                ++startup_chime_attempts;
                if (chos_audio_play_startup_chime()) startup_chime_queued = 1;
            } else if (startup_chime_init_attempts < 2u) {
                /* QEMU's audio backend can become ready after the first device
                   probe. Allow two delayed, bounded retries after the splash,
                   but never reinitialize the device on every GUI frame. */
                ++startup_chime_init_attempts;
                if (chos_audio_init() && chos_audio_available()) {
                    ++startup_chime_attempts;
                    if (chos_audio_play_startup_chime()) startup_chime_queued = 1;
                }
                next_delay_ms = startup_chime_init_attempts == 1u ? 500u : 1000u;
            }
            /* Device setup gets at most two delayed attempts, and playback is
               checked for completion before the splash audio window ends. */
            startup_chime_next_probe_ms = elapsed_ms + next_delay_ms;
        }
        if (elapsed_ms >= 5000u || (!startup_chime_queued && startup_chime_attempts >= 20u))
            startup_chime_pending = 0;
    }
    if (virtio_gpu_cursor_available() && cursor_move_pending && !dirty) {
        cursor_move_pending = 0;
        if (!virtio_gpu_cursor_move(mouse_x, mouse_y, !splash_active)) gui_mark_dirty();
    }
    if (dirty || __atomic_load_n(&dirty_generation,__ATOMIC_RELAXED)!=presented_generation) {
        gui_render();
        /* If another state change arrives during a drag, redraw the base scene
         * once and then restore the lightweight preview over it. */
        if (dragging_window_id != CHOS_WM_NO_WINDOW && left_button_down &&
            (drag_preview_x != (chos_wm_get_const(&window_manager, dragging_window_id) != NULL
                ? chos_wm_get_const(&window_manager, dragging_window_id)->x : drag_preview_x) ||
             drag_preview_y != (chos_wm_get_const(&window_manager, dragging_window_id) != NULL
                ? chos_wm_get_const(&window_manager, dragging_window_id)->y : drag_preview_y))) {
            drag_preview_dirty = 1;
            gui_drag_overlay_update();
        }
    } else if (drag_preview_dirty && dragging_window_id != CHOS_WM_NO_WINDOW && left_button_down) {
        gui_drag_overlay_update();
    } else if (cursor_move_pending) {
        cursor_move_pending = 0;
        (void)gui_cursor_move_only();
        if (dirty) gui_render(); /* immediate software-cursor fallback after cursorq failure */
    }
}

int chos_gui_ready(void) { return gui_active && virtio_gpu_ready(); }
const char *chos_gui_view_name(void) {
    gui_app_client_t *client;
    if (view >= VIEW_WELCOME && view < (int)GUI_VIEW_COUNT) return view_names[view];
    client = gui_app_find_window(view);
    return client != NULL ? client->title : "Welcome";
}
