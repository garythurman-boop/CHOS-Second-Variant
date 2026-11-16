#!/usr/bin/env python3
"""Check actual native string exports and protected terminators on disposable CHOS."""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import time

root = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests'))
from qemu_platform import Guest, wait

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=pathlib.Path, required=True)
p.add_argument('--application', type=pathlib.Path, required=True)
p.add_argument('--image-root', type=pathlib.Path, required=True)
p.add_argument('--mode', choices=['direct', 'uefi'], default='direct')
o = p.parse_args()
out = o.out.resolve()
out.mkdir(parents=True, exist_ok=False)
app = out / 'application.elf'
shutil.copyfile(o.application, app)
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
disk = out / 'disposable.img'
subprocess.run(['python3', str(root / 'tools/mkchfs.py'), 'create', str(disk), '--size-mib', '16'], check=True)
subprocess.run(['python3', str(root / 'tools/mkchfs.py'), 'put', str(disk), '/STRINGS.ELF', str(app)], check=True)
a = argparse.Namespace(mode=o.mode, kernel=o.image_root / 'chos.elf',
    uefi_kernel=o.image_root / 'uefi-kernel.elf', iso=o.image_root / 'chos.iso',
    memory='2G' if o.mode == 'uefi' else '512M',
    qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'), rtc_base=None,
    trace=False, firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),
    no_usb=False, usb_pci_bus=0, xhci_model='qemu-xhci', pci_virtio=True,
    net_model='e1000e', storage_error=False, nvme=False, no_rng=False, hub_depth=0,
    empty_usb=False, tablet=True)
g = None
marker = '[ OK ] native strings threads and mappings cleaned up'

def cold():
    header = 'CHOS process table (foreground and scheduled EL0 processes)'
    deadline = time.monotonic() + 15
    while True:
        offset = len(g.text())
        g.serial('procinfo')
        wait(lambda: header in g.text()[offset:] and re.search(r'Active: \d+;[^\r\n]*pending cleanup: \d+', g.text()[offset:]), 4, 'complete process status')
        section = g.text()[offset:]
        if re.search(r'Active: 0;[^\r\n]*pending cleanup: 0', section):
            break
        assert time.monotonic() < deadline, 'Process cleanup not quiescent'
        time.sleep(.03)
    offset = len(g.text())
    g.serial('meminfo')
    wait(lambda: re.search(r'Free pages: (\d+)', g.text()[offset:]), 4, 'physical page report')
    assert g.symbol('committed_frame_limits') == 0
    assert g.symbol('engine_owner_slot') == 0xffffffff
    assert g.symbol('futex_queued_waiters') == 0
    return int(re.search(r'Free pages: (\d+)', g.text()[offset:])[1]), section

runs = []
try:
    assert not subprocess.check_output(['llvm-nm', '-u', str(app)], text=True).strip()
    g = Guest(a, out, disk)
    wait(lambda: 'chos> ' in g.text(), 60, 'scheduled runtime')
    wait(lambda: g.symbol('splash_active') == 0, 10, 'desktop splash')
    if o.mode == 'uefi':
        assert 'UEFI CPU isolation active' in g.text()
    baseline, _ = cold()
    for cycle in range(3):
        offset = len(g.text())
        g.serial('exec /STRINGS.ELF')
        wait(lambda: marker in g.text()[offset:] or any(x in g.text()[offset:] for x in ('[FAIL]', '[FATAL]', '[USER FAULT]')), 90, 'actual native strings')
        text = g.text()[offset:]
        assert marker in text and '[ OK ] native string byte cases and protected terminators passed' in text, text
        assert not any(x in text for x in ('[FAIL]', '[FATAL]', '[USER FAULT]')), text
        pages, section = cold()
        assert pages == baseline, (baseline, pages)
        pid = int(re.search(r'pid=(\d+) path=/STRINGS.ELF', g.text()[offset:])[1])
        assert re.search(r'\bpid=' + str(pid) + r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b', section), section
        runs.append({'cycle': cycle, 'pid': pid, 'cold_free_pages_restored': pages})
    result = {'status': 'PASS', 'mode': o.mode, 'application_sha256': digest(app),
        'cold_free_pages': baseline, 'runs': runs,
        'checks': ['Actual native public strpbrk: 65280 unsigned byte-alphabet/set-length cases, plus 24576 cases in three workers',
            'Inaccessible pages immediately follow input and set NUL terminators; selected strpbrk/strspn/strcspn operations complete without faults',
            'Real pthread workers, zero native heap/mappings after cleanup and exact cold process/frame/futex restoration'],
        'limits': ['Host reference/sanitizer checks are separate evidence; this native byte oracle uses linear memchr membership',
            'Selected C runtime prerequisite only, not WebCore/browser or new physical platform qualification', 'Stage 3 remains incomplete']}
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
except Exception as error:
    (out / 'result.json').write_text(json.dumps({'status': 'FAIL', 'error': str(error)}, indent=2) + '\n')
    raise
finally:
    if g is not None:
        g.close()
