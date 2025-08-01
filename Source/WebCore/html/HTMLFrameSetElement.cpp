/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Simon Hausmann (hausmann@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004-2017 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "HTMLFrameSetElement.h"

#include "ContainerNodeInlines.h"
#include "CSSPropertyNames.h"
#include "DOMWrapperWorld.h"
#include "Document.h"
#include "ElementAncestorIteratorInlines.h"
#include "Event.h"
#include "FrameLoader.h"
#include "HTMLBodyElement.h"
#include "HTMLCollection.h"
#include "HTMLFrameElement.h"
#include "HTMLNames.h"
#include "HTMLParserIdioms.h"
#include "Length.h"
#include "LocalFrame.h"
#include "LocalFrameLoaderClient.h"
#include "MouseEvent.h"
#include "NodeName.h"
#include "RenderFrameSet.h"
#include "RenderObjectInlines.h"
#include "Text.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(HTMLFrameSetElement);

using namespace HTMLNames;

HTMLFrameSetElement::HTMLFrameSetElement(const QualifiedName& tagName, Document& document)
    : HTMLElement(tagName, document, TypeFlag::HasCustomStyleResolveCallbacks)
    , m_totalRows(1)
    , m_totalCols(1)
    , m_border(6)
    , m_borderSet(false)
    , m_borderColorSet(false)
    , m_frameborder(true)
    , m_frameborderSet(false)
    , m_noresize(false)
{
    ASSERT(hasTagName(framesetTag));
}

Ref<HTMLFrameSetElement> HTMLFrameSetElement::create(const QualifiedName& tagName, Document& document)
{
    return adoptRef(*new HTMLFrameSetElement(tagName, document));
}

bool HTMLFrameSetElement::hasPresentationalHintsForAttribute(const QualifiedName& name) const
{
    if (name == bordercolorAttr)
        return true;
    return HTMLElement::hasPresentationalHintsForAttribute(name);
}

void HTMLFrameSetElement::collectPresentationalHintsForAttribute(const QualifiedName& name, const AtomString& value, MutableStyleProperties& style)
{
    if (name == bordercolorAttr)
        addHTMLColorToStyle(style, CSSPropertyBorderColor, value);
    else
        HTMLElement::collectPresentationalHintsForAttribute(name, value, style);
}

void HTMLFrameSetElement::attributeChanged(const QualifiedName& name, const AtomString& oldValue, const AtomString& newValue, AttributeModificationReason attributeModificationReason)
{
    if (auto& eventName = HTMLBodyElement::eventNameForWindowEventHandlerAttribute(name); !eventName.isNull())
        protectedDocument()->setWindowAttributeEventListener(eventName, name, newValue, mainThreadNormalWorldSingleton());
    else
        HTMLElement::attributeChanged(name, oldValue, newValue, attributeModificationReason);

    switch (name.nodeName()) {
    case AttributeNames::rowsAttr:
        // FIXME: What is the right thing to do when removing this attribute?
        // Why not treat it the same way we treat setting it to the empty string?
        if (!newValue.isNull()) {
            m_rowLengths = newLengthArray(newValue.string(), m_totalRows);
            // FIXME: Would be nice to optimize the case where m_rowLengths did not change.
            invalidateStyleForSubtree();
        }
        break;
    case AttributeNames::colsAttr:
        // FIXME: What is the right thing to do when removing this attribute?
        // Why not treat it the same way we treat setting it to the empty string?
        if (!newValue.isNull()) {
            m_colLengths = newLengthArray(newValue.string(), m_totalCols);
            // FIXME: Would be nice to optimize the case where m_colLengths did not change.
            invalidateStyleForSubtree();
        }
        break;
    case AttributeNames::frameborderAttr:
        if (!newValue.isNull()) {
            if (equalLettersIgnoringASCIICase(newValue, "no"_s) || newValue == "0"_s) {
                m_frameborder = false;
                m_frameborderSet = true;
            } else if (equalLettersIgnoringASCIICase(newValue, "yes"_s) || newValue == "1"_s) {
                m_frameborderSet = true;
            }
        } else {
            m_frameborder = false;
            m_frameborderSet = false;
        }
        // FIXME: Do we need to trigger repainting?
        break;
    case AttributeNames::noresizeAttr:
        // FIXME: This should set m_noresize to false if the value is null.
        m_noresize = true;
        break;
    case AttributeNames::borderAttr:
        if (!newValue.isNull()) {
            m_border = parseHTMLInteger(newValue).value_or(0);
            m_borderSet = true;
        } else
            m_borderSet = false;
        // FIXME: Do we need to trigger repainting?
        break;
    case AttributeNames::bordercolorAttr:
        m_borderColorSet = !newValue.isEmpty();
        // FIXME: Clearly wrong: This can overwrite the value inherited from the parent frameset.
        // FIXME: Do we need to trigger repainting?
        break;
    default:
        break;
    }
}

RenderPtr<RenderElement> HTMLFrameSetElement::createElementRenderer(RenderStyle&& style, const RenderTreePosition&)
{
    if (style.hasContent())
        return RenderElement::createFor(*this, WTFMove(style));
    
    return createRenderer<RenderFrameSet>(*this, WTFMove(style));
}

RefPtr<HTMLFrameSetElement> HTMLFrameSetElement::findContaining(Element* descendant)
{
    return ancestorsOfType<HTMLFrameSetElement>(*descendant).first();
}

void HTMLFrameSetElement::willAttachRenderers()
{
    // Inherit default settings from parent frameset.
    // FIXME: This is not dynamic.
    const auto containingFrameSet = findContaining(this);
    if (!containingFrameSet)
        return;
    if (!m_frameborderSet)
        m_frameborder = containingFrameSet->hasFrameBorder();
    if (m_frameborder) {
        if (!m_borderSet)
            m_border = containingFrameSet->border();
        if (!m_borderColorSet)
            m_borderColorSet = containingFrameSet->hasBorderColor();
    }
    if (!m_noresize)
        m_noresize = containingFrameSet->noResize();
}

void HTMLFrameSetElement::defaultEventHandler(Event& event)
{
    if (auto* mouseEvent = dynamicDowncast<MouseEvent>(event); mouseEvent && !m_noresize) {
        if (CheckedPtr renderFrameSet = dynamicDowncast<RenderFrameSet>(renderer())) {
            if (renderFrameSet->userResize(*mouseEvent)) {
                event.setDefaultHandled();
                return;
            }
        }
    }
    HTMLElement::defaultEventHandler(event);
}

void HTMLFrameSetElement::willRecalcStyle(OptionSet<Style::Change>)
{
    if (needsStyleRecalc() && renderer())
        renderer()->setNeedsLayout();
}

Node::InsertedIntoAncestorResult HTMLFrameSetElement::insertedIntoAncestor(InsertionType insertionType, ContainerNode& parentOfInsertedTree)
{
    HTMLElement::insertedIntoAncestor(insertionType, parentOfInsertedTree);
    return InsertedIntoAncestorResult::Done;
}

void HTMLFrameSetElement::removedFromAncestor(RemovalType removalType, ContainerNode& oldParentOfRemovedTree)
{
    HTMLElement::removedFromAncestor(removalType, oldParentOfRemovedTree);
}

WindowProxy* HTMLFrameSetElement::namedItem(const AtomString& name)
{
    RefPtr frameElement = dynamicDowncast<HTMLFrameElement>(children()->namedItem(name));
    return frameElement ? frameElement->contentWindow() : nullptr;
}

bool HTMLFrameSetElement::isSupportedPropertyName(const AtomString& name)
{
    return namedItem(name);
}

Vector<AtomString> HTMLFrameSetElement::supportedPropertyNames() const
{
    // NOTE: Left empty as no specification defines this named getter and we
    //       have not historically exposed these named items for enumeration.
    return { };
}

} // namespace WebCore
