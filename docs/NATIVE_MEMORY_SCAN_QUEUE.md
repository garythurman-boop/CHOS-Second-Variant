# Native memory scanning and standard queues

The native SDK now supplies `sscanf` and `vsscanf` through pinned musl1.2.5
conversion routines and a private, immutable, bounded input view. The terminating
NUL is EOF, never a character returned by `%c`. Each call owns its view; this
does not expose the private representation as the public `FILE` ABI or add
`fscanf`, `vfscanf`, stdin scanning or filesystem refills. Existing strto*
lookahead behavior is unchanged. Binary128 scanning uses the existing original
floating parser and compiler-runtime support, rather than narrowing to double.

Original upstream files remain byte-identical and hash checked. A checked
generated copy initializes cleanup pointers explicitly, rejects overflowing
field widths and positional selector0 before assignment, and counts complete
UTF-8 characters for wide fields. Suppressed wide fields advance correctly;
truncated sequences report EILSEQ. Wide scanset patterns currently accept ASCII
only; non-ASCII patterns fail with ENOTSUP. This is the fixed UTF-8 native profile,
not complete locale or Unicode scanset support. Output buffers must satisfy the
usual scanf contract: callers must supply suitable widths and storage.

Original LLVM libc++19.1.7 `deque` and `queue` headers, including priority_queue,
are staged without modifying their algorithms. They use the existing bounded
native allocator and fail under its actual allocation policy. No new resource
quota or kernel capability is introduced.

The scanner is required by actual JavaScriptCore option parsing. That unit also
now includes the real native strings header for strncasecmp. The CHOS JSCOnly
profile disables the optional sampling profiler by default and explicitly rejects
enabling it until a foreign-thread context backend exists. Current-thread
register visibility does not implement foreign sampling. This configuration
does not remove JavaScript, WebAssembly or browser completion requirements;
JIT remains disabled under the existing fail-closed interpreter profile.

Host checks compare10,000 independent integer/hex-float inputs against the host
scanner, exercise exact-size/red-zone input bounds, varargs, allocations, limits,
UTF-8 widths and errors. The native fixture tests actual binary128 bits, UTF-8
conversion, deque growth/reference stability, move-only FIFO and priority order,
thread-local errno and concurrent scanner/container lifecycles. Guest runners
require three process lifecycles, cold page equality and zero futex waiters.
Accepted results and retained failures are recorded in
[the qualification manifest](validation/stage3-native-memory-scan.json).
The full host and configured ASan/UBSan suites pass, followed by thirteen
synchronized guest profiles covering the component, floating conversions,
stdio/threads/synchronization, ICU resources, IPv4/IPv6 TLS, typing and the
e1000e/UEFI platform. These results do not resolve the earlier synchronization
timeout recorded in the chrono qualification. Complete raw guest evidence is
retained in verified nested archives; only matching duplicate disposable disks
and firmware-variable copies are removed from the working evidence directories.

Build with the prepared toolchain and SDK:

```sh
make test-memoryscan test-memoryscan-sanitize CC=clang LD=ld.lld -j1
make native-sdk CC=clang LD=ld.lld -j4
python3 tools/build_native_scan_queue.py --native-sdk build/native-sdk --out /tmp/chos-scan-queue
python3 tests/qemu_native_scan_queue.py --application /tmp/chos-scan-queue/cmake/native_scan_queue --out /tmp/chos-scan-direct
python3 tests/qemu_native_scan_queue.py --mode uefi --application /tmp/chos-scan-queue/cmake/native_scan_queue --out /tmp/chos-scan-uefi
```

Use fresh output directories and synchronized kernel/ISO symbols. These component
checks do not qualify a complete engine, JavaScript, browser rendering, production
security, native Wi-Fi or independent Fusion/Pi5/laptop support. The development
version remains0.30.0; Stage3/version0.50.0 remain gated on the revised specification.
