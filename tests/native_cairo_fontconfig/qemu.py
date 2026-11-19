#!/usr/bin/env python3
"""Qualify real Cairo/Fontconfig integration in disposable isolated CHOS guests."""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
from qemu_platform import Guest, wait


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--mode', choices=['direct', 'uefi'], default='direct')
    parser.add_argument('--application', type=pathlib.Path, required=True)
    parser.add_argument('--image-root', type=pathlib.Path, required=True)
    parser.add_argument('--runs', type=int, choices=[1, 3], default=3)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    guest = None
    phase = 'preflight'
    runs = []
    started = time.monotonic()

    def save(report):
        report.update(seconds=time.monotonic() - started, runs=runs)
        (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')

    try:
        manifest_path = ROOT / 'docs/dependencies/text-fixtures.json'
        manifest = json.loads(manifest_path.read_text())
        fixtures = ROOT / 'third_party/text-fixtures'
        entries = list(fixtures.rglob('*'))
        assert not fixtures.is_symlink() and not any(path.is_symlink() for path in entries)
        assert {str(path.relative_to(fixtures)) for path in entries if path.is_file()} == set(manifest['files'])
        for name, expected in manifest['files'].items():
            assert sha(fixtures / name) == expected, name
        font_files = []
        for font in manifest['fonts']:
            source = fixtures / font['path']
            assert source.stat().st_size == font['bytes'] and sha(source) == font['sha256']
            assert manifest['files'][font['path']] == font['sha256']
            assert font['guest_path'] == '/FONTS/F' + str(font['index']) + '.TTF'
            font_files.append({'guest_path': font['guest_path'], 'sha256': font['sha256'],
                               'bytes': font['bytes']})
        (out / 'fonts.json').write_text(json.dumps({
            'manifest_sha256': sha(manifest_path), 'files': font_files}, indent=2) + '\n')

        application = out / 'application.elf'
        shutil.copyfile(args.application.resolve(), application)
        application_sha = sha(application)
        assert not subprocess.check_output(['llvm-nm', '-u', str(application)], text=True).strip()
        elf_header = subprocess.check_output(['llvm-readelf', '-h', str(application)], text=True)
        assert 'AArch64' in elf_header
        program_headers = subprocess.check_output(['llvm-readelf', '-lW', str(application)], text=True)
        assert not re.search(r'^\s+TLS\s', program_headers, re.M)
        assert not re.search(r'^\s+LOAD[^\n]*\bRWE\b', program_headers, re.M)
        disk = out / 'disposable.img'
        mkchfs = ['python3', str(ROOT / 'tools/mkchfs.py')]
        subprocess.run([*mkchfs, 'create', str(disk), '--size-mib', '16'], check=True)
        subprocess.run([*mkchfs, 'put', str(disk), '/CAIROFC.ELF', str(application)], check=True)
        for font in manifest['fonts']:
            subprocess.run([*mkchfs, 'put', str(disk), font['guest_path'],
                            str(fixtures / font['path'])], check=True)
        images = args.image_root.resolve()
        guest_args = argparse.Namespace(
            mode=args.mode, kernel=images / 'chos.elf', uefi_kernel=images / 'uefi-kernel.elf',
            iso=images / 'chos.iso', memory='2G' if args.mode == 'uefi' else '512M',
            qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),
            rtc_base=None, trace=False,
            firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),
            no_usb=False, usb_pci_bus=0, xhci_model='qemu-xhci', pci_virtio=True,
            net_model='e1000e', storage_error=False, nvme=False, no_rng=False,
            hub_depth=0, empty_usb=False, tablet=True)
        phase = 'guest-startup'
        # Guest snapshots the selected kernel/ISO, verifies the UEFI ISO kernel,
        # and records hashes in artifacts.json before launching its own VM.
        guest = Guest(guest_args, out, disk)
        markers = [
            '[ OK ] actual Cairo Fontconfig pattern and explicit FreeType pixels,metrics and lifetime passed',
            '[ OK ] actual Cairo Fontconfig workers and cleanup passed']
        failures = ['[FAIL]', '[USER FAULT]', '[FATAL]']

        def cold():
            deadline = time.monotonic() + 15
            header = 'CHOS process table (foreground and scheduled EL0 processes)'
            end = 'Loader sources: packaged ROMFS and checked CHFS executables.'
            while True:
                count = guest.text().count(header)
                guest.serial('procinfo')
                wait(lambda: guest.text().count(header) > count and
                     end in guest.text().rsplit(header, 1)[-1], 4, 'complete process status')
                section = guest.text().rsplit(header, 1)[-1]
                if re.search(r'Active: 0;.*pending cleanup: 0', section):
                    break
                assert time.monotonic() < deadline, 'Process cleanup failed'
                time.sleep(.03)
            page_report = r'Free pages: (\d+) \(approximately \d+ MiB\)\r?\n'
            count = len(re.findall(page_report, guest.text()))
            guest.serial('meminfo')
            wait(lambda: len(re.findall(page_report, guest.text())) > count,
                 4, 'complete physical page report')
            assert guest.symbol('committed_frame_limits') == 0
            assert guest.symbol('engine_owner_slot') == 0xffffffff
            assert guest.symbol('futex_queued_waiters') == 0
            return int(re.findall(page_report, guest.text())[-1]), section

        wait(lambda: 'chos> ' in guest.text(), 60, 'scheduled runtime')
        if args.mode == 'uefi':
            assert 'UEFI CPU isolation active' in guest.text()
        wait(lambda: guest.symbol('splash_active') == 0, 10, 'splash')
        baseline, _ = cold()
        phase = 'native-fixture'
        for cycle in range(args.runs):
            offset = len(guest.text())
            cycle_started = time.monotonic()
            guest.serial('exec /CAIROFC.ELF')
            heap_report = r'\[CAIRO FC HEAP\] allocations=(\d+) mapped=(\d+)\r?\n'
            wait(lambda: (markers[-1] in guest.text()[offset:] and
                          re.search(heap_report, guest.text()[offset:])) or
                 any(failure in guest.text()[offset:] for failure in failures),
                 90, 'Cairo Fontconfig pixels, metrics, lifetime and workers')
            text = guest.text()[offset:]
            assert not any(failure in text for failure in failures), text
            assert all(text.count(marker) == 1 for marker in markers), text
            heap = re.findall(heap_report, text)
            rounds = re.findall(r'\[CAIRO FC ROUND\] round=(\d+) allocations=(\d+) mapped=(\d+)\r?\n', text)
            assert len(heap) == 1 and len(rounds) == 3, (heap, rounds)
            assert [r[0] for r in rounds] == ['0', '1', '2'], rounds
            assert all(r[1:] == heap[0] for r in rounds), (heap, rounds)
            pages, section = cold()
            assert pages == baseline, (baseline, pages)
            pid = int(re.search(r'pid=(\d+) path=/CAIROFC.ELF', guest.text()[offset:])[1])
            assert re.search(r'\bpid=' + str(pid) + r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b', section), section
            statistics = [dict(zip(('comparisons', 'pixels', 'glyphs'), map(int, groups)))
                          for groups in re.findall(r'\[CAIRO FC\] comparisons=(\d+) pixels=(\d+) glyphs=(\d+)', text)]
            runs.append({'cycle': cycle, 'pid': pid, 'seconds': time.monotonic() - cycle_started,
                         'cold_pages_restored': pages, 'fixture_statistics': statistics,
                         'heap_before_main_return': {'allocations': int(heap[0][0]), 'mapped_bytes': int(heap[0][1])},
                         'inside_process_rounds': [{'round':int(r[0]),'allocations':int(r[1]),'mapped_bytes':int(r[2])} for r in rounds]})
        assert sha(application) == application_sha, 'Application changed during qualification'
        assert sha(manifest_path) == json.loads((out / 'fonts.json').read_text())['manifest_sha256']
        assert not any(failure in guest.text() for failure in failures)
        save({'status': 'PASS', 'mode': args.mode, 'application_sha256': application_sha,
              'harness_sha256': sha(pathlib.Path(__file__).resolve()),
              'font_manifest_sha256': sha(manifest_path), 'cold_free_pages': baseline,
              'boot_artifacts': json.loads((out / 'artifacts.json').read_text()),
              'checks': ['Real Cairo Fontconfig patterns compared with explicit FreeType pixels and metrics',
                         'Font/surface lifetime, native file descriptor headroom, actual pthread workers and stable retained global/TLS heap across three inside-process teardown rounds',
                         'Normal exit 0, exact cold physical pages, released engine/frame ownership and no futex waiters'],
              'limits': ['Component-only Cairo Fontconfig integration; no owned GUI window, WebCore or browser acceptance',
                         'Selected QEMU fixtures; Fusion, Pi 5 and physical laptop execution not run; Stage 3 incomplete']})
    except Exception as error:
        if guest is not None:
            try:
                guest.serial('procinfo')
                guest.serial('taskinfo')
                guest.serial('meminfo')
                time.sleep(.2)
                guest.screen('failure')
                (out / 'registers.txt').write_text(guest.hmp('info registers'))
            except Exception:
                pass
        save({'status': 'FAIL', 'qualified': False, 'phase': phase, 'error': str(error)})
        raise
    finally:
        if guest is not None:
            guest.close()


if __name__ == '__main__':
    main()
