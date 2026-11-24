#!/usr/bin/env python3
"""Execute genuine original WebCore request/response/error value semantics on CHOS."""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import struct
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
from qemu_platform import Guest, wait


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--application', type=pathlib.Path, required=True)
    parser.add_argument('--image-root', type=pathlib.Path, required=True)
    parser.add_argument('--embedded', action='store_true', help='Application is the exact staged diagnostic ROMFS engine')
    parser.add_argument('--mode', choices=['direct', 'uefi'], default='direct')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    application = out / 'application.elf'
    shutil.copyfile(args.application, application)
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    application_sha = sha(application)
    assert not subprocess.check_output(['llvm-nm', '-u', str(application)], text=True).strip()
    disk = out / 'disposable.img'
    mkchfs = ['python3', str(ROOT / 'tools/mkchfs.py')]
    subprocess.run([*mkchfs, 'create', str(disk), '--size-mib', '16'], check=True)
    executable = '/BIN/JSCVM.ELF' if args.embedded else '/VALUES.ELF'
    if not args.embedded:
        subprocess.run([*mkchfs, 'put', str(disk), executable, str(application)], check=True)
    images = args.image_root.resolve()
    options = argparse.Namespace(mode=args.mode, kernel=images / 'chos.elf',
        uefi_kernel=images / 'uefi-kernel.elf', iso=images / 'chos.iso',
        memory='2G' if args.mode == 'uefi' else '512M',
        qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),
        rtc_base=None, trace=False,
        firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),
        no_usb=False, usb_pci_bus=0, xhci_model='qemu-xhci', pci_virtio=True,
        net_model='e1000e', storage_error=False, nvme=False, no_rng=False,
        hub_depth=0, empty_usb=False, tablet=True)
    marker = '[ OK ] actual WebCore request response error values passed; checks=39'
    failures = ('[FAIL]', '[FATAL]', '[USER FAULT]')
    guest = None
    runs = []
    try:
        guest = Guest(options, out, disk)
        if args.embedded:
            raw = (out / 'kernel.elf').read_bytes()
            start = guest.symbols['chos_romfs_engine_start']; end = guest.symbols['chos_romfs_engine_end']
            offset = struct.unpack_from('<Q', raw, 32)[0]; embedded = None
            for i in range(struct.unpack_from('<H', raw, 56)[0]):
                kind, flags, file_offset, va, pa, size, memory, alignment = struct.unpack_from('<IIQQQQQQ', raw, offset + i * 56)
                if kind == 1 and va <= start < end <= va + size:
                    embedded = raw[file_offset + start - va:file_offset + end - va]
            assert embedded == application.read_bytes(), 'Diagnostic kernel embeds different application bytes'


        def cold():
            header = 'CHOS process table (foreground and scheduled EL0 processes)'
            end = 'Loader sources: packaged ROMFS and checked CHFS executables.'
            deadline = time.monotonic() + 15
            while True:
                offset = len(guest.text())
                guest.serial('procinfo')
                wait(lambda: header in guest.text()[offset:] and end in guest.text()[offset:], 4, 'complete process report')
                section = guest.text()[offset:]
                if re.search(r'Active: 0;[^\r\n]*pending cleanup: 0', section):
                    break
                assert time.monotonic() < deadline, 'Process cleanup incomplete'
                time.sleep(.03)
            offset = len(guest.text())
            guest.serial('meminfo')
            wait(lambda: re.search(r'Free pages: (\d+) \(approximately \d+ MiB\)\r?\n', guest.text()[offset:]), 4, 'physical-page report')
            assert guest.symbol('committed_frame_limits') == 0
            assert guest.symbol('engine_owner_slot') == 0xffffffff
            assert guest.symbol('futex_queued_waiters') == 0
            return int(re.search(r'Free pages: (\d+)', guest.text()[offset:])[1]), section

        wait(lambda: 'chos> ' in guest.text(), 60, 'scheduled runtime')
        wait(lambda: guest.symbol('splash_active') == 0, 10, 'desktop splash')
        if args.mode == 'uefi':
            assert 'UEFI CPU isolation active' in guest.text()
        baseline, _ = cold()
        for cycle in range(3):
            offset = len(guest.text())
            guest.serial('exec ' + executable)
            wait(lambda: marker in guest.text()[offset:] or any(item in guest.text()[offset:] for item in failures), 30, 'native WebCore values')
            text = guest.text()[offset:]
            assert text.count(marker) == 1 and not any(item in text for item in failures), text
            rows = re.findall(r'\[VALUES HEAP\] round=(\d+) allocations=(\d+) mapped=(\d+)', text)
            assert len(rows) == 3 and [int(row[0]) for row in rows] == [0, 1, 2], text
            assert len({row[1:] for row in rows}) == 1, rows
            pages, section = cold()
            assert pages == baseline, (baseline, pages)
            pid = int(re.search(r'pid=(\d+) path=' + re.escape(executable), guest.text()[offset:])[1])
            assert re.search(r'\bpid=' + str(pid) + r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b', section), section
            runs.append({'cycle': cycle, 'pid': pid, 'cold_pages_restored': pages})
        assert sha(application) == application_sha
        result = {'status': 'PASS', 'mode': args.mode, 'application_sha256': application_sha, 'embedded_ROMFS': args.embedded,
            'harness_sha256': sha(pathlib.Path(__file__).resolve()), 'cold_free_pages': baseline,
            'boot_artifacts': json.loads((out / 'artifacts.json').read_text()), 'runs': runs,
            'checks': ['Actual original WebCore Request/Response/Error/FormData and HTTP/origin value sources with CHOS owned values',
                'Policy-preserving isolated copy, Base-typed self/reuse, independent body metadata, request/response/error serialization',
                'Actual HTTP redirect credential/referrer handling, cache/filename metadata, privacy errors and three receiving pthreads',
                'Three exit-zero native lifetimes, stable warm ownership and exact cold process/frame/futex cleanup'],
            'limits': ['Selected value types; no resource fetcher, WebCore page rendering or modern browser acceptance',
                'QEMU only; new physical Fusion/Pi5/laptop execution not run; Stage 3 incomplete']}
        (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    except Exception as error:
        (out / 'result.json').write_text(json.dumps({'status': 'FAIL', 'error': str(error), 'runs': runs}, indent=2) + '\n')
        raise
    finally:
        if guest is not None:
            guest.close()


if __name__ == '__main__':
    main()
