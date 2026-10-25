# Selected wireless target: Raspberry Pi 5 onboard CYW43455

The user's requested first wireless target is the Pi 5's onboard adapter. The
Raspberry Pi OS firmware repository independently maps **Pi 5 Model B** to the
**Cypress/Infineon CYW43455 family**, exposed through SDIO. This is consistent with
the CYW43455 identification, rather than an assumption based on the CPU's brand.
The radio's actual chip/revision must still be read before firmware download.

Pinned primary-source evidence:

* Raspberry Pi firmware-nonfree commit
  `3bab0f823f5b53150b76aab77093adef6655b920`:
  [board binary mapping](https://github.com/RPi-Distro/firmware-nonfree/blob/3bab0f823f5b53150b76aab77093adef6655b920/debian/added-firmware/brcm/brcmfmac43455-sdio.raspberrypi%2C5-model-b.bin)
  points to `../cypress/cyfmac43455-sdio.bin`; its CLM mapping points to
  `../cypress/cyfmac43455-sdio.clm_blob`. The board NVRAM entry points to
  `brcmfmac43455-sdio.txt`. Preserve the selected release's mappings and licenses;
  never substitute unverified generic firmware or invent calibration data.
* Raspberry Pi's Linux `bcm2712-rpi-5-b.dts` explicitly routes WLAN through
  **SDIO2**, four-bit, nonremovable, with `wl_on_reg` GPIO28 and a 150 ms startup
  delay. **SDIO1 serves the SD card.** Its `brcm,bcm4329-fmac` child compatible is
  a generic binding, not evidence that the physical radio is BCM4329. The live
  rpi-6.12.y source was inspected; it is hardware-reference evidence, not code
  compiled into CHOS.
* Official Raspberry Pi documentation commit
  `41e24fbd459d47e7db979cd084676c790b60a3de`,
  [Pi 5 components](https://github.com/raspberrypi/documentation/blob/41e24fbd459d47e7db979cd084676c790b60a3de/documentation/asciidoc/computers/sbcs/sbc-models.adoc),
  specifies dual-band 2.4/5 GHz 802.11ac and Bluetooth 5.0/BLE. It does not identify
  the chip number; the board firmware mappings supply that evidence.

The public product-brief/news hosts returned proxy 403 in this environment. Those
unread documents are not used as evidence. Initial guessed documentation/NVRAM
paths returned 404; the successful paths above were obtained from repository trees.

## Implementation and acceptance boundary

CHOS now has a bounded native SDIO/CYW protocol layer: native command enumeration,
CCCR/CIS identity, CMD52/53 byte/block/FIFO transfers, CYW backplane windows, actual
chip-ID decoding, SDPCM framing and board-NVRAM preparation. It has host/sanitizer
and freestanding AArch64 build evidence. See [the implementation and exact limits](SDIO_CYW_PROTOCOL.md).
It is not registered as an attached adapter and produces no scan/connection data.

CHOS does not yet have a boot-validated Pi 5 platform, SDIO2 host driver, regulator/
pin/clock/interrupt ownership, complete CYW43455 firmware transport state machine,
firmware loader/start, asynchronous escan/connect events, supplicant/key management
or protected wireless credential store. Identifying the chipset and implementing
the wire formats do not complete those layers.
The Pi's SDIO radio is not exposed by Fusion and cannot be certified by an Ethernet
guest. Live Pi 5 wireless, wrong-password, AP loss, WPA2, reconnect and regulatory
tests remain NOT RUN until the driver and actual board environment are available.

Use separate bus/device transport and radio operations below the existing OS
network service. Preserve VirtIO, Intel 82574L and VMXNET3 Ethernet; never replace
them with a wireless placeholder or fake scan results. Hardware capabilities,
firmware/revision/regulatory restrictions and supported security modes must be
reported from the real radio. WPA3 requires explicit firmware/security evidence.
Firmware redistribution and CLM/calibration license obligations must be recorded
before binaries are shipped. Bluetooth is a separate UART service and is outside
this Wi-Fi target.
