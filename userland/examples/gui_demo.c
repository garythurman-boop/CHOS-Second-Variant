#include <stddef.h>
#include <stdint.h>
#include "chos/user.h"

static const char title[] = "CHOS User-Space GUI Demo";
static const char heading[] = "This window is owned by an EL0 process.";
static const char instruction[] = "Press Escape or use the close button to exit.";
static const char pointer_note[] = "Pointer input was delivered through the GUI event queue.";

static void message_begin(chos_gui_message_t *message, uint16_t type, size_t payload_size) {
    chos_memset(message, 0, sizeof(*message));
    message->header.version = CHOS_GUI_ABI_VERSION;
    message->header.type = type;
    message->header.size_bytes = (uint32_t)(offsetof(chos_gui_message_t, payload) + payload_size);
}

static long submit(chos_gui_message_t *message) {
    return chos_gui_submit(message, message->header.size_bytes);
}

static int set_title(uint32_t window_id) {
    chos_gui_message_t message;
    size_t length = sizeof(title) - 1u;
    message_begin(&message, CHOS_GUI_MSG_SET_TITLE, sizeof(message.payload.set_title));
    message.payload.set_title.window_id = window_id;
    message.payload.set_title.title_length = (uint16_t)length;
    chos_memcpy(message.payload.set_title.title, title, length);
    return submit(&message) == 0;
}

static int draw_text(uint32_t window_id, int32_t x, int32_t y,
                     const char *value, uint32_t argb) {
    chos_gui_message_t message;
    size_t length = chos_strlen(value);
    if (length >= CHOS_GUI_MAX_TEXT) return 0;
    message_begin(&message, CHOS_GUI_MSG_DRAW_TEXT, sizeof(message.payload.draw_text));
    message.payload.draw_text.window_id = window_id;
    message.payload.draw_text.x = x;
    message.payload.draw_text.y = y;
    message.payload.draw_text.argb = argb;
    message.payload.draw_text.text_length = (uint16_t)length;
    message.payload.draw_text.font_id = 0u;
    chos_memcpy(message.payload.draw_text.text, value, length);
    return submit(&message) == 0;
}

static int show_window(uint32_t window_id) {
    chos_gui_message_t message;
    message_begin(&message, CHOS_GUI_MSG_SHOW_WINDOW, sizeof(message.payload.window));
    message.payload.window.window_id = window_id;
    return submit(&message) == 0;
}

static void clear_and_draw_pointer_note(uint32_t window_id) {
    chos_gui_message_t message;
    message_begin(&message, CHOS_GUI_MSG_CLEAR, sizeof(message.payload.clear));
    message.payload.clear.window_id = window_id;
    message.payload.clear.background_argb = 0xfff0f0f0u;
    if (submit(&message) != 0) return;
    (void)draw_text(window_id, 18, 18, heading, 0xff000000u);
    (void)draw_text(window_id, 18, 48, instruction, 0xff202020u);
    (void)draw_text(window_id, 18, 90, pointer_note, 0xff000080u);
}

static void destroy_window(uint32_t window_id) {
    chos_gui_message_t message;
    message_begin(&message, CHOS_GUI_MSG_DESTROY_WINDOW, sizeof(message.payload.window));
    message.payload.window.window_id = window_id;
    (void)submit(&message);
}

int main(int argc, char **argv) {
    chos_gui_message_t message, event;
    long window_result;
    uint32_t window_id;
    (void)argc;
    (void)argv;
    if (chos_getpid() <= 0) chos_exit(1);

    message_begin(&message, CHOS_GUI_MSG_CREATE_WINDOW, sizeof(message.payload.create_window));
    message.payload.create_window.x = 160;
    message.payload.create_window.y = 140;
    message.payload.create_window.width = 460u;
    message.payload.create_window.height = 260u;
    message.payload.create_window.background_argb = 0xfff0f0f0u;
    window_result = submit(&message);
    if (window_result < 100 || window_result > 103) chos_exit(2);
    window_id = (uint32_t)window_result;

    if (!set_title(window_id) ||
        !draw_text(window_id, 18, 18, heading, 0xff000000u) ||
        !draw_text(window_id, 18, 48, instruction, 0xff202020u) ||
        !draw_text(window_id, 18, 90, "Keyboard and pointer events are routed to this process.", 0xff000080u) ||
        !show_window(window_id)) {
        destroy_window(window_id);
        chos_exit(3);
    }

    for (;;) {
        long result = chos_gui_poll_event(&event, sizeof(event));
        if (result == -(long)CHOS_ERRNO_EAGAIN) {
            (void)chos_sleep_ticks(1u);
            continue;
        }
        if (result < 0) break;
        if (event.header.type == CHOS_GUI_MSG_CLOSE_REQUEST) break;
        if (event.header.type == CHOS_GUI_MSG_KEY_EVENT &&
            event.payload.key_event.keycode == 1u &&
            (event.payload.key_event.event_flags & CHOS_GUI_EVENT_KEY_DOWN) != 0u) break;
        if (event.header.type == CHOS_GUI_MSG_POINTER_EVENT &&
            (event.payload.pointer_event.event_flags & CHOS_GUI_EVENT_POINTER_DOWN) != 0u) {
            clear_and_draw_pointer_note(window_id);
        }
        (void)chos_yield();
    }
    destroy_window(window_id);
    chos_exit(0);
}
