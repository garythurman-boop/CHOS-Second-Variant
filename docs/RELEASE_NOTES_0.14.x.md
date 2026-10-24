# CHOS 0.14.x development release notes

## 0.14.2-dev — interactive desktop and basic file management

### Desktop interaction
- Desktop content is represented by a CHFS-backed icon model. Eight columns and six rows provide a fixed logical grid; dragging snaps to a cell and swaps with an icon already at the destination. Layout cells are stored in `/CFG/DESKTOP.CFG` and reloaded when the desktop model is refreshed.
- A permanent desktop icon titled `Trash` opens a dedicated window with item selection, Restore, Delete, and Empty Trash actions. Delete and Empty Trash require confirmation. Moving files/directories into Trash and restoring them changes paths through CHFS rename operations rather than copying data extents.
- Right-click on unused desktop space opens Arrange Icons, Refresh, New, and Settings actions. Right-click desktop icons opens the actions appropriate to application icons, files/folders, and Trash.
- New Folder creates a directory under `/DESKTOP`. New Text Document creates a real `.TXT` file in CHFS and opens it in the editable text window; typing, backspace, Ctrl+S, and save-on-close are supported. File/folder names and collisions are checked before operations.
- Opening a desktop folder passes its path into the user-space File Manager. Rename can rename files or directory trees. Item creation, rename, move-to-Trash, restore, delete and empty operations invalidate/update the desktop model, and successful icon-layout writes invalidate the layout cache so later refreshes reload saved positions.

### Start menu
- The vertical `CHOS` branding uses the bold DejaVu Sans proportional bitmap face, a modest increase in glyph size, and additional letter spacing while retaining bottom-origin upward reading and the navy-to-black background.
- Menu items maintain a selected highlight based on pointer hover and Up/Down keyboard movement. Moving the pointer outside the menu does not erase the last highlight; Enter activates it and Escape closes the menu.

### Filesystem changes
- Added `chfs_rename_directory()` and the storage wrapper. It updates directory-entry paths as a subtree in one metadata commit, preserves file extents, disallows renaming the root/moving a directory into its own subtree, and rejects destination collisions.
- Added regression coverage for moving nested directories, content preservation, invalid self-descendant moves and destination conflicts.

### Verification and limitations
- Clean AArch64 Clang/LLD build with `-Werror`, full host tests, ASan/UBSan tests, and userland packaging tests passed. This environment does not provide QEMU, so the new GUI interactions still need live acceptance testing on the developer's Mac.
- The desktop grid has 48 cells, while CHFS currently supports 23 entries. Most importantly, the GUI uses the existing CHFS filesystem rather than a new virtual file store.
- Network packet builders/parsers remain the existing 0.14.0/0.14.1 groundwork. End-to-end DHCP configuration/renewal, live IPv4 and ARP integration, DNS query/retry, TCP sockets, TLS/X.509 validation and a browser are still roadmap items.

## 0.14.1-dev — taskbar hover behavior, Start-menu refresh and network packet groundwork

### Desktop and input
- Application taskbar slots remain fixed at 88 logical pixels with a 92-pixel stride; titles are measured with the UI font and clipped with a literal `...` suffix.
- Hovering a taskbar button waits 24 timer ticks, then fades in a pale tooltip over six steps. The tooltip uses the full original title rather than the shortened caption, wraps to a bounded number of lines, and is clamped inside the screen.
- Tooltip hit-testing respects button bounds/gaps and avoids the clock area. Hover effects are suppressed while the Start menu, power confirmation or a mouse drag is active.
- The Start-menu sidebar is a vertical dark-blue-to-black gradient. The `CHOS` letters are vertically rotated and read upward, with the C at the bottom.

### Network packet primitives
- Added ARP IPv4 request and Ethernet/IPv4/UDP frame builders, including IPv4 and UDP pseudo-header checksums.
- Added ICMPv4 echo request/reply packet construction and validation, including checksum, type/code and fragmentation checks.
- Added DHCPREQUEST construction alongside DHCP Discover and the bounded DHCP OFFER/ACK/NAK reply parser.
- Added DNS A-query construction (lowercases validated host labels and supports a trailing root dot) alongside the guarded DNS A-response parser.
- UDP parser now validates nonzero UDP checksums using the IPv4 pseudo-header, while allowing the IPv4-permitted zero checksum. Host tests cover checksums, bad/truncated packets, option bounds and transaction/question matching.

### Limitations and verification
These changes are packet-level foundations. A DHCP lease state machine, live interface configuration, ARP cache, asynchronous DNS resolver, TCP, user-facing sockets, trusted time/entropy, TLS/X.509 validation and a browser engine are not complete. No general Internet-access claim is made. The clean AArch64 `-Werror` build, full host regression suite, sanitizer suite and userland packaging check passed in this build environment. Live QEMU desktop/network testing remains a separate acceptance step; QEMU is not installed in this packaging environment.

## 0.14.0-dev — fixed taskbar buttons and network services groundwork

### Desktop
- Open-application taskbar buttons use a fixed 88-logical-pixel width and 92-pixel slot spacing.
- The title renderer uses the proportional bold UI font's actual width before clipping; overlong titles get a literal three-dot `...` suffix.
- Updated click hit-testing so taskbar selection follows the new slot geometry and excludes gaps and slots that would overlap the clock.

### Networking
- Added `chos_net_build_dhcpv4_discover()` to construct a BOOTP/DHCP Discover payload with transaction ID, broadcast flag, client MAC, client identifier and requested network options.
- Added `chos_net_parse_dhcpv4_reply()` with bounded option traversal, transaction-ID validation, DHCP cookie/message-type validation, and extraction of yiaddr, subnet mask, router, DNS, lease and server identifier.
- Added `chos_net_parse_dns_a_response()` validating response ID, opcode/status flags, one matching A/IN question, answer bounds and DNS compression pointers. Returns the first A/IN address and optional TTL.
- Added host tests for packet construction, option parsing, question/transaction mismatches, compression-pointer rejection and truncation.

### Limits
These are packet-level primitives. There is not yet an integrated DHCP state machine, IPv4 configuration, ARP neighbor cache, DNS query/retry engine, TCP implementation, user-space socket API, TLS/X.509 validation or browser engine. No end-to-end Internet-access claim is made.

## 0.14.x target sequence

- Integrate DHCP lease negotiation/renewal, DNS queries with bounded retries, ARP neighbor resolution, ICMP basics and interface configuration.
- Establish UDP/TCP user-space APIs and repeatable QEMU network integration tests.
- Evaluate a maintainable portable TLS/X.509 implementation, trusted-root updates, time/entropy prerequisites and strict hostname/chain validation.
- Select and measure a practical ARM64 browser engine before building out a CHOS-branded navigation shell. Full HTTPS browsing remains gated on the previous prerequisites.
