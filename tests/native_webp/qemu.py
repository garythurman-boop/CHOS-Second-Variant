#!/usr/bin/env python3
"""Execute real WebP memory decode/demux in isolated CHOS processes with cold cleanup."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from qemu_platform import Guest, wait

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=pathlib.Path, required=True)
parser.add_argument('--mode', choices=['direct', 'uefi'], default='direct')
parser.add_argument('--application', type=pathlib.Path, required=True)
parser.add_argument('--image-root', type=pathlib.Path, required=True)
parser.add_argument('--cases', type=pathlib.Path, required=True)
parser.add_argument('--runs', type=int, choices=[1, 3], default=3)
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
application = args.application.resolve()
application_sha = sha(application)
cases = json.loads(args.cases.read_text())
assert len(cases['vectors']) == 3 and cases['animation']['frames'] == 2
for name, expected in cases['generated_files_sha256'].items():
    assert sha(args.cases.parent/name) == expected, name
lengths = [(args.cases.parent/(case['name']+'.webp')).stat().st_size for case in cases['vectors']]
expected_pixels = sum(2*case['width']*case['height'] for case in cases['vectors'])
expected_deliveries = sum(n+(n+6)//7+(n+10)//11+(n//2+2)//3 for n in lengths)
expected_partial_demuxes = sum((n+5)//7+1 for n in lengths)
assert not subprocess.check_output(['llvm-nm', '-u', str(application)], text=True).strip()
headers = subprocess.check_output(['llvm-readelf', '-lW', str(application)], text=True)
assert not re.search(r'^\s+TLS\s', headers, re.M)
assert not re.search(r'^\s+LOAD[^\n]*\bRWE\b', headers, re.M)
disk = out/'disposable.img'
subprocess.run(['python3', 'tools/mkchfs.py', 'create', str(disk), '--size-mib', '16'], check=True)
subprocess.run(['python3', 'tools/mkchfs.py', 'put', str(disk), '/WEBP.ELF', str(application)], check=True)
image_root = args.image_root.resolve()
guest_args = argparse.Namespace(
    mode=args.mode, kernel=image_root/'chos.elf', uefi_kernel=image_root/'uefi-kernel.elf',
    iso=image_root/'chos.iso', memory='2G' if args.mode == 'uefi' else '512M',
    qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'), rtc_base=None, trace=False,
    firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),
    no_usb=False, usb_pci_bus=0, xhci_model='qemu-xhci', pci_virtio=True,
    net_model='e1000e', storage_error=False, nvme=False, no_rng=False,
    hub_depth=0, empty_usb=False, tablet=True)
try:
    guest = Guest(guest_args, out, disk)
except Exception as error:
    (out/'result.json').write_text(json.dumps({'status': 'FAIL', 'qualified': False,
                                            'phase': 'guest-startup', 'error': str(error)}, indent=2)+'\n')
    raise
marker = '[ OK ] actual WebP lossless/lossy alpha,animation,cancellation,rejections,pthreads and cleanup passed'
failures = ['[FAIL]', '[USER FAULT]', '[FATAL]']
runs = []


def cold():
    deadline = time.monotonic()+15
    header = 'CHOS process table (foreground and scheduled EL0 processes)'
    while True:
        count = guest.text().count(header)
        guest.serial('procinfo')
        wait(lambda: guest.text().count(header) > count
             and 'Loader sources: packaged ROMFS and checked CHFS executables.'
             in guest.text().rsplit(header, 1)[-1], 4, 'complete process status')
        section = guest.text().rsplit(header, 1)[-1]
        if re.search(r'Active: 0;.*pending cleanup: 0', section):
            break
        assert time.monotonic() < deadline, 'Process cleanup failed'
        time.sleep(.03)
    count = len(re.findall(r'Free pages: (\d+)', guest.text()))
    guest.serial('meminfo')
    wait(lambda: len(re.findall(r'Free pages: (\d+)', guest.text())) > count,
         4, 'physical page report')
    assert guest.symbol('committed_frame_limits') == 0
    assert guest.symbol('engine_owner_slot') == 0xffffffff
    assert guest.symbol('futex_queued_waiters') == 0
    return int(re.findall(r'Free pages: (\d+)', guest.text())[-1]), section


try:
    wait(lambda: 'chos> ' in guest.text(), 60, 'scheduled runtime')
    if args.mode == 'uefi':
        assert 'UEFI CPU isolation active' in guest.text()
    wait(lambda: guest.symbol('splash_active') == 0, 10, 'splash')
    baseline, _ = cold()
    for cycle in range(args.runs):
        offset = len(guest.text())
        started = time.monotonic()
        guest.serial('exec /WEBP.ELF')
        wait(lambda: marker in guest.text()[offset:]
             or any(failure in guest.text()[offset:] for failure in failures),
             90, 'native WebP fragmented decoding,demux and concurrent workers')
        text = guest.text()[offset:]
        assert not any(failure in text for failure in failures), text
        assert text.count(marker) == 1, text
        pages, section = cold()
        assert pages == baseline, (baseline, pages)
        pid = int(re.search(r'pid=(\d+) path=/WEBP.ELF', guest.text()[offset:])[1])
        assert re.search(r'\bpid='+str(pid)+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b', section), section
        stats = [line.strip() for line in text.splitlines()
                 if '[ OK ] actual WebP complete pixels=' in line]
        assert len(stats) == 1, stats
        counts = re.search(r'pixels=(\d+) fragmented deliveries=(\d+) partial demux=(\d+) allocation failures=(\d+)', stats[0])
        assert counts, stats
        observed = tuple(int(value) for value in counts.groups())
        assert observed[:3] == (expected_pixels, expected_deliveries, expected_partial_demuxes), observed
        assert observed[3] > 0, observed
        runs.append({'cycle': cycle, 'pid': pid, 'seconds': time.monotonic()-started,
                     'cold_pages_restored': pages, 'fixture_statistics': stats})
    assert sha(application) == application_sha, 'Application changed during qualification'
    assert not any(failure in guest.text() for failure in failures)
    (out/'result.json').write_text(json.dumps({
        'status': 'PASS', 'mode': args.mode, 'application_sha256': application_sha,
        'cases_manifest_sha256': sha(args.cases),
        'cold_free_pages': baseline, 'runs': runs,
        'checks': ['Exact independent-formula lossless RGBA and original-host-reference lossy RGBA pixels',
                   'Incremental append/update,partial demux,two-frame animation,reset,cancellation and malformed inputs',
                   'Allocation failure cleanup and three actual pthread workers',
                   'Normal exit 0, exact cold physical pages, released engine ownership, zero frame reservations and futex waiters'],
        'limits': ['Memory WebP dependency prerequisite; no WebCore/page/browser integration claim',
                   'Selected tests; not full upstream conformance, fuzzing or a security audit',
                   'QEMU only; new Fusion, Pi 5 and laptop execution NOT RUN; Stage 3 incomplete'],
    }, indent=2)+'\n')
except Exception as error:
    try:
        guest.serial('procinfo')
        guest.serial('taskinfo')
        guest.serial('meminfo')
        time.sleep(.2)
        guest.screen('failure')
        (out/'registers.txt').write_text(guest.hmp('info registers'))
    except Exception:
        pass
    (out/'result.json').write_text(json.dumps({'status': 'FAIL', 'qualified': False,
                                            'error': str(error)}, indent=2)+'\n')
    raise
finally:
    guest.close()
