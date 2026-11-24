/*
 * Copyright (C) 2023 Sony Interactive Entertainment Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Adapted for CHOS from the original PlayStation client-relay backend.
 * Local wrapper ownership and callbacks are real. A ChromeClient consumer must
 * provide any native assistive-technology bridge; this backend supplies none. */
#include "config.h"
#include "AXObjectCache.h"
#include "AXNotifications.h"
#include "AccessibilityObject.h"
#include "Chrome.h"
#include "ChromeClient.h"
#include "DocumentPage.h"
#include "DocumentView.h"
#include "FrameDestructionObserverInlines.h"
#include "HTMLSelectElement.h"
#include "LocalFrame.h"
#include "NodeDocument.h"
#include "Page.h"
#include "RenderView.h"

namespace WebCore {

void AXObjectCache::attachWrapper(AccessibilityObject& object)
{
    auto wrapper = adoptRef(*new AccessibilityObjectWrapper());
    object.setWrapper(wrapper.ptr());
}

void AXObjectCache::detachWrapper(AXCoreObject* object, AccessibilityDetachmentType type)
{
    if (object)
        object->detachWrapper(type);
}

static RefPtr<Page> pageForObject(Document* owner, const AccessibilityObject& object)
{
    RefPtr document = object.document();
    if (!owner || !document || document != owner || !document->view()
        || document->view()->layoutContext().layoutState() || document->childNeedsStyleRecalc())
        return nullptr;
    return document->page();
}

static AXCoreObject* selectedItem(AXCoreObject* object)
{
    if (!object || !object->isListBox())
        return object;
    auto* node = object->node();
    if (!is<HTMLSelectElement>(node))
        return object;
    auto& select = downcast<HTMLSelectElement>(*node);
    int index = select.activeSelectionStartListIndex();
    const auto& children = object->children();
    return index >= 0 && static_cast<unsigned>(index) < children.size() ? children[index].ptr() : object;
}

void AXObjectCache::postPlatformNotification(AccessibilityObject& object, AXNotification notification)
{
    if (!pageForObject(document(), object))
        return;
    Ref protectedObject = object;
    if (notification == AXNotification::SelectedChildrenChanged) {
        if (auto* selected = dynamicDowncast<AccessibilityObject>(selectedItem(&object)))
            protectedObject = *selected;
    } else if (notification == AXNotification::PressDidSucceed
        && (!object.isEnabled() || (object.isTextControl() && !object.canSetValueAttribute())))
        notification = AXNotification::PressDidFail;
    if (RefPtr page = pageForObject(document(), protectedObject.get()))
        page->chrome().client().postAccessibilityNotification(protectedObject.get(), notification);
}

void AXObjectCache::nodeTextChangePlatformNotification(AccessibilityObject* object,
    AXTextChange change, unsigned offset, const String& text)
{
    RefPtr protectedObject = object;
    if (protectedObject) {
        if (RefPtr page = pageForObject(document(), *protectedObject))
            page->chrome().client().postAccessibilityNodeTextChangeNotification(protectedObject.get(), change, offset, text);
    }
}

void AXObjectCache::frameLoadingEventPlatformNotification(RenderView* renderView, AXLoadingEvent event)
{
    if (!renderView || !document())
        return;
    RefPtr object = getOrCreate(*renderView);
    if (object) {
        if (RefPtr page = pageForObject(document(), *object))
            page->chrome().client().postAccessibilityFrameLoadingEventNotification(object.get(), event);
    }
}

void AXObjectCache::handleScrolledToAnchor(const Node& node)
{
    if (RefPtr object = AccessibilityObject::firstAccessibleObjectFromNode(&node))
        postPlatformNotification(*object, AXNotification::ScrolledToAnchor);
}

void AXObjectCache::platformHandleFocusedUIElementChanged(AccessibilityObject*, AccessibilityObject* focus)
{
    if (!focus)
        return;
    RefPtr page = focus->page();
    if (!page)
        return;
    if (RefPtr object = focusedObjectForPage(page.get()))
        postPlatformNotification(*object, AXNotification::FocusedUIElementChanged);
}

void AXObjectCache::platformPerformDeferredCacheUpdate()
{
    // Generic AXObjectCache has completed the deferred tree updates before
    // this hook. This identity/client-relay backend has no second platform
    // cache or native queue to drain.
}

} // namespace WebCore
