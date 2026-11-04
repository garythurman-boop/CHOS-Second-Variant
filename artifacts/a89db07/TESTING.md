# Fusion native engine checkpoint — CHOS 0.30.0

Exact source: `a89db07f60345bae56676eacfbefc7cf08f5973e`. Stage 3 remains incomplete. This tests actual JavaScriptCore; a modern page-rendering browser is still being built.

The normal `CHOS-Fusion-a89db07.iso` is installation media. The separate `CHOS-Native-Engine-a89db07.iso` in this handoff boots the engine diagnostic and has **no installer payload**. Use a temporary or cloned ARM64 VM with 1 CPU and 2 GB RAM. If an installed disk boots first, select the diagnostic CD/DVD in the firmware boot menu.

Open the desktop Console and run these commands separately, waiting for completion:

```text
run /bin/jscvm.elf wasm
run /bin/jscvm.elf async-wasm
run /bin/jscvm.elf watchdog
run /bin/jscvm.elf mixed
run /bin/jscvm.elf test262
```

Each prints a positive `[ OK ] actual ...` completion line. The last command must print `[TEST262 TOTAL] passed=172 failed=0`, followed by its completion line; QEMU takes roughly two minutes. `watchdog` intentionally evaluates and terminates infinite scripts internally. Its five case lines must report success, including threshold1/calls1 and threshold2/calls2. The desktop must remain usable while it runs. `async-wasm` checks real asynchronous compilation/instantiation and Promise completion.

Repeat `mixed` three times. Check the clock, mouse and keyboard during each run; inspect `tasklist` afterward for normal exits and completed cleanup. This build has automated direct/UEFI QEMU evidence, but independent Fusion execution of the new engine is not yet established.

Also check `netstatus` and example.com in the separate HTTP recovery browser; held Backspace and blinking carets in Console/browser/editor; typing `i` without Setup opening; Console wheel/PageUp/PageDown; resolution controls. Test installation/persistent files, disk-only restart and shutdown using the **normal** ISO. Shutdown must show its window for at least five seconds and then power off.

For a failure, capture the exact command and its output, plus `bootlog`, `drivers`, `pci net`, `netstatus` and `sysinfo`. The known target configuration is Fusion26H1u1/macOS27/M1MacBookAir,1CPU/2GB; this remains a user-test target, not an automated pass.

WebCore rendering, the full browser application, native Pi5 Wi-Fi, production browser security and remaining platform acceptance are still open. Version0.50.0 is reserved for all revised Stage3 gates. ENGINE-DETAILS.md records exact selected-suite scope and prior failures. README.txt and DISTRIBUTION.md describe supplied sources, licenses and relocatable relinking material.

If engine initialization fails, also run `rng-test` and `clock` and capture their output. The engine requires real OS entropy and clock services; unavailable services must report failure. See ENGINE-REBUILD.json for actual build configuration and relinking records.
