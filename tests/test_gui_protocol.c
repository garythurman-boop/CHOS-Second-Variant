#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "chos/gui_protocol.h"

int main(void) {
    chos_gui_message_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = CHOS_GUI_ABI_VERSION;
    h.type = CHOS_GUI_MSG_CREATE_WINDOW;
    h.size_bytes = (uint32_t)(sizeof(h) + sizeof(chos_gui_create_window_t));
    assert(chos_gui_message_header_valid(&h, sizeof(chos_gui_message_t)));
    assert(!chos_gui_message_header_valid(&h, sizeof(h)));
    h.version++;
    assert(!chos_gui_message_header_valid(&h, sizeof(chos_gui_message_t)));
    h.version = CHOS_GUI_ABI_VERSION;
    h.type = 0;
    assert(!chos_gui_message_header_valid(&h, sizeof(chos_gui_message_t)));
    h.type = CHOS_GUI_MSG_CREATE_WINDOW;
    h.size_bytes = CHOS_GUI_MAX_MESSAGE_BYTES + 1u;
    assert(!chos_gui_message_header_valid(&h, sizeof(chos_gui_message_t)));
    h.size_bytes = (uint32_t)sizeof(h);
    h.reserved = 1u;
    assert(!chos_gui_message_header_valid(&h, sizeof(chos_gui_message_t)));

    /* CLEAR is a supported command and event types use the same bounded envelope. */
    h.reserved = 0u;
    h.type = CHOS_GUI_MSG_CLEAR;
    h.size_bytes = (uint32_t)(sizeof(h) + sizeof(chos_gui_clear_t));
    assert(chos_gui_message_header_valid(&h, sizeof(chos_gui_message_t)));
    h.type = CHOS_GUI_MSG_POINTER_EVENT;
    h.size_bytes = (uint32_t)(sizeof(h) + sizeof(chos_gui_pointer_event_t));
    assert(chos_gui_message_header_valid(&h, sizeof(chos_gui_message_t)));
    h.type = CHOS_GUI_MSG_FOCUS_EVENT;
    h.size_bytes = (uint32_t)(sizeof(h) + sizeof(chos_gui_key_event_t));
    assert(chos_gui_message_header_valid(&h, sizeof(chos_gui_message_t)));
    h.type = CHOS_GUI_MSG_REPLY;
    h.size_bytes = (uint32_t)sizeof(h);
    assert(chos_gui_message_header_valid(&h, sizeof(chos_gui_message_t))); /* framing only; submit rejects replies */
    puts("gui protocol: command/event framing and fixed ABI layouts passed");
    return 0;
}
