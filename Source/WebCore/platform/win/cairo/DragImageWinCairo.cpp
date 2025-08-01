/*
 * Copyright (C) 2008, 2013 Apple Inc. All rights reserved.
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

#include "config.h"
#include "DragImage.h"

#include "BitmapInfo.h"
#include "CachedImage.h"
#include "Element.h"
#include "FloatRoundedRect.h"
#include "FontCascade.h"
#include "FontDescription.h"
#include "FontSelector.h"
#include "GraphicsContextCairo.h"
#include "HWndDC.h"
#include "Image.h"
#include "NativeImage.h"
#include "StringTruncator.h"
#include "TextIndicator.h"
#include "TextRun.h"
#include "WebCoreTextRenderer.h"
#include <cairo-win32.h>
#include <windows.h>
#include <wtf/RetainPtr.h>
#include <wtf/URL.h>
#include <wtf/win/GDIObject.h>

namespace WebCore {

void deallocContext(GraphicsContextCairo* target)
{
    delete target;
}

GDIObject<HBITMAP> allocImage(HDC dc, IntSize size, GraphicsContextCairo** targetRef)
{
    BitmapInfo bmpInfo = BitmapInfo::create(size);

    LPVOID bits;
    auto hbmp = adoptGDIObject(::CreateDIBSection(dc, &bmpInfo, DIB_RGB_COLORS, &bits, 0, 0));

    // At this point, we have a Cairo surface that points to a Windows DIB. The DIB interprets
    // with the opposite meaning of positive Y axis, so everything we draw into this cairo
    // context is going to be upside down.
    if (!targetRef)
        return hbmp;

    cairo_surface_t* bitmapContext = cairo_image_surface_create_for_data((unsigned char*)bits,
        CAIRO_FORMAT_ARGB32,
        bmpInfo.bmiHeader.biWidth,
        bmpInfo.bmiHeader.biHeight,
        bmpInfo.bmiHeader.biWidth * 4);

    if (!bitmapContext)
        return GDIObject<HBITMAP>();

    cairo_t* cr = cairo_create(bitmapContext);
    cairo_surface_destroy(bitmapContext);

    // At this point, we have a Cairo surface that points to a Windows DIB. The DIB interprets
    // with the opposite meaning of positive Y axis, so everything we draw into this cairo
    // context is going to be upside down.
    //
    // So, we must invert the CTM for the context so that drawing commands will be flipped
    // before they get written to the internal buffer.
    cairo_matrix_t matrix;
    cairo_matrix_init(&matrix, 1.0, 0.0, 0.0, -1.0, 0.0, size.height());
    cairo_set_matrix(cr, &matrix);

    *targetRef = new PlatformGraphicsContext(cr);
    cairo_destroy(cr);

    return hbmp;
}

static cairo_surface_t* createCairoContextFromBitmap(HBITMAP bitmap)
{
    BITMAP info;
    GetObject(bitmap, sizeof(info), &info);
    ASSERT(info.bmBitsPixel == 32);

    // At this point, we have a Cairo surface that points to a Windows BITMAP. The BITMAP
    // has the opposite meaning of positive Y axis, so everything we draw into this cairo
    // context is going to be upside down.
    return cairo_image_surface_create_for_data((unsigned char*)info.bmBits,
        CAIRO_FORMAT_ARGB32,
        info.bmWidth,
        info.bmHeight,
        info.bmWidthBytes);
}

DragImageRef scaleDragImage(DragImageRef imageRef, FloatSize scale)
{
    // FIXME: due to the way drag images are done on windows we need
    // to preprocess the alpha channel <rdar://problem/5015946>
    if (!imageRef)
        return 0;

    auto image = adoptGDIObject(imageRef);

    IntSize srcSize = dragImageSize(image.get());
    IntSize dstSize(static_cast<int>(srcSize.width() * scale.width()), static_cast<int>(srcSize.height() * scale.height()));

    HWndDC dc(0);
    auto dstDC = adoptGDIObject(::CreateCompatibleDC(dc));
    if (!dstDC)
        return image.leak();

    GraphicsContextCairo* targetContext;
    GDIObject<HBITMAP> hbmp = allocImage(dstDC.get(), dstSize, &targetContext);
    if (!hbmp)
        return image.leak();

    cairo_surface_t* srcImage = createCairoContextFromBitmap(image.get());

    // Scale the target surface to the new image size, and flip it
    // so that when we set the srcImage as the surface it will draw
    // right-side-up.
    cairo_t* cr = targetContext->cr();
    cairo_translate(cr, 0, dstSize.height());
    cairo_scale(cr, scale.width(), -scale.height());
    cairo_set_source_surface(cr, srcImage, 0.0, 0.0);

    // Now we can paint and get the correct result
    cairo_paint(cr);

    cairo_surface_destroy(srcImage);
    deallocContext(targetContext);

    return hbmp.leak();
}

DragImageRef createDragImageFromImage(Image* img, ImageOrientation, GraphicsClient*, float)
{
    HWndDC dc(0);
    auto workingDC = adoptGDIObject(::CreateCompatibleDC(dc));
    if (!workingDC)
        return 0;

    GraphicsContextCairo* drawContext = 0;
    auto hbmp = allocImage(workingDC.get(), IntSize(img->size()), &drawContext);
    if (!hbmp || !drawContext)
        return 0;

    cairo_t* cr = drawContext->cr();
    cairo_set_source_rgb(cr, 1.0, 0.0, 1.0);
    cairo_fill_preserve(cr);

    if (auto nativeImage = img->currentNativeImage()) {
        auto& surface = nativeImage->platformImage();
        // Draw the image.
        cairo_set_source_surface(cr, surface.get(), 0.0, 0.0);
        cairo_paint(cr);
    }

    deallocContext(drawContext);

    return hbmp.leak();
}

IntSize dragImageSize(DragImageRef image)
{
    if (!image)
        return IntSize();
    BITMAP b;
    GetObject(image, sizeof(BITMAP), &b);
    return IntSize(b.bmWidth, b.bmHeight);
}

void deleteDragImage(DragImageRef image)
{
    if (image)
        ::DeleteObject(image);
}

DragImageRef dissolveDragImageToFraction(DragImageRef image, float)
{
    // We don't do this on windows as the dragimage is blended by the OS
    return image;
}

DragImageRef createDragImageIconForCachedImageFilename(const String& filename)
{
    SHFILEINFO shfi { };
    auto fname = filename.wideCharacters();
    if (FAILED(SHGetFileInfo(fname.data(), FILE_ATTRIBUTE_NORMAL, &shfi, sizeof(shfi), SHGFI_ICON | SHGFI_USEFILEATTRIBUTES)))
        return 0;

    ICONINFO iconInfo;
    if (!GetIconInfo(shfi.hIcon, &iconInfo)) {
        DestroyIcon(shfi.hIcon);
        return 0;
    }

    DestroyIcon(shfi.hIcon);
    DeleteObject(iconInfo.hbmMask);

    return iconInfo.hbmColor;
}

const float DragLabelBorderX = 4;
// Keep border_y in synch with DragController::LinkDragBorderInset.
const float DragLabelBorderY = 2;
const float DragLabelRadius = 5;
const float LabelBorderYOffset = 2;

const float MaxDragLabelWidth = 200;
const float MaxDragLabelStringWidth = (MaxDragLabelWidth - 2 * DragLabelBorderX);

const float DragLinkLabelFontsize = 11;
const float DragLinkURLFontSize = 10;

static FontCascade dragLabelFont(int size, bool bold)
{
    FontCascade result;
    NONCLIENTMETRICS metrics;
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0);

    FontCascadeDescription description;
    description.setWeight(bold ? boldWeightValue() : normalWeightValue());
    description.setOneFamily(metrics.lfSmCaptionFont.lfFaceName);
    description.setSpecifiedSize((float)size);
    description.setComputedSize((float)size);
    result = FontCascade(WTFMove(description));
    result.update();
    return result;
}

DragImageData createDragImageForLink(Element&, URL& url, const String& inLabel, float)
{
    static const FontCascade labelFont = dragLabelFont(DragLinkLabelFontsize, true);
    static const FontCascade urlFont = dragLabelFont(DragLinkURLFontSize, false);

    bool drawURLString = true;
    bool clipURLString = false;
    bool clipLabelString = false;

    String urlString = url.string();
    String label = inLabel;
    if (label.isEmpty()) {
        drawURLString = false;
        label = urlString;
    }

    // First step in drawing the link drag image width.
    TextRun labelRun(label);
    TextRun urlRun(urlString);
    IntSize labelSize(labelFont.width(labelRun), labelFont.metricsOfPrimaryFont().intAscent() + labelFont.metricsOfPrimaryFont().intDescent());

    if (labelSize.width() > MaxDragLabelStringWidth) {
        labelSize.setWidth(MaxDragLabelStringWidth);
        clipLabelString = true;
    }

    IntSize urlStringSize;
    IntSize imageSize(labelSize.width() + DragLabelBorderX * 2, labelSize.height() + DragLabelBorderY * 2);

    if (drawURLString) {
        urlStringSize.setWidth(urlFont.width(urlRun));
        urlStringSize.setHeight(urlFont.metricsOfPrimaryFont().intAscent() + urlFont.metricsOfPrimaryFont().intDescent());
        imageSize.setHeight(imageSize.height() + urlStringSize.height());
        if (urlStringSize.width() > MaxDragLabelStringWidth) {
            imageSize.setWidth(MaxDragLabelWidth);
            clipURLString = true;
        } else
            imageSize.setWidth(std::max(labelSize.width(), urlStringSize.width()) + DragLabelBorderX * 2);
    }

    // We now know how big the image needs to be, so we create and
    // fill the background
    HWndDC dc(0);
    auto workingDC = adoptGDIObject(::CreateCompatibleDC(dc));
    if (!workingDC)
        return 0;

    PlatformGraphicsContext* contextRef;
    auto image = allocImage(workingDC.get(), imageSize, &contextRef);
    if (!image)
        return 0;

    ::SelectObject(workingDC.get(), image.get());
    GraphicsContextCairo context(contextRef);
    // On Mac alpha is {0.7, 0.7, 0.7, 0.8}, however we can't control alpha
    // for drag images on win, so we use 1
    constexpr auto backgroundColor = SRGBA<uint8_t> { 140, 140, 140 };
    static const IntSize radii(DragLabelRadius, DragLabelRadius);
    IntRect rect(0, 0, imageSize.width(), imageSize.height());
    context.fillRoundedRect(FloatRoundedRect(rect, radii, radii, radii, radii), backgroundColor);

    // Draw the text
    constexpr auto topColor = Color::black; // original alpha = 0.75
    constexpr auto bottomColor = Color::white.colorWithAlphaByte(127); // original alpha = 0.5
    if (drawURLString) {
        if (clipURLString)
            urlString = StringTruncator::rightTruncate(urlString, imageSize.width() - (DragLabelBorderX * 2.0f), urlFont);
        IntPoint textPos(DragLabelBorderX, imageSize.height() - (LabelBorderYOffset + urlFont.metricsOfPrimaryFont().intDescent()));
        WebCoreDrawDoubledTextAtPoint(context, urlString, textPos, urlFont, topColor, bottomColor);
    }

    if (clipLabelString)
        label = StringTruncator::rightTruncate(label, imageSize.width() - (DragLabelBorderX * 2.0f), labelFont);

    IntPoint textPos(DragLabelBorderX, DragLabelBorderY + labelFont.size());
    WebCoreDrawDoubledTextAtPoint(context, label, textPos, labelFont, topColor, bottomColor);

    deallocContext(contextRef);
    return { image.leak(), nullptr };
}

DragImageRef createDragImageForColor(const Color&, const FloatRect&, float, Path&)
{
    return nullptr;
}

}
