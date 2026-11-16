#!/usr/bin/env python3
"""Execute actual WebCore local-owned backing; unsupported IPC remains refused."""
import argparse, pathlib, hashlib, json, subprocess, re, time
import sys, shutil
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[3]/'tests'))
from qemu_platform import Guest, wait

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=pathlib.Path, required=True)
p.add_argument('--mode', choices=['direct', 'uefi'], default='direct')
p.add_argument('--application', type=pathlib.Path, required=True)

p.add_argument('--runs', type=int, choices=[1, 3], default=3)
p.add_argument('--image-root', type=pathlib.Path)
o = p.parse_args()
out = o.out.resolve(); out.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
app = out/'application.elf'; shutil.copyfile(o.application,app); app_sha = sha(app)
assert not subprocess.check_output(['llvm-nm', '-u', str(app)], text=True).strip()
headers = subprocess.check_output(['llvm-readelf', '-lW', str(app)], text=True)
assert not re.search(r'^\s+TLS\s', headers, re.M)
assert not re.search(r'^\s+LOAD[^\n]*\bRWE\b', headers, re.M)
disk = out / 'disposable.img'
subprocess.run(['python3', 'tools/mkchfs.py', 'create', str(disk), '--size-mib', '16'], check=True)
subprocess.run(['python3', 'tools/mkchfs.py', 'put', str(disk), '/LOCALMEM.ELF', str(app)], check=True)
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
markers = ['[ OK ] original WebCore local memory ownership,aliasing,refusal and threads passed']
failures = ['[FAIL]', '[USER FAULT]', '[FATAL]']
runs = []

def cold():
    deadline = time.monotonic() + 15
    header = 'CHOS process table (foreground and scheduled EL0 processes)'
    end = 'Loader sources: packaged ROMFS and checked CHFS executables.'
    while True:
        count = g.text().count(header); g.serial('procinfo')
        # Serial output can yield midway through a process row. Read the whole
        # command before inspecting exit status or sending the next command.
        wait(lambda: g.text().count(header) > count and
             end in g.text().rsplit(header, 1)[-1], 4, 'complete process status')
        section = g.text().rsplit(header, 1)[-1]
        if re.search(r'Active: 0;.*pending cleanup: 0', section): break
        assert time.monotonic() < deadline, 'Process cleanup failed'
        time.sleep(.03)
    page_report = r'Free pages: (\d+) \(approximately \d+ MiB\)\r?\n'
    count = len(re.findall(page_report, g.text())); g.serial('meminfo')
    wait(lambda: len(re.findall(page_report, g.text())) > count, 4, 'physical page report')
    assert g.symbol('committed_frame_limits') == 0
    assert g.symbol('engine_owner_slot') == 0xffffffff
    assert g.symbol('futex_queued_waiters') == 0
    return int(re.findall(page_report, g.text())[-1]), section

try:
    wait(lambda: 'chos> ' in g.text(), 60, 'scheduled runtime')
    if o.mode == 'uefi': assert 'UEFI CPU isolation active' in g.text()
    wait(lambda: g.symbol('splash_active') == 0, 10, 'splash')
    baseline, _ = cold()
    for cycle in range(o.runs):
        offset = len(g.text()); started = time.monotonic(); g.serial('exec /LOCALMEM.ELF')
        wait(lambda: markers[-1] in g.text()[offset:] or any(f in g.text()[offset:] for f in failures),
             90, 'original WebCore memory ownership, refusal and concurrent workers')
        text = g.text()[offset:]
        assert not any(f in text for f in failures), text
        assert all(text.count(marker) == 1 for marker in markers), text
        pages, section = cold(); assert pages == baseline, (baseline, pages)
        pid = int(re.search(r'pid=(\d+) path=/LOCALMEM.ELF', g.text()[offset:])[1])
        assert re.search(r'\bpid='+str(pid)+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b', section), section
        heaps = re.findall(r'\[WEBCORE LOCAL MEMORY HEAP\] allocations=(\d+) mapped=(\d+) warm_allocations=(\d+) warm_mapped=(\d+)', text)
        assert heaps == [('0', '0', '0', '0')], heaps
        runs.append({'cycle': cycle, 'pid': pid, 'seconds': time.monotonic()-started,
                     'cold_pages_restored': pages,
                     'heap_before_main_return': [
                         {'allocations': int(allocations), 'mapped_bytes': int(mapped)}
                         for allocations, mapped in re.findall(
                             r'\[WEBCORE LOCAL MEMORY HEAP\] allocations=(\d+) mapped=(\d+)', text)]})
    assert sha(app) == app_sha, 'Application changed during qualification'
    assert not any(f in g.text() for f in failures)
    (out/'result.json').write_text(json.dumps({
        'status': 'PASS', 'mode': o.mode, 'application_sha256': app_sha,
        'harness_sha256': sha(pathlib.Path(__file__).resolve()),
        'cold_free_pages': baseline, 'runs': runs,
        'checks': ['Original SharedBuffer and SharedMemory execute with actual CHOS ReadWrite retained backing',
                   'Zero fill, all-byte lifetime/alias checks, forged-size and overflow refusal, ReadOnly/COW/IPC refusal, three pthread workers and exact cold cleanup'],
        'limits': ['Selected local ReadWrite ownership component; full page/WebCore/browser and process IPC remain unqualified',
                   'ReadOnly alias mapping and CopyOnWrite explicitly fail; native component sanitizers and physical hardware not run; Stage3 incomplete',
                   'Selected fixture has zero native heap allocations before main returns; full engine globals are separate']}, indent=2)+'\n')
except Exception as error:
    try:
        g.serial('procinfo'); g.serial('taskinfo'); g.serial('meminfo'); time.sleep(.2)
        g.screen('failure'); (out/'registers.txt').write_text(g.hmp('info registers'))
    except Exception: pass
    (out/'result.json').write_text(json.dumps({'status': 'FAIL', 'qualified': False, 'error': str(error)}, indent=2)+'\n')
    raise
finally:
    g.close()
