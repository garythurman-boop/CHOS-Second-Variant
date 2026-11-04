# CHOS native engine checkpoint a89db07

Development **0.30.0**. Stage 3 remains incomplete.
Exact source: `a89db07f60345bae56676eacfbefc7cf08f5973e`.

- [Normal installation ISO](CHOS-Fusion-a89db07.iso?raw=1)
- [Native JavaScript/Wasm diagnostic ISO](CHOS-Native-Engine-a89db07.iso?raw=1) — no installer
- [Engine source, SDK and relinking handoff](CHOS-Fusion-a89db07-Handoff.zip?raw=1)
- [Companion CHOS source archive](CHOS-a89db07-source.zip?raw=1)
- [Fusion test checklist](TESTING.md)
- [Validation and exact binary hashes](VALIDATION.json)
- [Engine build configuration and relinking records](ENGINE-REBUILD.json)
- [All downloads and prepared image](manifest.json)
- [SHA-256 checksums](SHA256SUMS)

The diagnostic ISO embeds `/BIN/JSCVM.ELF`. The normal installation ISO does not.
Open Console on the diagnostic desktop and follow TESTING.md. Both the handoff
and companion CHOS source archive supply corresponding source, licenses and build
material; ENGINE-REBUILD.json records the actual configuration. The handoff's
relocatable libraries reproduce the qualified executable byte for byte.

Full host/sanitizer suites and 21 guest profiles pass, including 30 native engine
lifecycles and 1,032 selected Test262 variant executions. Nine additional profiles
pass on these exact source-stamped binaries. Independent Fusion testing of this
new engine remains pending; earlier user-confirmed Fusion Ethernet is separate.

Modern WebCore rendering, the complete browser, native Pi5 Wi-Fi and final browser
security/platform qualification remain open. Version 0.50.0 is reserved for all
revised Stage 3 acceptance gates. The separate HTTP preview remains recovery
functionality. Prior downloads are retained.
