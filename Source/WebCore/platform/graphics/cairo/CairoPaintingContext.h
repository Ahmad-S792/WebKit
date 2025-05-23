/*
 * Copyright (C) 2017 Metrological Group B.V.
 * Copyright (C) 2017 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if USE(CAIRO)
#include "CairoPaintingOperation.h"
#include "RefPtrCairo.h"
#include <memory>
#include <wtf/TZoneMalloc.h>

typedef struct _cairo_surface cairo_surface_t;

namespace WebCore {
class CoordinatedTileBuffer;
class GraphicsContext;

namespace Cairo {

class PaintingContext {
    WTF_MAKE_TZONE_ALLOCATED(PaintingContext);
public:
    template<typename T>
    static void paint(WebCore::CoordinatedTileBuffer& buffer, const T& paintFunctor)
    {
        auto paintingContext = PaintingContext::createForPainting(buffer);
        paintFunctor(paintingContext->graphicsContext());
    }

    template<typename T>
    static void record(PaintingOperations& paintingOperations, const T& recordFunctor)
    {
        auto recordingContext = PaintingContext::createForRecording(paintingOperations);
        recordFunctor(recordingContext->graphicsContext());
    }

    static void replay(WebCore::CoordinatedTileBuffer& buffer, const PaintingOperations& paintingOperations)
    {
        auto paintingContext = PaintingContext::createForPainting(buffer);
        paintingContext->replay(paintingOperations);
    }

    ~PaintingContext();

    WebCore::GraphicsContext& graphicsContext() { return *m_graphicsContext; }

private:
    static std::unique_ptr<PaintingContext> createForPainting(WebCore::CoordinatedTileBuffer&);
    static std::unique_ptr<PaintingContext> createForRecording(PaintingOperations&);

    explicit PaintingContext(WebCore::CoordinatedTileBuffer&);
    explicit PaintingContext(PaintingOperations&);

    void replay(const PaintingOperations&);

    RefPtr<cairo_surface_t> m_surface;
    std::unique_ptr<WebCore::GraphicsContext> m_graphicsContext;
#if ASSERT_ENABLED
    bool m_deletionComplete { false };
#endif
};

} // namespace Cairo
} // namespace WebCore

#endif // USE(CAIRO)
