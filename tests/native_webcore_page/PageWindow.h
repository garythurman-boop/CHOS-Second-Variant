#pragma once

#include <chos/user.h>
#include <cstring>

// Own the GUI window for the lifetime of one local WebCore page.
class PageWindow {
public:
    bool open()
    {
        chos_gui_message_t message { };
        message.header.version = CHOS_GUI_ABI_VERSION;
        message.header.type = CHOS_GUI_MSG_CREATE_WINDOW;
        message.header.size_bytes = offsetof(chos_gui_message_t, payload) + sizeof(message.payload.create_window);
        auto& window = message.payload.create_window;
        window.x = 100;
        window.y = 90;
        window.width = 600;
        window.height = 420;
        window.background_argb = 0xffeeeeee;
        long result = chos_gui_submit(&message, message.header.size_bytes);
        if (result < 0)
            return false;
        m_id = result;
        message = { };
        message.header.version = CHOS_GUI_ABI_VERSION;
        message.header.type = CHOS_GUI_MSG_SET_TITLE;
        message.header.size_bytes = offsetof(chos_gui_message_t, payload) + sizeof(message.payload.set_title);
        auto& title = message.payload.set_title;
        constexpr char text[] = "CHOS WebCore - Space to change page, Esc to close";
        title.window_id = m_id;
        title.title_length = sizeof(text) - 1;
        static_assert(sizeof(text) - 1 <= sizeof(title.title));
        std::memcpy(title.title, text, title.title_length);
        return !chos_gui_submit(&message, message.header.size_bytes);
    }

    ~PageWindow()
    {
        if (!m_id)
            return;
        chos_gui_message_t message { };
        message.header.version = CHOS_GUI_ABI_VERSION;
        message.header.type = CHOS_GUI_MSG_DESTROY_WINDOW;
        message.header.size_bytes = offsetof(chos_gui_message_t, payload) + sizeof(message.payload.window);
        message.payload.window.window_id = m_id;
        chos_gui_submit(&message, message.header.size_bytes);
    }

    bool present(const uint32_t (&pixels)[512 * 256])
    {
        auto begin = surface(CHOS_GUI_MSG_SURFACE_BEGIN, sizeof(chos_gui_surface_message_t::payload.begin), 0);
        begin.payload.begin.window_id = m_id;
        begin.payload.begin.x = 12;
        begin.payload.begin.y = 64;
        begin.payload.begin.width = 512;
        begin.payload.begin.height = 256;
        long id = -CHOS_ERRNO_EAGAIN;
        for (unsigned attempt = 0; attempt < 200 && id == -CHOS_ERRNO_EAGAIN; ++attempt) {
            id = chos_gui_surface_submit(&begin, begin.header.size_bytes);
            if (id == -CHOS_ERRNO_EAGAIN)
                chos_sleep_ticks(1);
        }
        if (id <= 0)
            return false;
        auto tile = surface(CHOS_GUI_MSG_SURFACE_TILE, sizeof(begin.payload.tile), id);
        auto& data = tile.payload.tile;
        data.window_id = m_id;
        data.pixel_count = sizeof(data.pixels) / sizeof(pixels[0]);
        for (unsigned start = 0; start < 512 * 256; start += data.pixel_count) {
            data.tile_index = start / data.pixel_count;
            std::memcpy(data.pixels, pixels + start, sizeof(data.pixels));
            if (chos_gui_surface_submit(&tile, tile.header.size_bytes))
                return false;
        }
        auto finish = surface(CHOS_GUI_MSG_SURFACE_PRESENT, sizeof(begin.payload.window), id);
        finish.payload.window.window_id = m_id;
        return !chos_gui_surface_submit(&finish, finish.header.size_bytes);
    }

    // 1 advances the document, 0 closes it, and -1 reports a GUI error.
    int waitForKey(bool allowAdvance)
    {
        for (;;) {
            chos_gui_message_t event;
            long result = chos_gui_poll_event(&event, sizeof(event));
            if (result == -CHOS_ERRNO_EAGAIN) {
                chos_sleep_ticks(1);
                continue;
            }
            if (result < 0)
                return -1;
            if (event.header.type == CHOS_GUI_MSG_CLOSE_REQUEST)
                return 0;
            if (event.header.type == CHOS_GUI_MSG_KEY_EVENT && (event.payload.key_event.event_flags & CHOS_GUI_EVENT_KEY_DOWN)) {
                if (event.payload.key_event.keycode == 1)
                    return 0;
                if (allowAdvance && event.payload.key_event.keycode == 57)
                    return 1;
            }
        }
    }

private:
    static chos_gui_surface_message_t surface(unsigned type, size_t bytes, uint64_t request)
    {
        chos_gui_surface_message_t message { };
        message.header.version = CHOS_GUI_ABI_VERSION;
        message.header.type = type;
        message.header.size_bytes = offsetof(chos_gui_surface_message_t, payload) + bytes;
        message.header.request_id = request;
        return message;
    }
    uint32_t m_id { 0 };
};
