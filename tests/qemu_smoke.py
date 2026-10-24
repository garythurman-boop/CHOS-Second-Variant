#!/usr/bin/env python3
"""Automated serial smoke test for the direct-boot QEMU configuration."""
from __future__ import annotations
import argparse
import os
import pathlib
import shutil
import select
import subprocess
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', default='qemu-system-aarch64')
    parser.add_argument('--kernel', required=True, type=pathlib.Path)
    parser.add_argument('--disk', required=True, type=pathlib.Path)
    parser.add_argument('--log', required=True, type=pathlib.Path)
    parser.add_argument('--timeout', type=float, default=45.0)
    args = parser.parse_args()

    qemu = shutil.which(args.qemu)
    if qemu is None:
        print(f'SKIP: {args.qemu} is not installed; cannot run a live guest smoke test.', file=sys.stderr)
        return 2
    if not args.kernel.is_file() or not args.disk.is_file():
        print('ERROR: kernel or disposable smoke-test disk is missing.', file=sys.stderr)
        return 2

    args.log.parent.mkdir(parents=True, exist_ok=True)
    command = [
        qemu, '-M', 'virt,gic-version=2', '-global', 'virtio-mmio.force-legacy=false',
        '-cpu', 'cortex-a72', '-m', '512M', '-rtc', 'base=utc',
        '-display', 'none', '-monitor', 'none', '-serial', 'stdio', '-no-reboot',
        '-object', 'rng-random,filename=/dev/urandom,id=chosrng',
        '-device', 'virtio-rng-device,rng=chosrng',
        '-drive', f'if=none,file={args.disk},format=raw,id=chosdisk',
        '-device', 'virtio-blk-device,drive=chosdisk',
        '-netdev', 'user,id=chosnet', '-device', 'virtio-net-device,netdev=chosnet',
        '-device', 'virtio-gpu-device', '-device', 'virtio-keyboard-device',
        '-device', 'virtio-tablet-device', '-device', f'loader,file={args.kernel},cpu-num=0',
    ]

    expected = [
        'CHOS ARM64 kernel 0.30.0',
        '[ OK ] C kernel entry reached',
        '[ OK ] CHFS persistent filesystem mounted',
        '[ OK ] CHOS desktop composed and presented',
        '[ OK ] VirtIO input devices initialized',
        '[ OK ] VirtIO RNG initialized; nonblocking hardware-backed entropy is available',
    ]
    captured = bytearray()
    deadline = time.monotonic() + max(1.0, args.timeout)
    try:
        with args.log.open('wb') as log:
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
            try:
                assert process.stdout is not None
                while time.monotonic() < deadline:
                    ready, _, _ = select.select([process.stdout], [], [], min(0.1, deadline - time.monotonic()))
                    if ready:
                        chunk = os.read(process.stdout.fileno(), 4096)
                        if not chunk:
                            break
                        captured.extend(chunk)
                        log.write(chunk)
                        log.flush()
                        text = captured.decode('utf-8', errors='replace')
                        if all(marker in text for marker in expected):
                            print('PASS: QEMU reached the desktop and initialized storage, input, and VirtIO RNG.')
                            print(f'Full serial log: {args.log}')
                            return 0
                    elif process.poll() is not None:
                        break
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=3)
    except OSError as exc:
        print(f'ERROR: could not run QEMU: {exc}', file=sys.stderr)
        return 2

    text = captured.decode('utf-8', errors='replace')
    missing = [marker for marker in expected if marker not in text]
    print('FAIL: QEMU did not reach all required smoke-test milestones.', file=sys.stderr)
    for marker in missing:
        print(f'  missing: {marker}', file=sys.stderr)
    print(f'Full serial log: {args.log}', file=sys.stderr)
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
