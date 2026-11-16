# Native string span and search operations

The native C runtime now implements `strspn` and `strcspn`, required by the real
SQLite dependency in roadmap step 2. A per-call byte-membership table handles
all unsigned byte values, stops at the first NUL and stores no shared mutable
state. Public declarations and native exports are supplied alongside the
existing separately named host-testable core.

Host checks compare 65,280 rotated byte-alphabet/set-length cases per operation
with the host C library, including empty inputs/sets and high-bit bytes. Real
inaccessible pages follow both input and accept/reject strings; operations must
stop before those guards. Address, undefined-behavior and leak sanitizers pass.
The new functions are part of the freshly built, verified SDK6 consumed by the
qualified native SQLite/XML fixtures. This is a C-library prerequisite, not
WebCore/page-rendering completion.

The first host guard setup mistakenly used the native `sysconf` selector through
a native header while linking the host C library. The test now calls the host's
argument-free `getpagesize`, avoiding incompatible selector values. The original
failed assertion is retained in evidence; no runtime workaround was introduced.

The subsequent SDK7 checkpoint adds real public `strpbrk`, required by Fontconfig,
using the same local unsigned-byte membership table. Host comparisons cover all
65,280 cases per function and protected terminators with address, undefined-behavior
and leak sanitizers. A separately linked native fixture checks actual public
exports against a linear-membership oracle, with 65,280 cases in its main thread
and 24,576 more across three pthread workers. Both strings immediately precede
real inaccessible pages in the selected boundary cases. Direct and isolated UEFI
each pass three process lifetimes with zero heap/mappings before return and exact
cold physical-page, engine, commit and futex restoration. Exact SDK, source, binary
and result hashes are in the [search validation](validation/stage3-native-string-search.json).
This remains a runtime prerequisite; it does not establish page rendering.
