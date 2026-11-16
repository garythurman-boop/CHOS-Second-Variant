# CHOS libwebp memory profile

Build original static decoder/encoder, SharpYUV and demux libraries through
upstream CMake, retaining genuine compiler, NEON and Threads probes. Native
owned executables use non-PIC static code generation. Command-line/mux/graphics
utilities are omitted. No original vendor file is patched.

See [source inventory](../../docs/dependencies/native-webp.json),
[qualified native decoding scope](../../docs/NATIVE_WEBP.md) and
[the build driver](../../tools/build_native_webp.py). The native encoder is
build-only; selected actual guest tests qualify decoder/demux/animation APIs.
The separately built host encoder generates the fixture bytes.
