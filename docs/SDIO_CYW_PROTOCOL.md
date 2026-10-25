# Pi 5 wireless transport implementation

The native SDIO and CYW protocol layers now have an actual bounded implementation,
freestanding AArch64 compilation and host/sanitizer evidence. They are prerequisites
for the Pi 5 CYW43455 driver. They are **not a working Wi-Fi adapter**: no Pi host
controller is bound, no firmware runs, and no scan, authentication or association
has occurred. The desktop continues to report the absence of a supported attached
wireless adapter. Ethernet selection and its device callbacks are untouched.

`include/chos/sdio.h` separates the SDIO protocol from a platform-owned host.
`include/chos/cyw_sdio.h` adds the radio's backplane and firmware wire formats. A
future host supplies the bounded native SDIO request callback and monotonic clock;
it must first own the real regulator, clock, pinmux and controller. The initial
contract accepts PIO only. A DMA callback cannot opt in without a separate reviewed
ownership, mapping and cancellation implementation.

## Implemented contracts

* Native CMD5 voltage negotiation, bounded power readiness, CMD3 RCA assignment,
  CMD7 selection and CCCR revision checks. `probe_step` submits one command per
  call. It rejects SD-memory/combo cards, incompatible voltage, missing functions,
  invalid responses and unsupported revisions. No SD-card probing occurs at boot.
* Common CIS pointer and tuple traversal, bounded to 512 bytes and 32 tuples, with
  actual little-endian MANFID vendor/product extraction. Duplicate/malformed IDs,
  invalid pointers, missing terminators and unterminated chains are rejected.
* CMD52 byte access and CMD53 finite byte/block transfers, checked function/address/
  count bounds, correct 512-byte zero count, and fixed-address FIFO versus incrementing
  memory access. Function-ready bits come from IORDY, never from the enable write.
  Function block sizes are committed only after both FBR writes succeed.
* Maximum transfer is 32 KiB, with host chunks at most 4 KiB and at most 64 command
  submissions. Each host callback receives a one-second limit. Timer checks also
  stop the enclosing operation before and after partial transfers. They cannot
  preempt a broken host callback; honoring its bounded completion contract is a
  mandatory host-driver acceptance test. Counter-independent iteration limits
  prevent a stopped clock from making protocol loops infinite.
* Host error, card response error or timeout quarantines the bus and clears ready
  functions. No further requests occur. Reinitializing an attached state is rejected;
  a fresh state is permitted only after actual platform power reset. This first
  implementation holds no DMA pages or credential data.
* CYW attachment requires real CIS vendor `02d0`, product `4345` or `a9bf`, F1 ready
  and its configured 64-byte block size. An unrelated card is rejected before any
  CYW register write. Backplane accesses program three F1 window bytes, cache the
  window only after all writes succeed, set the word-access flag, split at 32 KiB
  boundaries and reject wrap/unaligned transfers. The caller serializes this owner.
* Physical chipcommon ID decoding accepts only the selected `4345` revision 6
  CYW43455 profile. Other revisions return their actual parsed ID with UNSUPPORTED;
  firmware matching must be audited before extending the profile. No guessed chip
  revision, RAM size, firmware entry point or regulatory data is supplied.
* SDPCM control/event/data framing validates complemented hardware length, data
  offset and bounded payload. Coalesced/glom descriptors and unsupported channel
  flags are explicitly refused. It preserves actual sequence, flow-control and
  credit fields for the future bus state machine. A valid header does not imply
  available TX credit, a connected radio or an authenticated network.
* Board NVRAM conversion validates bounded text, keys, entries and conflicting
  duplicates; retains calibration/country fields; removes comments/line endings;
  writes NUL-separated entries, zero padding and the complemented word-length
  token. Identical duplicated board entries are coalesced. Multi-device files are
  refused. No calibration defaults or country/channel overrides are invented.
  The template MAC is always replaced by a nonzero unicast **platform-provided**
  MAC. Failures leave the caller's output/length unchanged. Packed NVRAM is not
  stored in the repository or sent to hardware by the tests.

## Evidence and reproducibility

Wire-format facts and Pi mappings were checked against immutable Raspberry Pi
Linux commit `5bd564aaa8dc4b1b8ed11733c19ec8ece63a72cc` and firmware-nonfree commit
`3bab0f823f5b53150b76aab77093adef6655b920`. URLs, SHA-256 hashes, inspected licenses
and failed path lookups are in [the reference inventory](dependencies/pi5-wireless-protocol.json).
No Linux implementation or firmware binary is vendored by this change.

The pinned Pi5 DTS identifies `brcm,bcm2712-sdhci` on SDIO2, host and configuration
register resources, clock source, SPI interrupt 274, WLAN GPIO28 and 150 ms power
startup. These are references for a future DT-validated host implementation,
**not hardcoded MMIO probes**. The firmware symlink `cyfmac43455-sdio.bin` is resolved
by Debian's standard/minimal update-alternatives policy at install time; it is not
an independently present binary in that repository. Exact firmware/CLM hashes,
revision compatibility, redistribution notices and regulatory policy still must
be fixed before installing or starting firmware.

`tests/test_sdio.c` exercises actual request construction and memory/window/FIFO
effects through an SDIO card model, including malformed discovery/tuples, CRC and
R5 errors, timeout/quarantine, exact bounds, block/byte count semantics, window
crossing/cache failure, wrong radio revisions, malformed SDPCM and NVRAM failures.
The normal and ASan/UBSan binaries pass. Both production sources also compile for
the freestanding AArch64 target. This is **host model/build evidence only**.

```sh
make test-sdio test-sdio-sanitize
# Optional independently downloaded and SHA-verified board-file fixture:
build/test-sdio /path/to/brcmfmac43455-sdio.txt
```

The actual pinned 2074-byte board NVRAM fixture packs to 1736 bytes with an explicit
synthetic **test-only** MAC. This verifies compatibility with the selected board
file, including its repeated `btc_mode=1`; it does not establish radio calibration
or MAC ownership. The fixture and produced output remain outside the repository.

## Open driver and acceptance gates

Pi5 boot/DT ownership, BCM2712 SDIO2 host/pinmux/clock/power sequencing, real CIS and
chip identity, core enumeration and RAM sizing, firmware/CLM integrity and licensing,
core reset/start, mailbox/interrupt handling, SDPCM sequence/credit/recovery state,
BCDC control, asynchronous scan/connect events, WPA2 key management, secure saved
credentials and network-service registration remain unimplemented. WPA3 must be
based on actual firmware/security capability evidence. Wi-Fi scan, association,
wrong-password, AP disappearance, reconnect, regulatory and throughput tests remain
NOT RUN. QEMU/Fusion Ethernet cannot satisfy those radio tests. No revised Stage3
completion or physical Pi5 support is claimed from these prerequisite tests.
