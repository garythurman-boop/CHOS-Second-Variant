# Native ordered containers and variadic ABI

The native SDK adds original libc++19.1.7 map, set, __tree and cstdarg headers.
The archive's existing pinned SHA256 and the expanded 770-file manifest protect
all original bytes. No substitute tree or variadic implementation is supplied.

Native C++20 direct and ordinary CMake/SDK C++23 isolated UEFI fixtures exercise
map/set/multimap/multiset, copy/move, node extraction, failed reinsertion and node
recovery, merge, duplicates, heterogeneous lookup, and lower bounds. A 3,000-step
insertion/update/removal/extraction workload compares the tree against independent
fixed arrays, checking complete sorted iteration. Three native workers repeat it
across three lifecycles; user heap allocations return to zero. Three process
executions retain stable warm physical frames and no private futex waiters.
C++23 adds real from_range/insert_range calls. The C++20 report records that those
C++23-only cases were not compiled.

The cstdarg fixture uses actual compiler va_list/va_copy/va_arg with independent
cursors and mixed int, double, AArch64 binary128 long double, uint64 and pointer
arguments. The existing genuine binary128 helpers remain linked. This is a
bounded SDK profile; no full C++23, exception-unwinding or engine claim follows.

The i18n dependency investigation also found that original ICU decContext.h
includes signal.h while its only raising code is explicitly disabled by ICU's
existing `#if 0`. `tools/prepare_icu_decimal_probe.py` verifies the pinned source,
checks that disabled code, and creates a header with only the unused include
removed. Force-including that header through its original DECCONTEXT guard lets
native dependency probing continue. It does not provide signal.h, signal
handlers, raise(), or altered trapping behavior. The output records original and
patched hashes; no original ICU source is silently modified.

See [recorded native results and current i18n gaps](validation/stage3-native-ordered.json).
These are QEMU/native runtime prerequisites, not a full i18n/data library, genuine
modern browser or Fusion/Pi/laptop qualification. The existing cc29ea8 runtime
ISO remains the selected Fusion user-test download.
