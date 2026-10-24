# CHOS GUI application protocol — 0.10.x experimental ABI

This document describes the currently implemented EL0 GUI service envelope. It is an experimental internal ABI for CHOS development, **not a stable third-party application contract**. Layouts and syscall behavior may change before a stable release.

## Transport and syscalls

- `CHOS_SYS_GUI_SUBMIT` (syscall 11): `x0` points to a message; `x1` is the exact byte count. Returns a newly created window ID for `CREATE_WINDOW`, zero on successful mutating commands, or a negative CHOS errno.
- `CHOS_SYS_GUI_POLL_EVENT` (syscall 12): `x0` points to a writable `chos_gui_message_t`; `x1` is its capacity. Returns the fixed envelope size when an event is delivered, or `-EAGAIN` when no event is queued.
- The convenience wrappers are `chos_gui_submit()` and `chos_gui_poll_event()` in `userland/libchos/include/chos/user.h`.
- All values use the native little-endian AArch64 ABI. No user application receives a framebuffer pointer or direct renderer access.

A message header is 24 bytes and contains the ABI version, message/event type, total `size_bytes`, flags, a reserved field that must be zero, and a 64-bit request ID. Callers must set `flags` to zero. The submit syscall copies user data into a kernel-owned message before dispatch and requires `header.size_bytes` and the supplied size to exactly match the payload size for the command.

## Supported commands

| Type | Command | Payload | Behavior |
|---:|---|---|---|
| 1 | `CREATE_WINDOW` | `chos_gui_create_window_t` | Creates one application-owned window; returns window ID 100–103 in the current implementation. |
| 2 | `DESTROY_WINDOW` | `{ window_id, reserved }` | Destroys the caller's window. |
| 3 | `SHOW_WINDOW` | `{ window_id, reserved }` | Activates/shows the caller's window. |
| 4 | `SET_TITLE` | `window_id`, title length, fixed-capacity title | Replaces the window title; title length must be nonzero and fit the supplied field. |
| 5 | `FILL_RECT` | `chos_gui_fill_rect_t` | Adds a retained filled rectangle in window client coordinates. |
| 6 | `DRAW_TEXT` | `chos_gui_draw_text_t` | Adds a retained text operation using UI font ID 0 or monospace font ID 1. |
| 8 | `CLEAR` | `chos_gui_clear_t` | Replaces the client background and clears retained drawing operations. |

`REPLY` is reserved in the protocol header and is not accepted as a submitted command. Coordinates use the 800×600 logical desktop coordinate space; the compositor applies the currently selected display scale. The current window service accepts windows from 260×160 logical pixels up to the logical desktop bounds, and clamps placement to the visible work area.

## Delivered events

| Type | Event | Payload | Notes |
|---:|---|---|---|
| 16 | `KEY_EVENT` | `chos_gui_key_event_t` | Key code, down/up flag, and simple shift/control modifier bits. |
| 17 | `POINTER_EVENT` | `chos_gui_pointer_event_t` | Client-area position, button state, and move/down/up/scroll flags. |
| 18 | `CLOSE_REQUEST` | `{ window_id, reserved }` | Request to close the application window; app decides when to exit. |
| 19 | `FOCUS_EVENT` | key-event-shaped focus payload | Carries `FOCUS_GAINED` or `FOCUS_LOST` in `event_flags`. |

Events are polled nonblocking. If no event is queued, poll returns `-EAGAIN`; applications should yield or sleep briefly rather than busy-spin. Pointer movement is lossy and may be coalesced under queue pressure. When the queue is full, the service evicts an older pointer-motion sample before a discrete event; if the queue contains only discrete events, it uses bounded FIFO eviction rather than blocking the input path. Applications should tolerate queue pressure and redraw from current application state.

## Ownership and current limits

- A process may own one GUI window at a time.
- There are four client window slots system-wide, with dynamically assigned IDs 100–103.
- A different PID cannot change or destroy another process's window; unauthorized accesses return `-EPERM`.
- Each client may retain at most 32 drawing operations. The event queue capacity is 16 messages per client.
- Process exit automatically removes its owned window and associated client state. Applications should still explicitly destroy windows during normal shutdown.
- The first demo application is `userland/examples/gui_demo.c`, packaged as `/BIN/GUIAPP.CHX`. Launch it in the graphical Console with `run /BIN/GUIAPP.CHX`. Click inside its window to exercise pointer events; press Escape or use the window close control to exit.

## Safety and validation

The syscall path rejects unknown ABI versions/types, nonzero reserved/flags fields, incorrect message lengths, oversized messages, invalid window handles, out-of-range window geometry, unsupported font IDs, out-of-bounds drawing requests, and drawing-list exhaustion. User buffers are copied via the calling process's address-space mappings; the kernel does not trust user virtual addresses as kernel pointers.

This is the first working transport, but the service remains intentionally small. Not yet provided: multiple windows per app, damage callbacks, clipboard, menus, timers, retained input text/IME, alpha drawing, a stable widget toolkit, arbitrary runtime image decoding, or a comprehensive accessibility/keybinding API. The desktop's main Console, File Manager and Text Viewer are still kernel-resident in 0.10.2-dev; the GUI demo validates that EL0 applications can use a real service boundary, but does not claim those core apps have migrated.
