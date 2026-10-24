CHOS_VERSION ?= 0.30.0
CROSS_COMPILE ?= aarch64-none-elf-
CC = clang
LD = ld.lld
CC_HOST ?= cc
OBJCOPY ?= llvm-objcopy
NM ?= nm
BUILD := build
CFLAGS := --target=aarch64-none-elf -mcpu=cortex-a72 -ffreestanding -fno-builtin \
          -fno-stack-protector -fno-pic -mgeneral-regs-only -mstrict-align \
          -Wall -Wextra -Werror \
          -O2 -g -std=c11 -Iinclude -MMD -MP
ASFLAGS := --target=aarch64-none-elf -mcpu=cortex-a72 -g
LDFLAGS := -T linker.ld -nostdlib -static -z max-page-size=4096

SOURCES_C := kernel/core/kernel.c kernel/core/memory.c kernel/core/shell.c kernel/core/scheduler.c kernel/core/scheduler_core.c kernel/core/spinlock.c \
             kernel/core/exception.c kernel/core/installer_plan.c kernel/core/syscall.c kernel/core/user_mode.c kernel/core/power.c kernel/core/process.c kernel/core/process_format.c kernel/core/fdt.c kernel/fs/romfs.c kernel/fs/romfs_core.c \
             kernel/mm/pmm.c kernel/mm/kheap.c kernel/mm/vmm.c kernel/mm/vmm_layout.c kernel/mm/address_space.c kernel/mm/address_space_layout.c kernel/platform/qemu_virt.c kernel/platform/boot_info.c kernel/platform/acpi.c kernel/platform/pci.c kernel/platform/nvme_parse.c kernel/platform/usb_hid.c kernel/platform/usb.c kernel/platform/input.c kernel/platform/xhci_ring.c kernel/platform/xhci.c kernel/platform/virtio_rng.c kernel/platform/uefi_console.c \
             kernel/fs/blockdev.c kernel/fs/gpt.c kernel/fs/chfs.c kernel/fs/storage.c kernel/gfx/gfx_core.c kernel/gfx/display_modes.c kernel/gfx/gui.c kernel/gfx/window_manager.c kernel/gfx/taskbar_text.c \
             kernel/arch/aarch64/virtio_snd.c kernel/arch/aarch64/virtio_gpu.c kernel/arch/aarch64/virtio_input.c kernel/arch/aarch64/virtio_net.c kernel/net/net_packet.c kernel/net/net_services.c kernel/net/net_stack.c kernel/net/net_tcp.c kernel/arch/aarch64/gicv2.c kernel/arch/aarch64/gicv3.c kernel/arch/aarch64/timer.c kernel/arch/aarch64/uart.c kernel/arch/aarch64/virtio_blk.c kernel/arch/aarch64/nvme.c
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES_C)) $(BUILD)/kernel/arch/aarch64/vectors.o \
           $(BUILD)/kernel/arch/aarch64/context_switch.o $(BUILD)/kernel/arch/aarch64/register_stress.o $(BUILD)/kernel/arch/aarch64/user_entry.o \
           $(BUILD)/kernel/arch/aarch64/user_demo.o $(BUILD)/kernel/arch/aarch64/user_exit.o $(BUILD)/kernel/arch/aarch64/user_preempt_good.o \
           $(BUILD)/kernel/arch/aarch64/user_preempt_fault.o $(BUILD)/kernel/arch/aarch64/power.o $(BUILD)/kernel/arch/aarch64/romfs_apps.o $(BUILD)/kernel/arch/aarch64/romfs_userland.o $(BUILD)/boot/aarch64/start.o

# QEMU's generic loader expects the ordinary kernel to be ET_EXEC. Only the
# UEFI path uses a separately compiled relocatable PIE kernel.
KERNEL_ASM_SOURCES := kernel/arch/aarch64/vectors.S kernel/arch/aarch64/context_switch.S \
                      kernel/arch/aarch64/register_stress.S kernel/arch/aarch64/user_entry.S \
                      kernel/arch/aarch64/user_demo.S kernel/arch/aarch64/user_exit.S \
                      kernel/arch/aarch64/user_preempt_good.S kernel/arch/aarch64/user_preempt_fault.S \
                      kernel/arch/aarch64/power.S kernel/arch/aarch64/romfs_apps.S \
                      kernel/arch/aarch64/romfs_userland.S boot/aarch64/start.S
UEFI_KERNEL_BUILD := $(BUILD)/uefi-kernel
UEFI_KERNEL_ELF := $(UEFI_KERNEL_BUILD)/chos.elf
UEFI_KERNEL_CFLAGS := $(filter-out -fno-pic -fno-pie -fpie -fPIC -fPIE,$(CFLAGS)) -fpie
UEFI_KERNEL_LDFLAGS := -T linker.ld -nostdlib -pie -e chos_uefi_start -z notext -z max-page-size=4096

.PHONY: all clean run run-gui run-gui-nettrace run-gui-audio run-gui-silent run-uefi uefi-loader uefi-iso test-uefi-iso test-uefi-kernel inspect test test-kernel-build-modes test-usb-hid test-boot-info test-acpi test-pci test-pmm-uefi test-gfx test-gui test-window-manager test-display-modes test-gui-protocol test-gpu-damage test-net-packet test-virtio-net-abi test-net-services test-net-stack test-net-tcp test-platform-boot test-taskbar-text test-assets test-sanitize test-userland test-browser-core test-browser-fixture test-uefi-handoff-contract userland-image disk-image disk-image-ready disk-image-list sync-system-apps assets run-installed-uefi test-usb-hid test-virtio-rng test-installer-plan qemu-smoke
all: $(BUILD)/chos.elf

ASSET_PNGS := $(wildcard assets/icons/*.png) assets/wallpaper/custom.png assets/cursor/default.png
ASSET_GENERATOR := tools/png_to_header.py
ASSET_HEADER := kernel/gfx/embedded_assets.h

$(ASSET_HEADER): $(ASSET_PNGS) $(ASSET_GENERATOR)
	python3 $(ASSET_GENERATOR) --icons-dir assets/icons --wallpaper assets/wallpaper/custom.png --cursor assets/cursor/default.png --output $@

assets: $(ASSET_HEADER)

$(BUILD)/chos.elf: $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

# Replacing a source PNG regenerates embedded_assets.h; ensure the GUI object
# is rebuilt too, even when kernel/gfx/gui.c itself has not changed.
$(BUILD)/kernel/gfx/gui.o: $(ASSET_HEADER) include/chos/gui.h

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel/arch/aarch64/vectors.o: kernel/arch/aarch64/vectors.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel/arch/aarch64/power.o: kernel/arch/aarch64/power.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel/arch/aarch64/context_switch.o: kernel/arch/aarch64/context_switch.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel/arch/aarch64/user_entry.o: kernel/arch/aarch64/user_entry.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel/arch/aarch64/user_demo.o: kernel/arch/aarch64/user_demo.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel/arch/aarch64/user_exit.o: kernel/arch/aarch64/user_exit.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel/arch/aarch64/user_preempt_good.o: kernel/arch/aarch64/user_preempt_good.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel/arch/aarch64/user_preempt_fault.o: kernel/arch/aarch64/user_preempt_fault.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel/arch/aarch64/romfs_apps.o: kernel/arch/aarch64/romfs_apps.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/kernel/arch/aarch64/register_stress.o: kernel/arch/aarch64/register_stress.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/boot/aarch64/start.o: boot/aarch64/start.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@


# Build a freestanding C user program and package segmented CHCX v2 image.
USER_CFLAGS := --target=aarch64-none-elf -mcpu=cortex-a72 -ffreestanding -fno-builtin \
               -fno-stack-protector -fno-pic -fno-pie -mgeneral-regs-only -mstrict-align \
               -Wall -Wextra -Werror -O2 -g -std=c11 -Iinclude -Iuserland/libchos/include

$(BUILD)/userland/cdemo.o: userland/examples/cdemo.c userland/libchos/include/chos/user.h include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/userland/gui_demo.o: userland/examples/gui_demo.c userland/libchos/include/chos/user.h include/chos/gui_protocol.h include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/userland/start.o: userland/start.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/userland/cdemo.elf: $(BUILD)/userland/start.o $(BUILD)/userland/cdemo.o userland/user.ld
	$(LD) -T userland/user.ld -nostdlib -static -z max-page-size=4096 -o $@ $(BUILD)/userland/start.o $(BUILD)/userland/cdemo.o

$(BUILD)/userland/cdemo.bin: $(BUILD)/userland/cdemo.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/userland/cdemo.sym: $(BUILD)/userland/cdemo.elf
	$(NM) --defined-only --numeric-sort $< > $@

$(BUILD)/userland/cdemo.chcx: $(BUILD)/userland/cdemo.bin $(BUILD)/userland/cdemo.sym tools/mkchcx.py
	python3 tools/mkchcx.py --v2-symbols $(BUILD)/userland/cdemo.sym $(BUILD)/userland/cdemo.bin $@
	python3 tools/mkchcx.py --verify $@

$(BUILD)/userland/gui_demo.elf: $(BUILD)/userland/start.o $(BUILD)/userland/gui_demo.o userland/user.ld
	$(LD) -T userland/user.ld -nostdlib -static -z max-page-size=4096 -o $@ $(BUILD)/userland/start.o $(BUILD)/userland/gui_demo.o

$(BUILD)/userland/gui_demo.bin: $(BUILD)/userland/gui_demo.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/userland/gui_demo.sym: $(BUILD)/userland/gui_demo.elf
	$(NM) --defined-only --numeric-sort $< > $@

$(BUILD)/userland/gui_demo.chcx: $(BUILD)/userland/gui_demo.bin $(BUILD)/userland/gui_demo.sym tools/mkchcx.py
	python3 tools/mkchcx.py --stack-pages 2 --v2-symbols $(BUILD)/userland/gui_demo.sym $(BUILD)/userland/gui_demo.bin $@
	python3 tools/mkchcx.py --verify $@


$(BUILD)/userland/text_viewer.o: userland/examples/text_viewer.c userland/libchos/include/chos/user.h include/chos/gui_protocol.h include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/userland/text_viewer.elf: $(BUILD)/userland/start.o $(BUILD)/userland/text_viewer.o userland/user.ld
	$(LD) -T userland/user.ld -nostdlib -static -z max-page-size=4096 -o $@ $(BUILD)/userland/start.o $(BUILD)/userland/text_viewer.o

$(BUILD)/userland/text_viewer.bin: $(BUILD)/userland/text_viewer.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/userland/text_viewer.sym: $(BUILD)/userland/text_viewer.elf
	$(NM) --defined-only --numeric-sort $< > $@

$(BUILD)/userland/text_viewer.chcx: $(BUILD)/userland/text_viewer.bin $(BUILD)/userland/text_viewer.sym tools/mkchcx.py
	python3 tools/mkchcx.py --stack-pages 2 --v2-symbols $(BUILD)/userland/text_viewer.sym $(BUILD)/userland/text_viewer.bin $@
	python3 tools/mkchcx.py --verify $@


$(BUILD)/userland/console.o: userland/examples/console.c userland/libchos/include/chos/user.h include/chos/file_api.h include/chos/gui_protocol.h include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/userland/console.elf: $(BUILD)/userland/start.o $(BUILD)/userland/console.o userland/user.ld
	$(LD) -T userland/user.ld -nostdlib -static -z max-page-size=4096 -o $@ $(BUILD)/userland/start.o $(BUILD)/userland/console.o

$(BUILD)/userland/console.bin: $(BUILD)/userland/console.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/userland/console.sym: $(BUILD)/userland/console.elf
	$(NM) --defined-only --numeric-sort $< > $@

$(BUILD)/userland/console.chcx: $(BUILD)/userland/console.bin $(BUILD)/userland/console.sym tools/mkchcx.py
	python3 tools/mkchcx.py --stack-pages 4 --v2-symbols $(BUILD)/userland/console.sym $(BUILD)/userland/console.bin $@
	python3 tools/mkchcx.py --verify $@

$(BUILD)/userland/file_manager.o: userland/examples/file_manager.c userland/libchos/include/chos/user.h include/chos/file_api.h include/chos/gui_protocol.h include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/userland/file_manager.elf: $(BUILD)/userland/start.o $(BUILD)/userland/file_manager.o userland/user.ld
	$(LD) -T userland/user.ld -nostdlib -static -z max-page-size=4096 -o $@ $(BUILD)/userland/start.o $(BUILD)/userland/file_manager.o

$(BUILD)/userland/file_manager.bin: $(BUILD)/userland/file_manager.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/userland/file_manager.sym: $(BUILD)/userland/file_manager.elf
	$(NM) --defined-only --numeric-sort $< > $@

$(BUILD)/userland/file_manager.chcx: $(BUILD)/userland/file_manager.bin $(BUILD)/userland/file_manager.sym tools/mkchcx.py
	python3 tools/mkchcx.py --stack-pages 8 --v2-symbols $(BUILD)/userland/file_manager.sym $(BUILD)/userland/file_manager.bin $@
	python3 tools/mkchcx.py --verify $@

$(BUILD)/userland/browser.o: userland/examples/browser.c userland/examples/browser_core.h userland/libchos/include/chos/user.h include/chos/gui_protocol.h include/chos/net_tcp_api.h include/chos/net_dns_api.h include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -Oz -Iuserland/examples -c $< -o $@

$(BUILD)/userland/browser_core.o: userland/examples/browser_core.c userland/examples/browser_core.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -Oz -Iuserland/examples -c $< -o $@

$(BUILD)/userland/browser.elf: $(BUILD)/userland/start.o $(BUILD)/userland/browser.o $(BUILD)/userland/browser_core.o userland/user.ld
	$(LD) -T userland/user.ld -nostdlib -static -z max-page-size=4096 -o $@ $(BUILD)/userland/start.o $(BUILD)/userland/browser.o $(BUILD)/userland/browser_core.o

$(BUILD)/userland/browser.bin: $(BUILD)/userland/browser.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/userland/browser.sym: $(BUILD)/userland/browser.elf
	$(NM) --defined-only --numeric-sort $< > $@

$(BUILD)/userland/browser.chcx: $(BUILD)/userland/browser.bin $(BUILD)/userland/browser.sym tools/mkchcx.py
	python3 tools/mkchcx.py --stack-pages 4 --v2-symbols $(BUILD)/userland/browser.sym $(BUILD)/userland/browser.bin $@
	python3 tools/mkchcx.py --verify $@

userland-image: $(BUILD)/userland/cdemo.chcx $(BUILD)/userland/gui_demo.chcx $(BUILD)/userland/text_viewer.chcx $(BUILD)/userland/console.chcx $(BUILD)/userland/file_manager.chcx $(BUILD)/userland/browser.chcx

# AArch64 UEFI boot path (development milestone). The EFI loader reads CHOS.ELF
# from the ISO EFI System Partition, validates ELF load segments, and exits boot
# services before transferring control to the kernel.
UEFI_CCFLAGS := --target=aarch64-pc-windows-msvc -Iinclude -fshort-wchar -ffreestanding -fno-builtin \
                -fno-stack-protector -mno-stack-arg-probe -Wall -Wextra -Werror -O2
UEFI_FIRMWARE ?=

$(BUILD)/uefi/bootaa64.obj: boot/uefi/bootaa64.c
	@mkdir -p $(dir $@)
	$(CC) $(UEFI_CCFLAGS) -c $< -o $@

$(BUILD)/uefi/BOOTAA64.EFI: $(BUILD)/uefi/bootaa64.obj
	lld-link /machine:arm64 /subsystem:efi_application /entry:efi_main /nodefaultlib \
	  /out:$@ /base:0x100000 /dynamicbase /filealign:512 $<

uefi-loader: $(BUILD)/uefi/BOOTAA64.EFI

UEFI_KERNEL_OBJECTS := $(patsubst %.c,$(UEFI_KERNEL_BUILD)/%.o,$(SOURCES_C)) \
                       $(patsubst %.S,$(UEFI_KERNEL_BUILD)/%.o,$(KERNEL_ASM_SOURCES))

$(UEFI_KERNEL_ELF): $(UEFI_KERNEL_OBJECTS) linker.ld
	$(LD) $(UEFI_KERNEL_LDFLAGS) -o $@ $(UEFI_KERNEL_OBJECTS)

$(UEFI_KERNEL_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(UEFI_KERNEL_CFLAGS) -c $< -o $@

$(UEFI_KERNEL_BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(UEFI_KERNEL_BUILD)/kernel/gfx/gui.o: $(ASSET_HEADER)
$(UEFI_KERNEL_BUILD)/kernel/arch/aarch64/romfs_userland.o: kernel/arch/aarch64/romfs_userland.S \
    $(BUILD)/userland/cdemo.chcx $(BUILD)/userland/gui_demo.chcx \
    $(BUILD)/userland/text_viewer.chcx $(BUILD)/userland/console.chcx \
    $(BUILD)/userland/file_manager.chcx $(BUILD)/userland/browser.chcx

$(BUILD)/chos-$(CHOS_VERSION).iso: $(UEFI_KERNEL_ELF) $(BUILD)/uefi/BOOTAA64.EFI tools/mkuefi_iso.py tests/test_uefi_kernel.py docs/RELEASE_NOTES_0.30.0.md
	python3 tests/test_uefi_kernel.py $(UEFI_KERNEL_ELF)
	python3 tools/mkuefi_iso.py --kernel $(UEFI_KERNEL_ELF) --efi $(BUILD)/uefi/BOOTAA64.EFI --readme docs/RELEASE_NOTES_0.30.0.md --output $@

uefi-iso: $(BUILD)/chos-$(CHOS_VERSION).iso

# Create an installable disk *image file*; this target never writes a host device.
.PHONY: install-disk-image
install-disk-image: $(UEFI_KERNEL_ELF) $(BUILD)/uefi/BOOTAA64.EFI $(BUILD)/userland/cdemo.chcx $(BUILD)/userland/gui_demo.chcx $(BUILD)/userland/text_viewer.chcx $(BUILD)/userland/console.chcx $(BUILD)/userland/browser.chcx $(BUILD)/userland/file_manager.chcx
	python3 tools/mkchos_install_disk.py --kernel $(UEFI_KERNEL_ELF) --efi $(BUILD)/uefi/BOOTAA64.EFI --readme docs/RELEASE_NOTES_0.30.0.md --size-mib 256 --output $(BUILD)/chos-$(CHOS_VERSION)-installed.img --force --app /BIN/CDEMO.CHX=$(BUILD)/userland/cdemo.chcx --app /BIN/GUIAPP.CHX=$(BUILD)/userland/gui_demo.chcx --app /BIN/TEXTVIEW.CHX=$(BUILD)/userland/text_viewer.chcx --app /BIN/CONSOLE.CHX=$(BUILD)/userland/console.chcx --app /BIN/BROWSER.CHX=$(BUILD)/userland/browser.chcx --app /BIN/FILEMAN.CHX=$(BUILD)/userland/file_manager.chcx

# This target requires an AArch64 UEFI firmware image (EDK2/AAVMF). On macOS,
# locate the QEMU/EDK2 firmware installed with your QEMU package and pass its path.
run-uefi: $(BUILD)/chos-$(CHOS_VERSION).iso disk-image-ready
	@if [ -z "$(UEFI_FIRMWARE)" ] || [ ! -f "$(UEFI_FIRMWARE)" ]; then \
	  echo "Set UEFI_FIRMWARE=/path/to/edk2-aarch64-code.fd (or AAVMF_CODE.fd)"; exit 2; fi
	qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a72 -m 512M -rtc base=utc \
	  -bios "$(UEFI_FIRMWARE)" -display default -monitor none -serial stdio -no-reboot \
	  -object rng-random,filename=/dev/urandom,id=chosrng -device virtio-rng-device,rng=chosrng \
	  -drive if=none,file=$(BUILD)/chos-$(CHOS_VERSION).iso,format=raw,media=cdrom,id=choscd \
	  -device virtio-scsi-device,id=chosscsi -device scsi-cd,drive=choscd -drive if=none,file=chos-disk.img,format=raw,id=chosdisk \
	  -device virtio-blk-device,drive=chosdisk -netdev user,id=chosnet -device virtio-net-device,netdev=chosnet \
	  -device virtio-gpu-device -device virtio-keyboard-device -device virtio-tablet-device

# Experimental installed-image boot. Firmware/device support for VirtIO-MMIO
# boot varies by QEMU/EDK2 build; this target is a test, not a bootability claim.
.PHONY: run-installed-uefi
run-installed-uefi: install-disk-image
	@if [ -z "$(UEFI_FIRMWARE)" ] || [ ! -f "$(UEFI_FIRMWARE)" ]; then \
	  echo "Set UEFI_FIRMWARE=/path/to/edk2-aarch64-code.fd (or AAVMF_CODE.fd)"; exit 2; fi
	qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a72 -m 512M -rtc base=utc \
	  -bios "$(UEFI_FIRMWARE)" -display default -monitor none -serial stdio -no-reboot \
	  -object rng-random,filename=/dev/urandom,id=chosrng -device virtio-rng-device,rng=chosrng \
	  -drive if=none,file=$(BUILD)/chos-$(CHOS_VERSION)-installed.img,format=raw,id=chosdisk \
	  -device virtio-blk-device,drive=chosdisk -netdev user,id=chosnet \
	  -device virtio-net-device,netdev=chosnet -device virtio-gpu-device \
	  -device virtio-keyboard-device -device virtio-tablet-device

# The disk lives at the project root and is intentionally not removed by `make clean`.
# It is created once for `make run`; use `make disk-image` to reset it deliberately.
.PHONY: disk-image disk-image-ready disk-image-list test-net-packet
disk-image-ready: $(BUILD)/userland/cdemo.chcx $(BUILD)/userland/gui_demo.chcx $(BUILD)/userland/text_viewer.chcx $(BUILD)/userland/console.chcx $(BUILD)/userland/file_manager.chcx $(BUILD)/userland/browser.chcx userland/assets/README.TXT tools/mkchfs.py tools/sync_system_apps.py
	@if [ ! -f chos-disk.img ]; then python3 tools/mkchfs.py create chos-disk.img --size-mib 8 --put /BIN/CDEMO.CHX=$(BUILD)/userland/cdemo.chcx --put /BIN/GUIAPP.CHX=$(BUILD)/userland/gui_demo.chcx --put /BIN/TEXTVIEW.CHX=$(BUILD)/userland/text_viewer.chcx --put /BIN/CONSOLE.CHX=$(BUILD)/userland/console.chcx --put /BIN/FILEMAN.CHX=$(BUILD)/userland/file_manager.chcx --put /BIN/BROWSER.CHX=$(BUILD)/userland/browser.chcx --put /DOCS/README.TXT=userland/assets/README.TXT; fi
	@python3 tools/sync_system_apps.py chos-disk.img --app /BIN/CDEMO.CHX=$(BUILD)/userland/cdemo.chcx --app /BIN/GUIAPP.CHX=$(BUILD)/userland/gui_demo.chcx --app /BIN/TEXTVIEW.CHX=$(BUILD)/userland/text_viewer.chcx --app /BIN/CONSOLE.CHX=$(BUILD)/userland/console.chcx --app /BIN/FILEMAN.CHX=$(BUILD)/userland/file_manager.chcx --app /BIN/BROWSER.CHX=$(BUILD)/userland/browser.chcx

sync-system-apps: $(BUILD)/userland/cdemo.chcx $(BUILD)/userland/gui_demo.chcx $(BUILD)/userland/text_viewer.chcx $(BUILD)/userland/console.chcx $(BUILD)/userland/file_manager.chcx disk-image-ready
	@echo "System applications synchronized; existing user files were preserved."

disk-image: $(BUILD)/userland/cdemo.chcx $(BUILD)/userland/gui_demo.chcx $(BUILD)/userland/text_viewer.chcx $(BUILD)/userland/console.chcx $(BUILD)/userland/file_manager.chcx $(BUILD)/userland/browser.chcx userland/assets/README.TXT tools/mkchfs.py
	python3 tools/mkchfs.py create chos-disk.img --force --size-mib 8 --put /BIN/CDEMO.CHX=$(BUILD)/userland/cdemo.chcx --put /BIN/GUIAPP.CHX=$(BUILD)/userland/gui_demo.chcx --put /BIN/TEXTVIEW.CHX=$(BUILD)/userland/text_viewer.chcx --put /BIN/CONSOLE.CHX=$(BUILD)/userland/console.chcx --put /BIN/FILEMAN.CHX=$(BUILD)/userland/file_manager.chcx --put /BIN/BROWSER.CHX=$(BUILD)/userland/browser.chcx --put /DOCS/README.TXT=userland/assets/README.TXT

disk-image-list:
	python3 tools/mkchfs.py list chos-disk.img

$(BUILD)/kernel/arch/aarch64/romfs_userland.o: kernel/arch/aarch64/romfs_userland.S $(BUILD)/userland/cdemo.chcx $(BUILD)/userland/gui_demo.chcx $(BUILD)/userland/text_viewer.chcx $(BUILD)/userland/console.chcx $(BUILD)/userland/file_manager.chcx $(BUILD)/userland/browser.chcx
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

run: $(BUILD)/chos.elf disk-image-ready
	qemu-system-aarch64 -M virt,gic-version=2 -global virtio-mmio.force-legacy=false \
	  -cpu cortex-a72 -m 256M -rtc base=utc -nographic \
	  -object rng-random,filename=/dev/urandom,id=chosrng -device virtio-rng-device,rng=chosrng \
	  -monitor none -serial stdio -no-reboot \
	  -drive if=none,file=chos-disk.img,format=raw,id=chosdisk \
	  -device virtio-blk-device,drive=chosdisk \
	  -netdev user,id=chosnet \
	  -device virtio-net-device,netdev=chosnet \
	  -device loader,file=$(BUILD)/chos.elf,cpu-num=0

# Portable default GUI profile. Audio is intentionally separate because QEMU
# audio backend names differ between macOS, Linux and Windows hosts.
run-gui: run-gui-silent

QEMU_AUDIO_BACKEND ?= coreaudio
run-gui-audio: $(BUILD)/chos.elf disk-image-ready
	qemu-system-aarch64 -M virt,gic-version=2 -global virtio-mmio.force-legacy=false \
	  -cpu cortex-a72 -m 512M -rtc base=utc -display default \
	  -object rng-random,filename=/dev/urandom,id=chosrng -device virtio-rng-device,rng=chosrng \
	  -audiodev $(QEMU_AUDIO_BACKEND),id=chos_audio \
	  -monitor none -serial stdio -no-reboot \
	  -drive if=none,file=chos-disk.img,format=raw,id=chosdisk \
	  -device virtio-blk-device,drive=chosdisk \
	  -netdev user,id=chosnet \
	  -device virtio-net-device,netdev=chosnet \
	  -device virtio-gpu-device -device virtio-keyboard-device -device virtio-tablet-device \
	  -device virtio-sound-device,audiodev=chos_audio,streams=1 \
	  -device loader,file=$(BUILD)/chos.elf,cpu-num=0

# Network-trace launch writes all traffic observed at QEMU's user-mode
# netdev into a PCAP file so the guest's DHCP TX/RX path can be separated.
run-gui-nettrace: $(BUILD)/chos.elf disk-image-ready
	qemu-system-aarch64 -M virt,gic-version=2 -global virtio-mmio.force-legacy=false \
	  -cpu cortex-a72 -m 512M -rtc base=utc -display default \
	  -object rng-random,filename=/dev/urandom,id=chosrng -device virtio-rng-device,rng=chosrng \
	  -monitor none -serial stdio -no-reboot \
	  -drive if=none,file=chos-disk.img,format=raw,id=chosdisk \
	  -device virtio-blk-device,drive=chosdisk \
	  -netdev user,id=chosnet \
	  -device virtio-net-device,netdev=chosnet \
	  -object filter-dump,id=chosnetdump,netdev=chosnet,file=chos-network.pcap \
	  -device virtio-gpu-device -device virtio-keyboard-device -device virtio-tablet-device \
	  -device loader,file=$(BUILD)/chos.elf,cpu-num=0

run-gui-silent: $(BUILD)/chos.elf disk-image-ready
	qemu-system-aarch64 -M virt,gic-version=2 -global virtio-mmio.force-legacy=false \
	  -cpu cortex-a72 -m 512M -rtc base=utc -display default \
	  -object rng-random,filename=/dev/urandom,id=chosrng -device virtio-rng-device,rng=chosrng \
	  -monitor none -serial stdio -no-reboot \
	  -drive if=none,file=chos-disk.img,format=raw,id=chosdisk \
	  -device virtio-blk-device,drive=chosdisk \
	  -netdev user,id=chosnet \
	  -device virtio-net-device,netdev=chosnet \
	  -device virtio-gpu-device -device virtio-keyboard-device -device virtio-tablet-device \
	  -device loader,file=$(BUILD)/chos.elf,cpu-num=0


# Disposable, automated QEMU smoke disk lives under build/; it never overwrites
# the interactive-development disk at the repository root.
$(BUILD)/qemu-smoke-disk.img: $(BUILD)/userland/cdemo.chcx $(BUILD)/userland/gui_demo.chcx $(BUILD)/userland/text_viewer.chcx $(BUILD)/userland/console.chcx $(BUILD)/userland/file_manager.chcx $(BUILD)/userland/browser.chcx tools/mkchfs.py
	python3 tools/mkchfs.py create $@ --force --size-mib 16 --put /BIN/CDEMO.CHX=$(BUILD)/userland/cdemo.chcx --put /BIN/GUIAPP.CHX=$(BUILD)/userland/gui_demo.chcx --put /BIN/TEXTVIEW.CHX=$(BUILD)/userland/text_viewer.chcx --put /BIN/CONSOLE.CHX=$(BUILD)/userland/console.chcx --put /BIN/FILEMAN.CHX=$(BUILD)/userland/file_manager.chcx --put /BIN/BROWSER.CHX=$(BUILD)/userland/browser.chcx

qemu-smoke: $(BUILD)/chos.elf $(BUILD)/qemu-smoke-disk.img
	python3 tests/qemu_smoke.py --kernel $(BUILD)/chos.elf --disk $(BUILD)/qemu-smoke-disk.img --log $(BUILD)/qemu-smoke.log

inspect: $(BUILD)/chos.elf
	file $(BUILD)/chos.elf

test-gfx: $(BUILD)/test-gfx
	./$(BUILD)/test-gfx

test-gui: $(BUILD)/test-gui
	./$(BUILD)/test-gui

test-window-manager: $(BUILD)/test-window-manager
	./$(BUILD)/test-window-manager

test-display-modes: $(BUILD)/test-display-modes
	./$(BUILD)/test-display-modes

test-gui-protocol: $(BUILD)/test-gui-protocol
	./$(BUILD)/test-usb-hid
	./$(BUILD)/test-virtio-rng
	./$(BUILD)/test-installer-plan
	./$(BUILD)/test-gui-protocol

test-assets:
	python3 tests/test_png_to_header.py

test-uefi-iso:
	python3 tests/test_mkuefi_iso.py
	python3 -m py_compile tests/qemu_smoke.py
	python3 tests/test_release_version.py
	python3 tests/test_mkchos_install_disk.py

test-uefi-kernel: $(UEFI_KERNEL_ELF)
	python3 tests/test_uefi_kernel.py $(UEFI_KERNEL_ELF)

test-kernel-build-modes: $(BUILD)/chos.elf $(UEFI_KERNEL_ELF)
	python3 tests/test_kernel_build_modes.py $(BUILD)/chos.elf $(UEFI_KERNEL_ELF)

test-boot-info: $(BUILD)/test-boot-info
	./$(BUILD)/test-boot-info

$(BUILD)/test-acpi: tests/test_acpi.c kernel/platform/acpi.c kernel/platform/boot_info.c include/chos/acpi.h include/chos/boot_info.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_acpi.c kernel/platform/acpi.c kernel/platform/boot_info.c -o $@

test-acpi: $(BUILD)/test-acpi
	./$(BUILD)/test-acpi

$(BUILD)/test-pci: tests/test_pci.c kernel/platform/pci.c kernel/platform/boot_info.c include/chos/pci.h include/chos/boot_info.h include/chos/acpi.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_pci.c kernel/platform/pci.c kernel/platform/boot_info.c -o $@

test-pci: $(BUILD)/test-pci
	./$(BUILD)/test-pci

$(BUILD)/test-nvme-parse: tests/test_nvme_parse.c kernel/platform/nvme_parse.c include/chos/nvme.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_nvme_parse.c kernel/platform/nvme_parse.c -o $@

test-nvme-parse: $(BUILD)/test-nvme-parse
	./$(BUILD)/test-nvme-parse

$(BUILD)/test-gpt: tests/test_gpt.c kernel/fs/gpt.c kernel/fs/blockdev.c include/chos/gpt.h include/chos/blockdev.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_gpt.c kernel/fs/gpt.c kernel/fs/blockdev.c kernel/fs/chfs.c -o $@

test-gpt: $(BUILD)/test-gpt
	./$(BUILD)/test-gpt

$(BUILD)/test-pmm-uefi: tests/test_pmm_uefi.c kernel/mm/pmm.c kernel/platform/boot_info.c kernel/core/spinlock.c include/chos/pmm.h include/chos/boot_info.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Iinclude -pthread tests/test_pmm_uefi.c kernel/mm/pmm.c kernel/platform/boot_info.c kernel/core/spinlock.c -o $@

test-pmm-uefi: $(BUILD)/test-pmm-uefi
	./$(BUILD)/test-pmm-uefi

$(BUILD)/test-usb-hid: tests/test_usb_hid.c kernel/platform/usb_hid.c include/chos/usb_hid.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -Wall -Wextra -Werror -std=c11 -Iinclude tests/test_usb_hid.c kernel/platform/usb_hid.c -o $@

test-usb-hid: $(BUILD)/test-usb-hid
	./$(BUILD)/test-usb-hid

$(BUILD)/test-virtio-rng: tests/test_virtio_rng.c kernel/platform/virtio_rng.c include/chos/entropy.h include/chos/platform.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_virtio_rng.c kernel/platform/virtio_rng.c -o $@

test-virtio-rng: $(BUILD)/test-virtio-rng
	./$(BUILD)/test-virtio-rng

$(BUILD)/test-installer-plan: tests/test_installer_plan.c kernel/core/installer_plan.c include/chos/installer.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_installer_plan.c kernel/core/installer_plan.c -o $@

test-installer-plan: $(BUILD)/test-installer-plan
	./$(BUILD)/test-installer-plan

$(BUILD)/test-boot-info: tests/test_boot_info.c kernel/platform/boot_info.c include/chos/boot_info.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -Wall -Wextra -Werror -std=c11 -Iinclude tests/test_boot_info.c kernel/platform/boot_info.c -o $@

test: $(BUILD)/chos.elf $(UEFI_KERNEL_ELF) $(BUILD)/test-usb-hid $(BUILD)/test-virtio-rng $(BUILD)/test-installer-plan $(BUILD)/test-boot-info $(BUILD)/test-acpi $(BUILD)/test-pci $(BUILD)/test-nvme-parse $(BUILD)/test-gpt $(BUILD)/test-pmm-uefi $(BUILD)/userland/gui_demo.chcx $(BUILD)/userland/text_viewer.chcx $(BUILD)/userland/console.chcx $(BUILD)/userland/file_manager.chcx $(BUILD)/userland/browser.chcx $(BUILD)/test-browser-core $(BUILD)/test-gpu-damage $(BUILD)/test-net-packet $(BUILD)/test-virtio-net-abi $(BUILD)/test-net-services $(BUILD)/test-net-stack $(BUILD)/test-net-tcp $(BUILD)/test-platform-boot $(BUILD)/test-taskbar-text $(BUILD)/test-gui-protocol $(BUILD)/test-fdt $(BUILD)/test-pmm $(BUILD)/test-kheap $(BUILD)/test-vmm $(BUILD)/test-scheduler $(BUILD)/test-irqctx $(BUILD)/test-spinlock $(BUILD)/test-address-space $(BUILD)/test-process-format $(BUILD)/test-romfs $(BUILD)/test-user-abi $(BUILD)/test-chfs $(BUILD)/test-gfx $(BUILD)/test-gui $(BUILD)/test-window-manager $(BUILD)/test-display-modes $(BUILD)/userland/async_demo.o $(BUILD)/userland/cdemo.chcx $(BUILD)/test-net-tcp-api-compile.o $(BUILD)/test-browser-api-compile.o
	./$(BUILD)/test-usb-hid
	./$(BUILD)/test-virtio-rng
	./$(BUILD)/test-installer-plan
	./$(BUILD)/test-gui-protocol
	./$(BUILD)/test-boot-info
	./$(BUILD)/test-acpi
	./$(BUILD)/test-pci
	./$(BUILD)/test-nvme-parse
	./$(BUILD)/test-gpt
	./$(BUILD)/test-pmm-uefi
	./$(BUILD)/test-browser-core
	./$(BUILD)/test-gpu-damage
	./$(BUILD)/test-net-packet
	./$(BUILD)/test-virtio-net-abi
	./$(BUILD)/test-net-services
	./$(BUILD)/test-net-stack
	./$(BUILD)/test-net-tcp
	./$(BUILD)/test-platform-boot
	./$(BUILD)/test-taskbar-text
	python3 tests/test_png_to_header.py
	./$(BUILD)/test-fdt
	./$(BUILD)/test-pmm
	./$(BUILD)/test-kheap
	./$(BUILD)/test-vmm
	./$(BUILD)/test-scheduler
	./$(BUILD)/test-irqctx
	python3 tests/test_irq_sp_el0_contract.py
	./$(BUILD)/test-spinlock
	./$(BUILD)/test-address-space
	./$(BUILD)/test-process-format
	./$(BUILD)/test-romfs
	./$(BUILD)/test-user-abi
	./$(BUILD)/test-chfs
	./$(BUILD)/test-gfx
	./$(BUILD)/test-gui
	./$(BUILD)/test-window-manager
	./$(BUILD)/test-display-modes
	python3 tests/test_mkchcx.py
	python3 tests/test_app_stack_pages.py $(BUILD)/userland
	python3 tests/test_mkchfs.py -v
	python3 tests/test_sync_system_apps.py
	python3 tests/test_mkuefi_iso.py
	python3 -m py_compile tests/qemu_smoke.py
	python3 tests/test_release_version.py
	python3 tests/test_mkchos_install_disk.py
	python3 tests/test_uefi_handoff_contract.py
	python3 tests/test_uefi_kernel.py $(UEFI_KERNEL_ELF)
	python3 tests/test_kernel_build_modes.py $(BUILD)/chos.elf $(UEFI_KERNEL_ELF)
	python3 tests/test_browser_test_server.py

$(BUILD)/test-fdt: tests/test_fdt.c kernel/core/fdt.c kernel/mm/pmm.c kernel/platform/boot_info.c include/chos/fdt.h include/chos/pmm.h include/chos/boot_info.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Iinclude -pthread tests/test_fdt.c kernel/core/fdt.c kernel/mm/pmm.c kernel/platform/boot_info.c kernel/core/spinlock.c -o $@

$(BUILD)/test-pmm: tests/test_pmm.c kernel/mm/pmm.c kernel/platform/boot_info.c include/chos/pmm.h include/chos/fdt.h include/chos/boot_info.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Iinclude -pthread tests/test_pmm.c kernel/mm/pmm.c kernel/platform/boot_info.c kernel/core/spinlock.c -o $@

$(BUILD)/test-kheap: tests/test_kheap.c kernel/mm/kheap.c include/chos/kheap.h include/chos/pmm.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Iinclude -pthread tests/test_kheap.c kernel/mm/kheap.c kernel/core/spinlock.c -o $@

clean:
	rm -rf $(BUILD)

$(BUILD)/test-vmm: tests/test_vmm.c kernel/mm/vmm_layout.c include/chos/vmm.h include/chos/fdt.h include/chos/platform.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_vmm.c kernel/mm/vmm_layout.c -o $@

$(BUILD)/test-scheduler: tests/test_scheduler.c kernel/core/scheduler_core.c include/chos/scheduler.h include/chos/task_stack_layout.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_scheduler.c kernel/core/scheduler_core.c -o $@

$(BUILD)/test-irqctx: tests/test_irq_context.c include/chos/irq_context.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_irq_context.c -o $@

$(BUILD)/test-spinlock: tests/test_spinlock.c kernel/core/spinlock.c include/chos/spinlock.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -DCHOS_HOST_TEST -std=c11 -Wall -Wextra -Werror -Iinclude -pthread tests/test_spinlock.c kernel/core/spinlock.c -o $@

# Host-side sanitizer pass for parsers, memory allocators, VMM layout and scheduler policy.
SAN_FLAGS := -std=c11 -Wall -Wextra -Werror -Iinclude -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g
SAN_DIR := $(BUILD)/sanitize

$(SAN_DIR)/test-usb-hid: tests/test_usb_hid.c kernel/platform/usb_hid.c include/chos/usb_hid.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_usb_hid.c kernel/platform/usb_hid.c -o $@

$(SAN_DIR)/test-virtio-rng: tests/test_virtio_rng.c kernel/platform/virtio_rng.c include/chos/entropy.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -DCHOS_HOST_TEST tests/test_virtio_rng.c kernel/platform/virtio_rng.c -o $@

$(SAN_DIR)/test-installer-plan: tests/test_installer_plan.c kernel/core/installer_plan.c include/chos/installer.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_installer_plan.c kernel/core/installer_plan.c -o $@

$(SAN_DIR)/test-acpi: tests/test_acpi.c kernel/platform/acpi.c kernel/platform/boot_info.c
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -DCHOS_HOST_TEST tests/test_acpi.c kernel/platform/acpi.c kernel/platform/boot_info.c -o $@

$(SAN_DIR)/test-pci: tests/test_pci.c kernel/platform/pci.c kernel/platform/boot_info.c
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -DCHOS_HOST_TEST tests/test_pci.c kernel/platform/pci.c kernel/platform/boot_info.c -o $@

$(SAN_DIR)/test-gpt: tests/test_gpt.c kernel/fs/gpt.c kernel/fs/blockdev.c
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -DCHOS_HOST_TEST tests/test_gpt.c kernel/fs/gpt.c kernel/fs/blockdev.c kernel/fs/chfs.c -o $@

$(SAN_DIR)/test-pmm-uefi: tests/test_pmm_uefi.c kernel/mm/pmm.c kernel/platform/boot_info.c kernel/core/spinlock.c
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -pthread -DCHOS_HOST_TEST tests/test_pmm_uefi.c kernel/mm/pmm.c kernel/platform/boot_info.c kernel/core/spinlock.c -o $@

test-sanitize: $(SAN_DIR)/test-usb-hid $(SAN_DIR)/test-virtio-rng $(SAN_DIR)/test-installer-plan $(SAN_DIR)/test-boot-info $(SAN_DIR)/test-acpi $(SAN_DIR)/test-pci $(SAN_DIR)/test-gpt $(SAN_DIR)/test-pmm-uefi $(SAN_DIR)/test-gpu-damage $(SAN_DIR)/test-net-packet $(SAN_DIR)/test-virtio-net-abi $(SAN_DIR)/test-net-services $(SAN_DIR)/test-net-stack $(SAN_DIR)/test-net-tcp $(SAN_DIR)/test-platform-boot $(SAN_DIR)/test-taskbar-text $(SAN_DIR)/test-gui-protocol $(SAN_DIR)/test-fdt $(SAN_DIR)/test-pmm $(SAN_DIR)/test-kheap $(SAN_DIR)/test-vmm $(SAN_DIR)/test-scheduler $(SAN_DIR)/test-irqctx $(SAN_DIR)/test-spinlock $(SAN_DIR)/test-address-space $(SAN_DIR)/test-process-format $(SAN_DIR)/test-romfs $(SAN_DIR)/test-user-abi $(SAN_DIR)/test-chfs $(SAN_DIR)/test-gfx $(SAN_DIR)/test-gui $(SAN_DIR)/test-window-manager $(SAN_DIR)/test-display-modes $(SAN_DIR)/test-browser-core
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-usb-hid
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-virtio-rng
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-installer-plan
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-boot-info
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-acpi
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-pci
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-gpt
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-pmm-uefi
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-gpu-damage
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-net-packet
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-virtio-net-abi
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-net-services
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-net-stack
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-net-tcp
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-platform-boot
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-taskbar-text
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-gui-protocol
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-fdt
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-pmm
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-kheap
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-vmm
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-scheduler
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-irqctx
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-spinlock
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-address-space
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-process-format
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-romfs
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-user-abi
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-chfs
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-gfx
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-gui
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-window-manager
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-display-modes
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-browser-core

$(SAN_DIR)/test-boot-info: tests/test_boot_info.c kernel/platform/boot_info.c include/chos/boot_info.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_boot_info.c kernel/platform/boot_info.c -o $@

$(SAN_DIR)/test-gpu-damage: tests/test_gpu_damage.c include/chos/virtio_gpu.h include/chos/display_modes.h include/chos/gfx.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_gpu_damage.c -o $@

$(SAN_DIR)/test-gui-protocol: tests/test_gui_protocol.c include/chos/gui_protocol.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_gui_protocol.c -o $@

$(SAN_DIR)/test-fdt: tests/test_fdt.c kernel/core/fdt.c kernel/mm/pmm.c kernel/platform/boot_info.c include/chos/fdt.h include/chos/pmm.h include/chos/boot_info.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -DCHOS_HOST_TEST -pthread tests/test_fdt.c kernel/core/fdt.c kernel/mm/pmm.c kernel/platform/boot_info.c kernel/core/spinlock.c -o $@

$(SAN_DIR)/test-pmm: tests/test_pmm.c kernel/mm/pmm.c kernel/platform/boot_info.c include/chos/pmm.h include/chos/fdt.h include/chos/boot_info.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -DCHOS_HOST_TEST -pthread tests/test_pmm.c kernel/mm/pmm.c kernel/platform/boot_info.c kernel/core/spinlock.c -o $@

$(SAN_DIR)/test-kheap: tests/test_kheap.c kernel/mm/kheap.c include/chos/kheap.h include/chos/pmm.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -DCHOS_HOST_TEST -pthread tests/test_kheap.c kernel/mm/kheap.c kernel/core/spinlock.c -o $@

$(SAN_DIR)/test-vmm: tests/test_vmm.c kernel/mm/vmm_layout.c include/chos/vmm.h include/chos/fdt.h include/chos/platform.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_vmm.c kernel/mm/vmm_layout.c -o $@

$(SAN_DIR)/test-scheduler: tests/test_scheduler.c kernel/core/scheduler_core.c include/chos/scheduler.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_scheduler.c kernel/core/scheduler_core.c -o $@

$(SAN_DIR)/test-irqctx: tests/test_irq_context.c include/chos/irq_context.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_irq_context.c -o $@

$(SAN_DIR)/test-spinlock: tests/test_spinlock.c kernel/core/spinlock.c include/chos/spinlock.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -DCHOS_HOST_TEST -pthread tests/test_spinlock.c kernel/core/spinlock.c -o $@

$(BUILD)/test-address-space: tests/test_address_space.c kernel/mm/address_space_layout.c kernel/mm/vmm_layout.c include/chos/address_space.h include/chos/vmm.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_address_space.c kernel/mm/address_space_layout.c kernel/mm/vmm_layout.c -o $@

$(SAN_DIR)/test-address-space: tests/test_address_space.c kernel/mm/address_space_layout.c kernel/mm/vmm_layout.c include/chos/address_space.h include/chos/vmm.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_address_space.c kernel/mm/address_space_layout.c kernel/mm/vmm_layout.c -o $@

$(BUILD)/test-display-modes: tests/test_display_modes.c kernel/gfx/display_modes.c include/chos/display_modes.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_display_modes.c kernel/gfx/display_modes.c -o $@

$(SAN_DIR)/test-display-modes: tests/test_display_modes.c kernel/gfx/display_modes.c include/chos/display_modes.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_display_modes.c kernel/gfx/display_modes.c -o $@

# Header-specific rebuild dependencies for the shared task-stack/MMU layout.
$(BUILD)/kernel/core/scheduler.o: include/chos/scheduler.h include/chos/task_stack_layout.h include/chos/timer.h
$(BUILD)/kernel/mm/vmm.o: include/chos/task_stack_layout.h include/chos/vmm.h
$(BUILD)/kernel/mm/address_space.o: include/chos/address_space.h include/chos/vmm.h include/chos/pmm.h include/chos/spinlock.h include/chos/scheduler.h
$(BUILD)/kernel/mm/address_space_layout.o: include/chos/address_space.h include/chos/vmm.h
$(BUILD)/kernel/mm/vmm_layout.o: include/chos/vmm.h
$(BUILD)/kernel/mm/address_space.o: include/chos/syscall.h

$(BUILD)/kernel/arch/aarch64/uart.o: include/chos/spinlock.h include/chos/uart.h
$(BUILD)/kernel/core/shell.o: include/chos/register_stress.h include/chos/uart.h include/chos/address_space.h include/chos/vmm.h include/chos/scheduler.h include/chos/user_mode.h
$(BUILD)/kernel/core/syscall.o: include/chos/syscall.h include/chos/user_mode.h include/chos/address_space.h include/chos/irq_context.h include/chos/gui.h
$(BUILD)/kernel/core/user_mode.o: include/chos/user_mode.h include/chos/address_space.h include/chos/scheduler.h include/chos/syscall.h

$(BUILD)/kernel/mm/pmm.o: include/chos/spinlock.h
$(BUILD)/kernel/mm/kheap.o: include/chos/spinlock.h
$(BUILD)/test-pmm: include/chos/spinlock.h kernel/core/spinlock.c
$(BUILD)/test-kheap: include/chos/spinlock.h kernel/core/spinlock.c
$(BUILD)/test-fdt: include/chos/spinlock.h kernel/core/spinlock.c

$(BUILD)/test-process-format: tests/test_process_format.c kernel/core/process_format.c include/chos/process.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_process_format.c kernel/core/process_format.c -o $@

$(SAN_DIR)/test-process-format: tests/test_process_format.c kernel/core/process_format.c include/chos/process.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_process_format.c kernel/core/process_format.c -o $@

$(BUILD)/kernel/core/process.o: include/chos/process.h include/chos/user_mode.h include/chos/address_space.h include/chos/spinlock.h include/chos/scheduler.h include/chos/timer.h include/chos/storage.h include/chos/kheap.h include/chos/syscall.h include/chos/romfs.h
$(BUILD)/kernel/core/process_format.o: include/chos/process.h

$(BUILD)/kernel/core/shell.o: include/chos/process.h include/chos/user_mode.h
$(BUILD)/kernel/core/user_mode.o: include/chos/user_mode.h include/chos/address_space.h include/chos/scheduler.h

$(BUILD)/test-romfs: tests/test_romfs.c kernel/fs/romfs_core.c kernel/core/process_format.c include/chos/romfs.h include/chos/process.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_romfs.c kernel/fs/romfs_core.c kernel/core/process_format.c -o $@

$(BUILD)/test-user-abi: tests/test_user_abi.c include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_user_abi.c -o $@

$(BUILD)/userland/async_demo.o: userland/examples/async_demo.c userland/libchos/include/chos/user.h include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iuserland/libchos/include -c $< -o $@

$(BUILD)/test-net-tcp-api-compile.o: tests/test_net_tcp_api_compile.c userland/libchos/include/chos/user.h include/chos/net_tcp_api.h include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iuserland/libchos/include -c $< -o $@

$(BUILD)/test-browser-api-compile.o: tests/test_browser_api_compile.c userland/libchos/include/chos/user.h include/chos/net_tcp_api.h include/chos/net_dns_api.h include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iuserland/libchos/include -c $< -o $@

$(BUILD)/test-browser-core: tests/test_browser_core.c userland/examples/browser_core.c userland/examples/browser_core.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iuserland/examples tests/test_browser_core.c userland/examples/browser_core.c -o $@

test-browser-core: $(BUILD)/test-browser-core
	./$(BUILD)/test-browser-core

test-browser-fixture:
	python3 tests/test_browser_test_server.py

test-userland: $(BUILD)/userland/async_demo.o $(BUILD)/userland/cdemo.chcx $(BUILD)/userland/gui_demo.chcx $(BUILD)/userland/text_viewer.chcx $(BUILD)/userland/console.chcx $(BUILD)/userland/file_manager.chcx $(BUILD)/userland/browser.chcx $(BUILD)/test-net-tcp-api-compile.o $(BUILD)/test-browser-api-compile.o
	@echo "CHOS C user applications (CDEMO, GUIAPP, TEXTVIEW, CONSOLE, FILEMAN and BROWSER) and TCP/DNS EL0 API compile probes passed"

$(SAN_DIR)/test-romfs: tests/test_romfs.c kernel/fs/romfs_core.c kernel/core/process_format.c include/chos/romfs.h include/chos/process.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_romfs.c kernel/fs/romfs_core.c kernel/core/process_format.c -o $@

$(SAN_DIR)/test-user-abi: tests/test_user_abi.c include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_user_abi.c -o $@

$(SAN_DIR)/test-browser-core: tests/test_browser_core.c userland/examples/browser_core.c userland/examples/browser_core.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -Iuserland/examples tests/test_browser_core.c userland/examples/browser_core.c -o $@

$(BUILD)/kernel/fs/romfs.o: include/chos/romfs.h include/chos/process.h
$(BUILD)/kernel/fs/romfs_core.o: include/chos/romfs.h include/chos/process.h
$(BUILD)/kernel/core/process.o: include/chos/romfs.h
$(BUILD)/kernel/core/shell.o: include/chos/romfs.h include/chos/storage.h include/chos/chfs.h include/chos/virtio_blk.h
$(BUILD)/kernel/core/syscall.o: include/chos/process.h include/chos/storage.h

$(BUILD)/test-chfs: tests/test_chfs.c kernel/fs/chfs.c kernel/fs/blockdev.c kernel/fs/romfs_core.c kernel/core/process_format.c include/chos/chfs.h include/chos/blockdev.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_chfs.c kernel/fs/chfs.c kernel/fs/blockdev.c kernel/fs/romfs_core.c kernel/core/process_format.c -o $@

$(SAN_DIR)/test-chfs: tests/test_chfs.c kernel/fs/chfs.c kernel/fs/blockdev.c kernel/fs/romfs_core.c kernel/core/process_format.c include/chos/chfs.h include/chos/blockdev.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_chfs.c kernel/fs/chfs.c kernel/fs/blockdev.c kernel/fs/romfs_core.c kernel/core/process_format.c -o $@

$(BUILD)/kernel/fs/storage.o: include/chos/storage.h include/chos/blockdev.h include/chos/chfs.h include/chos/virtio_blk.h
$(BUILD)/kernel/fs/chfs.o: include/chos/chfs.h include/chos/blockdev.h
$(BUILD)/kernel/fs/blockdev.o: include/chos/blockdev.h
$(BUILD)/kernel/arch/aarch64/virtio_blk.o: include/chos/virtio_blk.h include/chos/blockdev.h include/chos/platform.h
$(BUILD)/kernel/arch/aarch64/virtio_net.o: include/chos/virtio_net.h include/chos/virtio_net_abi.h include/chos/platform.h
$(BUILD)/kernel/platform/qemu_virt.o: include/chos/platform.h include/chos/vmm.h
$(BUILD)/kernel/core/kernel.o: include/chos/storage.h include/chos/virtio_net.h
$(BUILD)/kernel/gfx/gui.o: include/chos/taskbar_text.h
$(BUILD)/kernel/gfx/taskbar_text.o: include/chos/taskbar_text.h include/chos/gfx.h
$(BUILD)/kernel/net/net_services.o: include/chos/net_services.h
$(BUILD)/kernel/net/net_stack.o: include/chos/net_stack.h include/chos/net_services.h include/chos/net_packet.h include/chos/virtio_net.h include/chos/timer.h

$(BUILD)/test-gpu-damage: tests/test_gpu_damage.c include/chos/virtio_gpu.h include/chos/display_modes.h include/chos/gfx.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_gpu_damage.c -o $@

test-gpu-damage: $(BUILD)/test-gpu-damage
	./$(BUILD)/test-gpu-damage


$(BUILD)/test-net-packet: tests/test_net_packet.c kernel/net/net_packet.c include/chos/net_packet.h include/chos/virtio_net_abi.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_net_packet.c kernel/net/net_packet.c -o $@

test-net-packet: $(BUILD)/test-net-packet
	./$(BUILD)/test-net-packet

$(SAN_DIR)/test-net-packet: tests/test_net_packet.c kernel/net/net_packet.c include/chos/net_packet.h include/chos/virtio_net_abi.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_net_packet.c kernel/net/net_packet.c -o $@

$(BUILD)/test-virtio-net-abi: tests/test_virtio_net_abi.c include/chos/virtio_net_abi.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_virtio_net_abi.c -o $@

test-virtio-net-abi: $(BUILD)/test-virtio-net-abi
	./$(BUILD)/test-virtio-net-abi

$(SAN_DIR)/test-virtio-net-abi: tests/test_virtio_net_abi.c include/chos/virtio_net_abi.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_virtio_net_abi.c -o $@

$(BUILD)/test-net-services: tests/test_net_services.c kernel/net/net_services.c include/chos/net_services.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_net_services.c kernel/net/net_services.c -o $@

$(SAN_DIR)/test-net-services: tests/test_net_services.c kernel/net/net_services.c include/chos/net_services.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_net_services.c kernel/net/net_services.c -o $@

$(BUILD)/test-net-stack: tests/test_net_stack.c kernel/net/net_stack.c kernel/net/net_packet.c kernel/net/net_services.c kernel/net/net_tcp.c include/chos/net_stack.h include/chos/net_tcp.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_net_stack.c kernel/net/net_stack.c kernel/net/net_packet.c kernel/net/net_services.c kernel/net/net_tcp.c -o $@

$(SAN_DIR)/test-net-stack: tests/test_net_stack.c kernel/net/net_stack.c kernel/net/net_packet.c kernel/net/net_services.c kernel/net/net_tcp.c include/chos/net_stack.h include/chos/net_tcp.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_net_stack.c kernel/net/net_stack.c kernel/net/net_packet.c kernel/net/net_services.c kernel/net/net_tcp.c -o $@

$(BUILD)/test-net-tcp: tests/test_net_tcp.c kernel/net/net_tcp.c include/chos/net_tcp.h include/chos/net_packet.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_net_tcp.c kernel/net/net_tcp.c -o $@

$(SAN_DIR)/test-net-tcp: tests/test_net_tcp.c kernel/net/net_tcp.c include/chos/net_tcp.h include/chos/net_packet.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_net_tcp.c kernel/net/net_tcp.c -o $@

test-net-tcp: $(BUILD)/test-net-tcp
	./$(BUILD)/test-net-tcp

test-net-stack: $(BUILD)/test-net-stack
	./$(BUILD)/test-net-stack

test-net-services: $(BUILD)/test-net-services
	./$(BUILD)/test-net-services

test-taskbar-text: $(BUILD)/test-taskbar-text
	./$(BUILD)/test-taskbar-text

$(BUILD)/test-platform-boot: tests/test_platform_boot.c kernel/platform/qemu_virt.c include/chos/platform.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_platform_boot.c kernel/platform/qemu_virt.c -o $@

$(SAN_DIR)/test-platform-boot: tests/test_platform_boot.c kernel/platform/qemu_virt.c include/chos/platform.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) tests/test_platform_boot.c kernel/platform/qemu_virt.c -o $@

$(BUILD)/test-taskbar-text: tests/test_taskbar_text.c kernel/gfx/taskbar_text.c kernel/gfx/gfx_core.c kernel/gfx/font_ui_dejavu.h include/chos/taskbar_text.h include/chos/gfx.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude -Ikernel/gfx tests/test_taskbar_text.c kernel/gfx/taskbar_text.c kernel/gfx/gfx_core.c -o $@

$(SAN_DIR)/test-taskbar-text: tests/test_taskbar_text.c kernel/gfx/taskbar_text.c kernel/gfx/gfx_core.c kernel/gfx/font_ui_dejavu.h include/chos/taskbar_text.h include/chos/gfx.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -Ikernel/gfx tests/test_taskbar_text.c kernel/gfx/taskbar_text.c kernel/gfx/gfx_core.c -o $@

$(BUILD)/test-gui-protocol: tests/test_gui_protocol.c include/chos/gui_protocol.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_gui_protocol.c -o $@

$(BUILD)/test-gfx: tests/test_gfx.c kernel/gfx/gfx_core.c kernel/gfx/font8x12.h kernel/gfx/font_ui_dejavu.h include/chos/gfx.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude -Ikernel/gfx tests/test_gfx.c kernel/gfx/gfx_core.c -o $@

$(SAN_DIR)/test-gfx: tests/test_gfx.c kernel/gfx/gfx_core.c kernel/gfx/font8x12.h kernel/gfx/font_ui_dejavu.h include/chos/gfx.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -Iinclude -Ikernel/gfx tests/test_gfx.c kernel/gfx/gfx_core.c -o $@

$(BUILD)/kernel/gfx/gfx_core.o: kernel/gfx/font8x12.h kernel/gfx/font_ui_dejavu.h include/chos/gfx.h
$(BUILD)/kernel/gfx/gui.o: $(ASSET_HEADER) include/chos/gui.h include/chos/window_manager.h include/chos/gui.h include/chos/gfx.h include/chos/virtio_gpu.h include/chos/storage.h include/chos/virtio_input.h include/chos/chfs.h include/chos/process.h include/chos/romfs.h
$(BUILD)/kernel/arch/aarch64/virtio_gpu.o: include/chos/virtio_gpu.h include/chos/platform.h
$(BUILD)/kernel/arch/aarch64/virtio_input.o: include/chos/virtio_input.h include/chos/gui.h include/chos/platform.h
$(BUILD)/kernel/core/kernel.o: include/chos/virtio_gpu.h include/chos/virtio_input.h include/chos/gui.h
$(BUILD)/kernel/core/shell.o: include/chos/gui.h include/chos/virtio_input.h include/chos/uart.h
$(BUILD)/kernel/arch/aarch64/uart.o: include/chos/uart.h


$(BUILD)/test-window-manager: tests/test_window_manager.c kernel/gfx/window_manager.c include/chos/window_manager.h include/chos/window_manager.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_window_manager.c kernel/gfx/window_manager.c -o $@

$(SAN_DIR)/test-window-manager: tests/test_window_manager.c kernel/gfx/window_manager.c include/chos/window_manager.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -Iinclude tests/test_window_manager.c kernel/gfx/window_manager.c -o $@

$(BUILD)/test-gui: $(ASSET_HEADER) tests/test_gui.c kernel/gfx/gui.c kernel/gfx/gfx_core.c kernel/gfx/window_manager.c kernel/gfx/taskbar_text.c kernel/gfx/font8x12.h kernel/gfx/font_ui_dejavu.h include/chos/gui.h include/chos/window_manager.h include/chos/virtio_gpu.h include/chos/virtio_input.h include/chos/storage.h include/chos/chfs.h include/chos/process.h include/chos/romfs.h include/chos/taskbar_text.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude -Ikernel/gfx tests/test_gui.c kernel/gfx/gui.c kernel/gfx/gfx_core.c kernel/gfx/window_manager.c kernel/gfx/display_modes.c kernel/gfx/taskbar_text.c -o $@

$(SAN_DIR)/test-gui: $(ASSET_HEADER) tests/test_gui.c kernel/gfx/gui.c kernel/gfx/gfx_core.c kernel/gfx/window_manager.c kernel/gfx/taskbar_text.c kernel/gfx/font8x12.h kernel/gfx/font_ui_dejavu.h include/chos/gui.h include/chos/window_manager.h include/chos/virtio_gpu.h include/chos/virtio_input.h include/chos/storage.h include/chos/chfs.h include/chos/process.h include/chos/romfs.h include/chos/taskbar_text.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) -Iinclude -Ikernel/gfx tests/test_gui.c kernel/gfx/gui.c kernel/gfx/gfx_core.c kernel/gfx/window_manager.c kernel/gfx/display_modes.c kernel/gfx/taskbar_text.c -o $@

$(BUILD)/kernel/net/net_tcp.o: include/chos/net_tcp.h include/chos/net_tcp_api.h include/chos/net_packet.h
$(BUILD)/kernel/net/net_stack.o: include/chos/net_tcp.h include/chos/net_packet.h
$(BUILD)/kernel/core/syscall.o: include/chos/net_tcp.h include/chos/net_tcp_api.h include/chos/syscall.h
$(BUILD)/kernel/core/process.o: include/chos/net_tcp.h
$(BUILD)/test-net-tcp: tests/test_net_tcp.c kernel/net/net_tcp.c include/chos/net_tcp.h include/chos/net_tcp_api.h
$(BUILD)/test-net-stack: tests/test_net_stack.c kernel/net/net_stack.c kernel/net/net_tcp.c include/chos/net_tcp.h include/chos/net_tcp_api.h
$(BUILD)/test-user-abi: tests/test_user_abi.c include/chos/syscall.h include/chos/net_tcp_api.h
$(SAN_DIR)/test-net-tcp: tests/test_net_tcp.c kernel/net/net_tcp.c include/chos/net_tcp.h include/chos/net_tcp_api.h


.PHONY: test-uefi-handoff-contract
test-uefi-handoff-contract:
	python3 tests/test_uefi_handoff_contract.py

$(BUILD)/kernel/platform/virtio_rng.o: include/chos/entropy.h include/chos/platform.h
$(BUILD)/kernel/core/installer_plan.o: include/chos/installer.h
$(BUILD)/kernel/core/syscall.o: include/chos/entropy.h

# Native USB transport policy and report normalization tests.
USB_TEST_SOURCES := tests/test_usb.c kernel/platform/usb.c kernel/platform/usb_hid.c kernel/platform/xhci_ring.c
$(BUILD)/test-usb: $(USB_TEST_SOURCES) include/chos/usb.h include/chos/xhci.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude $(USB_TEST_SOURCES) -o $@
$(SAN_DIR)/test-usb: $(USB_TEST_SOURCES) include/chos/usb.h include/chos/xhci.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $(USB_TEST_SOURCES) -o $@
.PHONY: test-usb
test-usb: $(BUILD)/test-usb
	./$(BUILD)/test-usb
test: test-usb
test-sanitize: test-usb-sanitize
.PHONY: test-usb-sanitize
test-usb-sanitize: $(SAN_DIR)/test-usb
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-usb

# Rebuild consumers after platform/header contract changes in both boot modes.
-include $(patsubst %.o,%.d,$(OBJECTS) $(UEFI_KERNEL_OBJECTS))

XHCI_TEST_SOURCES := tests/test_xhci.c kernel/platform/usb.c kernel/platform/usb_hid.c kernel/platform/xhci_ring.c
$(BUILD)/test-xhci: $(XHCI_TEST_SOURCES) kernel/platform/xhci.c
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude $(XHCI_TEST_SOURCES) -o $@
$(SAN_DIR)/test-xhci: $(XHCI_TEST_SOURCES) kernel/platform/xhci.c
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $(XHCI_TEST_SOURCES) -o $@
.PHONY: test-xhci test-xhci-sanitize
test-xhci: $(BUILD)/test-xhci
	./$(BUILD)/test-xhci
test-xhci-sanitize: $(SAN_DIR)/test-xhci
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-xhci
test: test-xhci
test-sanitize: test-xhci-sanitize

$(BUILD)/test-pci-resources: tests/test_pci_resources.c include/chos/pci_resources.h include/chos/boot_info.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude $< -o $@
$(SAN_DIR)/test-pci-resources: tests/test_pci_resources.c include/chos/pci_resources.h include/chos/boot_info.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $< -o $@
.PHONY: test-pci-resources test-pci-resources-sanitize
test-pci-resources: $(BUILD)/test-pci-resources
	./$(BUILD)/test-pci-resources
test-pci-resources-sanitize: $(SAN_DIR)/test-pci-resources
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-pci-resources
test: test-pci-resources
test-sanitize: test-pci-resources-sanitize

$(BUILD)/test-virtio-blk: tests/test_virtio_blk.c kernel/arch/aarch64/virtio_blk.c include/chos/dma.h
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude $< -o $@
$(SAN_DIR)/test-virtio-blk: tests/test_virtio_blk.c kernel/arch/aarch64/virtio_blk.c include/chos/dma.h
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $< -o $@
.PHONY: test-virtio-blk test-virtio-blk-sanitize
test-virtio-blk: $(BUILD)/test-virtio-blk
	./$(BUILD)/test-virtio-blk
test-virtio-blk-sanitize: $(SAN_DIR)/test-virtio-blk
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-virtio-blk
test: test-virtio-blk
test-sanitize: test-virtio-blk-sanitize

$(BUILD)/userland/rng_test.o: userland/examples/rng_test.c userland/libchos/include/chos/user.h include/chos/syscall.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@
$(BUILD)/userland/rng_test.elf: $(BUILD)/userland/start.o $(BUILD)/userland/rng_test.o userland/user.ld
	$(LD) -T userland/user.ld -nostdlib -static -z max-page-size=4096 -o $@ $(BUILD)/userland/start.o $(BUILD)/userland/rng_test.o
$(BUILD)/userland/rng_test.chcx: $(BUILD)/userland/rng_test.elf tools/mkchcx.py
	$(OBJCOPY) -O binary $< $(BUILD)/userland/rng_test.bin
	$(NM) --defined-only --numeric-sort $< > $(BUILD)/userland/rng_test.sym
	python3 tools/mkchcx.py --v2-symbols $(BUILD)/userland/rng_test.sym $(BUILD)/userland/rng_test.bin $@
	python3 tools/mkchcx.py --verify $@
test-userland test qemu-smoke: $(BUILD)/userland/rng_test.chcx

$(BUILD)/test-gicv3: tests/test_gicv3.c kernel/arch/aarch64/gicv3.c
	@mkdir -p $(dir $@)
	$(CC_HOST) -std=c11 -Wall -Wextra -Werror -Iinclude $< -o $@
$(SAN_DIR)/test-gicv3: tests/test_gicv3.c kernel/arch/aarch64/gicv3.c
	@mkdir -p $(dir $@)
	$(CC_HOST) $(SAN_FLAGS) $< -o $@
.PHONY: test-gicv3 test-gicv3-sanitize
test-gicv3: $(BUILD)/test-gicv3
	./$(BUILD)/test-gicv3
test-gicv3-sanitize: $(SAN_DIR)/test-gicv3
	ASAN_OPTIONS=detect_leaks=0 ./$(SAN_DIR)/test-gicv3
test: test-gicv3
test-sanitize: test-gicv3-sanitize
