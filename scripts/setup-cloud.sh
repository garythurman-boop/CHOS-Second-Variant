#!/bin/bash
# Unprivileged Debian toolchain setup; leaves system directories unchanged.
set -euo pipefail
CHOS_TOOLS=/workspace/chos-tools
mkdir -p "$CHOS_TOOLS/apt/lists/partial" "$CHOS_TOOLS/apt/archives/partial" "$CHOS_TOOLS/root"
cat > "$CHOS_TOOLS/apt/apt.conf" <<'CONFIG'
Dir::Etc::Parts "-";
Dir::Etc::main "-";
Dir::Etc::sourcelist "/workspace/chos-tools/apt/sources.list";
Dir::Etc::sourceparts "-";
Dir::State::lists "/workspace/chos-tools/apt/lists";
Dir::Cache::archives "/workspace/chos-tools/apt/archives";
Dir::Cache::pkgcache "";
Dir::Cache::srcpkgcache "";
APT::Sandbox::User "agent";
CONFIG
cat > "$CHOS_TOOLS/apt/sources.list" <<'SOURCES'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian-security trixie-security main
SOURCES
export APT_CONFIG="$CHOS_TOOLS/apt/apt.conf"
/usr/bin/apt-get update
/usr/bin/apt-get -o Debug::NoLocking=true install --download-only --no-install-recommends -y \
    clang lld llvm qemu-system-arm qemu-efi-aarch64 ipxe-qemu seabios
for package in "$CHOS_TOOLS"/apt/archives/*.deb; do
    dpkg-deb -x "$package" "$CHOS_TOOLS/root"
done
# QEMU data files are relocatable; supply the installed RAMFB ROM alongside them.
cp "$CHOS_TOOLS/root/usr/share/seabios/vgabios-ramfb.bin" "$CHOS_TOOLS/root/usr/share/qemu/vgabios-ramfb.bin"
cat > "$CHOS_TOOLS/activate" <<'ACTIVATE'
export PATH=/workspace/chos-tools/root/usr/bin:/workspace/chos-tools/root/usr/lib/llvm-19/bin:$PATH
export LD_LIBRARY_PATH=/workspace/chos-tools/root/usr/lib/x86_64-linux-gnu:/workspace/chos-tools/root/usr/lib/llvm-19/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export QEMU_MODULE_DIR=/workspace/chos-tools/root/usr/lib/x86_64-linux-gnu/qemu
export QEMU_AUDIO_DRV=none
ACTIVATE
source "$CHOS_TOOLS/activate"
cd /workspace/CHOS-Second-Variant
make CC=clang LD=ld.lld -j4 all uefi-iso install-disk-image build/qemu-smoke-disk.img build/userland/rng_test.chcx
