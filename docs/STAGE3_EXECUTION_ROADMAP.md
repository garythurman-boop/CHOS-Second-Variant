# Stage 3 execution roadmap

This is the user-approved order of work. Continue from the latest checkpoint
when the user says “Continue”, “Continue work”, or a similar instruction. Do not
restart earlier milestones or seek another approval to proceed within this scope.

1. **Run the original WebCore image pipeline.** Link the original image adapters,
   resolve their real dependencies, and display decoded images in an owned CHOS
   window. Qualify incremental input, pixels, lifetime, cancellation and cleanup.
2. **Reach real page rendering.** Complete the CHOS WebCore/PAL backend, image
   buffers, font selection and graphics/input integration. Validate actual HTML,
   CSS layout and JavaScript DOM changes.
3. **Connect WebCore to secure networking.** Implement real asynchronous resource
   requests, HTTPS/certificate/trust/time policy, cancellation and persistent data.
4. **Build the browser application.** Integrate the revised navigation, tabs,
   bookmarks/history, forms, transfers, find/zoom, private browsing and errors in
   the established CHOS interface.
5. **Complete Pi 5 Wi-Fi and network management.** Implement CYW43455 SDIO/firmware,
   discovery, authentication, protected profiles and reconnection. Preserve Ethernet.
6. **Harden and qualify the complete system.** Exercise security, permissions,
   malformed input, resource pressure, persistence, network failures and performance
   on QEMU, Fusion, Pi 5 and explicitly identified ARM64 laptop hardware.

## Checkpoint discipline

Work continuously without asking for permission between these steps. Aim to push
coherent source checkpoints approximately every thirty minutes, including during
longer sessions, so a model rate limit does not lose the work. Record exact checks,
known failures, current source hashes and the next action in the checkpoint.
A partial component must be labeled incomplete; committing it does not establish
feature acceptance. Do not publish unqualified binaries. Critical tested user-test
milestones may receive binaries under the existing publication policy.

The latest durable technical checkpoint is in
[the current Stage 3 report](STAGE3_CURRENT_RESULTS.md), with exact test reports
linked there. Update this roadmap's current position when a step advances.
Full revised Stage 3 acceptance is required before version 0.50.0.

## Current position

**Step 1 selected image checkpoint complete; step 2 starting.** Original PNG/JPEG
adapters now link and run in owned CHOS windows, including fragmented input,
cancellation and surface lifetime. A real 3 MiB contiguous WebCore image backing
also passes complete/fragmented decoding and cleanup with the qualified engine
allocator. Direct and UEFI tests compare every visible frame pixel. Broader codec,
security and platform qualification remains open. Next: configure and build the
real CHOS WebCore/PAL page target, supply its actual XML/SQLite dependencies, then
reach local HTML/CSS/DOM painting. See [image execution](NATIVE_WEBCORE_IMAGES.md)
and [the full port boundary](WEBCORE_PORT_BOUNDARY.md).
