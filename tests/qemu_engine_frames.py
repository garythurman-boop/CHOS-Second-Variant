#!/usr/bin/env python3
"""Engine frame admission, pressure refusal, and preemptible physical cleanup."""
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
p.add_argument('--pressure', action='store_true')
o = p.parse_args()
out = o.out.resolve()
out.mkdir(parents=True, exist_ok=False)
disk = out / 'disposable.img'
shutil.copyfile('build/qemu-smoke-disk.img', disk)
a = argparse.Namespace(
    mode=o.mode, kernel=pathlib.Path('build/chos.elf'),
    uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),
    iso=pathlib.Path('build/chos-0.30.0.iso'),
    memory='256M' if o.pressure else ('2G' if o.mode == 'uefi' else '512M'),
    qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),
    rtc_base=None, trace=False,
    firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),
    no_usb=False, usb_pci_bus=0, xhci_model='qemu-xhci', pci_virtio=True,
    net_model='e1000e', storage_error=False, nvme=False, no_rng=False,
    hub_depth=0, empty_usb=False, tablet=True)
g = Guest(a, out, disk)
marker = '[ OK ] engine192MiB frame admission, reuse and incremental teardown passed'


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
    teardown_ticks = []
    ticks_before = int.from_bytes(g.block('tick_count', size=8), 'little')
    assert g.symbol('committed_frame_limits') == 0
    assert g.symbol('engine_owner_slot') == 0xffffffff
    if o.pressure:
        marker = '[FAIL] engine frame profile refused or cleanup baseline failed'
    for run in range(3):
        offset = len(g.text())
        started = time.monotonic()
        g.serial('as-engine-frames-test')
        wait(lambda: marker in g.text()[offset:] or '[FAIL]' in g.text()[offset:],
             120, 'engine quota allocation/map/free/refill/destruction')
        times.append(time.monotonic() - started)
        assert marker in g.text()[offset:], g.text()[offset:]
        if not o.pressure:
            assert '[FAIL]' not in g.text()[offset:], g.text()[offset:]
        assert g.symbol('committed_frame_limits') == 0
        assert g.symbol('engine_owner_slot') == 0xffffffff
        if not o.pressure:
            teardown_ticks.append(int.from_bytes(g.block('last_engine_teardown_ticks', size=8), 'little'))
            assert teardown_ticks[-1] > 0
        assert '[FATAL]' not in g.text() and '[USER FAULT]' not in g.text()
        assert free_pages() == cold, ('frame ownership leaked PMM pages', cold, free_pages())
    if o.pressure:
        offset = len(g.text())
        g.serial('as-frames-test')
        legacy = '[ OK ] full-quota indexed frame ownership, page reuse and cleanup passed'
        wait(lambda: legacy in g.text()[offset:], 60, 'legacy still admitted after engine pressure refusal')
        assert free_pages() == cold
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
        'pressure_refusal': o.pressure, 'runs': 3, 'rounds_per_run': 1, 'frame_quota': 49152,
        'allocations_per_run': 0 if o.pressure else 73728, 'run_seconds_observed': times,
        'timer_ticks_before': ticks_before,
        'timer_ticks_after': ticks_after,
        'timer_irqs_during_each_sparse_teardown': teardown_ticks,
        'checks': ([
            'three engine pressure refusals with exact cold PMM/no logical commitment/pool lease',
            'subsequent legacy full-quota diagnostic still passes'
        ] if o.pressure else [
            'actual192MiB simultaneous owned/mapped RW/XN frames;all allocated bytes zero checked',
            'quota/mapped/duplicate/cross-owner/stale refusals and exclusive engine-pool admission',
            'odd-frame release/refill preserves every other VA/PA/payload;permuted release and sparse destruction',
            'timer IRQ count advances inside each actual96MiB sparse destruction call',
            'exact cold PMM,zero logical commitment and free engine pool after each full lifecycle']),
        'limits': [
            'kernel engine frame service only;executable loader still uses legacy4MiB/8MiB profile',
            'QEMU evidence does not establish Fusion/physical hardware qualification']
    }, indent=2) + '\n')
except Exception:
    (out / 'failure.txt').write_text(g.text())
    raise
finally:
    g.close()
