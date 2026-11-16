# Fontconfig memory catalogue profile

This port builds 25 original Fontconfig 2.17.1 translation units with actual
FreeType, libxml2, pthread mutexes and compiler atomics. The original sources
remain byte-identical in `third_party/fontconfig`; the builder applies
`memory-catalogue.patch` only to a fresh build copy and runs the original header,
alias, language, case-folding and gperf generators.

The caller installs an explicit `FcConfig` and adds real read-only font files
before global `FcInit`/NULL-config matching can work. Family, language, charset,
style fallback, pattern substitution/formatting, font sorting/listing and
`FcFontRenderPrepare` use the original implementations. FreeType opens actual
CHFS snapshot files; directory/file checks use the existing CHOS metadata API.
This is not a fabricated catalogue or a fake successful font scan.

Default configuration paths, directory scanning, disk-cache serialization and
change watching are excluded. Unsupported configured disk operations return
failure with `ENOTSUP`; excluded exported scanning/cache definitions remain
unlinked. An empty directory set legitimately produces an empty system font set
while keeping the real application font set. Serialized cache patterns cannot be
created by this profile; unexpected immutable cache-pattern misuse aborts.

The explicit patch also bounds both upstream retry loops when no default
configuration can be loaded, finalizes complete libxml2 configuration documents
so truncated XML is rejected in required loads, and uses real bounded `snprintf`
instead of the unavailable unbounded `sprintf`. Optional configuration loads
retain upstream's `complain=false` return convention; use `complain=true` when
invalid input must make the load return false.

After releasing consumers, release the caller's configuration reference with
`FcConfigDestroy`, then the global reference with `FcFini`. Each extra successful
`FcInit` on an already installed configuration adds a reference and needs its own
balanced `FcFini`. XML thread/error globals have their separate process lifetime;
Fontconfig does not independently tear down another consumer's libxml2 runtime.

See [native qualification](../../docs/NATIVE_FONTCONFIG.md). Full filesystem
Fontconfig, WebCore page rendering and Stage 3 acceptance remain incomplete.
