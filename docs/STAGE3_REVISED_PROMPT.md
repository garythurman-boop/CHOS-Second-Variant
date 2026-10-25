STAGE 3 — MODERN INTERNET PLATFORM: NETWORK MANAGER, WI-FI/ETHERNET, AND FULL WEB BROWSER
Mission
Transform CHOS from an operating system with basic networking and a limited HTTP browser into a fully functional, secure, modern internet platform.
The finished system must provide the network connectivity, network configuration, and web-browsing capabilities users reasonably expect from a contemporary desktop operating system, while preserving CHOS's lightweight ARM64 architecture, custom kernel, established boot paths, classic Windows-inspired identity, and performance goals.
This is a major engineering phase. It includes network-driver integration, automatic network discovery, wireless connectivity, network configuration utilities, secure transport, and the integration of a genuine modern browser engine.
Do not interpret this assignment as merely adding HTTPS support to the existing browser. Build the complete supported internet stack from hardware detection through user-facing applications.
Do not stop after planning, producing mockups, implementing interfaces with placeholder functionality, or passing isolated host tests. Implement, integrate, debug, and test the deliverables until their acceptance criteria are satisfied. Continue across development sessions as necessary. Never claim a feature is complete without evidence that it works.

PART 1 — Understand CHOS and establish the architecture
Before modifying code, inspect the complete CHOS source tree, build system, kernel interfaces, memory model, filesystem, process and threading support, GUI framework, syscalls, existing network stack, browser, entropy subsystem, and driver abstractions.
CHOS is a custom AArch64 operating system. It is not a Linux distribution, and a Linux browser application cannot automatically be assumed to run on it.
The existing 0.30.0 source includes an HTTP-oriented browser, URL and redirect handling, network-stack foundations, VirtIO networking for QEMU, a VirtIO RNG foundation, and explicit TLS/security boundaries. Determine precisely which capabilities are working before changing them.
Produce a short architecture assessment that identifies:
Existing working code to preserve.
Missing driver, kernel, userland, networking, graphics, and runtime capabilities.
Browser engines and integration strategies that are technically viable.
Dependencies that must be implemented before the complete system can work.
Resource constraints, memory-management requirements, threading requirements, and performance risks.
A test matrix covering QEMU, VMware Fusion, Raspberry Pi 5, and ARM64 laptops.
Do not replace the existing working networking or boot implementations without a demonstrated improvement and regression tests.
PART 2 — Build a genuine modern web browser
2.1 Integrate a real browser engine
Research and evaluate maintained, open-source browser engines, including Chromium/Blink, WebKit, Servo, and other technically suitable alternatives.
Assess their actual current support, licensing, dependencies, AArch64 build requirements, rendering capabilities, JavaScript support, portability, security architecture, memory consumption, and feasibility on CHOS.
Select an engine based on evidence rather than familiarity or convenience.
Prefer adapting an established browser engine over writing a new rendering engine from scratch. Do not assume that any engine is a drop-in library or that a Linux/ARM64 binary will run on the CHOS kernel without an appropriate runtime and system interfaces.
If the selected engine requires functionality CHOS does not provide, implement the necessary compatibility layer, runtime, system services, process interfaces, filesystem interfaces, graphics integration, and networking interfaces. Where a separate browser runtime is technically necessary, design it as a maintainable component of CHOS rather than an unrelated external workaround.
Do not silently downgrade the objective to a text-only renderer, an HTML demo, a browser shell around a remote rendering service, or a collection of fixed website screenshots.
2.2 Required browser capabilities
The resulting browser should support the following, to the extent required by contemporary standards-based websites:
Web platform and rendering
Modern HTML and DOM behavior.
A standards-compliant CSS engine with practical support for Flexbox, Grid, responsive layouts, media queries, typography, and modern styling.
A genuine JavaScript engine with modern ECMAScript support.
Common browser APIs required by mainstream websites.
Image decoding, SVG, canvas, and common web fonts.
Correct text shaping, scrolling, layout, resizing, and high-resolution rendering.
Form controls, links, downloads, uploads, and ordinary website navigation.
Appropriate handling of accessibility features and keyboard navigation.
Browser application
Multiple tabs and windows.
Address bar, search integration, back/forward navigation, reload, stop, and loading indicators.
Bookmarks, browsing history, a downloads interface, and useful error pages.
Find-in-page, zoom controls, and configurable homepage and search provider.
Cookies, local storage, session storage, HTTP caching, and persistence.
Private browsing that does not retain its designated private session data after closure.
Permission prompts and clear security indicators.
Usable certificate and connection-error information.
Session restoration where reliable and safe.
Download destination selection, cancellation, and failure reporting.
Media playback, WebRTC, WebAssembly, service workers, and other substantial web APIs should be supported according to the selected engine's capabilities and CHOS's available device services. Do not invent support that has not been implemented and tested.
The overall target is a browser capable of using real, contemporary websites, not merely displaying simple demonstration pages. Use an established engine's supported APIs and conformance suites wherever possible.
2.3 Browser security
Security is mandatory.
Implement and validate:
Real TLS integration using a maintained and reviewed implementation.
Certificate-chain validation and hostname verification.
SNI and appropriate certificate validity checks.
A documented CA trust-store policy and update mechanism.
Reliable cryptographic entropy and a trustworthy clock.
Correct handling of redirects, partial reads/writes, malformed records, network failures, and timeouts.
Isolation between untrusted web content and privileged operating-system functionality.
Safe permission handling and download-path validation.
Secure process or equivalent isolation appropriate to the architecture.
A documented engine security-update and dependency-update policy.
Never disable certificate verification to make a website work. Never retry a failed HTTPS request over plaintext HTTP without an explicit, safe user action. Do not create homegrown cryptographic primitives as a substitute for an established TLS implementation.
Browser content must not receive direct administrative access to network devices, storage, or unrestricted kernel operations.
2.4 Rendering and application integration
Integrate browser windows into the CHOS window manager and desktop.
The browser must resize correctly, respond to input, use the available graphics acceleration when supported, and remain responsive while pages load, scripts execute, or downloads run.
Browser operations must not monopolize the system's main GUI execution path. Use appropriate worker threads, asynchronous I/O, or process isolation where supported. Bound resource consumption so a tab processing complex content cannot unnecessarily freeze the desktop.
The browser's toolbar, dialogs, tab strip, context menus, and error pages must use CHOS's established visual system. The browser should feel like a native application while rendering modern websites faithfully within the selected engine's capabilities.

PART 3 — Automatic Ethernet and Wi-Fi detection
Develop a system-wide network service that discovers available network interfaces and exposes them through a consistent API.
The browser must not implement its own independent hardware discovery or connection manager. Network configuration should be controlled by the operating system, with appropriate separation between privileged network operations and ordinary applications.
3.1 Ethernet connectivity
For supported Ethernet adapters, implement and validate:
Automatic device discovery and driver initialization.
Link-state detection, including cable disconnected, link establishing, and connected states.
Automatic configuration using DHCPv4.
Static IPv4 address, subnet mask, gateway, and DNS configuration.
IPv6 support, including appropriate address autoconfiguration and DHCPv6 functionality where required.
DNS configuration and resolution.
Default-route management.
Link and connection diagnostics.
Clear handling of DHCP failure, unreachable gateways, DNS failure, and physical link loss.
Reconnection after a link returns.
Persistent settings and predictable application of changes.
When Ethernet is connected and automatic configuration is selected, CHOS should configure the interface and provide internet connectivity without requiring users to open the console.
3.2 Wi-Fi discovery and connection
Implement the complete wireless functionality needed by supported hardware.
This includes:
Wireless adapter discovery and initialization.
Driver and firmware loading where required.
Scanning for nearby wireless networks.
Presenting discovered networks by SSID, signal strength, security type, and relevant connection status.
Refreshing scan results without freezing the GUI.
Connecting to a selected network through an appropriate authentication interface.
Password entry and handling of secured networks.
WPA2-Personal support and WPA3-Personal support where the chosen hardware, driver, and security implementation permit it.
Secure credential storage and appropriate protection of saved network profiles.
Remembered networks, automatic reconnection, connection priority, disconnect, forget, and manual reconnect operations.
Correct error reporting for authentication failures, unavailable networks, unsupported security modes, and missing firmware.
Appropriate handling of wireless regulatory requirements, channels, and bands for the supported hardware.
Do not implement fake scanning results or simulate a connection when the underlying adapter has not connected.
A complete Wi-Fi implementation may require substantial 802.11 MAC/PHY handling, firmware interfaces, authentication services, key management, and drivers. Implement these requirements properly for the selected supported devices. Do not claim general Wi-Fi support based solely on Ethernet tests.
3.3 Network hardware and driver strategy
Preserve the existing QEMU VirtIO network path.
For other platforms, inspect actual hardware and implement the appropriate drivers and firmware interfaces. Do not assume all ARM64 machines share one network adapter, wireless chipset, firmware format, or peripheral bus.
Establish an extensible hardware-support architecture that can accommodate additional controllers without duplicating the entire network stack.
Record a support matrix covering:
QEMU VirtIO networking.
VMware Fusion networking, including VMware-specific drivers if the selected configuration requires them.
Raspberry Pi 5 Ethernet and any selected supported wireless adapter.
Representative ARM64 laptops with explicitly identified supported network hardware.
Where hardware requires unavailable firmware, undocumented interfaces, licensing restrictions, or unimplemented kernel functionality, identify and address those dependencies rather than pretending the device is supported.
The goal is broad and extensible ARM64 compatibility, expanded through real driver implementations and tests. Do not claim compatibility with every ARM64 machine before it has been demonstrated.

PART 4 — Build the graphical Network Manager
Create a functional Network Manager integrated into the desktop, system tray, notification area, and Control Panel.
It must use the actual system network-service API and hardware state. The UI must not merely display hardcoded adapters or fictitious network names.
4.1 Connection list
Provide a network icon in the taskbar or notification area.
When opened, display available connections and their real status, including:
Connected Ethernet adapters.
Available Wi-Fi networks.
Currently connected Wi-Fi network.
Signal strength and security information where available.
Connecting, connected, disconnected, authentication-failed, and unavailable states.
Ethernet and Wi-Fi must be distinguishable. Refreshing the list must be responsive and must not interrupt an established connection unnecessarily.
The user must be able to select a Wi-Fi network, enter its password, connect, disconnect, and inspect the result. Ethernet should connect automatically according to the configured policy.
If no adapter is supported or no networks are found, show a useful explanation and a diagnostic route.
4.2 Network settings
Create a proper network configuration utility in the CHOS Control Panel.
For each interface, provide supported configuration options, including:
Automatic DHCP configuration or manually specified IPv4 settings.
IPv6 configuration options appropriate to the implementation.
DNS server configuration.
Gateway and subnet configuration where applicable.
Wi-Fi profile management and saved-network controls.
Connection priority and automatic-connect settings.
Adapter status and basic diagnostics.
Details such as assigned addresses, link state, gateway, DNS, and connection failures.
Validate user input before applying changes. Support Apply, Cancel, and restoration of the last known working configuration when a change makes the interface unusable.
Protect privileged network operations through a system service or equivalent access-control boundary. The browser and ordinary applications must never be able to change network configuration without authorization.
4.3 Classic Windows-inspired interaction
The Network Manager and network settings must be integrated into CHOS's classic desktop design.
Use the Windows 98/2000/XP Classic Theme as the reference for window frames, menus, system tray interactions, property sheets, buttons, tabs, lists, and dialogs.
The UI must have consistent typography, spacing, visual states, keyboard focus, accessible hit targets, and clear feedback. All visible controls must perform their advertised functions.

PART 5 — Network resilience and system-wide integration
Integrate the network manager, drivers, TCP/IP stack, DNS, TLS, browser, Control Panel, and system diagnostics as one coherent platform.
Implement and test:
Automatic recovery after Ethernet disconnect/reconnect.
Wi-Fi disconnect and reconnect handling.
DHCP lease acquisition, renewal, expiration, and failure.
Network changes while applications are running.
DNS failures, unavailable gateways, and loss of internet connectivity.
Multiple interfaces and route selection where supported.
Network service startup and shutdown.
Persistence of network profiles across reboots.
Graceful handling of adapters that disappear or fail initialization.
Proper cancellation of browser requests during network transitions.
Bounded retries and timeouts to prevent permanent waits.
A coherent network status API shared by the desktop, browser, and diagnostic utilities.
Where appropriate and implementable, provide proxy configuration and captive-portal detection. Do not report internet access simply because a network interface is administratively enabled or has an IP address.
Support IPv4 and IPv6 as defined by the implemented networking capabilities. Clearly distinguish a local link, a valid configured interface, gateway reachability, DNS success, and actual internet connectivity.

PART 6 — Performance and resource management
CHOS must remain responsive while networking and browsing.
Profile real behavior and optimize the measured bottlenecks.
Requirements:
Nonblocking network operations and responsive GUI updates.
Bounded queues, memory allocations, retries, and timeouts.
Efficient storage and caching policies.
Controlled browser process and tab resource usage.
No unbounded polling, busy-waiting, or repeated full-interface scans during ordinary UI operation.
No unnecessary rendering of unchanged controls.
Predictable cleanup of resources after navigation, cancellation, network loss, or tab closure.
Graceful error handling when memory or other resources are exhausted.
Measure boot-to-desktop behavior, network connection time, DNS resolution, HTTPS handshake, browser navigation, page rendering, memory consumption, and concurrent tab behavior on documented test configurations.
Establish reproducible benchmarks and regression thresholds using the available instrumentation. Do not invent performance metrics or optimize one test at the expense of system stability.

PART 7 — Testing and verification
Create a comprehensive test suite covering the complete internet subsystem.
Network service and driver tests
Test interface discovery, link state, DHCP, static configuration, DNS, IPv4/IPv6 behavior, connection loss, reconnect, saved profiles, authentication failures, adapter failures, and configuration validation.
Use host-side tests and simulated or virtual-device tests as appropriate, but distinguish these from actual guest hardware tests.
Wi-Fi tests
Test scan results, secured and open networks where appropriate, correct password handling, failed authentication, loss of signal, access-point disappearance, reconnect behavior, profile persistence, and unsupported hardware.
Complete live tests require actual Wi-Fi hardware exposed to the guest. QEMU Ethernet success does not prove Wi-Fi support.
Browser tests
Run the selected engine's relevant test suites and standards conformance tests. Add integration tests for:
Modern HTML and CSS layouts.
JavaScript execution and browser APIs.
Multi-tab navigation and resource loading.
Forms, images, downloads, browser storage, and history.
HTTPS certificate validation and hostname checks.
Redirects and mixed or insecure-content behavior.
Malformed responses, interrupted connections, and slow servers.
Memory pressure, long-running scripts, and browser recovery.
Browser isolation and permission boundaries.
Test against local fixtures first, then a documented selection of real contemporary websites from a live guest.
Do not use a list of websites that happen to load as the sole evidence of standards compliance.
Platform matrix
Test the configurations available to the project and record exact hardware, firmware, driver, build, and test details.
At minimum, perform QEMU integration testing and validate the selected VMware Fusion configuration independently. Add Raspberry Pi 5 and ARM64 laptop tests when the corresponding hardware or reliable test environment is available.
A test requiring unavailable hardware must be marked NOT RUN or BLOCKED, not PASS.

PART 8 — Required deliverables
Do not declare this stage complete until the following have been delivered and verified:
A documented, technically justified browser-engine and runtime architecture.
A working browser using a genuine modern browser engine, with real JavaScript execution and modern website rendering.
Integrated TLS with certificate validation and an explicit security policy.
A functional system-wide network service and interface-discovery API.
Automatic Ethernet connectivity on supported hardware.
Working Wi-Fi discovery and connection management for the explicitly supported wireless hardware.
A graphical Network Manager showing real adapter and connection state.
A functional Control Panel utility for configuring network interfaces, addresses, DNS, gateways, and Wi-Fi profiles.
Persistent network profiles and safe credential handling.
Browser-to-network-service integration with bounded I/O and meaningful connection errors.
Automated regression tests, engine conformance results, and documented live networking and browser tests.
Performance measurements and fixes for identified responsiveness or resource problems.
Updated source code, build instructions, dependency and licensing records, troubleshooting documentation, driver support matrix, known limitations, and SHA-256 manifests.
A clear final report stating what is implemented, what was tested, and which hardware configurations are genuinely supported.
Non-negotiable completion rules
Do not substitute the existing text-preview browser for a modern browser engine.
Do not simulate Wi-Fi networks, fake adapter detection, or misrepresent offline status.
Do not claim modern website compatibility without real JavaScript and rendering tests.
Do not bypass certificate verification or use insecure cryptographic fallbacks.
Do not hardcode support claims for untested ARM64 devices.
Do not regress the existing direct-boot, UEFI, QEMU VirtIO, filesystem, or desktop paths.
Do not leave visible controls as decorative placeholders.
Do not hide timeouts, skipped tests, unsupported hardware, or incomplete features.
Fix regressions caused by the implementation before declaring the relevant milestone complete.
If a dependency is missing, implement or properly integrate the required layer rather than silently removing the requirement.
If the selected architecture proves infeasible, document the evidence, evaluate viable alternatives, and continue toward the full functional objective instead of declaring success with a toy implementation.
Definition of Done
Stage 3 is complete only when a user can boot into CHOS, see the real network interfaces available to the supported machine, connect through Ethernet or choose and join a Wi-Fi network, configure their connection through the graphical settings interface, and use the integrated modern browser to access contemporary websites securely.
The complete system must remain responsive during network changes and browsing, retain supported configuration across restarts, and pass the required security and integration tests.
Do not proceed to final release qualification on the assumption that this stage is done. Produce the evidence required by every completion gate and maintain a transparent list of anything that still blocks the full objective.

