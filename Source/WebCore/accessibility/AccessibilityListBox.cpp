/*
 * Copyright (C) 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "AccessibilityListBox.h"

#include "AXObjectCache.h"
#include "AccessibilityListBoxOption.h"
#include "HTMLNames.h"
#include "HTMLSelectElement.h"
#include "HitTestResult.h"
#include "RenderListBox.h"
#include "RenderObject.h"
#include <wtf/Scope.h>

namespace WebCore {

using namespace HTMLNames;

AccessibilityListBox::AccessibilityListBox(AXID axID, RenderObject& renderer, AXObjectCache& cache)
    : AccessibilityRenderObject(axID, renderer, cache)
{
}

AccessibilityListBox::~AccessibilityListBox() = default;

Ref<AccessibilityListBox> AccessibilityListBox::create(AXID axID, RenderObject& renderer, AXObjectCache& cache)
{
    return adoptRef(*new AccessibilityListBox(axID, renderer, cache));
}

void AccessibilityListBox::addChildren()
{
    m_childrenInitialized = true;
    auto clearDirtySubtree = makeScopeExit([&] {
        m_subtreeDirty = false;
    });

    RefPtr selectElement = dynamicDowncast<HTMLSelectElement>(node());
    if (!selectElement)
        return;

    for (const auto& listItem : selectElement->listItems())
        addChild(listBoxOptionAccessibilityObject(listItem.get()), DescendIfIgnored::No);

#ifndef NDEBUG
    verifyChildrenIndexInParent();
#endif
}

void AccessibilityListBox::setSelectedChildren(const AccessibilityChildrenVector& children)
{
    if (!canSetSelectedChildren())
        return;

    // Unselect any selected option.
    for (const auto& child : unignoredChildren()) {
        if (child->isSelected())
            child->setSelected(false);
    }

    for (const auto& object : children) {
        if (object->isListBoxOption())
            object->setSelected(true);
    }
}

AXCoreObject::AccessibilityChildrenVector AccessibilityListBox::visibleChildren()
{
    ASSERT(!m_renderer || is<RenderListBox>(m_renderer.get()));
    auto* listBox = dynamicDowncast<RenderListBox>(m_renderer.get());
    if (!listBox)
        return { };

    if (!childrenInitialized())
        addChildren();
    
    const auto& children = const_cast<AccessibilityListBox*>(this)->unignoredChildren();
    AccessibilityChildrenVector result;
    size_t size = children.size();
    for (size_t i = 0; i < size; i++) {
        if (listBox->listIndexIsVisible(i))
            result.append(children[i]);
    }
    return result;
}

AccessibilityObject* AccessibilityListBox::listBoxOptionAccessibilityObject(HTMLElement* element) const
{
    // FIXME: Why does AccessibilityMenuListPopup::menuListOptionAccessibilityObject check inRenderedDocument, but this does not?
    RefPtr document = this->document();
    if (CheckedPtr cache = document ? document->axObjectCache() : nullptr)
        return cache->getOrCreate(element);
    return nullptr;
}

AccessibilityObject* AccessibilityListBox::elementAccessibilityHitTest(const IntPoint& point) const
{
    // the internal HTMLSelectElement methods for returning a listbox option at a point
    // ignore optgroup elements.
    if (!m_renderer)
        return nullptr;
    
    if (!m_renderer->node())
        return nullptr;
    
    LayoutRect parentRect = boundingBoxRect();
    
    RefPtr<AccessibilityObject> listBoxOption;
    const auto& children = const_cast<AccessibilityListBox*>(this)->unignoredChildren();
    unsigned length = children.size();
    for (unsigned i = 0; i < length; ++i) {
        LayoutRect rect = downcast<RenderListBox>(*m_renderer).itemBoundingBoxRect(parentRect.location(), i);
        // The cast to HTMLElement below is safe because the only other possible listItem type
        // would be a WMLElement, but WML builds don't use accessibility features at all.
        if (rect.contains(point)) {
            listBoxOption = downcast<AccessibilityObject>(children[i].get());
            break;
        }
    }
    
    if (listBoxOption && !listBoxOption->isIgnored())
        return listBoxOption.get();
    
    return axObjectCache()->getOrCreate(renderer());
}

} // namespace WebCore
