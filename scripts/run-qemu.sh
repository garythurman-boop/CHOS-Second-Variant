#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
make CC="${CC:-clang}" LD="${LD:-ld.lld}" all disk-image-ready
exec qemu-system-aarch64 -M virt,gic-version=2 -global virtio-mmio.force-legacy=false \
  -cpu cortex-a72 -m 256M -nographic \
  -monitor none -serial stdio -no-reboot \
  -drive if=none,file=chos-disk.img,format=raw,id=chosdisk \
  -device virtio-blk-device,drive=chosdisk \
  -device loader,file=build/chos.elf,cpu-num=0
