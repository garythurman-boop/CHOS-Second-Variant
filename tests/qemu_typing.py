#!/usr/bin/env python3
"""Current live typing acceptance uses actual isolated application processes.

The former fixture read standby kernel singleton fields; those fields no longer
represent the Console/browser opened by the scheduled desktop. Preserve all
checks in the owner-aware runner, including repeat, visible caret pixels, focus,
USB unplug/replug and text persistence. Kernel recovery widgets retain host GUI
coverage independently.
"""
import pathlib,runpy
runpy.run_path(str(pathlib.Path(__file__).with_name('qemu_native_typing.py')),run_name='__main__')
