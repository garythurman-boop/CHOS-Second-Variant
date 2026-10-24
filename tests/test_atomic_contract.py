#!/usr/bin/env python3
"""Compile ARM64 probes: Device-typed RAM must never require exclusive access."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
for directory in ('kernel', 'include/chos', 'boot'):
    for path in (root / directory).rglob('*'):
        if path.suffix not in ('.c', '.h', '.S') or path.name == 'atomic.h':
            continue
        assert not re.search(r'__atomic_|__sync_|\b(?:ldaxr|ldxr|stxr|stlxr)\b', path.read_text()), path

probes = '''
uint32_t probe_load32(volatile uint32_t *p) { return chos_atomic_load_u32(p); }
void probe_store32(volatile uint32_t *p, uint32_t v) { chos_atomic_store_u32(p, v); }
uint32_t probe_exchange(volatile uint32_t *p, uint32_t v) { return chos_atomic_exchange_u32(p, v); }
uint64_t probe_load64(volatile uint64_t *p) { return chos_atomic_load_u64(p); }
uint64_t probe_add(volatile uint64_t *p, uint64_t v) { return chos_atomic_fetch_add_u64(p, v); }
'''
with tempfile.TemporaryDirectory() as folder:
    source = Path(folder) / 'probe.c'
    assembly = Path(folder) / 'probe.s'
    for mode in (0, 1, None):
        prefix = '#include "chos/atomic.h"\n'
        if mode is not None:
            prefix = '#define CHOS_ATOMIC_TEST_PLATFORM\n' + prefix + f'''
int chos_atomic_normal_memory(void) {{ return {mode}; }}
uint64_t chos_atomic_irq_save(void) {{
    uint64_t state;
    __asm__ volatile("mrs %0, daif\\n\\tmsr daifset, #2\\n\\tisb" : "=r"(state) :: "memory");
    return state;
}}
void chos_atomic_irq_restore(uint64_t state) {{
    __asm__ volatile("msr daif, %0\\n\\tisb" :: "r"(state) : "memory");
}}
void chos_atomic_barrier(void) {{ __asm__ volatile("dmb sy" ::: "memory"); }}
'''
        source.write_text(prefix + probes)
        subprocess.run([os.environ.get('CHOS_CLANG', 'clang'), '--target=aarch64-none-elf',
                        '-mcpu=cortex-a72', '-ffreestanding', '-O2', '-S', '-I' + str(root / 'include'),
                        str(source), '-o', str(assembly)], check=True)
        output = assembly.read_text().lower()
        exclusive = re.search(r'\b(?:ldaxr|ldxr|stxr|stlxr)\b', output)
        if mode == 0:
            assert not exclusive and not re.search(r'\b(?:ldar|stlr)\b', output), output
            assert 'daifset' in output and 'dmb' in output
        else:
            assert exclusive, 'Normal-RAM path must still use hardware atomic RMW'
        if mode is None:
            assert 'sctlr_el1' in output and re.search(r'\btb(?:nz|z)\s+[wx]\d+, #0,', output), output
print('ARM64 atomic contract: no bypasses; MMU-off code has no exclusive/acquire/release accesses; Normal-RAM path retained')
