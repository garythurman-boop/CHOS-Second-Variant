#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chos/gui.h"
#include "chos/syscall.h"
#include "chos/audio.h"
#include "chos/virtio_gpu.h"
#include "chos/virtio_input.h"
#include "chos/storage.h"
#include "chos/process.h"
#include "chos/romfs.h"
#include "chos/timer.h"
#include "chos/power.h"
#include "chos/net_stack.h"
#include "chos/net_manager.h"
#include "chos/net6.h"
#include "chos/entropy.h"
#include "chos/installer_io.h"
static int mock_setup_available;
static chos_install_job_t mock_setup_job;
int chos_install_available(void){return mock_setup_available;}
chos_install_job_t *chos_install_runtime(void){return &mock_setup_job;}

static uint32_t pixels[CHOS_DISPLAY_MAX_WIDTH * CHOS_DISPLAY_MAX_HEIGHT];
static size_t mock_screen_width = 800u;
static size_t mock_screen_height = 600u;
static unsigned presentations;
static unsigned partial_presentations;
static uint64_t last_pair_transfer_pixels;
static int input_count = 2;
static int gpu_present_success = 1;
static unsigned program_launches;
static int allow_user_console_spawn;
static char last_program_path[CHOS_CHFS_PATH_MAX];
static uint64_t mock_ticks;
static int mock_timer_ready = 1;
static size_t mock_display_mode_index;
static int mock_read_failure;
static char mock_settings_data[128];
static size_t mock_settings_size;
static int mock_cfg_directory_exists;
static unsigned mock_cfg_directory_create_calls;
static unsigned mock_audio_plays;
static unsigned mock_audio_init_calls;
static unsigned mock_audio_pumps;
static unsigned mock_audio_ready_after_init;
static unsigned mock_audio_failures_remaining;
static int mock_audio_ready;
static chos_audio_chime_state_t mock_audio_chime_state;
static const char test_file_contents[] =
    "This is a deliberately long line with enough words to wrap across the text viewer width and verify layout without clipping at the right edge.\n"
    "row 01: viewer scrolling works.\n"
    "row 02: viewer scrolling works.\n"
    "row 03: viewer scrolling works.\n"
    "row 04: viewer scrolling works.\n"
    "row 05: viewer scrolling works.\n"
    "row 06: viewer scrolling works.\n"
    "row 07: viewer scrolling works.\n"
    "row 08: viewer scrolling works.\n"
    "row 09: viewer scrolling works.\n"
    "row 10: viewer scrolling works.\n"
    "row 11: viewer scrolling works.\n"
    "row 12: viewer scrolling works.\n"
    "row 13: viewer scrolling works.\n"
    "row 14: viewer scrolling works.\n"
    "row 15: viewer scrolling works.\n"
    "row 16: viewer scrolling works.\n"
    "row 17: viewer scrolling works.\n"
    "row 18: viewer scrolling works.\n"
    "row 19: viewer scrolling works.\n"
    "row 20: viewer scrolling works.\n";

static chos_net_config_t mock_network_config;
static int mock_network_trial;
void chos_net6_get_info(chos_net6_info_t *n){*n=(chos_net6_info_t){.version=1,.size_bytes=sizeof(*n)};}
int chos_net6_ping(const char *s){(void)s;return 0;}
int chos_net6_format(const uint8_t a[16],char *s,size_t n){(void)a;if(n)s[0]=0;return n>0;}
chos_block_device_t *storage_device(void){return NULL;}
void chos_net_manager_get_info(chos_net_manager_info_t *out){*out=(chos_net_manager_info_t){.configured=mock_network_config,.adapter="Mock Ethernet",.driver="mock ready",.message="Network test",.adapter_ready=1,.link_known=1,.link_up=1,.trial=mock_network_trial,.trial_seconds=mock_network_trial?30:0};}
int chos_net_manager_apply(const chos_net_config_t *c){if(chos_net_config_error(c))return 0;mock_network_config=*c;mock_network_trial=1;return 1;}
int chos_net_manager_keep(int save){(void)save;mock_network_trial=0;return 1;}
void chos_net_manager_revert(void){mock_network_trial=0;}
int storage_sync(void) {return 1;}
const char *storage_error(void) {return "mock storage error";}
size_t chos_entropy_read(void *p,size_t n) {(void)p;(void)n;return 0;}
int chos_entropy_ready(void) {return 0;}
const char *chos_entropy_last_error(void) {return "unavailable";}
void chos_net_stack_get_info(chos_net_stack_info_t *out) {*out=(chos_net_stack_info_t){0};}
void chos_net_ping_get_info(chos_net_ping_info_t *out) {*out=(chos_net_ping_info_t){0};}
const char *chos_net_dhcp_state_name(chos_net_dhcp_state_t s) {(void)s;return "offline";}
const char *chos_net_ping_state_name(chos_net_ping_state_t s) {(void)s;return "idle";}
int chos_net_ping_start(uint32_t ip) {(void)ip;return 0;}
const char *chos_clock_source(void) {return "mock clock";}
const char *uefi_console_boot_log(void) {return "PCI test inventory\n";}
int timer_is_ready(void) { return mock_timer_ready; }
uint64_t timer_ticks(void) { return mock_ticks; }
uint64_t timer_counter(void) { return mock_ticks; }
uint64_t timer_frequency_hz(void) { return 100u; }
uint64_t platform_rtc_unix_seconds(void) { return 1792641600u; } /* deterministic test clock */

int virtio_gpu_ready(void) { return 1; }
int virtio_gpu_cursor_init(const uint32_t *pixels_argb, uint32_t width, uint32_t height,
                           uint32_t hot_x, uint32_t hot_y) {
    (void)pixels_argb; (void)width; (void)height; (void)hot_x; (void)hot_y; return 0;
}
int virtio_gpu_cursor_available(void) { return 0; }
int virtio_gpu_cursor_move(int32_t x, int32_t y, int visible) {
    (void)x; (void)y; (void)visible; return 0;
}
int virtio_gpu_set_mode(size_t mode_index, chos_surface_t *surface) {
    chos_display_mode_t mode;
    if (surface == NULL || !chos_display_mode_get(mode_index, &mode)) return 0;
    mock_display_mode_index = mode_index;
    mock_screen_width = mode.width;
    mock_screen_height = mode.height;
    *surface = (chos_surface_t){ pixels, mode.width, mode.height, mode.width };
    return 1;
}
size_t virtio_gpu_current_mode_index(void) { return mock_display_mode_index; }
const char *virtio_gpu_last_error(void) { return "mock gpu ready"; }
void uart_puts(const char *message) { (void)message; }
int chos_net_dns_set_server(uint32_t ip){return ip!=0;}
const char *chos_net_device_name(void){return "Mock Ethernet";}
const char *chos_net_device_error(void){return "mock ready";}
const char *chos_power_execute(chos_power_action_t action) {
    (void)action;
    abort(); /* A host GUI test must not request a real power transition. */
}
int chos_audio_available(void) { return mock_audio_ready; }
int chos_audio_init(void) {
    ++mock_audio_init_calls;
    if (mock_audio_ready_after_init != 0u && mock_audio_init_calls >= mock_audio_ready_after_init)
        mock_audio_ready = 1;
    return mock_audio_ready;
}
int chos_audio_play_startup_chime(void) {
    ++mock_audio_plays;
    if (!mock_audio_ready) return 0;
    mock_audio_chime_state = CHOS_AUDIO_CHIME_PENDING;
    return 1;
}
chos_audio_chime_state_t chos_audio_startup_chime_state(void) { return mock_audio_chime_state; }
void chos_audio_pump(void) {
    ++mock_audio_pumps;
    if (mock_audio_chime_state == CHOS_AUDIO_CHIME_PENDING) {
        if (mock_audio_failures_remaining > 0u) {
            --mock_audio_failures_remaining;
            mock_audio_chime_state = CHOS_AUDIO_CHIME_FAILED;
        } else {
            mock_audio_chime_state = CHOS_AUDIO_CHIME_PLAYED;
        }
    }
}
const char *chos_audio_last_error(void) { return "mock audio unavailable"; }
static int mutate_during_present;
int virtio_gpu_present(void) {
    ++presentations;
    if(mutate_during_present) {mutate_during_present=0;chos_gui_input_key(30u,1);}
    return gpu_present_success;
}
int virtio_gpu_present_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (width == 0u || height == 0u || x + width > mock_screen_width || y + height > mock_screen_height) return 0;
    ++partial_presentations; return gpu_present_success;
}
int virtio_gpu_present_rects(const chos_gpu_rect_t *rects, size_t count) {
    size_t i;
    uint64_t pixels_seen = 0u;
    if (rects == NULL || count == 0u || count > CHOS_GPU_MAX_DIRTY_RECTS) return 0;
    for (i = 0u; i < count; ++i) {
        if (rects[i].width == 0u || rects[i].height == 0u ||
            rects[i].x + rects[i].width > mock_screen_width ||
            rects[i].y + rects[i].height > mock_screen_height) return 0;
        pixels_seen += (uint64_t)rects[i].width * rects[i].height;
    }
    ++partial_presentations;
    last_pair_transfer_pixels = pixels_seen;
    return gpu_present_success;
}
int virtio_gpu_present_rect_pair(uint32_t x1, uint32_t y1, uint32_t width1, uint32_t height1,
                                 uint32_t x2, uint32_t y2, uint32_t width2, uint32_t height2) {
    if (width1 == 0u || height1 == 0u || width2 == 0u || height2 == 0u ||
        x1 + width1 > mock_screen_width || y1 + height1 > mock_screen_height ||
        x2 + width2 > mock_screen_width || y2 + height2 > mock_screen_height) return 0;
    ++partial_presentations;
    last_pair_transfer_pixels = (uint64_t)width1 * height1 + (uint64_t)width2 * height2;
    return gpu_present_success;
}
unsigned chos_input_device_count(void) { return (unsigned)input_count; }
const char *chos_input_last_error(void) { return "mock input ready"; }
int storage_mounted(void) { return 1; }
int storage_is_directory(const char *path) {
    if (path == NULL) return 0;
    if (strcmp(path, "/") == 0) return 1;
    return strcmp(path, "/CFG") == 0 && mock_cfg_directory_exists;
}
int storage_make_directory(const char *path) {
    if (path == NULL || strcmp(path, "/CFG") != 0) return 0;
    ++mock_cfg_directory_create_calls;
    mock_cfg_directory_exists = 1;
    return 1;
}
int storage_lookup(const char *path, chos_chfs_entry_t *out) {
    if (path == NULL || out == NULL || strcmp(path, "/DOCS/TEST.TXT") != 0) return 0;
    memset(out, 0, sizeof(*out));
    memcpy(out->path, "/DOCS/TEST.TXT", sizeof("/DOCS/TEST.TXT"));
    out->size_bytes = sizeof(test_file_contents) - 1u;
    return 1;
}
int storage_read_file(const char *path, void *buffer, size_t capacity, size_t *bytes_read) {
    size_t i, size = sizeof(test_file_contents) - 1u;
    if (path == NULL || buffer == NULL || bytes_read == NULL) return 0;
    if (strcmp(path, "/CFG/CHOS.CFG") == 0) {
        if (mock_settings_size == 0u || capacity < mock_settings_size) return 0;
        for (i = 0u; i < mock_settings_size; ++i) ((char *)buffer)[i] = mock_settings_data[i];
        *bytes_read = mock_settings_size;
        return 1;
    }
    if (mock_read_failure || strcmp(path, "/DOCS/TEST.TXT") != 0 || capacity < size) return 0;
    for (i = 0u; i < size; ++i) ((char *)buffer)[i] = test_file_contents[i];
    *bytes_read = size;
    return 1;
}
int storage_write_file(const char *path, const void *buffer, size_t size) {
    size_t i;
    if (path == NULL || buffer == NULL || strcmp(path, "/CFG/CHOS.CFG") != 0 ||
        !mock_cfg_directory_exists || size > sizeof(mock_settings_data)) return 0;
    for (i = 0u; i < size; ++i) mock_settings_data[i] = ((const char *)buffer)[i];
    mock_settings_size = size;
    return 1;
}
int storage_remove_file(const char *path) { return path != NULL; }
int storage_remove_directory(const char *path) { return path != NULL; }
int storage_rename_file(const char *old_path, const char *new_path) { return old_path != NULL && new_path != NULL; }
int storage_rename_directory(const char *old_path, const char *new_path) { return old_path != NULL && new_path != NULL; }
size_t romfs_file_count(void) { return 0u; }
int romfs_get_at(size_t index, chos_romfs_file_t *out) { (void)index; (void)out; return 0; }
int romfs_lookup(const char *path, chos_romfs_file_t *out) { (void)path; (void)out; return 0; }
chos_process_error_t process_run_path(const char *path, chos_process_result_t *result) {
    size_t i = 0u;
    ++program_launches;
    while (path != NULL && path[i] != '\0' && i + 1u < sizeof(last_program_path)) { last_program_path[i] = path[i]; ++i; }
    last_program_path[i] = '\0';
    if (result != NULL) { result->pid = 7u; result->exit_status = 0; result->completed = 1; }
    chos_gui_console_write("demo output\n", sizeof("demo output\n") - 1u);
    return PROCESS_OK;
}
chos_process_error_t process_spawn_path(const char *path, uint32_t *pid_out) {
    size_t i = 0u;
    /* This host GUI test has no embedded userland ROMFS table, so exercise the
       documented fallback to kernel-resident views when demo apps are unavailable. */
    if (path != NULL && strcmp(path, "/BIN/CONSOLE.CHX") == 0 && allow_user_console_spawn) {
        if (pid_out != NULL) *pid_out = 77u;
        return PROCESS_OK;
    }
    if (path != NULL && (strcmp(path, "/BIN/CONSOLE.CHX") == 0 ||
                         strcmp(path, "/BIN/FILEMAN.CHX") == 0)) {
        if (pid_out != NULL) *pid_out = 0u;
        return PROCESS_ERR_NOT_FOUND;
    }
    ++program_launches;
    while (path != NULL && path[i] != '\0' && i + 1u < sizeof(last_program_path)) { last_program_path[i] = path[i]; ++i; }
    last_program_path[i] = '\0';
    if (pid_out != NULL) *pid_out = 7u;
    chos_gui_console_write("demo output\n", sizeof("demo output\n") - 1u);
    return PROCESS_OK;
}
chos_process_error_t process_spawn_path_args(const char *path, const char *arguments, uint32_t *pid_out) {
    (void)arguments;
    return process_spawn_path(path, pid_out);
}
int process_get_info_at(size_t index, chos_process_info_t *out) { (void)index; (void)out; return 0; }
int process_query_pid(uint32_t pid, chos_process_state_t *state_out, int64_t *status_out) {
    if (pid != 7u) return -1;
    if (state_out != NULL) *state_out = CHOS_PROCESS_QUEUED;
    if (status_out != NULL) *status_out = 0;
    return 1;
}
const char *process_state_name(chos_process_state_t state) {
    switch (state) {
        case CHOS_PROCESS_EMPTY: return "empty";
        case CHOS_PROCESS_LOADING: return "loading";
        case CHOS_PROCESS_QUEUED: return "queued";
        case CHOS_PROCESS_RUNNING: return "running";
        case CHOS_PROCESS_EXITED: return "exited";
        case CHOS_PROCESS_FAILED: return "failed";
        default: return "unknown";
    }
}
const char *process_error_string(chos_process_error_t error) { (void)error; return "mock error"; }
int storage_get_at(size_t index, chos_chfs_entry_t *out) {
    const char *path;
    if (out == NULL || index > 1u) return 0;
    path = index == 0u ? "/DOCS/TEST.TXT" : "/BIN/CDEMO.CHX";
    memset(out, 0, sizeof(*out));
    memcpy(out->path, path, strlen(path) + 1u);
    out->size_bytes = index == 0u ? sizeof(test_file_contents) - 1u : 8252u;
    out->sector_count = index == 0u ? 3u : 17u;
    return 1;
}

static void click_at(int32_t x, int32_t y) {
    int32_t ax = (int32_t)(((uint64_t)(uint32_t)x * 32767u) / (mock_screen_width - 1u));
    int32_t ay = (int32_t)(((uint64_t)(uint32_t)y * 32767u) / (mock_screen_height - 1u));
    chos_gui_input_pointer_absolute(ax, ay);
    chos_gui_input_button(0x110u, 1);
    chos_gui_input_button(0x110u, 0);
    chos_gui_pump();
}

static uint16_t code_for_char(char ch) {
    switch (ch) {
        case 'r': return 19u; case 'u': return 22u; case 'n': return 49u;
        case 'i': return 23u; case 'c': return 46u; case 'd': return 32u;
        case 'e': return 18u; case 'm': return 50u; case 'o': return 24u;
        case '.': return 52u; case '/': return 53u; case ' ': return 57u;
        case 'h': return 35u; case 'l': return 38u; case 'p': return 25u;
        case 's': return 31u; case 'y': return 21u; case 't': return 20u;
        case 'x': return 45u; case 'v': return 47u; case 'a': return 30u;
        case 'b': return 48u; default: return 0u;
    }
}
static void type_text(const char *value) {
    while (*value != '\0') {
        char ch = *value++;
        int upper = ch >= 'A' && ch <= 'Z';
        uint16_t code;
        if (upper) ch = (char)(ch - 'A' + 'a');
        code = code_for_char(ch);
        if (upper) chos_gui_input_key(42u, 1);
        if (code != 0u) chos_gui_input_key(code, 1);
        if (upper) chos_gui_input_key(42u, 0);
    }
}

static size_t count_red_text_pixels(void) {
    size_t count = 0u;
    int x, y;
    for (y = 145; y < 460; ++y) {
        for (x = 204; x < 750; ++x) {
            uint32_t color = pixels[(size_t)y * 800u + (size_t)x];
            unsigned r = (color >> 16) & 0xffu;
            unsigned g = (color >> 8) & 0xffu;
            unsigned b = color & 0xffu;
            if (r > g + 25u && r > b + 20u) ++count;
        }
    }
    return count;
}

static void gui_message_begin(chos_gui_message_t *message, uint16_t type, size_t payload_size) {
    memset(message, 0, sizeof(*message));
    message->header.version = CHOS_GUI_ABI_VERSION;
    message->header.type = type;
    message->header.size_bytes = (uint32_t)(offsetof(chos_gui_message_t, payload) + payload_size);
}

static uint32_t test_create_gui_client(uint32_t owner_pid, const char *title) {
    chos_gui_message_t message;
    long result;
    size_t length = strlen(title);
    gui_message_begin(&message, CHOS_GUI_MSG_CREATE_WINDOW, sizeof(message.payload.create_window));
    message.payload.create_window.x = 150;
    message.payload.create_window.y = 130;
    message.payload.create_window.width = 420u;
    message.payload.create_window.height = 240u;
    message.payload.create_window.background_argb = 0xfff0f0f0u;
    result = chos_gui_submit_message(owner_pid, &message, message.header.size_bytes);
    assert(result >= 100 && result <= 103);
    message.header.type = CHOS_GUI_MSG_SET_TITLE;
    message.header.size_bytes = (uint32_t)(offsetof(chos_gui_message_t, payload) + sizeof(message.payload.set_title));
    message.payload.set_title.window_id = (uint32_t)result;
    message.payload.set_title.title_length = (uint16_t)length;
    memcpy(message.payload.set_title.title, title, length);
    assert(chos_gui_submit_message(owner_pid, &message, message.header.size_bytes) == 0);
    gui_message_begin(&message, CHOS_GUI_MSG_DRAW_TEXT, sizeof(message.payload.draw_text));
    message.payload.draw_text.window_id = (uint32_t)result;
    message.payload.draw_text.x = 12;
    message.payload.draw_text.y = 14;
    message.payload.draw_text.argb = 0xff000000u;
    message.payload.draw_text.text_length = (uint16_t)strlen("EL0 GUI service test");
    memcpy(message.payload.draw_text.text, "EL0 GUI service test", message.payload.draw_text.text_length);
    assert(chos_gui_submit_message(owner_pid, &message, message.header.size_bytes) == 0);
    gui_message_begin(&message, CHOS_GUI_MSG_SHOW_WINDOW, sizeof(message.payload.window));
    message.payload.window.window_id = (uint32_t)result;
    assert(chos_gui_submit_message(owner_pid, &message, message.header.size_bytes) == 0);
    return (uint32_t)result;
}

static void test_el0_gui_service(void) {
    chos_gui_message_t message, event;
    uint32_t window_id;
    unsigned i;
    int saw_focus = 0, saw_down = 0, saw_up = 0, saw_escape = 0;
    window_id = test_create_gui_client(42u, "GUI Service Test");
    chos_gui_pump();
    assert(strcmp(chos_gui_view_name(), "GUI Service Test") == 0);

    /* The browser UI needs more than 32 buffered ops for its toolbar and page.
       Ensure the bounded per-window draw list can hold a complete application frame. */
    gui_message_begin(&message, CHOS_GUI_MSG_CLEAR, sizeof(message.payload.clear));
    message.payload.clear.window_id = window_id;
    message.payload.clear.background_argb = 0xfff0f0f0u;
    assert(chos_gui_submit_message(42u, &message, message.header.size_bytes) == 0);
    for (i = 0u; i < 70u; ++i) {
        gui_message_begin(&message, CHOS_GUI_MSG_DRAW_TEXT, sizeof(message.payload.draw_text));
        message.payload.draw_text.window_id = window_id;
        message.payload.draw_text.x = 8;
        message.payload.draw_text.y = 40 + (int32_t)(i % 140u);
        message.payload.draw_text.argb = 0xff000000u;
        message.payload.draw_text.text_length = 2u;
        message.payload.draw_text.text[0] = 'O';
        message.payload.draw_text.text[1] = 'K';
        assert(chos_gui_submit_message(42u, &message, message.header.size_bytes) == 0);
    }
    chos_gui_pump();

    /* A different PID cannot alter the owner's window. */
    gui_message_begin(&message, CHOS_GUI_MSG_CLEAR, sizeof(message.payload.clear));
    message.payload.clear.window_id = window_id;
    message.payload.clear.background_argb = 0xfff0f0f0u;
    assert(chos_gui_submit_message(43u, &message, message.header.size_bytes) == -(int64_t)CHOS_ERRNO_EPERM);
    message.payload.clear.window_id = window_id + 1u;
    assert(chos_gui_submit_message(42u, &message, message.header.size_bytes) == -(int64_t)CHOS_ERRNO_EPERM);

    for (i = 0u; i < CHOS_GUI_EVENT_QUEUE_CAPACITY; ++i) {
        if (chos_gui_pop_event(42u, &event) <= 0) break;
        if (event.header.type == CHOS_GUI_MSG_FOCUS_EVENT &&
            (event.payload.key_event.event_flags & CHOS_GUI_EVENT_FOCUS_GAINED) != 0u) saw_focus = 1;
    }
    assert(saw_focus);

    /* Click the client area and ensure press/release transitions reach EL0. */
    chos_gui_input_pointer_absolute((int32_t)((230u * 32767u) / 799u),
                                    (int32_t)((290u * 32767u) / 599u));
    chos_gui_pump();
    chos_gui_input_button(0x110u, 1);
    chos_gui_input_button(0x110u, 0);
    chos_gui_pump();
    for (i = 0u; i < CHOS_GUI_EVENT_QUEUE_CAPACITY; ++i) {
        if (chos_gui_pop_event(42u, &event) <= 0) break;
        if (event.header.type == CHOS_GUI_MSG_POINTER_EVENT) {
            if ((event.payload.pointer_event.event_flags & CHOS_GUI_EVENT_POINTER_DOWN) != 0u) saw_down = 1;
            if ((event.payload.pointer_event.event_flags & CHOS_GUI_EVENT_POINTER_UP) != 0u) saw_up = 1;
        }
    }
    assert(saw_down && saw_up);

    chos_gui_input_key(1u, 1);
    for (i = 0u; i < CHOS_GUI_EVENT_QUEUE_CAPACITY; ++i) {
        if (chos_gui_pop_event(42u, &event) <= 0) break;
        if (event.header.type == CHOS_GUI_MSG_KEY_EVENT && event.payload.key_event.keycode == 1u &&
            (event.payload.key_event.event_flags & CHOS_GUI_EVENT_KEY_DOWN) != 0u) saw_escape = 1;
    }
    assert(saw_escape);

    /* When the bounded event queue is full, fresh pointer motion must not evict
     * a keyboard transition. First fill with motions then ensure Escape arrives. */
    for (i = 0u; i < CHOS_GUI_EVENT_QUEUE_CAPACITY + 4u; ++i) {
        uint32_t px = 230u + (uint32_t)(i % 30u);
        chos_gui_input_pointer_absolute((int32_t)((px * 32767u) / 799u),
                                        (int32_t)((290u * 32767u) / 599u));
        chos_gui_pump();
    }
    chos_gui_input_key(1u, 1);
    for (i = 0u; i < CHOS_GUI_EVENT_QUEUE_CAPACITY; ++i) {
        if (chos_gui_pop_event(42u, &event) <= 0) break;
        if (event.header.type == CHOS_GUI_MSG_KEY_EVENT && event.payload.key_event.keycode == 1u &&
            (event.payload.key_event.event_flags & CHOS_GUI_EVENT_KEY_DOWN) != 0u) saw_escape = 1;
    }
    assert(saw_escape);

    /* Conversely, if the queue contains only discrete events, pointer motion is
     * dropped instead of displacing key transitions. */
    for (i = 0u; i < CHOS_GUI_EVENT_QUEUE_CAPACITY; ++i) {
        chos_gui_input_key(30u + (uint32_t)i, 1);
        chos_gui_pump();
    }
    chos_gui_input_pointer_absolute((int32_t)((260u * 32767u) / 799u),
                                    (int32_t)((290u * 32767u) / 599u));
    chos_gui_pump();
    {
        size_t key_events = 0u;
        for (i = 0u; i < CHOS_GUI_EVENT_QUEUE_CAPACITY; ++i) {
            if (chos_gui_pop_event(42u, &event) <= 0) break;
            if (event.header.type == CHOS_GUI_MSG_KEY_EVENT) ++key_events;
        }
        assert(key_events == CHOS_GUI_EVENT_QUEUE_CAPACITY);
    }
    /* The synthetic queue-pressure sequence includes a Shift key-down; deliver its
       matching release so later legacy-console tests begin with neutral modifiers. */
    chos_gui_input_key(42u, 0);

    gui_message_begin(&message, CHOS_GUI_MSG_DESTROY_WINDOW, sizeof(message.payload.window));
    message.payload.window.window_id = window_id;
    assert(chos_gui_submit_message(42u, &message, message.header.size_bytes) == 0);
    assert(strcmp(chos_gui_view_name(), "GUI Service Test") != 0);

    /* Process termination performs the same window cleanup as an explicit destroy. */
    window_id = test_create_gui_client(43u, "GUI Exit Cleanup Test");
    assert(strcmp(chos_gui_view_name(), "GUI Exit Cleanup Test") == 0);
    chos_gui_process_exited(43u);
    assert(strcmp(chos_gui_view_name(), "GUI Exit Cleanup Test") != 0);
    (void)window_id;
}

static unsigned report_pages_seen;
static int report_missing=1;
static int mock_needs_recovery(void){return report_missing;}
static unsigned mock_driver_report(unsigned page,char lines[CHOS_DRIVER_REPORT_ROWS][CHOS_DRIVER_REPORT_COLS+1]) {
    memset(lines,0,CHOS_DRIVER_REPORT_ROWS*(CHOS_DRIVER_REPORT_COLS+1));
    strcpy(lines[0],"Automatic hardware report");report_pages_seen|=1u<<page;return 3;
}
int main(void) {
    chos_surface_t surface = { pixels, 800u, 600u, 800u };
    size_t i;
    unsigned nonzero = 0u;
    assert(chos_gui_init(&surface));
    assert(chos_gui_ready());
    assert(strcmp(chos_gui_view_name(), "Welcome") == 0);
    assert(presentations == 1u);
    assert(pixels[0] == 0xff008080u); /* classic Windows 9x/2000-era teal desktop */
    chos_gui_startup_complete();
    for (i = 0u; i < 800u * 600u; ++i) if (pixels[i] != 0u) ++nonzero;
    assert(nonzero > 800u * 600u / 3u);
    mock_ticks = 299u; /* mock timer is 100 Hz: the splash must last at least 3 seconds. */
    chos_gui_pump();
    assert(pixels[0] == 0xff008080u); /* splash redraw preserves classic teal wallpaper */
    mock_ticks = 300u;
    chos_gui_pump(); /* splash completes only after the 3 second minimum. */
    assert(pixels[0] == 0xff008080u); /* classic teal desktop */
    mock_setup_available=1;
    chos_gui_input_key(23u,1);chos_gui_input_key(23u,0);chos_gui_pump();
    assert(strcmp(chos_gui_view_name(),"Welcome")==0); /* Plain I never launches Setup. */
    mock_setup_available=0;

    /* A syscall/task may invalidate the scene while the shell presents. That
     * update must not be erased by the old frame's final dirty=0 store. */
    unsigned before_preempt=presentations;
    mutate_during_present=1;chos_gui_show();
    assert(strcmp(chos_gui_view_name(),"About CHOS")==0);
    chos_gui_pump();assert(presentations>=before_preempt+2);
    chos_gui_show();
    chos_gui_input_key(15u, 1); /* Tab selects File Manager */
    chos_gui_input_key(28u, 1); /* Enter opens it */
    chos_gui_pump();
    assert(strcmp(chos_gui_view_name(), "File Manager") == 0);
    assert(presentations >= 2u);

    chos_gui_input_key(30u, 1); /* A opens About */
    chos_gui_pump();
    assert(strcmp(chos_gui_view_name(), "About CHOS") == 0);
    chos_gui_input_key(31u, 1); /* S opens settings */
    chos_gui_pump();
    assert(strcmp(chos_gui_view_name(), "Settings") == 0);
    click_at(400, 200); /* choose the bundled PNG wallpaper */
    assert(pixels[0] == 0xff2d8ad6u);
    click_at(280, 200); /* wallpaper button toggles back to classic teal */
    assert(pixels[0] == 0xff008080u);
    assert(mock_cfg_directory_exists); /* settings save creates /CFG on older disk images */
    assert(mock_cfg_directory_create_calls == 1u);
    click_at(670, 170); /* cycle taskbar clock from UTC to UTC+01 */
    assert(mock_settings_size < sizeof(mock_settings_data));
    mock_settings_data[mock_settings_size] = '\0';
    assert(strstr(mock_settings_data, "timezone=06") != NULL);

    chos_gui_input_key(1u, 1); /* Escape returns to Welcome */
    chos_gui_pump();
    assert(strcmp(chos_gui_view_name(), "About CHOS") == 0); /* most recently focused surviving window */
    chos_gui_show();
    assert(strcmp(chos_gui_view_name(), "Welcome") == 0);
    assert(presentations >= 6u);
    /* A transparent corner of the selected desktop PNG icon must preserve the
       selection background; this catches the old opaque dark shadow rectangle. */
    assert(pixels[86u * 800u + 57u] == 0xff316ac5u ||
           pixels[86u * 800u + 57u] == 0xff008080u);

    /* Cursor-only motion should use subrectangle presents at the default mode. */
    chos_gui_pump();
    {
        unsigned full_before = presentations;
        unsigned partial_before = partial_presentations;
        chos_gui_input_pointer_relative(4, 3);
        assert(partial_presentations == partial_before); /* coalesced until the input batch is pumped */
        chos_gui_pump();
        assert(partial_presentations == partial_before + 1u);
        assert(presentations == full_before);
        assert(last_pair_transfer_pixels < 800u * 600u / 100u);
        /* A large pointer jump still transfers two cursor-sized areas, not the
           giant rectangle spanning all intervening screen pixels. */
        chos_gui_input_pointer_relative(160, 0);
        chos_gui_input_pointer_relative(160, 0);
        chos_gui_input_pointer_relative(160, 0);
        chos_gui_pump();
        assert(last_pair_transfer_pixels < 800u * 600u / 100u);
    }
    /* Window dragging must present only the thin rubber-band edges while held,
       rather than repainting the full desktop at every movement. Release commits
       the final geometry with exactly one full redraw. */
    {
        unsigned full_before, partial_before;
        chos_gui_input_pointer_absolute((int32_t)(((uint64_t)250u * 32767u) / 799u),
                                        (int32_t)(((uint64_t)80u * 32767u) / 599u));
        chos_gui_input_button(0x110u, 1);
        chos_gui_pump(); /* full redraw on drag start only */
        full_before = presentations;
        partial_before = partial_presentations;
        chos_gui_input_pointer_relative(12, 7);
        chos_gui_pump();
        assert(presentations == full_before);
        assert(partial_presentations >= partial_before + 5u); /* four edges + pointer */
        assert(last_pair_transfer_pixels < 800u * 600u / 100u);
        chos_gui_input_pointer_relative(80, 35);
        chos_gui_input_pointer_relative(40, 20);
        chos_gui_pump();
        assert(presentations == full_before);
        chos_gui_input_button(0x110u, 0);
        chos_gui_pump();
        assert(presentations == full_before + 1u);
        full_before = presentations;
        chos_gui_input_pointer_relative(40, 20);
        chos_gui_pump();
        assert(presentations == full_before); /* post-release motion is cursor-only */
    }

    /* 0.10.2 GUI service enforces PID window ownership and routes focus/input events. */
    test_el0_gui_service();

    /* 0.9.1 terminal launches asynchronously and window state survives taskbar switches. */
    chos_gui_input_key(46u, 1); /* C opens Console */
    assert(strcmp(chos_gui_view_name(), "Console") == 0);
    assert(chos_gui_console_output_enabled());
    /* Bounded Console history can be read without losing the live prompt. */
    for(unsigned line=0;line<600;line++){
        char output[40];int n=snprintf(output,sizeof(output),"console history line %u\n",line);
        chos_gui_console_write(output,(size_t)n);
    }
    chos_gui_pump();
    uint64_t live_hash=0,older_hash=0,restored_hash=0;
    for(size_t p=0;p<800u*540u;p+=13)live_hash=live_hash*131u^pixels[p];
    chos_gui_input_key(104u,1);chos_gui_input_key(104u,0);chos_gui_pump();
    for(size_t p=0;p<800u*540u;p+=13)older_hash=older_hash*131u^pixels[p];
    assert(live_hash!=older_hash);
    chos_gui_input_key(107u,1);chos_gui_input_key(107u,0);chos_gui_pump();
    for(size_t p=0;p<800u*540u;p+=13)restored_hash=restored_hash*131u^pixels[p];
    assert(live_hash==restored_hash);
    chos_gui_input_scroll(-3);chos_gui_pump();older_hash=0;
    for(size_t p=0;p<800u*540u;p+=13)older_hash=older_hash*131u^pixels[p];
    assert(live_hash!=older_hash);
    chos_gui_input_key(107u,1);chos_gui_input_key(107u,0);
    type_text("clear");chos_gui_input_key(28u,1);chos_gui_input_key(28u,0);chos_gui_pump();
    click_at(747, 71); /* maximize at the rendered middle title-bar control */
    click_at(762, 13); /* restore using the maximized window control geometry */
    click_at(726, 71); /* minimize at the rendered left control */
    assert(strcmp(chos_gui_view_name(), "Welcome") == 0);
    click_at(206, 584); /* taskbar restores Console */
    assert(strcmp(chos_gui_view_name(), "Console") == 0);
    assert(chos_gui_console_output_enabled());
    type_text("run /BIN/CDEMO.CHX");
    chos_gui_input_key(28u, 1);
    chos_gui_pump();
    assert(program_launches == 1u);
    assert(strcmp(last_program_path, "/BIN/CDEMO.CHX") == 0);
    assert(strcmp(chos_gui_view_name(), "Console") == 0);
    chos_gui_input_key(1u, 1); /* Esc returns focus to the desktop */
    chos_gui_input_key(33u, 1); /* F opens File Manager */
    assert(chos_gui_console_output_enabled()); /* background app output retains its Console */
    assert(strcmp(chos_gui_view_name(), "File Manager") == 0);
    click_at(206, 584); /* taskbar can raise the existing Console over File Manager */
    assert(strcmp(chos_gui_view_name(), "Console") == 0);
    click_at(298, 584); /* taskbar restores File Manager without destroying Console */
    assert(strcmp(chos_gui_view_name(), "File Manager") == 0);
    click_at(220, 170); /* select file row */
    click_at(155, 420); /* Open selected text file after window offset */
    assert(strcmp(chos_gui_view_name(), "Text Viewer") == 0);
    /* Long lines wrap and key scrolling stays in the viewer. */
    {
        uint64_t before_scroll = 0u, after_scroll = 0u;
        for (i = 0u; i < 800u * 600u; i += 13u) before_scroll = (before_scroll * 131u) ^ pixels[i];
        chos_gui_input_key(108u, 1); /* Down */
        chos_gui_pump();
        for (i = 0u; i < 800u * 600u; i += 13u) after_scroll = (after_scroll * 131u) ^ pixels[i];
        assert(strcmp(chos_gui_view_name(), "Text Viewer") == 0);
        assert(before_scroll != after_scroll);
        chos_gui_input_key(104u, 1); /* Page Up */
        chos_gui_pump();
        chos_gui_input_scroll(1); /* mouse wheel down */
        chos_gui_pump();
    }
    /* The viewer must be a real WM window, not just an active-view enum. */
    chos_gui_input_key(1u, 1); /* Escape closes the registered viewer window */
    assert(strcmp(chos_gui_view_name(), "File Manager") == 0);
    mock_read_failure = 1;
    click_at(155, 420); /* Open must display an explicit read error, not fake empty text. */
    assert(strcmp(chos_gui_view_name(), "Text Viewer") == 0);
    assert(count_red_text_pixels() > 0u);
    chos_gui_input_key(1u, 1);
    assert(strcmp(chos_gui_view_name(), "File Manager") == 0);
    mock_read_failure = 0;
    click_at(155, 420); /* Open the selected text file again */
    assert(strcmp(chos_gui_view_name(), "Text Viewer") == 0);
    click_at(280, 488); /* Back to File Manager */
    assert(strcmp(chos_gui_view_name(), "File Manager") == 0);
    click_at(220, 190); /* select the second row: the C demo */
    click_at(250, 420); /* Run opens and activates a real Console window */
    assert(strcmp(chos_gui_view_name(), "Console") == 0);
    assert(program_launches == 2u);
    assert(strcmp(last_program_path, "/BIN/CDEMO.CHX") == 0);
    assert(chos_gui_console_output_enabled());
    click_at(768, 71); /* close Console; File Manager should regain focus */
    assert(strcmp(chos_gui_view_name(), "File Manager") == 0);
    assert(!chos_gui_console_output_enabled());

    /* Display changes are paged, support all aspect families and persist in CHFS. */
    chos_gui_input_key(31u, 1); /* Settings */
    assert(strcmp(chos_gui_view_name(), "Settings") == 0);
    /* Settings is centered; convert base-logical coordinates to the physical screen. */
    click_at(365, 335); /* mode row 2: 1024x768 */
    assert(mock_screen_width == 1024u && mock_screen_height == 768u);
    assert(mock_display_mode_index == 2u);
    assert(mock_settings_size > 0u);
    /* Navigate to the last page and select the 2560x1920 4:3 mode. */
    click_at(650 + (int32_t)(mock_screen_width - 800u) / 2 - 15,
             273 + (int32_t)(mock_screen_height - 600u) / 2 + 6);
    click_at(650 + (int32_t)(mock_screen_width - 800u) / 2 - 15,
             273 + (int32_t)(mock_screen_height - 600u) / 2 + 6);
    click_at(650 + (int32_t)(mock_screen_width - 800u) / 2 - 15,
             273 + (int32_t)(mock_screen_height - 600u) / 2 + 6);
    click_at(650 + (int32_t)(mock_screen_width - 800u) / 2 - 15,
             273 + (int32_t)(mock_screen_height - 600u) / 2 + 6);
    click_at(365 + (int32_t)(mock_screen_width - 800u) / 2 - 15,
             357 + (int32_t)(mock_screen_height - 600u) / 2 + 6);
    assert(mock_screen_width == 2560u && mock_screen_height == 1920u);
    assert(mock_display_mode_index == 23u);

    /* Simulate a reboot: GPU initializes at recovery mode, GUI restores persisted mode. */
    mock_screen_width = 800u; mock_screen_height = 600u; mock_display_mode_index = 0u;
    surface = (chos_surface_t){ pixels, 800u, 600u, 800u };
    assert(chos_gui_init(&surface));
    assert(mock_display_mode_index == 23u);
    assert(mock_screen_width == 2560u && mock_screen_height == 1920u);
    assert(mock_audio_pumps > 0u);

    /* The persisted high-resolution GUI uses 3x layout coordinates for controls. */
    chos_gui_startup_complete();
    mock_ticks += 300u; /* Let the minimum splash interval elapse. */
    chos_gui_pump();
    chos_gui_input_key(31u, 1); /* Open Settings so Escape can prove the dialog was modal. */
    assert(strcmp(chos_gui_view_name(), "Settings") == 0);
    /* Start button is centered in the high-resolution taskbar; select Shutdown. */
    click_at(165, 1850);
    click_at(283, 1677);
    chos_gui_input_key(1u, 1); /* Escape cancels confirmation without powering off. */
    chos_gui_pump();
    assert(strcmp(chos_gui_view_name(), "Settings") == 0);
    assert(chos_gui_ready());

    /* The architectural counter keeps the three-second minimum when timer IRQ setup is unavailable. */
    mock_timer_ready = 0;
    mock_ticks = 0u;
    assert(chos_gui_init(&surface));
    chos_gui_startup_complete();
    mock_ticks = 299u;
    chos_gui_pump();
    assert(pixels[0] == 0xff008080u);
    mock_ticks = 300u;
    chos_gui_pump();
    assert(pixels[0] == 0xff008080u);
    mock_timer_ready = 1;

    /* A failed first present must cleanly disable the GUI and preserve serial fallback. */
    gpu_present_success = 0;
    assert(!chos_gui_init(&surface));
    assert(!chos_gui_ready());
    gpu_present_success = 1;

    /* If VirtIO-Sound is not ready on the first desktop frame, startup gets
       two delayed initialization retries, not an unbounded probe on every pump. */
    memcpy(mock_settings_data, "CHOSCFG1\nmode=00\nwallpaper=0\naudio=1\ntimezone=06\n",
           sizeof("CHOSCFG1\nmode=00\nwallpaper=0\naudio=1\ntimezone=06\n") - 1u);
    mock_settings_size = sizeof("CHOSCFG1\nmode=00\nwallpaper=0\naudio=1\ntimezone=06\n") - 1u;
    mock_screen_width = 800u; mock_screen_height = 600u; mock_display_mode_index = 0u;
    surface = (chos_surface_t){ pixels, 800u, 600u, 800u };
    mock_timer_ready = 1;
    mock_ticks = 0u;
    mock_audio_init_calls = 0u; mock_audio_plays = 0u;
    mock_audio_ready_after_init = 2u; mock_audio_ready = 0;
    mock_audio_failures_remaining = 1u; mock_audio_chime_state = CHOS_AUDIO_CHIME_IDLE;
    assert(chos_gui_init(&surface));
    chos_gui_startup_complete();
    mock_ticks = 300u;
    chos_gui_pump(); /* splash clears and the bounded audio startup window begins */
    for (unsigned retry_tick = 0u; retry_tick < 26u; ++retry_tick) {
        mock_ticks += 25u; /* advance 250 ms per iteration */
        chos_gui_pump();
    }
    assert(mock_audio_init_calls == 2u);
    assert(mock_audio_ready == 1);
    assert(mock_audio_plays == 2u); /* first accepted transfer fails, bounded retry completes */
    assert(mock_audio_chime_state == CHOS_AUDIO_CHIME_PLAYED);
    mock_audio_ready_after_init = 0u; mock_audio_ready = 0;
    mock_audio_failures_remaining = 0u; mock_audio_chime_state = CHOS_AUDIO_CHIME_IDLE;

    /* The taskbar tooltip waits for the hover delay, then fades from the scene
       beneath it into a pale-yellow box containing the full window name. */
    chos_gui_show();
    {
        const size_t probe_index = 548u * 800u + 160u;
        const uint32_t before_tooltip = pixels[probe_index];
        const int absolute_x = (int)(((uint64_t)100u * 32767u + 399u) / 799u);
        const int absolute_y = (int)(((uint64_t)580u * 32767u + 299u) / 599u);
        chos_gui_input_pointer_absolute(absolute_x, absolute_y);
        chos_gui_pump();
        mock_ticks += 23u;
        chos_gui_pump();
        assert(pixels[probe_index] == before_tooltip); /* still inside hover delay */
        mock_ticks += 1u;
        chos_gui_pump();
        assert(pixels[probe_index] != before_tooltip && pixels[probe_index] != 0xffffffe1u);
        mock_ticks += 20u;
        chos_gui_pump();
        assert(pixels[probe_index] == 0xffffffe1u); /* tooltip fade completed */
    }
    {
        int absolute_x = (int)(((uint64_t)20u * 32767u + 399u) / 799u);
        int absolute_y = (int)(((uint64_t)580u * 32767u + 299u) / 599u);
        size_t x, y;
        int found_vertical_brand_pixel = 0;
        chos_gui_input_pointer_absolute(absolute_x, absolute_y);
        chos_gui_input_button(0x110u, 1);
        chos_gui_input_button(0x110u, 0);
        chos_gui_pump();
        assert(pixels[271u * 800u + 30u] == 0xff0a246au); /* classic navy at the top of the taller sidebar */
        assert(pixels[566u * 800u + 30u] == 0xff000000u); /* black at the bottom of the taller sidebar */
        for (y = 523u; y < 555u; ++y) {
            for (x = 10u; x < 50u; ++x) {
                if (pixels[y * 800u + x] == 0xffffffffu) found_vertical_brand_pixel = 1;
            }
        }
        assert(found_vertical_brand_pixel); /* CHOS is drawn vertically in white. */
    }

    /* EL0 Console output is a bounded stream. Only the owning Console PID may
       consume it; writes from child applications remain visible without focus. */
    mock_settings_size = sizeof("CHOSCFG1\nmode=00\nwallpaper=0\naudio=0\ntimezone=06\n") - 1u;
    memcpy(mock_settings_data, "CHOSCFG1\nmode=00\nwallpaper=0\naudio=0\ntimezone=06\n", mock_settings_size);
    allow_user_console_spawn = 1;
    mock_ticks = 0u;
    assert(chos_gui_init(&surface));
    chos_gui_startup_complete();
    mock_ticks = 300u; chos_gui_pump();
    chos_gui_input_key(46u, 1); /* launch /BIN/CONSOLE.CHX as EL0 owner PID 77 */
    assert(chos_gui_console_output_enabled());
    chos_gui_console_write("child output\n", sizeof("child output\n") - 1u);
    {
        char output[32] = {0};
        assert(chos_gui_console_session_read(76u, output, sizeof(output)) == -1);
        assert(chos_gui_console_session_read(77u, output, sizeof(output)) == (int64_t)(sizeof("child output\n") - 1u));
        assert(memcmp(output, "child output\n", sizeof("child output\n") - 1u) == 0);
    }
    chos_gui_process_exited(77u);
    assert(!chos_gui_console_output_enabled());
    allow_user_console_spawn = 0;

    /* Input recovery must need no input or timer IRQ; pages rotate on the
       architectural counter and must not repeatedly steal Console focus. */
    mock_ticks=0;mock_timer_ready=0;report_pages_seen=0;report_missing=1;
    assert(chos_gui_init(&surface));
    chos_gui_set_driver_report(mock_driver_report,mock_needs_recovery);
    chos_gui_startup_complete();mock_ticks=300;chos_gui_pump();
    mock_ticks=600;chos_gui_pump();
    assert(strcmp(chos_gui_view_name(),"Diagnostics")==0 && (report_pages_seen&1));
    mock_ticks=950;chos_gui_pump();assert(report_pages_seen&2);
    chos_gui_input_key(46,1);chos_gui_pump();assert(strcmp(chos_gui_view_name(),"Console")==0);
    mock_ticks=1300;chos_gui_pump();assert(strcmp(chos_gui_view_name(),"Console")==0);
    chos_gui_set_driver_report(NULL,NULL);mock_timer_ready=1;
    /* Typematic is server-owned, bounded and cancelled by release/focus/control. */
    {
        chos_gui_message_t m,e;
        uint32_t id=test_create_gui_client(93u,"Typing test");
        while(chos_gui_pop_event(93u,&e)>0){}
        uint64_t start=mock_ticks;
        chos_gui_input_key(14u,1);
        assert(chos_gui_pop_event(93u,&e)>0 && e.payload.key_event.keycode==14u);
        mock_ticks=start+49;chos_gui_pump();assert(!chos_gui_pop_event(93u,&e));
        mock_ticks=start+50;chos_gui_pump();
        assert(chos_gui_pop_event(93u,&e)>0 && (e.payload.key_event.event_flags&CHOS_GUI_EVENT_KEY_REPEAT));
        mock_ticks+=4;chos_gui_pump();assert(chos_gui_pop_event(93u,&e)>0);
        mock_ticks+=1000;chos_gui_pump();
        assert(chos_gui_pop_event(93u,&e)>0 && !chos_gui_pop_event(93u,&e)); /* no catch-up burst */
        chos_gui_input_key(14u,0);assert(chos_gui_pop_event(93u,&e)>0);
        mock_ticks+=100;chos_gui_pump();assert(!chos_gui_pop_event(93u,&e));
        chos_gui_input_key(14u,1);chos_gui_input_key(29u,1);chos_gui_input_key(29u,0);
        while(chos_gui_pop_event(93u,&e)>0){}
        mock_ticks+=100;chos_gui_pump();assert(!chos_gui_pop_event(93u,&e));
        chos_gui_input_key(14u,0);while(chos_gui_pop_event(93u,&e)>0){}

        gui_message_begin(&m,CHOS_GUI_MSG_SET_CARET,sizeof(m.payload.caret));
        m.payload.caret=(chos_gui_caret_t){.window_id=id,.x=10,.y=10,.width=1,.height=14,.argb=0xff123456u,.enabled=1};
        assert(chos_gui_submit_message(92u,&m,m.header.size_bytes)==-CHOS_ERRNO_EPERM);
        m.payload.caret.x=INT32_MAX;assert(chos_gui_submit_message(93u,&m,m.header.size_bytes)==-CHOS_ERRNO_EINVAL);
        m.payload.caret.x=10;m.payload.caret.text_length=CHOS_GUI_MAX_TEXT;
        assert(chos_gui_submit_message(93u,&m,m.header.size_bytes)==-CHOS_ERRNO_EINVAL);
        m.payload.caret.text_length=0;
        assert(chos_gui_submit_message(93u,&m,m.header.size_bytes-1)==-CHOS_ERRNO_EINVAL);
        m.payload.caret.enabled=0;m.payload.caret.x=INT32_MAX;m.payload.caret.text_length=1;m.payload.caret.text[0]='W';
        assert(chos_gui_submit_message(93u,&m,m.header.size_bytes)==0); /* disabled coordinates never overflow */
        m.payload.caret.enabled=1;m.payload.caret.x=10;m.payload.caret.text_length=0;
        assert(chos_gui_submit_message(93u,&m,m.header.size_bytes)==0);
        chos_gui_input_key(42u,1);chos_gui_input_key(42u,0);chos_gui_pump();
        assert(pixels[174u*800u+166u]==0xff123456u);
        mock_ticks+=50;chos_gui_pump();assert(pixels[174u*800u+166u]!=0xff123456u);
        mock_ticks+=50;chos_gui_pump();assert(pixels[174u*800u+166u]==0xff123456u);
        /* Focus another window: the old caret stays hidden, held key is cancelled. */
        chos_gui_input_key(14u,1);(void)test_create_gui_client(94u,"Other focus");
        while(chos_gui_pop_event(94u,&e)>0){}
        mock_ticks+=100;chos_gui_pump();assert(!chos_gui_pop_event(94u,&e));
        chos_gui_process_exited(94u);chos_gui_process_exited(93u);
    }
    puts("gui: desktop composition, taskbar hover-tooltip delay/fade, vertical start-menu gradient/branding, keyboard navigation, windowing, splash timing, persistent display modes, first-start audio readiness, bounded retries, power-dialog cancellation, and EL0 Console output ownership passed");
    return 0;
}

int storage_block_ready(void) { return storage_mounted(); }
