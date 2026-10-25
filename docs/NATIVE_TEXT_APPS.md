# Isolated desktop text application regressions

Enabling the real UEFI EL0 scheduler exposes ordinary application paths that the
previous firmware desktop handled through privileged recovery widgets. Console,
the HTTP recovery browser and text documents must be tested in their actual
process-owned address spaces. Passing a probe of the standby kernel Console or
browser does not establish that these application windows work. The default UEFI
desktop Console retains its trusted diagnostics and administrative commands;
explicitly launching `/BIN/CONSOLE.CHX` exercises the separate, limited EL0 Console.

The native Console now exposes read-only `netstatus` using NET_INFO: real adapter,
link, DHCP state, addresses, DNS and packet counters. A configured interface does
not claim Internet reachability. Administrative settings remain in the trusted
Network Manager/serial shell; the application receives no configuration setter.
The banner and help identify the current 0.30.0 application rather than an old
0.15.0 development snapshot. CHCX's link-relative, relocation-free format cannot
contain absolute pointer tables: state labels are inline character arrays. A live
test caught and corrected an earlier label-pointer table that faulted at EL0.

The native HTTP recovery browser now drains at most eight input events per frame,
redrawing for key-down/actionable pointer events and network changes. It no longer
redraws its entire toolbar for every key release or pointer motion. The old loop
added avoidable pressure to its bounded event queue; an earlier rapid-input test
did not receive all characters. Later 10 ms tap failures also demonstrated missed
physical input/modifier releases, so those timings cannot isolate a single cause.
One bounded network step and one sleep remain per frame. This is input
responsiveness work for recovery functionality, not the revised modern-browser
deliverable; JavaScript, modern rendering and secure HTTPS remain separate gates.

The native Text Viewer now provides append/backspace editing of persistent ASCII
text files up to 8191 bytes. It shows the server-owned blinking caret at the real
end of the visible text, supports newline/tab entry, scrolling, Ctrl+S and durable
save on close/Escape. Failed saves retain the open document. ROMFS, non-text,
binary/control-byte and truncated inputs remain read-only. An extra read detects
files larger than the buffer, so a partial preview can never silently overwrite
the full original. The editing model is explicitly append/backspace, matching the
existing bounded editor; it is not a full selection/insertion or Unicode editor.

## Real input and persistence tests

`tests/qemu_native_typing.py` (also the current `qemu_typing.py` entry point) uses
actual emulated USB or VirtIO keys, with ordinary taps held for 50 ms across
multiple USB report polls. The explicit held-key timing helper is unchanged.
It reads process memory through a read-only
probe that derives current structure offsets from the copied kernel's DWARF,
finds the GUI window's owning PID/address space and walks that process's real
AArch64 translation tables. Every probed user page must have EL0 permission. The
probe adds no production hooks and never writes guest memory.

The runner checks the removed plain-I Setup shortcut, the trusted UEFI Console's
repeat/caret path, the native Console's real
network status, typing/repeat/release in Console/browser/editor, cancellation on
focus changes, actual caret pixels, newline caret movement and USB keyboard
removal/re-attachment. Saves must survive closing and reopening the native editor;
after guest exit, the runner extracts and CRC-checks the actual disposable CHFS
files. Oversized and embedded-NUL originals must remain identical, and an exact
8191-byte document must reject an extra byte while permitting bounded delete/refill.

```sh
source /workspace/chos-tools/activate
make CC=clang LD=ld.lld all uefi-iso build/qemu-smoke-disk.img
python3 tests/qemu_native_typing.py --mode uefi --out /workspace/chos-evidence/NEW-NATIVE-TYPING
python3 tests/qemu_native_typing.py --mode direct --out /workspace/chos-evidence/NEW-DIRECT-TYPING
```

The test waits for completed application/caret messages and actual compositor
presentation before comparing a tight crop of caret pixels. Earlier fixed-delay
captures raced an in-progress newline redraw; those failures are retained rather
than used as acceptance. The typematic deadline remains 500 ms then at most 25 Hz,
with one repeat per pump and no catch-up burst. Actual uncached software-rendered
UEFI throughput is slower than that maximum deadline: the runner records observed
time to six repeat deletions and requires progress within a bounded three-second
budget, while verifying no early repeat and that key release stops deletion.
This does not close Stage 3's browser performance qualification.

All guest disks/firmware are disposable copies. Published binaries remain held
until the complete revised Stage 3 is satisfied and thoroughly tested. QEMU
evidence is distinct from user-reported Fusion and unavailable Pi 5 hardware tests.
