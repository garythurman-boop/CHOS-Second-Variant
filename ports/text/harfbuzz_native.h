#ifndef CHOS_HARFBUZZ_NATIVE_H
#define CHOS_HARFBUZZ_NATIVE_H
#ifndef __CHOS__
#error This capability override is only for the native CHOS target
#endif
/* CHOS mmap/mprotect support exists, but HarfBuzz's optional POSIX mapped-file
 * reader additionally requires stat/fstat, which this SDK does not expose.
 * Select its original fread snapshot loader. Keep general memory/thread probes
 * truthful; do not supply a successful fake stat or disable synchronization.
 */
#undef HAVE_MMAP
#define HB_NO_MMAP
#ifdef HB_NO_MT
#error Native HarfBuzz must retain real thread synchronization
#endif
#endif
