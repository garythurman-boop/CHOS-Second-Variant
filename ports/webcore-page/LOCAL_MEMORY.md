# Native WebCore local buffer ownership

The original WebCore `SharedMemory.cpp` and complete `SharedBuffer.cpp` now
compile, link and execute with a genuine CHOS in-process ReadWrite backing.
This is a selected platform component; full WebCore has not compiled or painted
a page. Cross-process IPC remains unavailable.

`SharedMemoryCHOSBacking` holds zero-initialized native engine-heap bytes under
the existing thread-safe reference count. Handle copies and ReadWrite maps retain
the same allocation. Mapped aliases observe the actual shared bytes, and a
`createSharedBuffer` view keeps those bytes alive after the original object,
handles and aliases are destroyed. Native heap limits and allocation failures
remain real; no kernel mapping, IPC handle or OS capability is invented.

CHOS currently cannot provide a distinct read-only alias of writable backing.
ReadOnly handle/map requests return failure, and CopyOnWrite requests fail.
The adapter does not silently return writable bytes or substitute private copies.
The original non-Cocoa `createVMShare`/`createVMCopy` failures remain unchanged.
Empty, moved-from and mismatched-size handles fail without reading outside their
backing. The original constructors, segment providers and copy operations execute.

The original `SharedBuffer::toIPCData` path for buffers at least one page in size
requests a ReadOnly handle. That request fails here, and the original
`fromIPCData` rejects the resulting missing handle. This failure is tested and
is a real limit, not a successful IPC fallback. The original smaller inline-byte
round trip is tested separately. Future browser process isolation needs genuine
kernel-supported handle transfer and permission-enforced mapping semantics.

The standalone builder verifies the immutable SDK, published native WTF/bmalloc
kit and curated original image headers, plus the pinned Git blobs for the two
additional original SharedMemory files. It applies `shared-memory.patch` only
to a fresh copy and records sources, commands, SDK, object and executable hashes.
The same explicit patch and adapter are selected by the full-port configure
prototype. That prototype still requires remaining dependencies and consistent
generated WTF/JSC/PAL/WebCore configuration before any full engine claim.

The final selected SDK7 executable is 278,456 bytes, SHA-256
`8025beca11e0fabc98261d2d50ed2a70ccce341d86651502c512a842ad90131e`.
Direct and isolated UEFI QEMU each pass three process lifetimes with this exact
binary. Tests exercise zero fill and all-byte patterns from one byte through
1,048,593 bytes; retained views; shared aliases; copied and moved handles;
overflow, size and unsupported capability failures; complete original inline IPC;
the explicit large IPC failure; and three concurrent workers on disjoint ranges.
Three additional rounds inside each process return to zero native heap allocations
before `main` returns. Every process restores exact cold physical-page,
engine-owner, commit and futex state (110,116 direct / 494,486 UEFI pages).

Exact build/result records are in [the qualification report](qualification-local-memory.json).
Native component sanitizers, full engine security and new physical Fusion/Pi 5/laptop
execution have not run. Version remains 0.30.0 and Stage 3 is incomplete.

Build and run with the existing retained tool activation:

```sh
python3 ports/webcore-page/build_shared_memory.py \
  --out /workspace/chos-evidence/UNIQUE-LOCAL-MEMORY \
  --native-sdk /workspace/chos-evidence/webcore-link-20261112/memory/sdk7 \
  --engine-kit /workspace/chos-rendering-inputs-a89db07 \
  --webkit-source /workspace/chos-reference/webkit-2.54.2
python3 ports/webcore-page/tests/qemu_shared_memory.py \
  --out /workspace/chos-evidence/UNIQUE-LOCAL-MEMORY-DIRECT \
  --application /workspace/chos-evidence/UNIQUE-LOCAL-MEMORY/native_webcore_local_memory.elf \
  --image-root /workspace/chos-evidence/native-cairo-20261108/clean-cairo-images
```

Repeat the second command with `--mode uefi` and another output directory.
