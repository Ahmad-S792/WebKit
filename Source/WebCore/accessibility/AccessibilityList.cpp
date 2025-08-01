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
#include "AccessibilityList.h"

#include "AXObjectCache.h"
#include "ContainerNodeInlines.h"
#include "HTMLElement.h"
#include "HTMLNames.h"
#include "PseudoElement.h"
#include "RenderElementInlines.h"
#include "RenderListItem.h"
#include "RenderStyleInlines.h"

namespace WebCore {
    
using namespace HTMLNames;

AccessibilityList::AccessibilityList(AXID axID, RenderObject& renderer, AXObjectCache& cache)
    : AccessibilityRenderObject(axID, renderer, cache)
{
}

AccessibilityList::AccessibilityList(AXID axID, Node& node, AXObjectCache& cache)
    : AccessibilityRenderObject(axID, node, cache)
{
}

AccessibilityList::~AccessibilityList() = default;

Ref<AccessibilityList> AccessibilityList::create(AXID axID, RenderObject& renderer, AXObjectCache& cache)
{
    return adoptRef(*new AccessibilityList(axID, renderer, cache));
}

Ref<AccessibilityList> AccessibilityList::create(AXID axID, Node& node, AXObjectCache& cache)
{
    return adoptRef(*new AccessibilityList(axID, node, cache));
}

bool AccessibilityList::computeIsIgnored() const
{
    return isIgnoredByDefault();
}
    
bool AccessibilityList::isUnorderedList() const
{
    // The ARIA spec says the "list" role is supposed to mimic a UL or OL tag.
    // Since it can't be both, it's probably OK to say that it's an un-ordered list.
    // On the Mac, there's no distinction to the client.
    if (ariaRoleAttribute() == AccessibilityRole::List)
        return true;

    auto elementName = this->elementName();
    return elementName == ElementName::HTML_menu || elementName == ElementName::HTML_ul;
}

bool AccessibilityList::isOrderedList() const
{
    // ARIA says a directory is like a static table of contents, which sounds like an ordered list.
    if (ariaRoleAttribute() == AccessibilityRole::Directory)
        return true;

    return elementName() == ElementName::HTML_ol;
}

bool AccessibilityList::isDescriptionList() const
{
    return elementName() == ElementName::HTML_dl;
}

bool AccessibilityList::childHasPseudoVisibleListItemMarkers(const Node* node)
{
    // Check if the list item has a pseudo-element that should be accessible (e.g. an image or text)
    auto* element = dynamicDowncast<Element>(node);
    RefPtr beforePseudo = element ? element->beforePseudoElement() : nullptr;
    if (!beforePseudo)
        return false;

    RefPtr axBeforePseudo = axObjectCache()->getOrCreate(beforePseudo->renderer());
    if (!axBeforePseudo)
        return false;
    
    if (!axBeforePseudo->isIgnored())
        return true;
    
    for (const auto& child : axBeforePseudo->unignoredChildren()) {
        if (!child->isIgnored())
            return true;
    }
    
    // Platforms which expose rendered text content through the parent element will treat
    // those renderers as "ignored" objects.
#if USE(ATSPI)
    String text = axBeforePseudo->textUnderElement();
    return !text.isEmpty() && !text.containsOnly<isASCIIWhitespace>();
#else
    return false;
#endif
}

AccessibilityRole AccessibilityList::determineAccessibilityRole()
{
    if (!m_childrenDirty && childrenInitialized())
        return determineAccessibilityRoleWithCleanChildren();

    m_ariaRole = determineAriaRoleAttribute();
    return isDescriptionList() ? AccessibilityRole::DescriptionList : AccessibilityRole::List;
}

AccessibilityRole AccessibilityList::determineAccessibilityRoleWithCleanChildren()
{
    ASSERT(!m_childrenDirty && childrenInitialized());
    m_ariaRole = determineAriaRoleAttribute();

    // Directory is mapped to list for now, but does not adhere to the same heuristics.
    if (ariaRoleAttribute() == AccessibilityRole::Directory)
        return AccessibilityRole::List;

    // Heuristic to determine if an ambiguous list is relevant to convey to the accessibility tree.
    //   1. If it's an ordered list or has role="list" defined, then it's a list.
    //      1a. Unless the list has no children, then it's not a list.
    //   2. If it is contained in <nav> or <el role="navigation">, it's a list.
    //   3. If it displays visible list markers, it's a list.
    //   4. If it does not display list markers, it's not a list.
    //   5. If it has one or zero listitem children, it's not a list.
    //   6. Otherwise it's a list.

    auto role = AccessibilityRole::List;

    // Temporarily set role so that we can query children (otherwise canHaveChildren returns false).
    SetForScope temporaryRole(m_role, role);

    unsigned listItemCount = 0;
    bool hasVisibleMarkers = false;

    const auto& children = unignoredChildren();
    // DescriptionLists are always semantically a description list, so do not apply heuristics.
    if (isDescriptionList() && children.size())
        return AccessibilityRole::DescriptionList;

    for (const auto& child : children) {
        RefPtr node = child->node();
        RefPtr axChild = dynamicDowncast<AccessibilityObject>(child.get());
        if (axChild && axChild->ariaRoleAttribute() == AccessibilityRole::ListItem)
            listItemCount++;
        else if (child->role() == AccessibilityRole::ListItem) {
            // Rendered list items always count.
            if (CheckedPtr renderListItem = dynamicDowncast<RenderListItem>(child->renderer())) {
                if (!hasVisibleMarkers && (!renderListItem->style().listStyleType().isNone() || renderListItem->style().listStyleImage() || childHasPseudoVisibleListItemMarkers(renderListItem->element())))
                    hasVisibleMarkers = true;
                listItemCount++;
            } else if (WebCore::elementName(node.get()) == ElementName::HTML_li) {
                // Inline elements that are in a list with an explicit role should also count.
                if (m_ariaRole == AccessibilityRole::List)
                    listItemCount++;

                if (childHasPseudoVisibleListItemMarkers(node.get())) {
                    hasVisibleMarkers = true;
                    listItemCount++;
                }
            }
        }
    }

    // Non <ul> lists and ARIA lists only need to have one child.
    // <ul>, <ol> lists need to have visible markers.
    if (ariaRoleAttribute() != AccessibilityRole::Unknown) {
        if (!listItemCount)
            role = AccessibilityRole::Group;
    } else if (!hasVisibleMarkers) {
        // http://webkit.org/b/193382 lists inside of navigation hierarchies should still be considered lists.
        if (Accessibility::findAncestor<AccessibilityObject>(*this, false, [] (auto& object) { return object.role() == AccessibilityRole::LandmarkNavigation; }))
            role = AccessibilityRole::List;
        else
            role = AccessibilityRole::Group;
    }

    return role;
}

} // namespace WebCore
