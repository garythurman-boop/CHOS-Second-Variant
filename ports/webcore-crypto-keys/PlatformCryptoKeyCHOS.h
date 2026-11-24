/* CHOS WebCore platform key containers, 2026-11-20.
 * The opaque object owns an actual private MbedTLS pk_context. Public/private
 * key identity and curve are validated by KeyBackend before construction. */
#pragma once
#include "KeyBackend.h"
#include <memory>

namespace WebCore {
struct CHOSPlatformKeyDeleter {
    void operator()(chos_webcore_key* key) const { chos_webcore_key_destroy(key); }
};
using PlatformECKeyContainer = std::unique_ptr<chos_webcore_key,CHOSPlatformKeyDeleter>;
}

/* The original RSA interface declares these platform names outside WebCore. */
using PlatformRSAKey = chos_webcore_key*;
using PlatformRSAKeyContainer = std::unique_ptr<chos_webcore_key,WebCore::CHOSPlatformKeyDeleter>;
