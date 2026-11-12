#!/usr/bin/env python3
"""Execute actual libjpeg corpus, suspended input, failure recovery and threads in isolated CHOS."""
import argparse, pathlib, hashlib, json, subprocess, re, time
from qemu_platform import Guest, wait

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=pathlib.Path, required=True)
p.add_argument('--mode', choices=['direct', 'uefi'], default='direct')
p.add_argument('--application', type=pathlib.Path, required=True)
p.add_argument('--cases', type=pathlib.Path, required=True)
p.add_argument('--runs', type=int, choices=[1, 3], default=3)
p.add_argument('--image-root', type=pathlib.Path)
o = p.parse_args()
out = o.out.resolve(); out.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
app = o.application.resolve(); app_sha = sha(app)
cases = json.loads(o.cases.read_text())
assert len(cases['cases']) >= 20
assert not subprocess.check_output(['llvm-nm', '-u', str(app)], text=True).strip()
headers = subprocess.check_output(['llvm-readelf', '-lW', str(app)], text=True)
assert not re.search(r'^\s+TLS\s', headers, re.M)
assert not re.search(r'^\s+LOAD[^\n]*\bRWE\b', headers, re.M)
disk = out / 'disposable.img'
subprocess.run(['python3', 'tools/mkchfs.py', 'create', str(disk), '--size-mib', '16'], check=True)
subprocess.run(['python3', 'tools/mkchfs.py', 'put', str(disk), '/JPEG.ELF', str(app)], check=True)
a = argparse.Namespace(mode=o.mode,
    kernel=o.image_root/'chos.elf' if o.image_root else pathlib.Path('build/chos.elf'),
    uefi_kernel=o.image_root/'uefi-kernel.elf' if o.image_root else pathlib.Path('build/uefi-kernel/chos.elf'),
    iso=o.image_root/'chos.iso' if o.image_root else pathlib.Path('build/chos-0.30.0.iso'),
    memory='2G' if o.mode == 'uefi' else '512M',
    qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'), rtc_base=None, trace=False,
    firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),
    no_usb=False, usb_pci_bus=0, xhci_model='qemu-xhci', pci_virtio=True,
    net_model='e1000e', storage_error=False, nvme=False, no_rng=False,
    hub_depth=0, empty_usb=False, tablet=True)
g = Guest(a, out, disk)
markers = [
    '[ OK ] all native JPEG reference images passed',
    '[ OK ] native JPEG arithmetic entropy uses bounded complete input (1 cases)',
    '[ OK ] native JPEG fragmented input and failures passed',
    '[ OK ] actual JPEG workers and cleanup passed']
failures = ['[FAIL]', '[USER FAULT]', '[FATAL]']
runs = []

def cold():
    deadline = time.monotonic() + 15
    header = 'CHOS process table (foreground and scheduled EL0 processes)'
    while True:
        count = g.text().count(header); g.serial('procinfo')
        wait(lambda: g.text().count(header) > count, 4, 'process status')
        section = g.text().rsplit(header, 1)[-1]
        if re.search(r'Active: 0;.*pending cleanup: 0', section): break
        assert time.monotonic() < deadline, 'Process cleanup failed'
        time.sleep(.03)
    count = len(re.findall(r'Free pages: (\d+)', g.text())); g.serial('meminfo')
    wait(lambda: len(re.findall(r'Free pages: (\d+)', g.text())) > count, 4, 'physical page report')
    assert g.symbol('committed_frame_limits') == 0
    assert g.symbol('engine_owner_slot') == 0xffffffff
    assert g.symbol('futex_queued_waiters') == 0
    return int(re.findall(r'Free pages: (\d+)', g.text())[-1]), section

try:
    wait(lambda: 'chos> ' in g.text(), 60, 'scheduled runtime')
    if o.mode == 'uefi': assert 'UEFI CPU isolation active' in g.text()
    wait(lambda: g.symbol('splash_active') == 0, 10, 'splash')
    baseline, _ = cold()
    for cycle in range(o.runs):
        offset = len(g.text()); started = time.monotonic(); g.serial('exec /JPEG.ELF')
        wait(lambda: markers[-1] in g.text()[offset:] or any(f in g.text()[offset:] for f in failures),
             90, 'native JPEG corpus, failure cleanup and concurrent workers')
        text = g.text()[offset:]
        assert not any(f in text for f in failures), text
        assert all(text.count(marker) == 1 for marker in markers), text
        observed = re.findall(r'\[JPEG PASS\] (\S+) (\d+)x(\d+)x(\d+) peak=(\d+) hash=([0-9a-f]+)', text)
        assert len(observed) == len(cases['cases']), text
        for row, case in zip(observed, cases['cases']):
            name, width, height, components, peak, digest = row
            assert (name, int(width), int(height), int(components), int(digest, 16)) == (
                case['name'], case['width'], case['height'], case['components'], int(case['pixel_FNV64'], 16)), row
            assert 0 < int(peak) <= 8*1024*1024
        pages, section = cold(); assert pages == baseline, (baseline, pages)
        pid = int(re.search(r'pid=(\d+) path=/JPEG.ELF', g.text()[offset:])[1])
        assert re.search(r'\bpid='+str(pid)+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b', section), section
        runs.append({'cycle': cycle, 'pid': pid, 'seconds': time.monotonic()-started,
                     'positive_cases': len(observed), 'cold_pages_restored': pages,
                     'peak_decode_bytes': max(int(row[4]) for row in observed)})
    assert sha(app) == app_sha, 'Application changed during qualification'
    assert not any(f in g.text() for f in failures)
    (out/'result.json').write_text(json.dumps({
        'status': 'PASS', 'mode': o.mode, 'application_sha256': app_sha,
        'cases_manifest_sha256': sha(o.cases), 'cold_free_pages': baseline, 'runs': runs,
        'checks': ['Original JPEG input dimensions and complete sample hashes match checked upstream/reference outputs',
                   'Selected integer IDCT,gray/RGB,upsampling,scaling,progressive and arithmetic decode cases',
                   '88 fragmented deliveries:84 suspend-capable Huffman paths and4 bounded complete-input arithmetic entropy paths',
                   'Real suspended source input,strict truncation/error policy,dimension and allocation bounds',
                   'Three real pthread workers and separate per-decoder error recovery',
                   'Normal exit0 and exact physical-page,engine,committed-frame and futex cleanup'],
        'limits': ['Selected native JPEG codec prerequisite;not WebCore/browser integration or full native upstream conformance',
                   'Upstream arithmetic entropy cannot suspend;the consumer buffers its bounded complete input before entropy decoding',
                   'Historical upstream MD5 values are regression checks,not security authentication;source uses signed SHA256-pinned release',
                   '8-bit output samples;ICC/color management,EXIF display orientation and other precision modes not qualified',
                   'Consumer hashes separate rows;large contiguous WebCore image buffers remain open',
                   'QEMU only;new Fusion,Pi5 and laptop execution NOT RUN;Stage3 incomplete']}, indent=2)+'\n')
except Exception as error:
    try:
        g.serial('procinfo'); g.serial('taskinfo'); g.serial('meminfo'); time.sleep(.2)
        g.screen('failure'); (out/'registers.txt').write_text(g.hmp('info registers'))
    except Exception: pass
    (out/'result.json').write_text(json.dumps({'status': 'FAIL', 'qualified': False, 'error': str(error)}, indent=2)+'\n')
    raise
finally:
    g.close()
