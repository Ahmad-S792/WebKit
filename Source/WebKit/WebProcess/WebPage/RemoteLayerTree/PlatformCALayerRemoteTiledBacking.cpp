/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#import "config.h"
#import "PlatformCALayerRemoteTiledBacking.h"

#import "RemoteLayerTreeContext.h"
#import <WebCore/GraphicsLayerCA.h>
#import <WebCore/PlatformCALayerCocoa.h>
#import <wtf/RetainPtr.h>

namespace WebKit {
using namespace WebCore;

PlatformCALayerRemoteTiledBacking::PlatformCALayerRemoteTiledBacking(LayerType layerType, PlatformCALayerClient* owner, RemoteLayerTreeContext& context)
    : PlatformCALayerRemote(layerType, owner, context)
    , m_tileController(makeUniqueRef<TileController>(this, WebCore::TileController::AllowScrollPerformanceLogging::No))
{
    PlatformCALayerRemote::setContentsScale(m_tileController->contentsScale());
}

PlatformCALayerRemoteTiledBacking::~PlatformCALayerRemoteTiledBacking()
{
}

void PlatformCALayerRemoteTiledBacking::setNeedsDisplayInRect(const FloatRect& dirtyRect)
{
    m_tileController->setNeedsDisplayInRect(enclosingIntRect(dirtyRect));
}

void PlatformCALayerRemoteTiledBacking::setNeedsDisplay()
{
    m_tileController->setNeedsDisplay();
}

const WebCore::PlatformCALayerList* PlatformCALayerRemoteTiledBacking::customSublayers() const
{
    m_customSublayers = m_tileController->containerLayers();
    return &m_customSublayers;
}

void PlatformCALayerRemoteTiledBacking::setBounds(const WebCore::FloatRect& bounds)
{
    PlatformCALayerRemote::setBounds(bounds);
    m_tileController->tileCacheLayerBoundsChanged();
}

bool PlatformCALayerRemoteTiledBacking::isOpaque() const
{
    return m_tileController->tilesAreOpaque();
}

void PlatformCALayerRemoteTiledBacking::setOpaque(bool opaque)
{
    m_tileController->setTilesOpaque(opaque);
}

bool PlatformCALayerRemoteTiledBacking::acceleratesDrawing() const
{
    return m_tileController->acceleratesDrawing();
}

void PlatformCALayerRemoteTiledBacking::setAcceleratesDrawing(bool acceleratesDrawing)
{
    m_tileController->setAcceleratesDrawing(acceleratesDrawing);
}

#if HAVE(SUPPORT_HDR_DISPLAY)
bool PlatformCALayerRemoteTiledBacking::setNeedsDisplayIfEDRHeadroomExceeds(float headroom)
{
    return m_tileController->setNeedsDisplayIfEDRHeadroomExceeds(headroom);
}

void PlatformCALayerRemoteTiledBacking::setTonemappingEnabled(bool enabled)
{
    PlatformCALayerRemote::setTonemappingEnabled(enabled);
    m_tileController->setTonemappingEnabled(enabled);
}
#endif


ContentsFormat PlatformCALayerRemoteTiledBacking::contentsFormat() const
{
    return m_tileController->contentsFormat();
}

void PlatformCALayerRemoteTiledBacking::setContentsFormat(ContentsFormat contentsFormat)
{
    m_tileController->setContentsFormat(contentsFormat);
}

float PlatformCALayerRemoteTiledBacking::contentsScale() const
{
    return m_tileController->contentsScale();
}

void PlatformCALayerRemoteTiledBacking::setContentsScale(float scale)
{
    PlatformCALayerRemote::setContentsScale(scale);
    m_tileController->setContentsScale(scale);
}

void PlatformCALayerRemoteTiledBacking::setBorderWidth(float borderWidth)
{
    m_tileController->setTileDebugBorderWidth(borderWidth / 2);
}

void PlatformCALayerRemoteTiledBacking::setBorderColor(const WebCore::Color& color)
{
    m_tileController->setTileDebugBorderColor(color);
}

} // namespace WebKit
