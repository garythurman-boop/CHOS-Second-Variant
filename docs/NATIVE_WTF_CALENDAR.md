# Native UTC calendar and WTF date, UTF and process helpers

The native runtime now provides real UTC mktime/timegm normalization using four
original musl1.2.5 calendar cores. gmtime_r/localtime_r support negative epochs and
the full representable native tm_year range, with checked failure when the result
cannot fit. Successful normalization sets weekday/year-day and UTC/DST metadata.
Failed conversions leave the caller's structure unchanged. The wrapper handles
INT_MIN day fields in64-bit arithmetic before invoking the original core, avoiding
the original int subtraction on that extreme input.

Local time remains UTC in this native C profile. timegm ignores DST hints;
mktime accepts zero/automatic hints and explicitly refuses positive DST requests.
This is not an IANA timezone service, desktop-zone synchronization, Intl completion
or a statement that wall-clock availability is authenticated trust. The existing
clock validity/error behavior remains checked independently.

The runtime also adds invariant-C strcasecmp/strncasecmp and public immutable
strerror messages with POSIX int-returning strerror_r. Case folding applies to
ASCII; other bytes compare unsigned. Unicode properties and localization belong
to the real ICU/UTF layers. The reentrant message function checks capacity, returns
ERANGE with terminated bounded output when truncated, and does not share scratch
storage. It uses the maintained musl message table, including its original
“No error information” fallback for unenumerated codes. No locale ABI is invented.

sleep provides a bounded native request of up to600seconds, including one extra
scheduler tick so a call just before a timer interrupt cannot return early. The
600-second boundary uses two bounded waits; all other supported requests use one.
Refused or malformed transport results return a conservatively rounded unslept
amount and errno. Zero returns immediately, excessive requests fail immediately,
and there is no retry loop without progress. _exit delegates to the actual
immediate process termination path and bypasses user finalizers; normal exit
retains the existing finalizer registry.

## Actual engine source integration

The checked upstream patch now includes native unistd declarations in WTFProcess.
A redundant iostream include in simdutf's scalar Base64 section is guarded only
for CHOS: that section does not use streams, and optional debug logging includes
its own header separately. No UTF/Base64 algorithm or stream implementation is
substituted. General C++ iostream support remains unprovided.

The native application links actual upstream DateMath, CurrentTime, WallTime,
MonotonicTime, SIMDUTF and WTFProcess units. It executes the real ES5 date parser,
UTC offset calculation, strict UTF8 validation and UTF8/UTF16 conversion. Malformed
encodings are refused. Inputs placed at the end of a readable page test vector
validation against an adjacent protected page. This is engine-component execution,
not a JavaScript VM or a rendered modern browser.

## Qualification

A separately compiled glibc oracle compares200,000 full-width field normalizations
and200,000 timestamp splits, with extreme integers, century/leap boundaries and
concurrent callers. Native structs are not aliased to host tm layouts. Normal and
ASan/UBSan builds pass. Independent unbounded Python/calendar arithmetic supplies
702 native vectors, including years outside datetime's ordinary range. Refusals
preserve input structures and successful values agree exactly.

Host checks also cover40,000 independent ASCII comparisons, bounded unterminated
inputs, immutable known/unknown messages, guarded truncation and concurrent error
callers. Sleep tests verify the request boundary, no early-zero fallback, denied
and malformed results and refusal of its second boundary chunk. Legacy clock and
platform tests now link the real calendar cores and treat epoch-1 as a valid
1969 timestamp; their invalid-pointer and unrepresentable-value checks remain.

In native guests, three workers repeat the date/UTF/string operations, then leave
zero user-heap ownership. Child cases check normal finalization, immediate exit
without finalizers, denied sleep and kernel reaping of a live sleeping worker plus
an abandoned16KiB allocation. Three process lifecycles return to the cold PMM
baseline with no queued futexes. A real one-second sleep is checked against the
monotonic clock, with explicit lower and upper bounds.

```sh
source /workspace/chos-tools/activate
make native-sdk CC=clang LD=ld.lld -j4
python3 tools/build_wtf_calendar.py --source "$WEBKIT_SOURCE" \
 --native-sdk build/native-sdk --engine-build "$JSC_BUILD/cmake" --out "$APP_OUT"
python3 tests/qemu_wtf_calendar.py --mode direct \
 --application "$APP_OUT/cmake/wtf_calendar" --out "$DIRECT_OUT"
python3 tests/qemu_wtf_calendar.py --mode uefi \
 --application "$APP_OUT/cmake/wtf_calendar" --out "$UEFI_OUT"
python3 tests/generate_calendar_vectors.py --check
make test CC=clang LD=ld.lld -j1
make test-sanitize CC=clang LD=ld.lld -j1
```

See [qualification and retained investigations](validation/stage3-wtf-calendar.json).
The SDK contains1,172 verified files and actual calendar/error/case/sleep/immediate
exit links. Existing missing-import/library and full-ICU image-budget refusals stay
enabled. Original selected musl sources now total280 unchanged files.

Two initial parallel regression checks timed out: one during QMP negotiation and
one while waiting for a process diagnostic. Neither trace reports a kernel/user
fault. Individual reruns pass with identical kernel/ISO inputs and the original
deadlines; the precise timeout cause is not established and both failed traces
remain retained. Separately, failed monitor negotiation now terminates/reaps the
owned VM and closes all pipe/socket handles. A real-child fault test covers both
greeting and capability failures, plus idempotent cleanup. No guest timeout or
resource-equality assertion was relaxed.

The complete WTF attempt still records eight failed compilation units: native
filesystem/descriptor services, actual CPU/memory reporting and C++ stream
integration need further work. Subsequent linking and complete Thread/GC lifecycle
are not qualified by this component. Engine-sized memory/stacks, JSC execution,
modern rendering/browser behavior, production security, live Pi5 wireless and
independent platform acceptance still gate Stage3/version0.50.0. No new ISO is
published; the latest user-test artifact remains cc29ea8/version0.30.0.
