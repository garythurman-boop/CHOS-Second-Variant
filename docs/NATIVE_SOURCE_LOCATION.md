# Native C++ source-location metadata

The native SDK now includes the original LLVM libc++ 19.1.7 `source_location`
header needed by WebCore's resource error interface. Its bytes match the existing
pinned release archive and the independently fetched immutable upstream commit.
The [import receipt](dependencies/libcxx-source-location.json) records archive,
member, SHA256, Git blob and the actual ten-header preprocessing closure. The
other nine original headers were already present. No replacement implementation
or runtime workaround was introduced.

The actual compiler builtin supplies caller metadata. Host and native tests check
file, line, column and function names for direct calls, default arguments,
forwarded locations, constructors, macros, explicit copies and empty defaults.
Two calls on the same line retain distinct columns. An explicit `#line` mapping
produces the expected generated filename, line 702 and column 1.

The [validation record](validation/stage3-native-source-location.json) pins the
SDK9, source, executable and guest results. Direct and isolated UEFI QEMU each
pass three normal process exits with zero native heap allocations and complete
cold physical-page, frame, engine-owner and futex restoration. This qualifies a
standard-header prerequisite. Full WebCore page rendering, browser acceptance
and new physical-platform tests remain separate work; version stays 0.30.0.
