/*
 * Copyright (C) 2014-2023 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#import "config.h"
#import "_WKThumbnailViewInternal.h"

#if PLATFORM(MAC)

#import "ImageOptions.h"
#import "WKAPICast.h"
#import "WKView.h"
#import "WKViewInternal.h"
#import "WKWebViewInternal.h"
#import "WebPageProxy.h"
#import <WebCore/IOSurface.h>
#import <WebCore/ShareableBitmap.h>
#import <pal/spi/cg/CoreGraphicsSPI.h>
#import <wtf/MathExtras.h>
#import <wtf/NakedPtr.h>
#import <wtf/SystemTracing.h>

// FIXME: Make it possible to leave a snapshot of the content presented in the WKView while the thumbnail is live.
// FIXME: Don't make new speculative tiles while thumbnailed.
// FIXME: Hide scrollbars in the thumbnail.
// FIXME: We should re-use existing tiles for unparented views, if we have them (we need to know if they've been purged; if so, repaint at scaled-down size).
// FIXME: We should switch to the low-resolution scale if a view we have high-resolution tiles for repaints.

@implementation _WKThumbnailView {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    RetainPtr<WKView> _wkView;
ALLOW_DEPRECATED_DECLARATIONS_END
    RetainPtr<WKWebView> _wkWebView;
    WeakPtr<WebKit::WebPageProxy> _webPageProxy;

    BOOL _originalMayStartMediaWhenInWindow;
    BOOL _originalSourceViewIsInWindow;

    BOOL _snapshotWasDeferred;
    CGFloat _lastSnapshotScale;
    CGSize _lastSnapshotMaximumSize;

    RetainPtr<NSColor> _overrideBackgroundColor;
}

@synthesize _waitingForSnapshot;
@synthesize _sublayerTranslation;

- (instancetype)initWithFrame:(NSRect)frame
{
    if (!(self = [super initWithFrame:frame]))
        return nil;

    self.wantsLayer = YES;
    _scale = 1;
    _lastSnapshotScale = NAN;
    
    return self;
}

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
- (instancetype)initWithFrame:(NSRect)frame fromWKView:(WKView *)wkView
{
    if (!(self = [self initWithFrame:frame]))
        return nil;

    _wkView = wkView;
    _webPageProxy = WebKit::toImpl([_wkView pageRef]);
    _originalMayStartMediaWhenInWindow = _webPageProxy->mayStartMediaWhenInWindow();
    _originalSourceViewIsInWindow = !![_wkView window];

    return self;
}
ALLOW_DEPRECATED_DECLARATIONS_END

- (instancetype)initWithFrame:(NSRect)frame fromWKWebView:(WKWebView *)webView
{
    if (!(self = [self initWithFrame:frame]))
        return nil;
    
    _wkWebView = webView;
    _webPageProxy = [_wkWebView _page].get();
    _originalMayStartMediaWhenInWindow = _webPageProxy->mayStartMediaWhenInWindow();
    _originalSourceViewIsInWindow = !![_wkWebView window];
    
    return self;
}

- (Ref<WebKit::WebPageProxy>)_protectedWebPageProxy
{
    return *_webPageProxy;
}

- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)wantsUpdateLayer
{
    return YES;
}

- (void)updateLayer
{
    [super updateLayer];

    RetainPtr backgroundColor = self.overrideBackgroundColor ?: [NSColor quaternaryLabelColor];
    self.layer.backgroundColor = backgroundColor.get().CGColor;
}

- (void)requestSnapshot
{
    if (_waitingForSnapshot) {
        _snapshotWasDeferred = YES;
        return;
    }

    tracePoint(TakeSnapshotStart);
    _waitingForSnapshot = YES;

    RetainPtr<_WKThumbnailView> thumbnailView = self;
    Ref webPageProxy = *_webPageProxy;
    auto obscuredContentInsets = webPageProxy->obscuredContentInsets();
    WebCore::IntRect snapshotRect(WebCore::IntPoint(), webPageProxy->viewSize() - WebCore::IntSize {
        static_cast<int>(obscuredContentInsets.left()),
        static_cast<int>(obscuredContentInsets.top())
    });
    WebKit::SnapshotOptions options { WebKit::SnapshotOption::InViewCoordinates, WebKit::SnapshotOption::UseScreenColorSpace, WebKit::SnapshotOption::Accelerated, WebKit::SnapshotOption::AllowHDR };
    WebCore::IntSize bitmapSize = snapshotRect.size();
    bitmapSize.scale(_scale * webPageProxy->deviceScaleFactor());

    if (!CGSizeEqualToSize(_maximumSnapshotSize, CGSizeZero)) {
        double sizeConstraintScale = 1;
        if (_maximumSnapshotSize.width)
            sizeConstraintScale = CGFloatMin(sizeConstraintScale, _maximumSnapshotSize.width / bitmapSize.width());
        if (_maximumSnapshotSize.height)
            sizeConstraintScale = CGFloatMin(sizeConstraintScale, _maximumSnapshotSize.height / bitmapSize.height());
        bitmapSize = WebCore::IntSize(CGCeiling(bitmapSize.width() * sizeConstraintScale), CGCeiling(bitmapSize.height() * sizeConstraintScale));
    }

    _lastSnapshotScale = _scale;
    _lastSnapshotMaximumSize = _maximumSnapshotSize;
    webPageProxy->takeSnapshot(snapshotRect, bitmapSize, options, [thumbnailView](CGImageRef image) {
        if (!image)
            return;
        tracePoint(TakeSnapshotEnd, !!image);
        [thumbnailView _didTakeSnapshot:image];
    });
}

- (void)setOverrideBackgroundColor:(NSColor *)overrideBackgroundColor
{
    if ([_overrideBackgroundColor isEqual:overrideBackgroundColor])
        return;

    _overrideBackgroundColor = overrideBackgroundColor;
    [self setNeedsDisplay:YES];
}

- (NSColor *)overrideBackgroundColor
{
    return _overrideBackgroundColor.get();
}

- (void)_viewWasUnparented
{
    if (!_exclusivelyUsesSnapshot) {
        self._sublayerTranslation = CGPointMake(0, 0);
        if (_wkView) {
            [_wkView _setThumbnailView:nil];
            [_wkView _setIgnoresAllEvents:NO];
        } else {
            ASSERT(_wkWebView);
            [_wkWebView _setThumbnailView:nil];
            [_wkWebView _setIgnoresAllEvents:NO];
        }
        self._protectedWebPageProxy->setMayStartMediaWhenInWindow(_originalMayStartMediaWhenInWindow);
    }

    if (_shouldKeepSnapshotWhenRemovedFromSuperview)
        return;

    self.layer.contents = nil;
    _lastSnapshotScale = NAN;
}

- (void)_viewWasParented
{
    if (_wkView && [_wkView _thumbnailView])
        return;
    if (_wkWebView && [_wkWebView _thumbnailView])
        return;

    if (!_exclusivelyUsesSnapshot && !_originalSourceViewIsInWindow)
        self._protectedWebPageProxy->setMayStartMediaWhenInWindow(false);

    [self _requestSnapshotIfNeeded];

    if (!_exclusivelyUsesSnapshot) {
        auto obscuredContentInsets = RefPtr { _webPageProxy.get() }->obscuredContentInsets();
        self._sublayerTranslation = CGPointMake(-obscuredContentInsets.left(), -obscuredContentInsets.top());
        if (_wkView) {
            [_wkView _setThumbnailView:self];
            [_wkView _setIgnoresAllEvents:YES];
        } else {
            ASSERT(_wkWebView);
            [_wkWebView _setThumbnailView:self];
            [_wkWebView _setIgnoresAllEvents:YES];
        }
    }
}

- (void)_requestSnapshotIfNeeded
{
    if (self.layer.contents && _lastSnapshotScale == _scale && CGSizeEqualToSize(_lastSnapshotMaximumSize, _maximumSnapshotSize))
        return;

    [self requestSnapshot];
}

- (void)_didTakeSnapshot:(CGImageRef)image
{
    [self willChangeValueForKey:@"snapshotSize"];

    _snapshotSize = CGSizeMake(CGImageGetWidth(image), CGImageGetHeight(image));
    _waitingForSnapshot = NO;
    self.layer.sublayers = @[];
    self.layer.contentsGravity = kCAGravityResizeAspectFill;
    self.layer.contents = (__bridge id)image;
#if HAVE(SUPPORT_HDR_DISPLAY_APIS)
    if (CGImageGetContentHeadroom(image) > 1) {
        self.layer.toneMapMode = CAToneMapModeIfSupported;
        self.layer.preferredDynamicRange = CADynamicRangeHigh;
    } else {
        self.layer.toneMapMode = CAToneMapModeAutomatic;
        self.layer.preferredDynamicRange = CADynamicRangeAutomatic;
    }
#endif

    // If we got a scale change while snapshotting, we'll take another snapshot once the first one returns.
    if (_snapshotWasDeferred) {
        _snapshotWasDeferred = NO;
        [self _requestSnapshotIfNeeded];
    }

    [self didChangeValueForKey:@"snapshotSize"];
}

- (void)viewDidMoveToWindow
{
    if (self.window)
        [self _viewWasParented];
    else
        [self _viewWasUnparented];
}

- (void)setScale:(CGFloat)scale
{
    if (_scale == scale)
        return;

    _scale = scale;

    [self _requestSnapshotIfNeeded];

    auto scaleTransform = CATransform3DMakeScale(_scale, _scale, 1);
    self.layer.sublayerTransform = CATransform3DTranslate(scaleTransform, _sublayerTranslation.x, _sublayerTranslation.y, 0);
}

- (void)_setSublayerTranslation:(CGPoint)translation
{
    if (WTF::areEssentiallyEqual(_sublayerTranslation.x, translation.x) && WTF::areEssentiallyEqual(_sublayerTranslation.y, translation.y))
        return;

    self.layer.sublayerTransform = CATransform3DTranslate(self.layer.sublayerTransform, translation.x - _sublayerTranslation.x, translation.y - _sublayerTranslation.y, 0);
    _sublayerTranslation = translation;
}

- (void)setMaximumSnapshotSize:(CGSize)maximumSnapshotSize
{
    if (CGSizeEqualToSize(_maximumSnapshotSize, maximumSnapshotSize))
        return;

    _maximumSnapshotSize = maximumSnapshotSize;

    [self _requestSnapshotIfNeeded];
}

- (void)_setThumbnailLayer:(CALayer *)layer
{
    self.layer.sublayers = layer ? @[ layer ] : @[ ];
}

- (CALayer *)_thumbnailLayer
{
    if (!self.layer.sublayers.count)
        return nil;

    return [self.layer.sublayers objectAtIndex:0];
}

@end

#endif // PLATFORM(MAC)
