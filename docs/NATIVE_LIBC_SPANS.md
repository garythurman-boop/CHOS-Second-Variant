# Native string span operations

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
