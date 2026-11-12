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

**Step 1, dependency closure and native execution.** The standalone PNG/JPEG,
Cairo, text and selected JavaScript checkpoints are complete within their stated
scope. Four original WebCore image implementation units compile for AArch64, but
have not linked or executed together. Next: close actual shared-buffer, decoder,
color/geometry and native-image dependencies; run the real component and present
its pixels. See [the integration boundary](WEBCORE_PORT_BOUNDARY.md).
