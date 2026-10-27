# Native static application startup and termination

The dense ARM64 native ELF profile now executes real preinit/init arrays before
main and supplies C exit/atexit, Itanium C++ process-destructor registration and
selective finalization. This closes the global-constructor/process-cleanup gap
for this bounded static profile. It is a browser-runtime prerequisite, not a
complete C++ ABI/POSIX implementation, ICU build or revised Stage3 completion.
Binary publication remains held; version0.50.0 remains reserved for completion.

## Startup and linkage

The shared user CRT preserves argc/argv while calling its initialization hook.
Dense applications explicitly link `userland/libc/static_startup.c` through
`NATIVE_STATIC_START` and use `userland/app.ld`. That hook registers fini-array
cleanup first, executes preinit arrays, then init arrays sorted by numeric
priority. Array pointers live in a file-backed RO segment; code remains RX and
mutable state RW/XN. Real attempted array writes fault only the user process.
Normal arguments survive constructors, including constructors that create and
join native pthreads. C++ global vectors and nontrivial function-local statics
now both construct and register their real destructors.

Raw CHCX and sparse-native/TLS link scripts reject constructor arrays explicitly.
Their CRT uses real local weak return functions when the dense hooks are absent;
there is no undefined-weak AArch64 call/address behavior. Unsupported legacy
.ctors/.dtors lists are rejected. PT_TLS, a dynamic loader, exception unwinding
and general DSO unloading are not supplied. Dense image2MiB, process4MiB, thread,
stack and mapping quotas remain in force. No kernel or driver contract is
expanded by this user runtime.

## Cleanup contract

A fixed128-entry process registry stores correctly typed C callbacks or C++
callbacks with object and opaque DSO-token arguments. Registration is serialized;
finalizers run in reverse registration order under a recursive finalization lock.
Each entry is deactivated before its callback. User code executes outside the
table lock, so callbacks can register more cleanup or selectively/reentrantly
finalize another token. Repeated finalization cannot call an entry twice. This
implements resident static-program token matching, not shared-library unloading.

Ordinary C atexit reports registration failure with nonzero. Valid C++ registration
fails the process on exhaustion/error: Clang does not check __cxa_atexit's return,
so returning an ignored error would silently omit destruction. Process finalization
has a shared512-callback budget and16-level recursion bound. Lock/state failures,
recursive process exit, and pthread_exit from process-finalizer code abort with
status134. Pure/deleted-virtual ABI failures terminate too. Callback count is
bounded; callback bodies remain ordinary preemptible application code and can
wait on application-owned resources.

Normal main return and exit run current-thread C++ TLS cleanup, then process
callbacks. The initially registered fini-array callback runs the array in reverse
priority order after ordinary statics. It can itself register additional atexit
cleanup, which executes before the registry closes. _Exit bypasses callbacks and
arrays. Process faults and abort also rely on existing kernel cancellation/reaping.
There is no stream-flush claim: native FILE/stdio streams are not implemented.

The C runtime counts its main thread and successfully created pthreads. Creation
reserves its count before the kernel can dispatch a worker and rolls it back on
failure. Each exiting thread accounts once, including recursive exit from key
cleanup. The last pthread performs process cleanup with status0, retaining its
compiler TLS until global destructors finish. Other pthread exits still preserve
siblings and join results. Raw kernel workers are a separate low-level API;
mixing unregistered raw workers into this C-runtime lifecycle is not qualified.
Concurrent/recursive process exit is not a supported synchronization mechanism.

Thread cleanup also drains C++ TLS destructors registered by ordinary key
callbacks before releasing compiler TLS storage. A per-TCB256-callback lifetime
budget spans cleanup passes, with a16-level recursion bound, so reentrancy cannot
reset the limit. This fixes real missing destruction and leaked callback nodes
when a key destructor constructs a previously unused C++ TLS object.

## Evidence and retained investigations

See [exact results](validation/stage3-static-crt.json). The final direct and UEFI
fixtures each execute18 cases with stable warm physical pages and no retained
futex waiters. They check priority order, constructor-created workers, argc/argv,
global/local C++ vectors, DSO matching, concurrent finalization, TLS ordering,
post-key TLS destruction, normal/explicit/last-thread/worker/early-constructor exit,
_Exit bypass, quotas, callback rearm/recursion, forbidden teardown exits, actual RO
array protection and clean relaunch. Both global and thread-owned allocations
are exercised through the real native allocator. Host normal/ASan/UBSan registry
tests include50 cycles with four concurrent registrars/finalizers. Negative link
checks require legacy profiles to reject actual emitted constructor arrays.

Evidence under `/workspace/chos-evidence` retains these investigations:

- `startup-direct1`: NOT PASS. Clang promoted a trivial constructor into initialized
  data, so its expected dynamic ordering was not a valid fixture. Adding an
  observable operation keeps the real constructor entry in its priority array.
- `startup-postkey-before` and `startup-final-direct`: NOT PASS. An initial TLS
  fixture put two nontrivial TLS variables in one translation unit. Clang grouped
  their initialization; an unrelated default object failed its value assertion.
  These runs do not prove the post-key ordering defect.
- `startup-postkey-order-baseline` and `startup-postkey-order-before`: controlled
  negative evidence. Separate translation units isolate the late-created object;
  an otherwise matched pthread variant omitting the post-key drain fails the
  destructor-count check. The production source was not rolled back.
- `startup-postkey-order-fixed`: PASS after the post-key drain. The final
  `static-crt-final-direct/uefi` additionally validate atexit registration from
  a fini-array function. Earlier passing profiles are intermediate evidence.

Completed firmware-variable snapshots were losslessly compressed to make room
for further guests. Logs, disks and read-only boot snapshots remain in place;
`completed-runtime-vars-archives.json` records archive paths, original SHA256,
modes and restoration instructions. No state was shared between mutable disks.
New Fusion/Pi/laptop execution remains NOT RUN. ICU, C++ standard threading,
full POSIX/events/large-image memory support, modern browser integration,
production trust policy and real Pi5 wireless remain required.
