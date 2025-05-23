/*
 * Copyright (C) 2001 Peter Kelly (pmk@post.com)
 * Copyright (C) 2001 Tobias Anton (anton@stud.fbi.fh-darmstadt.de)
 * Copyright (C) 2006 Samuel Weinig (sam.weinig@gmail.com)
 * Copyright (C) 2003, 2005, 2006, 2008, 2013 Apple Inc. All rights reserved.
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
#include "MouseRelatedEvent.h"

#include "Document.h"
#include "DocumentInlines.h"
#include "EventNames.h"
#include "LayoutPoint.h"
#include "LocalDOMWindow.h"
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "RenderLayer.h"
#include "RenderLayerInlines.h"
#include "RenderObject.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(MouseRelatedEvent);

// FIXME: Remove this variant.
MouseRelatedEvent::MouseRelatedEvent()
    : UIEventWithKeyState(EventInterfaceType::Invalid)
{
}

MouseRelatedEvent::MouseRelatedEvent(enum EventInterfaceType eventInterface)
    : UIEventWithKeyState(eventInterface)
{
}

MouseRelatedEvent::MouseRelatedEvent(enum EventInterfaceType eventInterface, const AtomString& eventType, CanBubble canBubble, IsCancelable isCancelable, IsComposed isComposed,
    MonotonicTime timestamp, RefPtr<WindowProxy>&& view, int detail,
    const IntPoint& screenLocation, const IntPoint& windowLocation, double movementX, double movementY, OptionSet<Modifier> modifiers, IsSimulated isSimulated, IsTrusted isTrusted)
    : UIEventWithKeyState(eventInterface, eventType, canBubble, isCancelable, isComposed, timestamp, WTFMove(view), detail, modifiers, isTrusted)
    , m_screenLocation(screenLocation)
    , m_movementX(movementX)
    , m_movementY(movementY)
    , m_windowLocation(windowLocation)
    , m_isSimulated(isSimulated == IsSimulated::Yes)
{
    init(m_isSimulated, windowLocation);
}

MouseRelatedEvent::MouseRelatedEvent(enum EventInterfaceType eventInterface, const AtomString& type, IsCancelable isCancelable, MonotonicTime timestamp, RefPtr<WindowProxy>&& view, const IntPoint& globalLocation, OptionSet<Modifier> modifiers)
    : MouseRelatedEvent(eventInterface, type, CanBubble::Yes, isCancelable, IsComposed::Yes, timestamp,
        WTFMove(view), 0, globalLocation, globalLocation /* Converted in init */, 0, 0, modifiers, IsSimulated::No)
{
}

MouseRelatedEvent::MouseRelatedEvent(enum EventInterfaceType eventInterface, const AtomString& eventType, const MouseRelatedEventInit& initializer, IsTrusted isTrusted)
    : UIEventWithKeyState(eventInterface, eventType, initializer, isTrusted)
    , m_screenLocation(IntPoint(initializer.screenX, initializer.screenY))
    , m_movementX(initializer.movementX)
    , m_movementY(initializer.movementY)
{
    init(false, IntPoint(0, 0));
}

static inline bool isMoveEventType(const AtomString& eventType)
{
    auto& eventNames = WebCore::eventNames();
    return eventType == eventNames.mousemoveEvent
        || eventType == eventNames.pointermoveEvent
        || eventType == eventNames.touchmoveEvent;
}

void MouseRelatedEvent::init(bool isSimulated, const IntPoint& windowLocation)
{
    if (!isSimulated) {
        if (RefPtr frameView = frameViewFromWindowProxy(view())) {
            FloatPoint absolutePoint = frameView->windowToContents(windowLocation);
            FloatPoint documentPoint = frameView->absoluteToDocumentPoint(absolutePoint);
            m_pageLocation = flooredLayoutPoint(documentPoint);
            m_clientLocation = pagePointToClientPoint(m_pageLocation, frameView.get());
        }
    }

    initCoordinates();

    if (!isConstructedFromInitializer() && !isMoveEventType(type())) {
        m_movementX = 0;
        m_movementY = 0;
    }
}

void MouseRelatedEvent::initCoordinates()
{
    // Set up initial values for coordinates.
    // Correct values are computed lazily, see computeRelativePosition.
    m_layerLocation = m_pageLocation;
    m_offsetLocation = m_pageLocation;

    computePageLocation();
    m_hasCachedRelativePosition = false;
}

LocalFrameView* MouseRelatedEvent::frameViewFromWindowProxy(WindowProxy* windowProxy)
{
    if (!windowProxy)
        return nullptr;

    auto* window = dynamicDowncast<LocalDOMWindow>(windowProxy->window());
    if (!window)
        return nullptr;

    auto* frame = window->localFrame();
    return frame ? frame->view() : nullptr;
}

LayoutPoint MouseRelatedEvent::pagePointToClientPoint(LayoutPoint pagePoint, LocalFrameView* frameView)
{
    if (!frameView)
        return pagePoint;

    return flooredLayoutPoint(frameView->documentToClientPoint(pagePoint));
}

LayoutPoint MouseRelatedEvent::pagePointToAbsolutePoint(LayoutPoint pagePoint, LocalFrameView* frameView)
{
    if (!frameView)
        return pagePoint;
    
    return pagePoint.scaled(frameView->documentToAbsoluteScaleFactor());
}

void MouseRelatedEvent::initCoordinates(const LayoutPoint& clientLocation)
{
    // Set up initial values for coordinates.
    // Correct values are computed lazily, see computeRelativePosition.
    FloatSize documentToClientOffset;
    if (RefPtr frameView = frameViewFromWindowProxy(view()))
        documentToClientOffset = frameView->documentToClientOffset();

    m_clientLocation = clientLocation;
    m_pageLocation = clientLocation - LayoutSize(documentToClientOffset);

    m_layerLocation = m_pageLocation;
    m_offsetLocation = m_pageLocation;

    computePageLocation();
    m_hasCachedRelativePosition = false;
}

float MouseRelatedEvent::documentToAbsoluteScaleFactor() const
{
    if (RefPtr frameView = frameViewFromWindowProxy(view()))
        return frameView->documentToAbsoluteScaleFactor();

    return 1;
}

void MouseRelatedEvent::computePageLocation()
{
    m_absoluteLocation = pagePointToAbsolutePoint(m_pageLocation, frameViewFromWindowProxy(view()));
}

void MouseRelatedEvent::receivedTarget()
{
    m_hasCachedRelativePosition = false;
}

void MouseRelatedEvent::computeRelativePosition()
{
    RefPtr targetNode = dynamicDowncast<Node>(target());
    if (!targetNode)
        return;

    // Compute coordinates that are based on the target.
    m_layerLocation = m_pageLocation;
    m_offsetLocation = m_pageLocation;

    // Must have an updated render tree for this math to work correctly.
    targetNode->protectedDocument()->updateLayoutIgnorePendingStylesheets();

    // Adjust offsetLocation to be relative to the target's position.
    if (CheckedPtr renderer = targetNode->renderer()) {
        m_offsetLocation = LayoutPoint(renderer->absoluteToLocal(absoluteLocation(), UseTransforms));
        float scaleFactor = 1 / documentToAbsoluteScaleFactor();
        if (scaleFactor != 1.0f)
            m_offsetLocation.scale(scaleFactor);
    }

    // Adjust layerLocation to be relative to the layer.
    // FIXME: event.layerX and event.layerY are poorly defined,
    // and probably don't always correspond to RenderLayer offsets.
    // https://bugs.webkit.org/show_bug.cgi?id=21868
    RefPtr node = WTFMove(targetNode);
    while (node && !node->renderer())
        node = node->parentNode();

    RenderLayer* layer;
    if (node && (layer = node->renderer()->enclosingLayer())) {
        for (; layer; layer = layer->parent()) {
            m_layerLocation -= toLayoutSize(layer->location());
        }
    }

    m_hasCachedRelativePosition = true;
}
    
FloatPoint MouseRelatedEvent::locationInRootViewCoordinates() const
{
    if (RefPtr frameView = frameViewFromWindowProxy(view()))
        return frameView->contentsToRootView(roundedIntPoint(m_absoluteLocation));

    return m_absoluteLocation;
}

int MouseRelatedEvent::layerX()
{
    if (!m_hasCachedRelativePosition)
        computeRelativePosition();
    return m_layerLocation.x();
}

int MouseRelatedEvent::layerY()
{
    if (!m_hasCachedRelativePosition)
        computeRelativePosition();
    return m_layerLocation.y();
}

int MouseRelatedEvent::offsetX()
{
    if (isSimulated())
        return 0;
    if (!m_hasCachedRelativePosition)
        computeRelativePosition();
    return roundToInt(m_offsetLocation.x());
}

int MouseRelatedEvent::offsetY()
{
    if (isSimulated())
        return 0;
    if (!m_hasCachedRelativePosition)
        computeRelativePosition();
    return roundToInt(m_offsetLocation.y());
}

int MouseRelatedEvent::pageX() const
{
    return m_pageLocation.x();
}

int MouseRelatedEvent::pageY() const
{
    return m_pageLocation.y();
}

} // namespace WebCore
