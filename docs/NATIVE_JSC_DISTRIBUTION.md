# Native engine diagnostic distribution

The native engine diagnostic is a development component, not the completed
modern browser or a production security release. Version remains 0.30.0.
[Its test scope and Fusion checklist](NATIVE_JSC_STANDARDS.md) distinguish actual
engine execution from the open WebCore, browser, wireless and security gates.

JavaScriptCore/WTF contain LGPL-2.0-or-later and BSD-licensed components; bmalloc
and bundled third-party files retain their own notices. ICU uses its original
Unicode license and data notices. The CHOS source includes the selected original
ICU, LLVM libc++/libc++abi/compiler-rt, musl, MbedTLS, lwIP and Test262 sources,
license files and pinned dependency manifests. Individual upstream notices govern
their files. WebKit's source reference is not a claim that the separate WebCore
browser has been ported or distributed.

An engine binary handoff must include the actual patched engine source closure,
original notices and license texts, the exact CHOS source archive with its native
port and build recipes, and the SDK/object material needed to relink the fixture
with modified engine libraries. A source URL alone is not the distribution
package. Preserve executable/source/build SHA-256 records and do not place
mutable acceptance-test disks or private build caches in the handoff.

The relinking material uses ordinary static engine archives containing the exact
original object bytes, rather than the build machine's thin archives with
absolute member paths. Keep all 53 separately linked interpreter/bytecode objects,
the fixture object and the native SDK with its CRT, ABI note, linker script and
runtime/ICU libraries. A relocatable recipe must work after extraction to a new
directory. Validate its result with the real engine-profile loader checker and
compare its stripped load/note segments with the qualified executable before
publishing. This permits inspecting and changing the libraries independently of
the diagnostic ISO; it does not create Linux binary compatibility.

Changes to the engine or SDK require rebuilding and qualifying a new executable.
An old successful fixture does not qualify newly modified code. Source/security
updates must preserve the strict manifest, patch replay, W^X, CPU/memory ownership,
certificate verification and explicit unsupported-feature boundaries. Production
engine update tracking, browser integration and complete security acceptance
remain open Stage 3 requirements.
