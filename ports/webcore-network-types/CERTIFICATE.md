# CHOS certificate value adapter

`CertificateInfo.h` / `CertificateInfoCHOS.cpp` own DER and caller-reported
verification metadata. `CertificateBackend` delegates exact DER parsing and
summary extraction to maintained original MbedTLS. `CertificateParser.cmake`
exports the actual `CHOS::CertificateParser` static target. The generated private
namespace avoids symbol collisions with other MbedTLS configurations.

Build and selected runtime evidence are documented in
[native certificate metadata](../../docs/NATIVE_WEBCORE_CERTIFICATES.md).
The default object represents unknown verification. Parsing, summaries and the
conservative SHA-1 inspection predicate do not authenticate a connection.
