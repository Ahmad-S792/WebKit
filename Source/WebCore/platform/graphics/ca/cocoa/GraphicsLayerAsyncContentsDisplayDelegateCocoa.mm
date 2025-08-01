/*
 * Copyright (C) 2022 Apple Inc. All rights reserved.
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
#import "GraphicsLayerAsyncContentsDisplayDelegateCocoa.h"

#import "GraphicsLayerCA.h"
#import "ImageBuffer.h"
#import "NativeImage.h"
#import "WebCoreCALayerExtras.h"

#import <pal/spi/cocoa/QuartzCoreSPI.h>

namespace WebCore {

GraphicsLayerAsyncContentsDisplayDelegateCocoa::GraphicsLayerAsyncContentsDisplayDelegateCocoa(GraphicsLayerCA& layer)
{
    m_layer = adoptNS([[CALayer alloc] init]);
    [m_layer setName:@"OffscreenCanvasLayer"];

    layer.setContentsToPlatformLayer(m_layer.get(), GraphicsLayer::ContentsLayerPurpose::Canvas);
}

bool GraphicsLayerAsyncContentsDisplayDelegateCocoa::tryCopyToLayer(ImageBuffer& image, bool opaque)
{
    m_image = ImageBuffer::sinkIntoNativeImage(image.clone());
    if (!m_image)
        return false;

    [CATransaction begin];
    [CATransaction setDisableActions:YES];

    [m_layer setContents:(__bridge id)m_image->platformImage().get()];
    [m_layer setContentsOpaque:opaque];

    [CATransaction commit];

    return true;
}

void GraphicsLayerAsyncContentsDisplayDelegateCocoa::updateGraphicsLayerCA(GraphicsLayerCA& layer)
{
    layer.setContentsToPlatformLayer(m_layer.get(), GraphicsLayer::ContentsLayerPurpose::Canvas);
    if (m_image)
        [m_layer setContents:(__bridge id)m_image->platformImage().get()];
}

}
