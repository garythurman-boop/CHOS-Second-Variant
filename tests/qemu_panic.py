#!/usr/bin/env python3
"""Terminal panic fault injection in disposable QEMU VMs, including nested aborts.

The debugger inserts BRK at the already-finished bootstrap entry and redirects
PC there. This deliberately changes guest memory/registers; it is not input or
Fusion acceptance. The copied ISO/kernel and on-disk images remain untouched.
"""
import argparse
import json
import pathlib
import re
import socket
import struct
import time

from qemu_platform import Guest, ROOT, wait


class Debugger:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(3)
        self.socket.connect(str(path))
        self.command('qSupported')

    def command(self, command):
        payload = command.encode()
        self.socket.sendall(b'$' + payload + b'#' + f'{sum(payload) % 256:02x}'.encode())
        while True:
            char = self.socket.recv(1)
            if not char:
                raise RuntimeError('Debugger disconnected')
            if char == b'$':
                break
        result = bytearray()
        while True:
            char = self.socket.recv(1)
            if not char:
                raise RuntimeError('Debugger disconnected')
            if char == b'#':
                break
            result.extend(char)
        checksum = bytearray()
        while len(checksum) < 2:
            char = self.socket.recv(2 - len(checksum))
            if not char:
                raise RuntimeError('Debugger disconnected')
            checksum.extend(char)
        assert checksum == f'{sum(result) % 256:02x}'.encode(), 'Debugger checksum mismatch'
        self.socket.sendall(b'+')
        return result.decode()

    def write(self, address, data):
        assert self.command(f'M{address:x},{len(data):x}:' + data.hex()) == 'OK'

    def register(self, number, data):
        assert self.command(f'P{number:x}=' + data.hex()) == 'OK'

    def trigger(self, address, immediate):
        self.write(address, struct.pack('<I', 0xd4200000 | immediate << 5))
        # AArch64 GDB core register numbers: PC 32, CPSR/PSTATE 33. Keep EL1h
        # and mask every asynchronous exception; BRK remains synchronous.
        self.register(33, struct.pack('<I', 0x3c5))
        self.register(31, struct.pack('<Q', 1))  # Deliberately unusable interrupted SP.
        self.register(32, struct.pack('<Q', address))


def check(args, out, no_uart):
    out.mkdir(parents=True)
    args.gdb_socket = out / 'debug.sock'
    guest = None
    debugger = None
    try:
        guest = Guest(args, out, out / 'unused-disposable.img')
        wait(lambda: '[OK] Initial CHOS GUI frame rendered.' in guest.text(), 45, 'desktop')
        guest.qmp('stop')
        debugger = Debugger(args.gdb_socket)
        physical_base = int(re.search(r'Kernel physical base: (0x[0-9a-f]+)', guest.text())[1], 16)
        delta = physical_base - guest.symbols['__kernel_start']
        entry = guest.symbols['_start'] + delta
        # Exercise a fault while the ordinary console lock is already held.
        debugger.write(guest.symbols['uart_console_lock'] + delta, struct.pack('<I', 1))
        if no_uart:
            debugger.write(guest.symbols['uart0_base'] + delta, bytes(8))
            debugger.write(guest.symbols['serial_ready'] + delta, bytes(4))
        serial_before = guest.text()
        before = guest.screen('before', full=True)
        debugger.trigger(entry, 0x735)
        guest.qmp('cont')
        wait(lambda: guest.symbol('chos_panic_report_complete') == 1, 5, 'panic report completion')
        record = guest.block('chos_first_panic', size=56)
        esr, elr, far, spsr, kind, image, sctlr = struct.unpack('<7Q', record)
        assert esr >> 26 == 0x3c and (esr & 0xffff) == 0x735, hex(esr)
        assert elr == entry and image == physical_base and kind == 0
        assert not (sctlr & 1), 'This regression must exercise MMU-off panic output'
        first = guest.screen('first-panic', full=True)
        assert first != before, 'Panic must replace the desktop with a visible fault report'
        if no_uart:
            assert guest.text() == serial_before, 'Absent UART must not be accessed'
        else:
            assert guest.text().count('*** CHOS KERNEL PANIC ***') == 1
            assert 'BRK instruction from AArch64' in guest.text()
            assert 'Reset the VM to continue.' in guest.text()
        serial_first = guest.text()
        guest.qmp('stop')
        # A second real synchronous exception must neither overwrite the first
        # record nor recursively clear/render/log another panic.
        debugger.trigger(entry + 4, 0x736)
        guest.qmp('cont')
        time.sleep(1)
        assert guest.block('chos_first_panic', size=56) == record
        assert guest.screen('nested-panic', full=True) == first
        assert guest.text() == serial_first
        (out / 'result.json').write_text(json.dumps({
            'status': 'PASS', 'no_uart': no_uart, 'mmu_off': True,
            'first_esr': hex(esr), 'first_elr': hex(elr), 'sctlr': hex(sctlr),
            'checks': ['real BRK while UART lock held and interrupted SP unusable', 'visible terminal report',
                       'first-fault record preserved after nested BRK', 'no flicker or repeated output',
                       'unattached UART bypass' if no_uart else 'lock-free serial panic output']
        }, indent=2) + '\n')
    finally:
        if debugger:
            debugger.socket.close()
        if guest:
            guest.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--firmware', type=pathlib.Path, required=True)
    parser.add_argument('--qemu-data', type=pathlib.Path)
    parser.add_argument('--iso', type=pathlib.Path, default=ROOT / 'build/chos-0.30.0.iso')
    parser.add_argument('--uefi-kernel', type=pathlib.Path, default=ROOT / 'build/uefi-kernel/chos.elf')
    parser.set_defaults(mode='uefi', memory='2G', no_usb=True, pci_virtio=False,
                        trace=False, rtc_base=None, hub_depth=0, empty_usb=False)
    args = parser.parse_args()
    args.out = args.out.resolve()
    if args.out.exists():
        parser.error('Use a fresh output directory to preserve evidence')
    check(args, args.out / 'serial', False)
    check(args, args.out / 'no-uart', True)
    print('PASS: MMU-off terminal panic, held lock, absent UART and nested synchronous exception')


if __name__ == '__main__':
    main()
