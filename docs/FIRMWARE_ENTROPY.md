# Firmware-seeded entropy prerequisite

UEFI handoff ABI7 can carry64 seed bytes obtained before ExitBootServices through
EFI_RNG_PROTOCOL. The loader tries raw entropy, SP800-90 Hash256, HMAC256 and
CTR256, once each, with no default/unknown-algorithm or timer fallback. Failure
zeroes the buffer and records absence. Only values are passed; firmware callbacks
are never called by the kernel. Boot-info validation checks size/algorithm pairs.

After heap initialization, the kernel consumes/erases a valid seed once. It refuses
all-zero or repeated32-byte blocks; those checks do not certify entropy quality.
Pinned MbedTLS3.6.7 LTS implements HMAC-DRBG/SHA256, with SHA256 selected explicitly.
The boot profile's26-file compiled closure is byte-identical to upstream commit
068ff080b369adfac81509f9b57b2afabaf82dc5, selected under Apache-2.0; full dual-license
text is retained. No cryptographic primitive is written in CHOS. Platform adapters
provide allocation, private string comparison, assertions and nonverbose self-test
output. The manifest verifies bytes/licenses, not a third-party security audit.

The boot DRBG has a finite4096-call budget, at most256 bytes/call (at most1MiB).
It has no live reseed source: boot callback pointers are cleared after seeding.
On failure/exhaustion its state is erased/freed and further reads return unavailable.
There is no claim of prediction resistance or recovery after state compromise.
Real reseeding and production lifetime policy remain open before finalStage3.
Native VirtIO remains preferred. A temporarily pending native queue returns no
bytes rather than silently switching providers. `rng-firmware-test` is a trusted
serial diagnostic: it draws32 bytes directly from the boot DRBG and erases them,
logging only success/failure. No seed or generated samples are printed.

The firmware/hypervisor is part of the seed trust boundary. Fusion must actually
provide one of the accepted algorithms; its availability has NOT RUN here. This
change cannot establish Fusion entropy, HTTPS, certificates, authenticated clock,
CA updates, a modern browser or a completed Stage3 by itself. No new binary is
published and0.50.0 remains reserved for total revised Stage3 completion.

Primary firmware interface sources are edk2-stable202605, commit
b03a21a63e3bd001f52c527e5a57feddb53a690b:
MdePkg/Include/Protocol/Rng.h and MdePkg/Include/Guid/Rng.h. They describe the shared
UEFI interface, algorithm GUIDs and exact-output-length contract. The CHOS code
implements that interface; the upstream headers are reference evidence only.

Validation includes upstream NIST CAVS14.3 SHA256 no-reseed vector0, SHA256 self-tests,
seed erasure, rejection, allocation failure, changing output, complete output-budget
exhaustion, state cleanup, reinitialization refusal and ASan/UBSan. The DRBG's SHA1-only
built-in self-test would be a no-op in this config; it is NOT used as DRBG evidence.
The explicit SHA256 CAVS test makes two128-byte generations and checks all128 bytes
of the second against the published vector.

Actual QEMU AAVMF tests cover firmware seed capture and generation after EBS, plus
an absent-device run that leaves both firmware/public entropy unavailable. Native
ELF protection/cleanup and Intel Ethernet regressions remain independently tested.
Use `make test-seed-rng test-seed-rng-sanitize test-crypto-integrity` and
`tests/qemu_firmware_rng.py` (optionally `--missing`) with fresh evidence directories.
Source provenance and exact test results are recorded in validation/stage3-entropy.json.
Monitor upstream LTS security updates and repeat provenance/negative tests before
shipping; do not treat a permanently frozen version as a security update policy.

The shared upstream directory now also contains the native TLS client closure.
The manifest records the original boot-profile files separately; TLS does not
change the kernel DRBG configuration or enable kernel-side secure browsing. See
[NATIVE_TLS_CLIENT.md](NATIVE_TLS_CLIENT.md) for its tested scope and open policy.
