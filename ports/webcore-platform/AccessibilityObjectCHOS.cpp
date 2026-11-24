/* CHOS in-process accessibility identity policy, 2026-11-22.
 * Generic WebCore remains responsible for DOM/ARIA inclusion. The opaque
 * RefCounted wrapper is a retained local identity, never a native AT handle. */
#include "config.h"
#include "AccessibilityObject.h"

namespace WebCore {

void AccessibilityObject::detachPlatformWrapper(AccessibilityDetachmentType)
{
    // There is no external native object to disconnect. Release the actual
    // owned identity; AXCoreObject's final clear is then idempotent.
    setWrapper(nullptr);
}

bool AccessibilityObject::accessibilityIgnoreAttachment() const
{
    // CHOS has no separately embedded native attachment view whose subtree
    // would replace the generic DOM accessibility representation.
    return false;
}

AccessibilityObjectInclusion AccessibilityObject::accessibilityPlatformIncludesObject() const
{
    return AccessibilityObjectInclusion::DefaultBehavior;
}

} // namespace WebCore
