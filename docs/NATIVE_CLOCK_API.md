# Native clock prerequisite

CLOCK_INFO (48) returns a fixed 48-byte, versioned, read-only status. It separates
monotonic and UTC availability, zeroes reserved/unavailable fields, and identifies
the existing firmware-snapshot or FDT PL031 source. Capacity, process ownership
and the complete writable user range are checked before copying. No clock-setting
operation or firmware callback is exposed to applications.

Monotonic seconds/nanoseconds come from the architectural counter and its nonzero
32-bit architectural frequency. The subsecond calculation is overflow-bounded.
The existing UEFI UTC snapshot advances with the same counter; invalid frequencies
or overflowing UTC values fail closed. PL031 supplies whole-second UTC. Wall-clock
availability is not authenticated time: this does not complete certificate date
policy, trusted clock synchronization, secure entropy or TLS.

The native C runtime adds clock_gettime(CLOCK_MONOTONIC/CLOCK_REALTIME), time and
stateless gmtime_r for 1970 through 9999, with explicit errors for unsupported,
unavailable or overflowing values. Outputs remain unchanged on failure. This is
not a complete POSIX time/locale/timezone or threading implementation; errno is
still per-process rather than per-thread.

Host/sanitizer tests cover fractional counters, wrap, missing source, malformed
frequency, Gregorian leap/century/year limits and output/error behavior. Actual
EL0 tests reject null/read-only/undersized destinations, preserve adjacent guards,
observe monotonic progress, call the API from native C/C++, convert real UTC,
and handle an invalid PL031 wall clock without inventing time. Synchronous direct
execution also works: its unavailable scheduler sleep returns ENOSYS, while clock
reads still progress. An early test incorrectly required scheduled sleep in that
profile; its failure is retained and the corrected test checks both contexts.

Run the clock host targets and tests/qemu_user_allocator.py in direct/uefi modes.
The latter now includes actual clock assertions and can use --rtc-base
1970-01-01T00:00:00 to test unavailable wall time. Results remain component evidence;
Fusion and physical target validation of the new API are NOT RUN. Stage 3 and
binary publication remain incomplete/held.
