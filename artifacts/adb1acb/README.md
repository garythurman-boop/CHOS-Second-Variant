# CHOS WebCore desktop preview for VMware Fusion

This Stage 3 diagnostic displays a local HTML page rendered by WebCore. Space
changes its text, color and layout through JavaScript. The preview runs in a
disposable ARM64 VM with one processor and 2 GB RAM.

1. Download the diagnostic ISO and the asset disk ZIP. Verify both downloads using the SHA-256 checksums. [ISO](CHOS-WebCore-window.iso?raw=1) · [Asset disk ZIP](CHOS-WebCore-assets.zip?raw=1) · [Checksums](SHA256SUMS)
2. Extract the ZIP. Keep both VMDK files in the same folder. With the test VM powered off, attach **CHOS-assets.vmdk** as its only hard disk, using the NVMe controller. Select the ISO for its CD/DVD drive and boot from CD/DVD.
3. Open **Diagnostics** and select **Detect NVMe (read only)** once. Check that persistent storage shows **Mounted**. The asset disk supplies the fonts and ICU data. Leave it read-only.
4. Open **Console** and enter `run /bin/jscvm.elf window`. Wait for the WebCore window to appear with text and colored rectangles.
5. Press **Space**. Look for the text **JavaScript changed the DOM**, the moved pink rectangle and the shifted green rectangle. [Expected desktop image](webcore-window.png)
6. Press **Escape** to close the preview. Repeat the launch to check cleanup and keyboard focus.

The distributed ISO and asset disk passed three launches under QEMU UEFI with
NVMe and a read-only mount. Direct and UEFI VirtIO also passed. Real Fusion
execution is awaiting user testing. The preview supports a fixed local page.
Internet navigation, text editing and general page input remain in development.

Source commit `adb1acba5099aef5650c6682ecbe45cf9557307d`. [Qualification results](../../docs/validation/stage3-webcore-window.json) · [Reproduction](../../tests/native_webcore_page/README.md)
