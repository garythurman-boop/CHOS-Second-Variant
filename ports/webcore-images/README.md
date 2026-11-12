# Native WebCore image component profile

This profile selects original concrete PNG/JPEG decoders, in-process shared
buffers and Cairo image backing. It does not provide WebCore's complete format
factory, shared-memory IPC, page layout or the modern browser.

The patch applies to a fresh source copy. It omits unsupported entry-point
**definitions**, so callers cannot accidentally obtain a successful fake shared
memory operation or a partial full-format factory. Original in-process buffer,
PNG/JPEG decoder and surface ownership implementations remain in use. Building
without `CHOS_WEBCORE_IMAGE_COMPONENT` retains the original definitions.

`HAVE_MMAP=1` selects the genuine native WTF MmapSpan-backed mapping layout.
Native private mappings and WTF FileHandle mapping are separately implemented;
this flag does not claim shared memory or arbitrary POSIX mmap compatibility.
Every component/consumer translation unit must use the same profile and headers.
The source list records the exact selected definitions and original units.

The upstream files retain their original notices and LGPL/BSD terms. CHOS
changes carry dated notices. Compilation and linking alone are not execution or
full WebCore ABI/conformance evidence; qualify the actual native consumer.
