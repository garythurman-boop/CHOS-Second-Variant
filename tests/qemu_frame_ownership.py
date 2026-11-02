#!/usr/bin/env python3
"""Full declared kernel frame quota, real PMM/map reuse, and cold cleanup."""
import argparse
import json
import pathlib
import re
import shutil
import time
from qemu_platform import Guest, wait

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=pathlib.Path, required=True)
p.add_argument('--mode', choices=['direct', 'uefi'], default='direct')
o = p.parse_args()
out = o.out.resolve()
out.mkdir(parents=True, exist_ok=False)
disk = out / 'disposable.img'
shutil.copyfile('build/qemu-smoke-disk.img', disk)
a = argparse.Namespace(
    mode=o.mode, kernel=pathlib.Path('build/chos.elf'),
    uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),
    iso=pathlib.Path('build/chos-0.30.0.iso'),
    memory='2G' if o.mode == 'uefi' else '512M',
    qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),
    rtc_base=None, trace=False,
    firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),
    no_usb=False, usb_pci_bus=0, xhci_model='qemu-xhci', pci_virtio=True,
    net_model='e1000e', storage_error=False, nvme=False, no_rng=False,
    hub_depth=0, empty_usb=False, tablet=True)
g = Guest(a, out, disk)
marker = '[ OK ] full-quota indexed frame ownership, page reuse and cleanup passed'


def free_pages():
    before = len(re.findall(r'Free pages: (\d+)', g.text()))
    g.serial('meminfo')
    wait(lambda: len(re.findall(r'Free pages: (\d+)', g.text())) > before, 3, 'PMM diagnostic')
    return int(re.findall(r'Free pages: (\d+)', g.text())[-1])


try:
    wait(lambda: 'chos> ' in g.text(), 45, 'scheduled runtime')
    if o.mode == 'uefi':
        assert 'UEFI CPU isolation active' in g.text()
    wait(lambda: g.symbol('splash_active') == 0, 10, 'splash')
    cold = free_pages()
    times = []
    ticks_before = int.from_bytes(g.block('tick_count', size=8), 'little')
    for run in range(3):
        offset = len(g.text())
        started = time.monotonic()
        g.serial('as-frames-test')
        wait(lambda: marker in g.text()[offset:] or '[FAIL]' in g.text()[offset:],
             60, 'full quota allocation/map/free/refill/destruction')
        times.append(time.monotonic() - started)
        assert marker in g.text()[offset:] and '[FAIL]' not in g.text()[offset:], g.text()[offset:]
        assert '[FATAL]' not in g.text() and '[USER FAULT]' not in g.text()
        assert free_pages() == cold, ('frame ownership leaked PMM pages', cold, free_pages())
    ticks_after = int.from_bytes(g.block('tick_count', size=8), 'little')
    assert ticks_after > ticks_before
    offset = len(g.text())
    g.serial('asinfo')
    wait(lambda: 'Root switching invalidates' in g.text()[offset:], 3, 'address-space report')
    assert 'Address-space slots: 4; live: 0' in g.text()[offset:]
    assert 'Active root: kernel (default)' in g.text()[offset:]
    (out / 'result.json').write_text(json.dumps({
        'status': 'PASS', 'mode': o.mode,
        'cold_free_pages': cold, 'final_free_pages': free_pages(),
        'runs': 3, 'rounds_per_run': 3, 'frame_quota': 2048,
        'allocations_per_run': 9216, 'run_seconds_observed': times,
        'timer_ticks_before': ticks_before,
        'timer_ticks_after': ticks_after,
        'checks': [
            'real full-quota PMM allocation, every 4 KiB byte zeroed, private RW/XN mapping',
            'quota/mapped free/duplicate VA/cross-owner/stale handle refusals',
            'odd-frame free/refill while even frames retain original PA/VA/payload',
            'permuted unmap/free/double-free and sparse owned-frame destruction',
            'exact cold PMM baseline after every round; timer advances across stress'],
        'limits': [
            'existing 2048-frame quota; large engine admission not enabled',
            'QEMU evidence does not establish Fusion/physical hardware qualification']
    }, indent=2) + '\n')
except Exception:
    (out / 'failure.txt').write_text(g.text())
    raise
finally:
    g.close()
