# Held keys and text carets

USB HID and VirtIO key transitions share the GUI's typematic producer. Held
Backspace, printable characters and navigation keys repeat after 500 ms, then at
25 Hz. Enter, Escape, function keys, toggles and control shortcuts do not repeat.
Each GUI pump emits at most one repeat, so a delayed frame never causes a burst.
Key-up, USB disconnect, focus change, pointer clicks and modal transitions cancel
repeat. Optional host VirtIO repeat events are ignored to avoid double repeat.

The Console, append-mode text editor, naming dialogs and editable Network Manager
fields have focus-aware 500 ms blinking carets. Typing makes the caret visible
immediately. Console scrollback and inactive/modal-covered fields hide it. The
text editor uses its existing wrapping metrics and includes a final blank line
after a newline; it remains an append-mode editor, not a new full document editor.

Applications can use GUI command 9, SET_CARET. Coordinates, dimensions, enable flag,
text-prefix length, exact message length and window ownership are checked. The
optional UI-font prefix positions the caret using the server's real glyph widths.
Applications cannot place a caret in another client's window. Carets disappear on
focus loss, window closure or process exit. Synthetic repeat key messages carry
KEY_REPEAT in addition to KEY_DOWN; they never evict discrete transitions from a
full event queue. Existing protocol commands remain compatible.

The HTTP recovery browser uses this service and shows the end of a long address
while editing. Its EL0 loop now redraws on events/network changes instead of
rebuilding unchanged pages every tick. Blinking is driven by the window server;
it is not a modern browser-engine implementation. The compositor still repaints
the scene on an active caret's phase transition; an idle desktop without editable
focus does not get a new blink redraw loop.

Verification includes host/ASan/UBSan GUI tests for deadlines, bounded catch-up,
release/control/focus cancellation, actual caret pixels, malformed messages and
ownership. `tests/qemu_typing.py` injects real VirtIO or USB keyboard events into
disposable guests: held deletion in Console/documents/browser, delayed repeat,
release, printable repeat, visible blink/newline and USB unplug cancellation.

Early screenshot checks sampled the phase before the framebuffer had been
presented. The runner now allows presentation after observing the phase. A second
early fixture tried to leave an unsaved document on an ISO read-only volume;
the editor correctly refused to close after save failure. Browser checks now run
before editing that document. These retained fixture failures are not passes.
