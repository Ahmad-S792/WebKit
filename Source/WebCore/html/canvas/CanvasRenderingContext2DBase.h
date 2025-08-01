/*
 * Copyright (C) 2006-2024 Apple Inc. All rights reserved.
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

#pragma once

#include "AffineTransform.h"
#include "CanvasDirection.h"
#include "CanvasFillRule.h"
#include "CanvasLineCap.h"
#include "CanvasLineJoin.h"
#include "CanvasPath.h"
#include "CanvasRenderingContext.h"
#include "CanvasRenderingContext2DSettings.h"
#include "CanvasStyle.h"
#include "CanvasTextAlign.h"
#include "CanvasTextBaseline.h"
#include "Color.h"
#include "Filter.h"
#include "FloatSize.h"
#include "FontCascade.h"
#include "FontSelectorClient.h"
#include "GraphicsContext.h"
#include "GraphicsTypes.h"
#include "ImageBuffer.h"
#include "ImageDataSettings.h"
#include "ImageSmoothingQuality.h"
#include "Path.h"
#include "PlatformLayer.h"
#include "Timer.h"
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class ByteArrayPixelBuffer;
class CachedImage;
class CanvasLayerContextSwitcher;
class CanvasGradient;
class DOMMatrix;
class FloatRect;
class GraphicsContext;
class ImageData;
class OffscreenCanvas;
class Path2D;
class RenderElement;
class RenderObject;
class SVGImageElement;
class TextMetrics;
class WebCodecsVideoFrame;

struct DOMMatrix2DInit;


using CanvasImageSource = Variant<RefPtr<HTMLImageElement>
    , RefPtr<SVGImageElement>
    , RefPtr<HTMLCanvasElement>
    , RefPtr<ImageBitmap>
    , RefPtr<CSSStyleImageValue>
#if ENABLE(OFFSCREEN_CANVAS)
    , RefPtr<OffscreenCanvas>
#endif
#if ENABLE(VIDEO)
    , RefPtr<HTMLVideoElement>
#endif
#if ENABLE(WEB_CODECS)
    , RefPtr<WebCodecsVideoFrame>
#endif
    >;

class CanvasRenderingContext2DBase : public CanvasRenderingContext, public CanvasPath {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(CanvasRenderingContext2DBase);
    friend class CanvasFilterContextSwitcher;
    friend class CanvasLayerContextSwitcher;
protected:
    CanvasRenderingContext2DBase(CanvasBase&, CanvasRenderingContext::Type, CanvasRenderingContext2DSettings&&, bool usesCSSCompatibilityParseMode);

public:
    virtual ~CanvasRenderingContext2DBase();

    bool isAccelerated() const;

    const CanvasRenderingContext2DSettings& getContextAttributes() const { return m_settings; }
    using RenderingMode = WebCore::RenderingMode;
    std::optional<RenderingMode> renderingModeForTesting() const final;
    std::optional<RenderingMode> getEffectiveRenderingModeForTesting();

    double lineWidth() const { return state().lineWidth; }
    void setLineWidth(double);

    CanvasLineCap lineCap() const { return state().canvasLineCap(); }
    void setLineCap(CanvasLineCap);
    void setLineCap(const String&);

    CanvasLineJoin lineJoin() const { return state().canvasLineJoin(); }
    void setLineJoin(CanvasLineJoin);
    void setLineJoin(const String&);

    double miterLimit() const { return state().miterLimit; }
    void setMiterLimit(double);

    const Vector<double>& getLineDash() const { return state().lineDash; }
    void setLineDash(const Vector<double>&);

    const Vector<double>& webkitLineDash() const { return getLineDash(); }
    void setWebkitLineDash(const Vector<double>&);

    double lineDashOffset() const { return state().lineDashOffset; }
    void setLineDashOffset(double);

    float shadowOffsetX() const { return state().shadowOffset.width(); }
    void setShadowOffsetX(float);

    float shadowOffsetY() const { return state().shadowOffset.height(); }
    void setShadowOffsetY(float);

    float shadowBlur() const { return state().shadowBlur; }
    void setShadowBlur(float);

    String shadowColor() const { return state().shadowColorString(); }
    void setShadowColor(const String&);

    double globalAlpha() const { return state().globalAlpha; }
    void setGlobalAlpha(double);

    String globalCompositeOperation() const { return state().globalCompositeOperationString(); }
    void setGlobalCompositeOperation(const String&);

    String filterString() const { return state().filterString; }
    void setFilterString(const String&);

    String letterSpacing() const { return state().letterSpacing; }
    void setLetterSpacing(const String&);

    String wordSpacing() const { return state().wordSpacing; }
    void setWordSpacing(const String&);

    void save() { ++m_unrealizedSaveCount; }
    void restore();

    void beginLayer();
    void endLayer();

    void scale(double sx, double sy);
    void rotate(double angleInRadians);
    void translate(double tx, double ty);
    void transform(double m11, double m12, double m21, double m22, double dx, double dy);

    Ref<DOMMatrix> getTransform() const;
    void setTransform(double m11, double m12, double m21, double m22, double dx, double dy);
    ExceptionOr<void> setTransform(DOMMatrix2DInit&&);
    void resetTransform();

    void setStrokeColor(String&& color, std::optional<float> alpha = std::nullopt);
    void setStrokeColor(float grayLevel, float alpha = 1.0);
    void setStrokeColor(float r, float g, float b, float a);

    void setFillColor(String&& color, std::optional<float> alpha = std::nullopt);
    void setFillColor(float grayLevel, float alpha = 1.0f);
    void setFillColor(float r, float g, float b, float a);

    void beginPath();

    void fill(CanvasFillRule = CanvasFillRule::Nonzero);
    void stroke();
    void clip(CanvasFillRule = CanvasFillRule::Nonzero);

    void fill(Path2D&, CanvasFillRule = CanvasFillRule::Nonzero);
    void stroke(Path2D&);
    void clip(Path2D&, CanvasFillRule = CanvasFillRule::Nonzero);

    bool isPointInPath(double x, double y, CanvasFillRule = CanvasFillRule::Nonzero);
    bool isPointInStroke(double x, double y);

    bool isPointInPath(Path2D&, double x, double y, CanvasFillRule = CanvasFillRule::Nonzero);
    bool isPointInStroke(Path2D&, double x, double y);

    void clearRect(double x, double y, double width, double height);
    void fillRect(double x, double y, double width, double height);
    void strokeRect(double x, double y, double width, double height);

    void setShadow(float width, float height, float blur, const String& color = String(), std::optional<float> alpha = std::nullopt);
    void setShadow(float width, float height, float blur, float grayLevel, float alpha = 1.0);
    void setShadow(float width, float height, float blur, float r, float g, float b, float a);

    void clearShadow();

    ExceptionOr<void> drawImage(CanvasImageSource&&, float dx, float dy);
    ExceptionOr<void> drawImage(CanvasImageSource&&, float dx, float dy, float dw, float dh);
    ExceptionOr<void> drawImage(CanvasImageSource&&, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh);

    void clearCanvas();

    using StyleVariant = Variant<String, RefPtr<CanvasGradient>, RefPtr<CanvasPattern>>;
    StyleVariant strokeStyle() const;
    void setStrokeStyle(StyleVariant&&);
    StyleVariant fillStyle() const;
    void setFillStyle(StyleVariant&&);

    ExceptionOr<Ref<CanvasGradient>> createLinearGradient(float x0, float y0, float x1, float y1);
    ExceptionOr<Ref<CanvasGradient>> createRadialGradient(float x0, float y0, float r0, float x1, float y1, float r1);
    ExceptionOr<Ref<CanvasGradient>> createConicGradient(float angleInRadians, float x, float y);
    ExceptionOr<RefPtr<CanvasPattern>> createPattern(CanvasImageSource&&, const String& repetition);

    ExceptionOr<Ref<ImageData>> createImageData(ImageData&) const;
    ExceptionOr<Ref<ImageData>> createImageData(int width, int height, std::optional<ImageDataSettings>) const;
    ExceptionOr<Ref<ImageData>> getImageData(int sx, int sy, int sw, int sh, std::optional<ImageDataSettings>) const;
    void putImageData(ImageData&, int dx, int dy);
    void putImageData(ImageData&, int dx, int dy, int dirtyX, int dirtyY, int dirtyWidth, int dirtyHeight);

    void reset();

    bool imageSmoothingEnabled() const { return state().imageSmoothingEnabled; }
    void setImageSmoothingEnabled(bool);

    ImageSmoothingQuality imageSmoothingQuality() const { return state().imageSmoothingQuality; }
    void setImageSmoothingQuality(ImageSmoothingQuality);

    void setPath(Path2D&);
    Ref<Path2D> getPath() const;

    String font() const { return state().fontString(); }

    CanvasTextAlign textAlign() const { return state().canvasTextAlign(); }
    void setTextAlign(CanvasTextAlign);

    CanvasTextBaseline textBaseline() const { return state().canvasTextBaseline(); }
    void setTextBaseline(CanvasTextBaseline);

    using Direction = CanvasDirection;
    void setDirection(Direction);

    class FontProxy final : public FontSelectorClient {
    public:
        FontProxy() = default;
        virtual ~FontProxy();
        FontProxy(const FontProxy&);
        FontProxy& operator=(const FontProxy&);

        bool realized() const { return m_font.fontSelector(); }
        void initialize(FontSelector&, const FontCascade&);
        const FontMetrics& metricsOfPrimaryFont() const;
        const FontCascadeDescription& fontDescription() const;
        float width(const TextRun&, GlyphOverflow* = 0) const;
        void drawBidiText(GraphicsContext&, const TextRun&, const FloatPoint&, FontCascade::CustomFontNotReadyAction) const;

#if ASSERT_ENABLED
        bool isPopulated() const { return m_font.fonts(); }
#endif

        const FontCascade& fontCascade() const { return m_font; }

        float letterSpacing() const { return m_font.letterSpacing(); }
        void setLetterSpacing(const Length& letterSpacing) { m_font.setLetterSpacing(letterSpacing); }

        float wordSpacing() const { return m_font.wordSpacing(); }
        void setWordSpacing(const Length& wordSpacing) { m_font.setWordSpacing(wordSpacing); }

    private:
        void update(FontSelector&);
        void fontsNeedUpdate(FontSelector&) final;

        FontCascade m_font;
    };

    struct State final {
        State();

        String unparsedStrokeColor;
        String unparsedFillColor;
        CanvasStyle strokeStyle;
        CanvasStyle fillStyle;
        double lineWidth;
        LineCap lineCap;
        LineJoin lineJoin;
        double miterLimit;
        FloatSize shadowOffset;
        float shadowBlur;
        Color shadowColor;
        double globalAlpha;
        CompositeOperator globalComposite;
        BlendMode globalBlend;
        AffineTransform transform;
        std::optional<AffineTransform> transformInverse;
        Vector<double> lineDash;
        double lineDashOffset;
        bool imageSmoothingEnabled;
        ImageSmoothingQuality imageSmoothingQuality;
        TextAlign textAlign;
        TextBaseline textBaseline;
        Direction direction;

        String filterString;
        FilterOperations filterOperations;

        String letterSpacing;
        String wordSpacing;

        String unparsedFont;
        FontProxy font;

        RefPtr<CanvasLayerContextSwitcher> targetSwitcher;

        CanvasLineCap canvasLineCap() const;
        CanvasLineJoin canvasLineJoin() const;
        CanvasTextAlign canvasTextAlign() const;
        CanvasTextBaseline canvasTextBaseline() const;
        String fontString() const;
        String globalCompositeOperationString() const;
        String shadowColorString() const;
    };
    const Vector<State, 1>& stateStack();

protected:
    static const int DefaultFontSize;
    static const ASCIILiteral DefaultFontFamily;

    const State& state() const { return m_stateStack.last(); }
    void realizeSaves();
    State& modifiableState() { ASSERT(!m_unrealizedSaveCount || m_stateStack.size() >= MaxSaveCount); return m_stateStack.last(); }

    // These methods are de-virtualized for performance reasons.
    GraphicsContext* drawingContext() const;
    GraphicsContext* effectiveDrawingContext() const;

    virtual GraphicsContext* existingDrawingContext() const;
    virtual AffineTransform baseTransform() const;

    enum class DidDrawOption {
        ApplyTransform = 1 << 0,
        ApplyShadow = 1 << 1,
        ApplyClip = 1 << 2,
        ApplyPostProcessing = 1 << 3,
        PreserveCachedContents = 1 << 4,
    };

    static constexpr OptionSet<DidDrawOption> defaultDidDrawOptions()
    {
        return {
            DidDrawOption::ApplyTransform,
            DidDrawOption::ApplyShadow,
            DidDrawOption::ApplyClip,
            DidDrawOption::ApplyPostProcessing,
        };
    }

    static constexpr OptionSet<DidDrawOption> defaultDidDrawOptionsWithoutPostProcessing()
    {
        return {
            DidDrawOption::ApplyTransform,
            DidDrawOption::ApplyShadow,
            DidDrawOption::ApplyClip,
        };
    }
    void didDraw(std::optional<FloatRect>, OptionSet<DidDrawOption> = defaultDidDrawOptions());
    void didDrawEntireCanvas(OptionSet<DidDrawOption> options = defaultDidDrawOptions());
    void didDraw(bool entireCanvas, const FloatRect&, OptionSet<DidDrawOption> options = defaultDidDrawOptions());
    template<typename RectProvider> void didDraw(bool entireCanvas, NOESCAPE const RectProvider&, OptionSet<DidDrawOption> options = defaultDidDrawOptions());

    virtual std::optional<FilterOperations> setFilterStringWithoutUpdatingStyle(const String&) { return std::nullopt; }

    virtual RefPtr<Filter> createFilter(const FloatRect&) const { return nullptr; }
    virtual IntOutsets calculateFilterOutsets(const FloatRect&) const { return { }; }

    static String normalizeSpaces(const String&);

    bool canDrawText(double x, double y, bool fill, std::optional<double> maxWidth = std::nullopt);
    void drawTextUnchecked(const TextRun&, double x, double y, bool fill, std::optional<double> maxWidth = std::nullopt);

    Ref<TextMetrics> measureTextInternal(const TextRun&);
    Ref<TextMetrics> measureTextInternal(const String& text);

    bool usesCSSCompatibilityParseMode() const { return m_usesCSSCompatibilityParseMode; }

    void updateStateTransform(const AffineTransform&);
private:
    struct CachedContentsTransparent {
    };
    struct CachedContentsUnknown {
    };
    struct CachedContentsImageData {
        CachedContentsImageData(CanvasRenderingContext2DBase&, Ref<ByteArrayPixelBuffer>);

        Ref<ByteArrayPixelBuffer> imageData;
        DeferrableOneShotTimer evictionTimer;
    };

    void applyLineDash() const;
    void setShadow(const FloatSize& offset, float blur, const Color&);
    void applyShadow();
    bool shouldDrawShadows() const;

    bool needsPreparationForDisplay() const final;
    void prepareForDisplay() final;

    void clearAccumulatedDirtyRect() final;
    bool isEntireBackingStoreDirty() const;
    FloatRect backingStoreBounds() const { return FloatRect { { }, FloatSize { canvasBase().size() } }; }

    ImageBufferPixelFormat pixelFormat() const final;
    DestinationColorSpace colorSpace() const final;
    bool willReadFrequently() const final;

    void unwindStateStack();
    void realizeSavesLoop();
    void setStrokeColorImpl(Color&& color, String&& unparsedColor = { });
    void setFillColorImpl(Color&& color, String&& unparsedColor = { });

    ExceptionOr<RefPtr<CanvasPattern>> createPattern(CachedImage&, RenderElement*, bool repeatX, bool repeatY);
    ExceptionOr<RefPtr<CanvasPattern>> createPattern(HTMLImageElement&, bool repeatX, bool repeatY);
    ExceptionOr<RefPtr<CanvasPattern>> createPattern(SVGImageElement&, bool repeatX, bool repeatY);
    ExceptionOr<RefPtr<CanvasPattern>> createPattern(CanvasBase&, bool repeatX, bool repeatY);
#if ENABLE(VIDEO)
    ExceptionOr<RefPtr<CanvasPattern>> createPattern(HTMLVideoElement&, bool repeatX, bool repeatY);
#endif
    ExceptionOr<RefPtr<CanvasPattern>> createPattern(ImageBitmap&, bool repeatX, bool repeatY);
    ExceptionOr<RefPtr<CanvasPattern>> createPattern(CSSStyleImageValue&, bool repeatX, bool repeatY);
#if ENABLE(WEB_CODECS)
    ExceptionOr<RefPtr<CanvasPattern>> createPattern(WebCodecsVideoFrame&, bool repeatX, bool repeatY);
#endif

    ExceptionOr<void> drawImage(HTMLImageElement&, const FloatRect& srcRect, const FloatRect& dstRect);
    ExceptionOr<void> drawImage(HTMLImageElement&, const FloatRect& srcRect, const FloatRect& dstRect, const CompositeOperator&, const BlendMode&);
    ExceptionOr<void> drawImage(SVGImageElement&, const FloatRect& srcRect, const FloatRect& dstRect);
    ExceptionOr<void> drawImage(SVGImageElement&, const FloatRect& srcRect, const FloatRect& dstRect, const CompositeOperator&, const BlendMode&);
    ExceptionOr<void> drawImage(CanvasBase&, const FloatRect& srcRect, const FloatRect& dstRect);
    ExceptionOr<void> drawImage(Document&, CachedImage&, const RenderObject*, const FloatRect& imageRect, const FloatRect& srcRect, const FloatRect& dstRect, const CompositeOperator&, const BlendMode&, ImageOrientation = ImageOrientation::Orientation::FromImage);
#if ENABLE(VIDEO)
    ExceptionOr<void> drawImage(HTMLVideoElement&, const FloatRect& srcRect, const FloatRect& dstRect);
#endif
    ExceptionOr<void> drawImage(CSSStyleImageValue&, const FloatRect& srcRect, const FloatRect& dstRect);
    ExceptionOr<void> drawImage(ImageBitmap&, const FloatRect& srcRect, const FloatRect& dstRect);
#if ENABLE(WEB_CODECS)
    ExceptionOr<void> drawImage(WebCodecsVideoFrame&, const FloatRect& srcRect, const FloatRect& dstRect);
#endif

    void beginCompositeLayer();
    void endCompositeLayer();

    void fillInternal(const Path&, CanvasFillRule);
    void strokeInternal(const Path&);
    void clipInternal(const Path&, CanvasFillRule);

    bool isPointInPathInternal(const Path&, double x, double y, CanvasFillRule);
    bool isPointInStrokeInternal(const Path&, double x, double y);

    Path transformAreaToDevice(const Path&) const;
    Path transformAreaToDevice(const FloatRect&) const;
    bool rectContainsCanvas(const FloatRect&) const;

    template<class T> IntRect calculateCompositingBufferRect(const T&, IntSize*);
    void compositeBuffer(ImageBuffer&, const IntRect&, CompositeOperator);

    FloatRect inflatedStrokeRect(const FloatRect&) const;

    template<class T> void fullCanvasCompositedDrawImage(T&, const FloatRect&, const FloatRect&, CompositeOperator);

    bool isSurfaceBufferTransparentBlack(SurfaceBuffer) const override;
#if USE(SKIA)
    RefPtr<GraphicsLayerContentsDisplayDelegate> layerContentsDisplayDelegate() override;
#endif
    bool hasDeferredOperations() const final;
    void flushDeferredOperations() final;

    // The relationship between FontCascade and CanvasRenderingContext2D::FontProxy must hold certain invariants.
    // Therefore, all font operations must pass through the proxy.
    virtual const FontProxy* fontProxy() { return nullptr; }

    FloatPoint textOffset(float width, TextDirection);

    RefPtr<ByteArrayPixelBuffer> cacheImageDataIfPossible(const ImageData&, const IntRect& sourceRect, const IntPoint& destinationPosition);
    RefPtr<ImageData> makeImageDataIfContentsCached(const IntRect& sourceRect, PredefinedColorSpace) const;
    void evictCachedImageData();

    static constexpr unsigned MaxSaveCount = 1024 * 16;
    Vector<State, 1> m_stateStack;
    FloatRect m_dirtyRect;
    unsigned m_unrealizedSaveCount { 0 };
    bool m_usesCSSCompatibilityParseMode;
    mutable Variant<CachedContentsTransparent, CachedContentsUnknown, CachedContentsImageData> m_cachedContents;
    CanvasRenderingContext2DSettings m_settings;
    bool m_hasDeferredOperations { false };
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_CANVASRENDERINGCONTEXT(WebCore::CanvasRenderingContext2DBase, is2dBase())
