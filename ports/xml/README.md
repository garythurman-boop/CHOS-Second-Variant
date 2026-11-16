# Native libxml2 memory/custom-I/O profile

Original libxml2 2.15.4 parser, tree, SAX/push, XPath, schema, reader and memory
serialization implementations build against the real native runtime. Pthreads
and compiler-emulated TLS are genuine. Builtin UTF-8/UTF-16 and selected ISO8859
conversions remain; iconv/ICU conversion, Catalog, XInclude, dynamic modules and
zlib are excluded in this profile. Native HTTP was removed upstream in 2.15.

The explicit patch applies only to a build copy. CHOS does not implement the
POSIX open/dup descriptor transport expected by xmlIO.c, and exposes no stdin
stream. Default pathname/fd entry points report unsupported or NULL as their
API permits. No successful file operation or fake fcntl declaration is provided.
Actual FILE and caller-provided I/O callbacks remain original implementations.
The browser must feed parser bytes through its resource service rather than
letting an XML document obtain privileged device/file access.

The consumer installs a deny-by-default per-context external resource loader,
uses NONET/NO_XXE where appropriate, and never enables the HUGE option. Tests
cover actual tree/namespace/UTF8/entity references, XPath, serialization, 1/7/32
byte progressive parsing, partial/error custom reads, malformed XML and UTF8,
external DTD denial and entity-amplification bounds. Three real pthread workers
exercise independent parser contexts. These selected checks do not certify all
XML features, production hostile-document security or a complete browser.

Original MIT notices and source bytes are retained. Authentication receipts
record the official GNOME archive SHA256 and the independent signed Debian
InRelease -> Sources digest -> original-archive digest comparison. All 975
shared originals match exactly; the complete GNOME release includes additional
test/documentation files. The attempted GNOME detached signature URL returns404;
no upstream detached signature is claimed. No TLS/signature bypass was used.
