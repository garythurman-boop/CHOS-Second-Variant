#!/usr/bin/env python3
"""Check native GUI screen metadata, bounds and capabilities on disposable CHOS."""
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
subprocess.run(['python3', str(root / 'tools/mkchfs.py'), 'put', str(disk), '/SCREEN.ELF', str(app)], check=True)
a = argparse.Namespace(mode=o.mode, kernel=o.image_root / 'chos.elf',
    uefi_kernel=o.image_root / 'uefi-kernel.elf', iso=o.image_root / 'chos.iso',
    memory='2G' if o.mode == 'uefi' else '512M',
    qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'), rtc_base=None,
    trace=False, firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),
    no_usb=False, usb_pci_bus=0, xhci_model='qemu-xhci', pci_virtio=True,
    net_model='e1000e', storage_error=False, nvme=False, no_rng=False, hub_depth=0,
    empty_usb=False, tablet=True)
g = None
marker = '[ OK ] GUI screen workers capability child and heap cleaned up'

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
        g.serial('exec /SCREEN.ELF')
        wait(lambda: marker in g.text()[offset:] or any(x in g.text()[offset:] for x in ('[FAIL]', '[FATAL]', '[USER FAULT]')), 90, 'actual native screen metadata')
        text = g.text()[offset:]
        assert marker in text and '[ OK ] GUI screen size flags pointers and protected outputs passed' in text, text
        assert not any(x in text for x in ('[FAIL]', '[FATAL]', '[USER FAULT]')), text
        assert '[ OK ] GUI screen capability refusal preserved output' in text, text
        metadata = re.search(r'\[SCREEN\] width=(\d+) height=(\d+) available=(\d+) scale=(\d+)/(\d+) bpp=(\d+) color=(\d+)', text)
        assert metadata, text
        geometry = list(map(int, metadata.groups()))
        g.screen('screen-info-' + str(cycle), full=True)
        ppm = (out / ('screen-info-' + str(cycle) + '.ppm')).read_bytes()
        image_size = list(map(int, ppm.split(b'\n', 3)[1].split()))
        assert geometry[:2] == image_size, (geometry, image_size)
        pages, section = cold()
        assert pages == baseline, (baseline, pages)
        pid = int(re.search(r'pid=(\d+) path=/SCREEN.ELF', g.text()[offset:])[1])
        assert re.search(r'\bpid=' + str(pid) + r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b', section), section
        runs.append({'cycle': cycle, 'pid': pid, 'cold_free_pages_restored': pages, 'screen_metadata': geometry, 'screenshot_size': image_size})
    result = {'status': 'PASS', 'mode': o.mode, 'application_sha256': digest(app),
        'cold_free_pages': baseline, 'runs': runs,
        'checks': ['Actual active framebuffer dimensions match independent QMP screenshot dimensions',
            'Real work area, 32-bit XRGB/24 color bits and desktop scale; three workers query 192 times per process',
            'Invalid size/flags/null/read-only/wrapping/cross-guard destinations refuse without partial output',
            'A real child with GUI_SUBMIT permission removed receives EPERM with unchanged output',
            'Three normal exits with zero user heap and exact cold process/frame/futex restoration'],
        'limits': ['Read-only single active desktop metadata; no physical DPI/HDR/multi-monitor claim',
            'Kernel/API prerequisite, not WebCore page rendering or revised Stage 3 completion',
            'QEMU direct and isolated UEFI evidence; new Fusion/Pi5/laptop execution remains unrun']}
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
except Exception as error:
    (out / 'result.json').write_text(json.dumps({'status': 'FAIL', 'error': str(error)}, indent=2) + '\n')
    raise
finally:
    if g is not None:
        g.close()
