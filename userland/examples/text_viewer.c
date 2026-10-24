#include <stddef.h>
#include <stdint.h>
#include "chos/user.h"

#define VIEW_BYTES 768u
#define LINE_BYTES 112u
#define VIEW_LINES 6u
static const char title[] = "CHOS Text Viewer";
static const char default_path[] = "/DOCS/README.TXT";
static char document[VIEW_BYTES];

static void begin(chos_gui_message_t *m, uint16_t type, size_t payload_size) {
    chos_memset(m, 0, sizeof(*m));
    m->header.version = CHOS_GUI_ABI_VERSION;
    m->header.type = type;
    m->header.size_bytes = (uint32_t)(offsetof(chos_gui_message_t, payload) + payload_size);
}
static long submit(chos_gui_message_t *m) { return chos_gui_submit(m, m->header.size_bytes); }
static int draw_text(uint32_t id, int x, int y, const char *s, size_t n, uint32_t color) {
    chos_gui_message_t m;
    if (n >= CHOS_GUI_MAX_TEXT) return 0;
    begin(&m, CHOS_GUI_MSG_DRAW_TEXT, sizeof(m.payload.draw_text));
    m.payload.draw_text.window_id = id; m.payload.draw_text.x = x; m.payload.draw_text.y = y;
    m.payload.draw_text.argb = color; m.payload.draw_text.text_length = (uint16_t)n;
    chos_memcpy(m.payload.draw_text.text, s, n);
    return submit(&m) == 0;
}
static void close_window(uint32_t id) {
    chos_gui_message_t m; begin(&m, CHOS_GUI_MSG_DESTROY_WINDOW, sizeof(m.payload.window));
    m.payload.window.window_id = id; (void)submit(&m);
}
int main(int argc, char **argv) {
    chos_gui_message_t m, event;
    long fd, bytes, result;
    uint32_t id;
    size_t pos = 0u, line = 0u, i;
    const char *path = (argc > 1 && argv != NULL && argv[1] != NULL && argv[1][0] == '/')
        ? argv[1] : default_path;
    (void)argv;
    fd = chos_open(path);
    if (fd < 0) chos_exit(1);
    bytes = 0;
    while ((size_t)bytes < sizeof(document) - 1u) {
        size_t room = sizeof(document) - 1u - (size_t)bytes;
        size_t request = room > CHOS_SYS_MAX_READ ? CHOS_SYS_MAX_READ : room;
        long got = chos_read((int)fd, document + (size_t)bytes, request);
        if (got < 0) { (void)chos_close((int)fd); chos_exit(2); }
        if (got == 0) break;
        bytes += got;
    }
    (void)chos_close((int)fd);
    document[(size_t)bytes] = '\0';

    begin(&m, CHOS_GUI_MSG_CREATE_WINDOW, sizeof(m.payload.create_window));
    m.payload.create_window.x = 100; m.payload.create_window.y = 90;
    m.payload.create_window.width = 580u; m.payload.create_window.height = 300u;
    m.payload.create_window.background_argb = 0xfff4f4f4u;
    result = submit(&m); if (result < 100 || result > 103) chos_exit(3);
    id = (uint32_t)result;
    begin(&m, CHOS_GUI_MSG_SET_TITLE, sizeof(m.payload.set_title));
    m.payload.set_title.window_id = id; m.payload.set_title.title_length = (uint16_t)(sizeof(title)-1u);
    chos_memcpy(m.payload.set_title.title, title, sizeof(title)-1u);
    if (submit(&m) != 0) { close_window(id); chos_exit(4); }
    (void)draw_text(id, 16, 14, path, chos_strlen(path) < CHOS_GUI_MAX_TEXT ? chos_strlen(path) : CHOS_GUI_MAX_TEXT - 1u, 0xff000080u);
    (void)draw_text(id, 16, 34, "Press Escape or close this window to exit.", 40u, 0xff404040u);
    for (i = 0u; i < VIEW_LINES && pos < (size_t)bytes; ++i) {
        char linebuf[LINE_BYTES]; size_t n = 0u;
        while (pos < (size_t)bytes && document[pos] != '\n' && n + 1u < sizeof(linebuf)) linebuf[n++] = document[pos++];
        if (pos < (size_t)bytes && document[pos] == '\n') ++pos;
        linebuf[n] = '\0';
        (void)draw_text(id, 16, 64 + (int)i * 24, linebuf, n, 0xff202020u);
        ++line;
    }
    if (line == 0u) (void)draw_text(id, 16, 64, "(empty file)", 12u, 0xff202020u);
    begin(&m, CHOS_GUI_MSG_SHOW_WINDOW, sizeof(m.payload.window)); m.payload.window.window_id = id;
    if (submit(&m) != 0) { close_window(id); chos_exit(5); }
    for (;;) {
        result = chos_gui_poll_event(&event, sizeof(event));
        if (result == -(long)CHOS_ERRNO_EAGAIN) { (void)chos_sleep_ticks(1u); continue; }
        if (result < 0) break;
        if (event.header.type == CHOS_GUI_MSG_CLOSE_REQUEST) break;
        if (event.header.type == CHOS_GUI_MSG_KEY_EVENT && event.payload.key_event.keycode == 1u &&
            (event.payload.key_event.event_flags & CHOS_GUI_EVENT_KEY_DOWN) != 0u) break;
        (void)chos_yield();
    }
    close_window(id); chos_exit(0);
}
