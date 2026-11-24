#include "config.h"
#include "PublicSuffixStore.h"
#include <libpsl.h>
#include <wtf/text/CString.h>

namespace WebCore {

bool PublicSuffixStore::platformIsPublicSuffix(StringView domain) const
{
    if (domain.isEmpty() || domain.contains(u'\0'))
        return false;
    auto utf8 = domain.toStringWithoutCopying().convertToLowercaseWithoutLocale().utf8();
    return psl_is_public_suffix2(psl_builtin(), utf8.data(), PSL_TYPE_ANY | PSL_TYPE_NO_STAR_RULE);
}

String PublicSuffixStore::platformTopPrivatelyControlledDomain(StringView domain) const
{
    if (domain.contains(u'\0'))
        return { };
    // Cookie domains may start with a dot. WebCore already lowercases ASCII
    // hosts and handles localhost and IP addresses before calling this method.
    while (domain.startsWith('.'))
        domain = domain.substring(1);
    if (domain.isEmpty())
        return { };
    auto utf8 = domain.utf8();
    auto* result = psl_registrable_domain(psl_builtin(), utf8.data());
    return result ? String::fromUTF8(result) : String { };
}

} // namespace WebCore
