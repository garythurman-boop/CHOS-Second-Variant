# Network Manager: first revised Stage 3 milestone

Open the tray network icon, press N on the desktop, or select Settings → Network.
The panel reports the actual selected Ethernet adapter, physical link and IPv4
configuration. Green status denotes a configured local interface; it does not
certify Internet access. No Wi-Fi networks, wireless driver or IPv6 support is
invented; their unavailable state is explicit.

Choose Automatic (DHCP), Static IPv4 or Disconnected. DHCP optionally accepts a
DNS resolver override. Static mode validates address, contiguous subnet mask,
on-subnet unicast gateway and optional DNS. Zero gateway/DNS represents absent
configuration rather than a fabricated route/resolver. Tab changes focus; Enter/
Space activates controls. Select a field and type digits/dots, using Backspace to
edit. Apply changes starts a 30-second trial. Keep accepts it, Revert restores the
previous configuration, and Cancel/Escape cancels the draft/reverts a trial.
Unconfirmed changes automatically revert while the GUI remains responsive.

Select Save before Keep to persist the profile. This requires writable CHFS.
Failure stays visible and leaves the trial reversible. A saved profile is a fixed
64-byte versioned/CRC-checked file, read back after flush, bound to the selected
adapter MAC. Wrong-size, corrupt, unknown-version or wrong-MAC profiles retain
safe default DHCP on boot. The codec contains no wireless passwords or credentials.
CRC detects corruption; it is not authentication or encryption.

Link loss clears IPv4/route/DNS and invalidates established sockets. Return of link
starts DHCP automatically or restores a static profile. DHCP lease renewals and
expiration retain existing bounded retry behavior. Disconnected mode stops network
configuration/transmission until explicitly enabled. Failed DMA ownership remains
quarantined; it is not silently rebound to another driver.

## Service and privilege boundary

The OS-owned manager applies configuration. Browser code consumes sockets and
status, not PCI discovery or configuration setters. `CHOS_SYS_NET_INFO` (34) gives
EL0 applications a fixed, pointer-free read-only 64-byte status snapshot, including
MAC, link/configuration flags, IPv4/subnet/gateway/DNS and counters. There is no
application setter syscall. User-buffer ownership/size/writability are checked.
EL0 filesystem mutation syscalls deny writes/unlink/rename/mkdir/rmdir of the
service-owned profile or its protected parent; the trusted GUI service writes it.
This does not claim a complete browser sandbox or general user/ACL system.

## Validation

Host/ASan/UBSan cover strict IPv4 parsing, mask/gateway validation, malformed and
truncated profiles, MAC binding, static/link transitions, 30-second rollback,
failed saves and reboot loading. `tests/qemu_net_manager.py` uses an installed
NVMe copy and real input devices to exercise the actual graphical controls,
static/DHCP configuration, invalid-mask refusal, cable loss/return, gateway ICMP,
profile persistence across fresh boot, timeout rollback and disconnect/revert.
`tests/qemu_net_policy.py` runs a real EL0 app to verify read-only status and reject
profile mutation plus bad user buffers. All image writes use disposable copies.

The initial manager guest run completed static/link/rollback checks but its runner
then queried a compiler-eliminated `ping_state` symbol. The corrected runner reads
actual Console ping status; it is not a driver recovery failure. The newer runs
and final delivery report identify exact build hashes and test results.

## Remaining revised Stage 3 work

This milestone supports a single selected Ethernet interface with IPv4. Multiple
interface selection/routes, IPv6, Wi-Fi drivers/firmware/authentication/regulatory
handling and protected credentials remain unimplemented. The modern engine,
complete isolated UEFI runtime, TLS/trust roots/entropy/time, browser features and
conformance gates remain open in BROWSER_ENGINE_PORT.md and STAGE3_ARCHITECTURE.md.
This panel and Ethernet success do not complete the revised Stage 3 definition.

## Validated delivery 54b9731

Clean source `54b97319a7abb6746eff491f167a82f00c08c08d` passes the complete
13-profile network milestone matrix: e1000e/VMXNET3/VirtIO manager configuration
and reconnect; HTTP/DNS for all three; actual EL0 status/profile policy; both
Console runtimes; native USB/storage/input/network; direct regression; real
graphical installation/persistence/reset/power-off and injected installer write
error. Aggregate host, ASan/UBSan and smoke checks pass. The install sample took
20.866 s, maximum step 12.815 ms, shutdown status 5.038 s. These are individual
shared-cloud measurements. All inputs are hash-recorded in the handoff.

Fusion e1000e traffic still needs an independent Mac test. Modern engine/runtime,
TLS, Wi-Fi, IPv6 and other revised Stage 3 gates remain incomplete. The local
engine-prerequisite probe exits nonzero for missing C++/pthread/mmap/poll/WPE
facilities; it is retained as a dependency failure, not a passing browser test.
