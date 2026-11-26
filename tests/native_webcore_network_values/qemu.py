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
    parser.add_argument('--fixture', choices=['network-values', 'clipboard', 'cookies', 'local-page'], default='network-values')
    parser.add_argument('--embedded', action='store_true', help='Application is the exact staged diagnostic ROMFS engine')
    parser.add_argument('--mode', choices=['direct', 'uefi'], default='direct')
    parser.add_argument('--icu-packages', type=pathlib.Path, help='Verified ICU package directory for the local page')
    args = parser.parse_args()
    if (args.fixture == 'local-page') != bool(args.icu_packages) or (args.icu_packages and not args.embedded):
        parser.error('The local page requires --embedded and --icu-packages')
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    application = out / 'application.elf'
    shutil.copyfile(args.application, application)
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    application_sha = sha(application)
    assert not subprocess.check_output(['llvm-nm', '-u', str(application)], text=True).strip()
    disk = out / 'disposable.img'
    mkchfs = ['python3', str(ROOT / 'tools/mkchfs.py')]
    subprocess.run([*mkchfs, 'create', str(disk), '--size-mib', '64' if args.icu_packages else '16'], check=True)
    if args.icu_packages:
        from qemu_webcore_component import stage_icu_packages
        stage_icu_packages(disk, args.icu_packages, 'complete')
        import mkchfs
        raw = bytearray(disk.read_bytes())
        mkchfs.mkdir(raw, '/FONTS')
        catalogue = json.loads((ROOT / 'docs/dependencies/text-fixtures.json').read_text())
        for font in catalogue['fonts']:
            data = (ROOT / 'third_party/text-fixtures' / font['path']).read_bytes()
            assert len(data) == font['bytes'] and hashlib.sha256(data).hexdigest() == font['sha256']
            mkchfs.put(raw, font['guest_path'], data)
        disk.write_bytes(raw)
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
    markers = {
        'network-values': '[ OK ] actual WebCore request response error values passed; checks=39',
        'local-page': '[ OK ] local WebCore page diagnostic completed',
        'cookies': '[ OK ] WebCore session cookie scope, policy, privileges and isolation passed',
        'clipboard': '[ OK ] native WebCore clipboard origin, reentry, buffers and modifiers passed',
    }
    marker = markers[args.fixture]
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
            next_status_check = time.monotonic() + 3

            def finished():
                nonlocal next_status_check
                output = guest.text()[offset:]
                if marker in output or any(item in output for item in failures):
                    return True
                if args.fixture == 'local-page' and '[PAGE]' in output and time.monotonic() >= next_status_check:
                    next_status_check = time.monotonic() + 1
                    if guest.symbol('engine_owner_slot') == 0xffffffff:
                        return True
                return False

            wait(finished, 90 if args.icu_packages else 30, 'native WebCore guest execution')
            text = guest.text()[offset:]
            assert text.count(marker) == 1 and not any(item in text for item in failures), text
            if args.fixture == 'local-page':
                rows = re.findall(r'\[PAGE FRAME\] epoch=(\d+) glyph_pixels=(\d+) hash=([0-9a-f]{16})', text)
                assert [int(row[0]) for row in rows] == [0, 1] and rows[0][2] != rows[1][2], text
                assert min(int(row[1]) for row in rows) > 64
                if runs:
                    assert rows == runs[0]['frames'], rows
            else:
                rows = re.findall(r'\[VALUES HEAP\] round=(\d+) allocations=(\d+) mapped=(\d+)', text)
                assert len(rows) == 3 and [int(row[0]) for row in rows] == [0, 1, 2], text
                assert len({row[1:] for row in rows}) == 1, rows
            pages, section = cold()
            assert pages == baseline, (baseline, pages)
            pid = int(re.search(r'pid=(\d+) path=' + re.escape(executable), guest.text()[offset:])[1])
            assert re.search(r'\bpid=' + str(pid) + r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b', section), section
            runs.append({'cycle': cycle, 'pid': pid, 'cold_pages_restored': pages})
            if args.fixture == 'local-page':
                runs[-1]['frames'] = rows
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
        result['fixture'] = args.fixture
        if args.fixture == 'clipboard':
            result['checks'] = ['WebCore custom format origin filtering and private staging',
                'Reader reentry with live replacement, indexed records and rich/plain fallback',
                'Retained binary data, shared revision and modifier transitions',
                'Three exit-zero lifetimes, stable warm heap and exact cold cleanup']
        if args.fixture == 'cookies':
            result['checks'] = ['DOM and request visibility, host/domain/path matching and public suffix rejection',
                'Secure, HttpOnly, SameSite, reserved prefixes, expiry and session isolation',
                'Raw-cookie permission, three exit-zero lifetimes and exact warm/cold ownership']
        if args.fixture == 'local-page':
            result['checks'] = ['Embedded local HTML, CSS geometry and JavaScript DOM mutation',
                'Two 512x256 Cairo frames with full geometry oracle and font coverage',
                'Repeatable frame hashes and exact cold process cleanup across three launches']
            result['limits'] = ['Offscreen local rendering in QEMU. Browser UI, secure network loading and hardware qualification remain open.']
        (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    except Exception as error:
        if guest is not None:
            try:
                guest.serial('procinfo')
                guest.serial('meminfo')
                time.sleep(.3)
                (out / 'registers.txt').write_text(guest.hmp('info registers'))
            except (OSError, RuntimeError, AssertionError):
                pass
        (out / 'result.json').write_text(json.dumps({'status': 'FAIL', 'error': str(error), 'runs': runs}, indent=2) + '\n')
        raise
    finally:
        if guest is not None:
            guest.close()


if __name__ == '__main__':
    main()
