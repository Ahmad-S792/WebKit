/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009 Apple Inc. All rights reserved.
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
#include "RenderView.h"

#include "ContainerNodeInlines.h"
#include "Document.h"
#include "Element.h"
#include "FloatQuad.h"
#include "GraphicsContext.h"
#include "HTMLBodyElement.h"
#include "HTMLFrameOwnerElement.h"
#include "HTMLFrameSetElement.h"
#include "HTMLHtmlElement.h"
#include "HTMLIFrameElement.h"
#include "HTMLImageElement.h"
#include "HitTestResult.h"
#include "ImageQualityController.h"
#include "LayoutBoxGeometry.h"
#include "LayoutInitialContainingBlock.h"
#include "LayoutIntegrationFormattingContextLayout.h"
#include "LayoutState.h"
#include "LegacyRenderSVGRoot.h"
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "Logging.h"
#include "NodeInlines.h"
#include "NodeTraversal.h"
#include "Page.h"
#include "RenderBoxInlines.h"
#include "RenderBoxModelObjectInlines.h"
#include "RenderCounter.h"
#include "RenderDescendantIterator.h"
#include "RenderElementInlines.h"
#include "RenderGeometryMap.h"
#include "RenderImage.h"
#include "RenderIterator.h"
#include "RenderLayerBacking.h"
#include "RenderLayerCompositor.h"
#include "RenderLayerInlines.h"
#include "RenderLayoutState.h"
#include "RenderMultiColumnFlow.h"
#include "RenderMultiColumnSet.h"
#include "RenderMultiColumnSpannerPlaceholder.h"
#include "RenderQuote.h"
#include "RenderSVGRoot.h"
#include "RenderStyleInlines.h"
#include "RenderTreeBuilder.h"
#include "RenderWidget.h"
#include "SVGElementTypeHelpers.h"
#include "SVGImage.h"
#include "SVGSVGElement.h"
#include "Settings.h"
#include "StyleInheritedData.h"
#include "TransformState.h"
#include <wtf/SetForScope.h>
#include <wtf/StackStats.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(RenderView);

RenderView::RenderView(Document& document, RenderStyle&& style)
    : RenderBlockFlow(Type::View, document, WTFMove(style))
    , m_frameView(*document.view())
    , m_initialContainingBlock(makeUniqueRef<Layout::InitialContainingBlock>(RenderStyle::clone(this->style())))
    , m_layoutState(makeUniqueRef<Layout::LayoutState>(document, m_initialContainingBlock, Layout::LayoutState::Type::Primary, LayoutIntegration::layoutWithFormattingContextForBox, LayoutIntegration::formattingContextRootLogicalWidthForType, LayoutIntegration::formattingContextRootLogicalHeightForType))
    , m_selection(*this)
{
    // FIXME: We should find a way to enforce this at compile time.
    ASSERT(document.view());

    // init RenderObject attributes
    setInline(false);
    
    m_minPreferredLogicalWidth = 0;
    m_maxPreferredLogicalWidth = 0;

    setNeedsPreferredWidthsUpdate(MarkOnlyThis);
    
    setPositionState(PositionType::Absolute); // to 0,0 :)

    ASSERT(isRenderView());
}

RenderView::~RenderView()
{
    ASSERT_WITH_MESSAGE(!m_rendererCount, "All renderers should be in the process of being deleted.");

    deleteLines();
}

void RenderView::styleDidChange(StyleDifference diff, const RenderStyle* oldStyle)
{
    RenderBlockFlow::styleDidChange(diff, oldStyle);

    if (!oldStyle)
        return;

    bool writingModeChanged = writingMode().computedWritingMode() != oldStyle->writingMode().computedWritingMode();
    bool directionChanged = writingMode().bidiDirection() != oldStyle->writingMode().bidiDirection();

    if ((writingModeChanged || directionChanged) && multiColumnFlow()) {
        if (frameView().pagination().mode != Pagination::Mode::Unpaginated)
            updateColumnProgressionFromStyle(style());
        updateStylesForColumnChildren(oldStyle);
    }

    if (directionChanged)
        frameView().topContentDirectionDidChange();
}

RenderBox::LogicalExtentComputedValues RenderView::computeLogicalHeight(LayoutUnit logicalHeight, LayoutUnit) const
{
    return { !shouldUsePrintingLayout() ? LayoutUnit(viewLogicalHeight()) : logicalHeight, 0_lu, ComputedMarginValues() };
}

inline int RenderView::viewLogicalWidth() const
{
    return writingMode().isHorizontal() ? viewWidth() : viewHeight();
}

void RenderView::updateLogicalWidth()
{
    setLogicalWidth(shouldUsePrintingLayout() ? m_pageLogicalSize->width() : LayoutUnit(viewLogicalWidth()));
}

LayoutUnit RenderView::availableLogicalHeight(AvailableLogicalHeightType) const
{
    // Make sure block progression pagination for percentages uses the column extent and
    // not the view's extent. See https://bugs.webkit.org/show_bug.cgi?id=135204.
    if (multiColumnFlow() && multiColumnFlow()->firstMultiColumnSet())
        return multiColumnFlow()->firstMultiColumnSet()->computedColumnHeight();

    Ref frameView = this->frameView();
#if PLATFORM(IOS_FAMILY)
    // Workaround for <rdar://problem/7166808>.
    if (document().isPluginDocument() && frameView->useFixedLayout())
        return frameView->fixedLayoutSize().height();
#endif
    return isHorizontalWritingMode() ? frameView->layoutSize().height() : frameView->layoutSize().width();
}

bool RenderView::isChildAllowed(const RenderObject& child, const RenderStyle&) const
{
    return child.isRenderBox();
}

void RenderView::layout()
{
    StackStats::LayoutCheckPoint layoutCheckPoint;
    if (!document().paginated())
        m_pageLogicalSize = { };

    if (shouldUsePrintingLayout()) {
        if (!m_pageLogicalSize)
            m_pageLogicalSize = LayoutSize(logicalWidth(), 0_lu);
        m_minPreferredLogicalWidth = m_pageLogicalSize->width();
        m_maxPreferredLogicalWidth = m_minPreferredLogicalWidth;
    }

    // Use calcWidth/Height to get the new width/height, since this will take the full page zoom factor into account.
    bool relayoutChildren = !shouldUsePrintingLayout() && (width() != viewWidth() || height() != viewHeight());
    if (relayoutChildren) {
        setChildNeedsLayout(MarkOnlyThis);

        for (auto& box : childrenOfType<RenderBox>(*this)) {
            if (box.hasRelativeLogicalHeight()
                || box.style().logicalHeight().isPercentOrCalculated()
                || box.style().logicalMinHeight().isPercentOrCalculated()
                || box.style().logicalMaxHeight().isPercentOrCalculated()
                || box.isRenderOrLegacyRenderSVGRoot()
                )
                box.setChildNeedsLayout(MarkOnlyThis);
        }
    }

    ASSERT(!frameView().layoutContext().layoutState());
    if (!needsLayout())
        return;

    LayoutStateMaintainer statePusher(*this, { }, false, valueOrDefault(m_pageLogicalSize).height(), m_pageLogicalHeightChanged);

    m_pageLogicalHeightChanged = false;

    // FIXME: This should be called only when frame view (or the canvas we render onto) size changes.
    updateInitialContainingBlockSize();
    RenderBlockFlow::layout();

#ifndef NDEBUG
    frameView().layoutContext().checkLayoutState();
#endif
}

void RenderView::updateQuirksMode()
{
    m_layoutState->updateQuirksMode(protectedDocument());
}

void RenderView::updateInitialContainingBlockSize()
{
    // Initial containing block has no margin/padding/border.
    m_layoutState->ensureGeometryForBox(m_initialContainingBlock).setContentBoxSize(frameView().size());
}

LayoutUnit RenderView::pageOrViewLogicalHeight() const
{
    if (shouldUsePrintingLayout())
        return m_pageLogicalSize->height();
    
    if (multiColumnFlow() && !style().hasInlineColumnAxis()) {
        if (int pageLength = frameView().pagination().pageLength)
            return pageLength;
    }

    return viewLogicalHeight();
}

LayoutUnit RenderView::clientLogicalWidthForFixedPosition() const
{
    Ref frameView = this->frameView();
    if (frameView->fixedElementsLayoutRelativeToFrame())
        return LayoutUnit((isHorizontalWritingMode() ? frameView->visibleWidth() : frameView->visibleHeight()) / frameView->protectedFrame()->frameScaleFactor());

#if PLATFORM(IOS_FAMILY)
    if (frameView->useCustomFixedPositionLayoutRect())
        return isHorizontalWritingMode() ? frameView->customFixedPositionLayoutRect().width() : frameView->customFixedPositionLayoutRect().height();
#endif

    if (settings().visualViewportEnabled())
        return isHorizontalWritingMode() ? frameView->layoutViewportRect().width() : frameView->layoutViewportRect().height();

    return clientLogicalWidth();
}

LayoutUnit RenderView::clientLogicalHeightForFixedPosition() const
{
    Ref frameView = this->frameView();
    if (frameView->fixedElementsLayoutRelativeToFrame())
        return LayoutUnit((isHorizontalWritingMode() ? frameView->visibleHeight() : frameView->visibleWidth()) / frameView->protectedFrame()->frameScaleFactor());

#if PLATFORM(IOS_FAMILY)
    if (frameView->useCustomFixedPositionLayoutRect())
        return isHorizontalWritingMode() ? frameView->customFixedPositionLayoutRect().height() : frameView->customFixedPositionLayoutRect().width();
#endif

    if (settings().visualViewportEnabled())
        return isHorizontalWritingMode() ? frameView->layoutViewportRect().height() : frameView->layoutViewportRect().width();

    return clientLogicalHeight();
}

void RenderView::mapLocalToContainer(const RenderLayerModelObject* ancestorContainer, TransformState& transformState, OptionSet<MapCoordinatesMode> mode, bool* wasFixed) const
{
    // If a container was specified, and was not nullptr or the RenderView,
    // then we should have found it by now.
    ASSERT_ARG(ancestorContainer, !ancestorContainer || ancestorContainer == this);
    ASSERT_UNUSED(wasFixed, !wasFixed || *wasFixed == (mode.contains(IsFixed)));

    if (mode.contains(IsFixed))
        transformState.move(toLayoutSize(frameView().scrollPositionRespectingCustomFixedPosition()));

    if (!ancestorContainer && mode.contains(UseTransforms) && shouldUseTransformFromContainer(nullptr)) {
        TransformationMatrix t;
        getTransformFromContainer(LayoutSize(), t);
        transformState.applyTransform(t);
    }
}

const RenderElement* RenderView::pushMappingToContainer(const RenderLayerModelObject* ancestorToStopAt, RenderGeometryMap& geometryMap) const
{
    // If a container was specified, and was not nullptr or the RenderView,
    // then we should have found it by now.
    ASSERT_ARG(ancestorToStopAt, !ancestorToStopAt || ancestorToStopAt == this);

    LayoutPoint scrollPosition = frameView().scrollPositionRespectingCustomFixedPosition();

    if (!ancestorToStopAt && shouldUseTransformFromContainer(nullptr)) {
        TransformationMatrix t;
        getTransformFromContainer(LayoutSize(), t);
        geometryMap.pushView(this, toLayoutSize(scrollPosition), &t);
    } else
        geometryMap.pushView(this, toLayoutSize(scrollPosition));

    return nullptr;
}

void RenderView::mapAbsoluteToLocalPoint(OptionSet<MapCoordinatesMode> mode, TransformState& transformState) const
{
    if (mode & UseTransforms && shouldUseTransformFromContainer(nullptr)) {
        TransformationMatrix t;
        getTransformFromContainer(LayoutSize(), t);
        transformState.applyTransform(t);
    }

    if (mode & IsFixed)
        transformState.move(toLayoutSize(frameView().scrollPositionRespectingCustomFixedPosition()));
}

bool RenderView::requiresColumns(int) const
{
    return frameView().pagination().mode != Pagination::Mode::Unpaginated;
}

void RenderView::computeColumnCountAndWidth()
{
    int columnWidth = contentBoxLogicalWidth();
    if (style().hasInlineColumnAxis()) {
        if (int pageLength = frameView().pagination().pageLength)
            columnWidth = pageLength;
    }
    setComputedColumnCountAndWidth(1, columnWidth);
}

void RenderView::paint(PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    // If we ever require layout but receive a paint anyway, something has gone horribly wrong.
    ASSERT(!needsLayout());
    // RenderViews should never be called to paint with an offset not on device pixels.
    ASSERT(LayoutPoint(IntPoint(paintOffset.x(), paintOffset.y())) == paintOffset);

    // This avoids painting garbage between columns if there is a column gap.
    Ref frameView = this->frameView();
    if (frameView->pagination().mode != Pagination::Mode::Unpaginated && paintInfo.shouldPaintWithinRoot(*this))
        paintInfo.context().fillRect(paintInfo.rect, frameView->baseBackgroundColor());

    paintObject(paintInfo, paintOffset);
}

RenderElement* RenderView::rendererForRootBackground() const
{
    auto* firstChild = this->firstChild();
    if (!firstChild)
        return nullptr;

    auto& documentRenderer = downcast<RenderElement>(*firstChild);
    if (documentRenderer.hasBackground())
        return &documentRenderer;

    // We propagate the background only for HTML content.
    if (!is<HTMLHtmlElement>(documentRenderer.element()))
        return &documentRenderer;

    if (documentRenderer.shouldApplyAnyContainment())
        return nullptr;

    if (RefPtr body = protectedDocument()->body()) {
        if (auto* renderer = body->renderer()) {
            if (!renderer->shouldApplyAnyContainment())
                return renderer;
        }
    }
    return &documentRenderer;
}

static inline bool rendererObscuresBackground(const RenderElement& rootElement)
{
    auto& style = rootElement.style();
    if (style.usedVisibility() != Visibility::Visible || !style.opacity().isOpaque() || style.hasTransform())
        return false;

    if (style.hasBorderRadius())
        return false;

    if (rootElement.isComposited())
        return false;

    if (rootElement.hasClipPath() && rootElement.isRenderOrLegacyRenderSVGRoot())
        return false;

    auto* rendererForBackground = rootElement.view().rendererForRootBackground();
    if (!rendererForBackground)
        return false;

    if (rendererForBackground->style().backgroundClip() == FillBox::Text)
        return false;

    return true;
}

void RenderView::paintBoxDecorations(PaintInfo& paintInfo, const LayoutPoint&)
{
    if (!paintInfo.shouldPaintWithinRoot(*this))
        return;

    // Check to see if we are enclosed by a layer that requires complex painting rules.  If so, we cannot blit
    // when scrolling, and we need to use slow repaints.  Examples of layers that require this are transparent layers,
    // layers with reflections, or transformed layers.
    // FIXME: This needs to be dynamic.  We should be able to go back to blitting if we ever stop being inside
    // a transform, transparency layer, etc.
    Ref document = this->document();
    for (RefPtr element = document->ownerElement(); element && element->renderer(); element = element->protectedDocument()->ownerElement()) {
        RenderLayer* layer = element->renderer()->enclosingLayer();
        if (layer->cannotBlitToWindow()) {
            frameView().setCannotBlitToWindow();
            break;
        }

        if (auto* compositingLayer = layer->enclosingCompositingLayerForRepaint().layer) {
            if (!compositingLayer->backing()->paintsIntoWindow()) {
                frameView().setCannotBlitToWindow();
                break;
            }
        }
    }

    if (!shouldPaintBaseBackground())
        return;

    if (paintInfo.skipRootBackground())
        return;

    bool rootFillsViewport = false;
    bool rootObscuresBackground = false;
    auto shouldPropagateBackgroundPaintingToInitialContainingBlock = true;
    RefPtr documentElement = document->documentElement();
    if (RenderElement* rootRenderer = documentElement ? documentElement->renderer() : nullptr) {
        // The document element's renderer is currently forced to be a block, but may not always be.
        auto* rootBox = dynamicDowncast<RenderBox>(*rootRenderer);
        rootFillsViewport = rootBox && !rootBox->x() && !rootBox->y() && rootBox->width() >= width() && rootBox->height() >= height();
        rootObscuresBackground = rendererObscuresBackground(*rootRenderer);
        shouldPropagateBackgroundPaintingToInitialContainingBlock = !!rendererForRootBackground();
    }

    compositor().rootBackgroundColorOrTransparencyChanged();

    RefPtr page = document->page();
    float pageScaleFactor = page ? page->pageScaleFactor() : 1;

    // If painting will entirely fill the view, no need to fill the background.
    if (rootFillsViewport && rootObscuresBackground && pageScaleFactor >= 1 && rootElementShouldPaintBaseBackground())
        return;

    // This code typically only executes if the root element's visibility has been set to hidden,
    // if there is a transform on the <html>, or if there is a page scale factor less than 1.
    // Only fill with a background color (typically white) if we're the root document, 
    // since iframes/frames with no background in the child document should show the parent's background.
    // We use the base background color unless the backgroundShouldExtendBeyondPage setting is set,
    // in which case we use the document's background color.
    Ref frameView = this->frameView();
    if (frameView->isTransparent()) // FIXME: This needs to be dynamic. We should be able to go back to blitting if we ever stop being transparent.
        frameView->setCannotBlitToWindow(); // The parent must show behind the child.
    else {
        const Color& documentBackgroundColor = frameView->documentBackgroundColor();
        const Color& backgroundColor = (shouldPropagateBackgroundPaintingToInitialContainingBlock && settings().backgroundShouldExtendBeyondPage() && documentBackgroundColor.isValid()) ? documentBackgroundColor : frameView->baseBackgroundColor();
        if (backgroundColor.isVisible()) {
            CompositeOperator previousOperator = paintInfo.context().compositeOperation();
            paintInfo.context().setCompositeOperation(CompositeOperator::Copy);
            paintInfo.context().fillRect(paintInfo.rect, backgroundColor);
            paintInfo.context().setCompositeOperation(previousOperator);
        } else
            paintInfo.context().clearRect(paintInfo.rect);
    }
}

bool RenderView::shouldRepaint(const LayoutRect& rect) const
{
    return !printing() && !rect.isEmpty();
}

void RenderView::repaintRootContents()
{
    if (layer()->isComposited()) {
        layer()->setBackingNeedsRepaint(GraphicsLayer::DoNotClipToLayer);
        return;
    }

    // Always use layoutOverflowRect() to fix rdar://problem/27182267.
    // This should be cleaned up via webkit.org/b/159913 and webkit.org/b/159914.
    CheckedPtr repaintContainer = containerForRepaint().renderer;
    repaintUsingContainer(repaintContainer.get(), computeRectForRepaint(layoutOverflowRect(), repaintContainer.get()));
}

void RenderView::repaintViewRectangle(const LayoutRect& repaintRect) const
{
    if (!shouldRepaint(repaintRect))
        return;

    // FIXME: enclosingRect is needed as long as we integral snap ScrollView/FrameView/RenderWidget size/position.
    auto enclosingRect = enclosingIntRect(repaintRect);
    Ref document = this->document();
    if (RefPtr ownerElement = document->ownerElement()) {
        auto* ownerBox = ownerElement->renderBox();
        if (!ownerBox)
            return;

        auto viewRect = LayoutRect { this->viewRect() };
#if PLATFORM(IOS_FAMILY)
        // Don't clip using the visible rect since clipping is handled at a higher level on iPhone.
        // FIXME: This statement is wrong for iframes.
        LayoutRect adjustedRect = enclosingRect;
#else
        LayoutRect adjustedRect = intersection(enclosingRect, viewRect);
#endif
        if (adjustedRect.isEmpty())
            return;

        if (adjustedRect == viewRect) {
            // We know this RenderView isn't composited here, which means it has no composited descendants, so it's OK to trigger `setNeedsFullRepaint`
            // which would otherwise force all compositing layers to repaint.
            ASSERT(!isComposited());
            frameView().layoutContext().setNeedsFullRepaint();
        }

        adjustedRect.moveBy(-viewRect.location());
        adjustedRect.moveBy(ownerBox->contentBoxRect().location());

        // A dirty rect in an iframe is relative to the contents of that iframe.
        // When we traverse between parent frames and child frames, we need to make sure
        // that the coordinate system is mapped appropriately between the iframe's contents
        // and the Renderer that contains the iframe. This transformation must account for a
        // left scrollbar (if one exists).
        Ref frameView = this->frameView();
        if (frameView->verticalScrollbar() && frameView->shouldPlaceVerticalScrollbarOnLeft())
            adjustedRect.move(LayoutSize(frameView->protectedVerticalScrollbar()->occupiedWidth(), 0));

        ownerBox->repaintRectangle(adjustedRect);
        return;
    }

    frameView().addTrackedRepaintRect(snapRectToDevicePixels(repaintRect, document->deviceScaleFactor()));
    if (!m_accumulatedRepaintRegion) {
        frameView().repaintContentRectangle(enclosingRect);
        return;
    }
    m_accumulatedRepaintRegion->unite(enclosingRect);

    // Region will get slow if it gets too complex. Merge all rects so far to bounds if this happens.
    // FIXME: Maybe there should be a region type that does this automatically.
    static const unsigned maximumRepaintRegionGridSize = 16 * 16;
    if (m_accumulatedRepaintRegion->gridSize() > maximumRepaintRegionGridSize)
        m_accumulatedRepaintRegion = makeUnique<Region>(m_accumulatedRepaintRegion->bounds());
}

void RenderView::flushAccumulatedRepaintRegion() const
{
    ASSERT(m_accumulatedRepaintRegion);
    auto repaintRects = m_accumulatedRepaintRegion->rects();
    for (auto& rect : repaintRects)
        frameView().repaintContentRectangle(rect);
    m_accumulatedRepaintRegion = nullptr;
}

void RenderView::repaintViewAndCompositedLayers()
{
    repaintRootContents();

    RenderLayerCompositor& compositor = this->compositor();
    if (compositor.usesCompositing())
        compositor.repaintCompositedLayers();
}

auto RenderView::computeVisibleRectsInContainer(const RepaintRects& rects, const RenderLayerModelObject* container, VisibleRectContext context) const -> std::optional<RepaintRects>
{
    // If a container was specified, and was not nullptr or the RenderView,
    // then we should have found it by now.
    ASSERT_ARG(container, !container || container == this);

    if (printing())
        return rects;

    auto adjustedRects = rects;
    if (writingMode().isBlockFlipped()) {
        // We have to flip by hand since the view's logical height has not been determined.  We
        // can use the viewport width and height.
        adjustedRects.flipForWritingMode(LayoutSize(viewWidth(), viewHeight()), writingMode().isHorizontal());
    }

    if (context.hasPositionFixedDescendant)
        adjustedRects.moveBy(frameView().scrollPositionRespectingCustomFixedPosition());

    // Apply our transform if we have one (because of full page zooming).
    if (!container && hasLayer() && layer()->transform())
        adjustedRects.transform(*layer()->transform(), protectedDocument()->deviceScaleFactor());

    return adjustedRects;
}

bool RenderView::isScrollableOrRubberbandableBox() const
{
    // The main frame might be allowed to rubber-band even if there is no content to scroll to. This is unique to
    // the main frame; subframes and overflow areas have to have content that can be scrolled to in order to rubber-band.
    LocalFrameView::Scrollability defineScrollable = frame().ownerElement() ? LocalFrameView::Scrollability::Scrollable : LocalFrameView::Scrollability::ScrollableOrRubberbandable;
    return frameView().isScrollable(defineScrollable);
}

void RenderView::boundingRects(Vector<LayoutRect>& rects, const LayoutPoint& accumulatedOffset) const
{
    // FIXME: It's weird that this gets is size from the layer.
    rects.append(LayoutRect { accumulatedOffset, layer()->size() });
}

void RenderView::absoluteQuads(Vector<FloatQuad>& quads, bool* wasFixed) const
{
    if (wasFixed)
        *wasFixed = false;
    quads.append(FloatRect(FloatPoint(), layer()->size()));
}

bool RenderView::printing() const
{
    return document().printing();
}

bool RenderView::shouldUsePrintingLayout() const
{
    if (!printing())
        return false;
    return frameView().protectedFrame()->shouldUsePrintingLayout();
}

LayoutRect RenderView::viewRect() const
{
    if (shouldUsePrintingLayout())
        return LayoutRect(LayoutPoint(), size());
    return frameView().visibleContentRect(ScrollableArea::LegacyIOSDocumentVisibleRect);
}

IntRect RenderView::unscaledDocumentRect() const
{
    LayoutRect overflowRect(layoutOverflowRect());
    flipForWritingMode(overflowRect);
    return snappedIntRect(overflowRect);
}

bool RenderView::rootBackgroundIsEntirelyFixed() const
{
    if (auto* rootBackgroundRenderer = rendererForRootBackground())
        return rootBackgroundRenderer->style().hasEntirelyFixedBackground();
    return false;
}

bool RenderView::shouldPaintBaseBackground() const
{
    Ref document = this->document();
    Ref frameView = this->frameView();
    RefPtr ownerElement = document->ownerElement();

    // Fill with a base color if we're the root document.
    if (frameView->frame().isMainFrame())
        return !frameView->isTransparent();

    if (ownerElement && ownerElement->hasTagName(HTMLNames::frameTag))
        return true;

    // Locate the <body> element using the DOM. This is easier than trying
    // to crawl around a render tree with potential :before/:after content and
    // anonymous blocks created by inline <body> tags etc. We can locate the <body>
    // render object very easily via the DOM.
    RefPtr body = document->bodyOrFrameset();

    // SVG documents and XML documents with SVG root nodes are transparent.
    if (!body)
        return !document->hasSVGRootNode();

    // Can't scroll a frameset document anyway.
    if (is<HTMLFrameSetElement>(*body))
        return true;

    auto* frameRenderer = ownerElement ? ownerElement->renderer() : nullptr;
    if (!frameRenderer)
        return false;

    // iframes should fill with a base color if the used color scheme of the
    // element and the used color scheme of the embedded document’s root
    // element do not match.
    if (frameView->useDarkAppearance() != frameRenderer->useDarkAppearance())
        return !frameView->isTransparent();

    return false;
}

bool RenderView::rootElementShouldPaintBaseBackground() const
{
    RefPtr documentElement = document().documentElement();
    if (RenderElement* rootRenderer = documentElement ? documentElement->renderer() : nullptr) {
        // The document element's renderer is currently forced to be a block, but may not always be.
        auto* rootBox = dynamicDowncast<RenderBox>(*rootRenderer);
        if (rootBox && rootBox->hasLayer()) {
            RenderLayer* layer = rootBox->layer();
            if (layer->isolatesBlending() || layer->isBackdropRoot())
                return false;
        }
    }
    return shouldPaintBaseBackground();
}
    
LayoutRect RenderView::unextendedBackgroundRect() const
{
    // FIXME: What is this? Need to patch for new columns?
    return unscaledDocumentRect();
}
    
LayoutRect RenderView::backgroundRect() const
{
    // FIXME: New columns care about this?
    Ref frameView = this->frameView();
    if (frameView->hasExtendedBackgroundRectForPainting())
        return frameView->extendedBackgroundRectForPainting();

    return unextendedBackgroundRect();
}

IntRect RenderView::documentRect() const
{
    FloatRect overflowRect(unscaledDocumentRect());
    if (isTransformed())
        overflowRect = layer()->currentTransform().mapRect(overflowRect);
    return IntRect(overflowRect);
}

int RenderView::viewHeight() const
{
    int height = 0;
    if (!shouldUsePrintingLayout()) {
        Ref frameView = this->frameView();
        height = frameView->layoutHeight();
        height = frameView->useFixedLayout() ? ceilf(style().usedZoom() * float(height)) : height;
    }
    return height;
}

int RenderView::viewWidth() const
{
    int width = 0;
    if (!shouldUsePrintingLayout()) {
        Ref frameView = this->frameView();
        width = frameView->layoutWidth();
        width = frameView->useFixedLayout() ? ceilf(style().usedZoom() * float(width)) : width;
    }
    return width;
}

int RenderView::viewLogicalHeight() const
{
    int height = writingMode().isHorizontal() ? viewHeight() : viewWidth();
    return height;
}

void RenderView::setPageLogicalSize(LayoutSize size)
{
    if (!m_pageLogicalSize || m_pageLogicalSize->height() != size.height())
        m_pageLogicalHeightChanged = true;

    m_pageLogicalSize = size;
}

float RenderView::zoomFactor() const
{
    return frameView().frame().pageZoomFactor();
}

FloatSize RenderView::sizeForCSSSmallViewportUnits() const
{
    return frameView().sizeForCSSSmallViewportUnits();
}

FloatSize RenderView::sizeForCSSLargeViewportUnits() const
{
    return frameView().sizeForCSSLargeViewportUnits();
}

FloatSize RenderView::sizeForCSSDynamicViewportUnits() const
{
    return frameView().sizeForCSSDynamicViewportUnits();
}

FloatSize RenderView::sizeForCSSDefaultViewportUnits() const
{
    return frameView().sizeForCSSDefaultViewportUnits();
}

Node* RenderView::nodeForHitTest() const
{
    return document().documentElement();
}

void RenderView::updateHitTestResult(HitTestResult& result, const LayoutPoint& point) const
{
    if (result.innerNode())
        return;

    if (multiColumnFlow() && multiColumnFlow()->firstMultiColumnSet())
        return multiColumnFlow()->firstMultiColumnSet()->updateHitTestResult(result, point);

    if (RefPtr node = nodeForHitTest()) {
        result.setInnerNode(node.get());
        if (!result.innerNonSharedNode())
            result.setInnerNonSharedNode(node.get());

        LayoutPoint adjustedPoint = point;
        offsetForContents(adjustedPoint);

        result.setLocalPoint(adjustedPoint);
    }
}

// FIXME: This function is obsolete and only used by embedded WebViews inside AppKit NSViews.
// Do not add callers of this function!
// The idea here is to take into account what object is moving the pagination point, and
// thus choose the best place to chop it.
void RenderView::setBestTruncatedAt(int y, RenderBoxModelObject* forRenderer, bool forcedBreak)
{
    // Nobody else can set a page break once we have a forced break.
    if (m_legacyPrinting.m_forcedPageBreak)
        return;

    // Forced breaks always win over unforced breaks.
    if (forcedBreak) {
        m_legacyPrinting.m_forcedPageBreak = true;
        m_legacyPrinting.m_bestTruncatedAt = y;
        return;
    }

    // Prefer the widest object that tries to move the pagination point
    LayoutRect boundingBox = forRenderer->borderBoundingBox();
    if (boundingBox.width() > m_legacyPrinting.m_truncatorWidth) {
        m_legacyPrinting.m_truncatorWidth = boundingBox.width();
        m_legacyPrinting.m_bestTruncatedAt = y;
    }
}

bool RenderView::usesCompositing() const
{
    return m_compositor && m_compositor->usesCompositing();
}

RenderLayerCompositor& RenderView::compositor()
{
    if (!m_compositor)
        m_compositor = makeUnique<RenderLayerCompositor>(*this);

    return *m_compositor;
}

void RenderView::setIsInWindow(bool isInWindow)
{
    if (m_compositor)
        m_compositor->setIsInWindow(isInWindow);
}

ImageQualityController& RenderView::imageQualityController()
{
    if (!m_imageQualityController)
        m_imageQualityController = makeUnique<ImageQualityController>(*this);
    return *m_imageQualityController;
}

void RenderView::registerForVisibleInViewportCallback(RenderElement& renderer)
{
    ASSERT(!m_visibleInViewportRenderers.contains(renderer));
    m_visibleInViewportRenderers.add(renderer);
}

void RenderView::unregisterForVisibleInViewportCallback(RenderElement& renderer)
{
    ASSERT(m_visibleInViewportRenderers.contains(renderer));
    m_visibleInViewportRenderers.remove(renderer);
}

void RenderView::updateVisibleViewportRect(const IntRect& visibleRect)
{
    resumePausedImageAnimationsIfNeeded(visibleRect);

    for (auto& renderer : m_visibleInViewportRenderers) {
        auto state = visibleRect.intersects(enclosingIntRect(renderer.absoluteClippedOverflowRectForRepaint())) ? VisibleInViewportState::Yes : VisibleInViewportState::No;
        renderer.setVisibleInViewportState(state);
    }
}

void RenderView::addRendererWithPausedImageAnimations(RenderElement& renderer, CachedImage& image)
{
    ASSERT(!renderer.hasPausedImageAnimations() || m_renderersWithPausedImageAnimation.contains(renderer));

    renderer.setHasPausedImageAnimations(true);
    auto& images = m_renderersWithPausedImageAnimation.ensure(renderer, [] {
        return Vector<WeakPtr<CachedImage>>();
    }).iterator->value;
    if (!images.contains(&image))
        images.append(image);
}

void RenderView::removeRendererWithPausedImageAnimations(RenderElement& renderer)
{
    ASSERT(renderer.hasPausedImageAnimations());
    ASSERT(m_renderersWithPausedImageAnimation.contains(renderer));

    renderer.setHasPausedImageAnimations(false);
    m_renderersWithPausedImageAnimation.remove(renderer);
}

void RenderView::removeRendererWithPausedImageAnimations(RenderElement& renderer, CachedImage& image)
{
    ASSERT(renderer.hasPausedImageAnimations());

    auto it = m_renderersWithPausedImageAnimation.find(renderer);
    ASSERT(it != m_renderersWithPausedImageAnimation.end());

    auto& images = it->value;
    if (!images.contains(&image))
        return;

    if (images.size() == 1)
        removeRendererWithPausedImageAnimations(renderer);
    else
        images.removeFirst(&image);
}

void RenderView::resumePausedImageAnimationsIfNeeded(const IntRect& visibleRect)
{
    Vector<std::pair<SingleThreadWeakPtr<RenderElement>, WeakPtr<CachedImage>>, 10> toRemove;
    for (auto it : m_renderersWithPausedImageAnimation) {
        auto& renderer = it.key;
        for (auto& image : it.value) {
            if (renderer.repaintForPausedImageAnimationsIfNeeded(visibleRect, *image))
                toRemove.append({ WeakPtr { renderer }, image });
        }
    }
    for (auto& pair : toRemove)
        removeRendererWithPausedImageAnimations(*pair.first, *pair.second);

    Vector<Ref<SVGSVGElement>> svgSvgElementsToRemove;
    m_SVGSVGElementsWithPausedImageAnimation.forEach([&] (WeakPtr<SVGSVGElement, WeakPtrImplWithEventTargetData> svgSvgElement) {
        if (svgSvgElement && svgSvgElement->resumePausedAnimationsIfNeeded(visibleRect))
            svgSvgElementsToRemove.append(*svgSvgElement);
    });
    for (auto& svgSvgElement : svgSvgElementsToRemove)
        m_SVGSVGElementsWithPausedImageAnimation.remove(svgSvgElement.get());
}

#if ENABLE(ACCESSIBILITY_ANIMATION_CONTROL)
static SVGSVGElement* svgSvgElementFrom(RenderElement& renderElement)
{
    if (auto* svgSvgElement = dynamicDowncast<SVGSVGElement>(renderElement.element()))
        return svgSvgElement;
    if (auto* svgRoot = dynamicDowncast<RenderSVGRoot>(renderElement))
        return &svgRoot->svgSVGElement();
    if (auto* svgRoot = dynamicDowncast<LegacyRenderSVGRoot>(renderElement))
        return &svgRoot->svgSVGElement();

    return nullptr;
}

void RenderView::updatePlayStateForAllAnimations(const IntRect& visibleRect)
{
    bool animationEnabled = page().imageAnimationEnabled();
    for (auto& renderElement : descendantsOfType<RenderElement>(*this)) {
        bool needsRepaint = false;
        bool shouldAnimate = animationEnabled && renderElement.isVisibleInDocumentRect(visibleRect);

        auto updateAnimation = [&](CachedImage* cachedImage) {
            if (!cachedImage)
                return;

            bool hasPausedAnimation = renderElement.hasPausedImageAnimations();
            RefPtr image = cachedImage->image();
            if (RefPtr svgImage = dynamicDowncast<SVGImage>(image.get())) {
                if (shouldAnimate && hasPausedAnimation) {
                    svgImage->resumeAnimation();
                    removeRendererWithPausedImageAnimations(renderElement, *cachedImage);
                } else if (!hasPausedAnimation) {
                    svgImage->stopAnimation();
                    addRendererWithPausedImageAnimations(renderElement, *cachedImage);
                }
            } else if (image && image->isAnimated()) {
                // Override any individual animation play state that may have been set.
                if (RefPtr imageElement = dynamicDowncast<HTMLImageElement>(renderElement.element()))
                    imageElement->setAllowsAnimation(std::nullopt);
                else
                    image->setAllowsAnimation(std::nullopt);

                // Animations of this type require a repaint to be paused or resumed.
                if (shouldAnimate && hasPausedAnimation) {
                    needsRepaint = true;
                    removeRendererWithPausedImageAnimations(renderElement, *cachedImage);
                } else if (!hasPausedAnimation) {
                    needsRepaint = true;
                    addRendererWithPausedImageAnimations(renderElement, *cachedImage);
                }
            }
        };

        for (RefPtr layer = renderElement.style().backgroundLayers(); layer; layer = layer->next())
            updateAnimation(layer->image() ? layer->image()->cachedImage() : nullptr);

        if (auto* renderImage = dynamicDowncast<RenderImage>(renderElement))
            updateAnimation(renderImage->cachedImage());

        if (needsRepaint)
            renderElement.repaint();

        if (RefPtr svgSvgElement = svgSvgElementFrom(renderElement)) {
            if (shouldAnimate) {
                svgSvgElement->unpauseAnimations();
                m_SVGSVGElementsWithPausedImageAnimation.remove(*svgSvgElement);
            } else {
                svgSvgElement->pauseAnimations();
                m_SVGSVGElementsWithPausedImageAnimation.add(*svgSvgElement);
            }
        }
    }
}
#endif // ENABLE(ACCESSIBILITY_ANIMATION_CONTROL)

RenderView::RepaintRegionAccumulator::RepaintRegionAccumulator(RenderView* view)
{
    if (!view)
        return;

    if (!view->protectedDocument()->isTopDocument())
        return;

    m_wasAccumulatingRepaintRegion = !!view->m_accumulatedRepaintRegion;
    if (!m_wasAccumulatingRepaintRegion)
        view->m_accumulatedRepaintRegion = makeUnique<Region>();
    m_rootView = *view;
}

RenderView::RepaintRegionAccumulator::~RepaintRegionAccumulator()
{
    if (m_wasAccumulatingRepaintRegion)
        return;
    if (!m_rootView)
        return;
    m_rootView->flushAccumulatedRepaintRegion();
}

unsigned RenderView::pageNumberForBlockProgressionOffset(int offset) const
{
    int columnNumber = 0;
    const Pagination& pagination = page().pagination();
    if (pagination.mode == Pagination::Mode::Unpaginated)
        return columnNumber;
    
    bool progressionIsInline = false;
    bool progressionIsReversed = false;
    
    if (multiColumnFlow()) {
        progressionIsInline = multiColumnFlow()->progressionIsInline();
        progressionIsReversed = multiColumnFlow()->progressionIsReversed();
    } else
        return columnNumber;
    
    if (!progressionIsInline) {
        if (!progressionIsReversed)
            columnNumber = (pagination.pageLength + pagination.gap - offset) / (pagination.pageLength + pagination.gap);
        else
            columnNumber = offset / (pagination.pageLength + pagination.gap);
    }

    return columnNumber;
}

unsigned RenderView::pageCount() const
{
    const Pagination& pagination = page().pagination();
    if (pagination.mode == Pagination::Mode::Unpaginated)
        return 0;
    
    if (multiColumnFlow() && multiColumnFlow()->firstMultiColumnSet())
        return multiColumnFlow()->firstMultiColumnSet()->columnCount();

    return 0;
}

void RenderView::registerBoxWithScrollSnapPositions(const RenderBox& box)
{
    m_boxesWithScrollSnapPositions.add(box);
}

void RenderView::unregisterBoxWithScrollSnapPositions(const RenderBox& box)
{
    m_boxesWithScrollSnapPositions.remove(box);
}

void RenderView::registerContainerQueryBox(const RenderBox& box)
{
    m_containerQueryBoxes.add(box);
}

void RenderView::unregisterContainerQueryBox(const RenderBox& box)
{
    m_containerQueryBoxes.remove(box);
}

void RenderView::registerAnchor(const RenderBoxModelObject& renderer)
{
    m_anchors.add(renderer);
}

void RenderView::unregisterAnchor(const RenderBoxModelObject& renderer)
{
    m_anchors.remove(renderer);
}

void RenderView::registerPositionTryBox(const RenderBox& box)
{
    m_positionTryBoxes.add(box);
}

void RenderView::unregisterPositionTryBox(const RenderBox& box)
{
    m_positionTryBoxes.remove(box);
}

void RenderView::addCounterNeedingUpdate(RenderCounter& renderer)
{
    m_countersNeedingUpdate.add(renderer);
}

SingleThreadWeakHashSet<RenderCounter> RenderView::takeCountersNeedingUpdate()
{
    return std::exchange(m_countersNeedingUpdate, { });
}

SingleThreadWeakPtr<RenderBlockFlow> RenderView::viewTransitionContainingBlock() const
{
    return m_viewTransitionContainingBlock;
}

void RenderView::setViewTransitionContainingBlock(RenderBlockFlow& renderer)
{
    m_viewTransitionContainingBlock = renderer;
}

void RenderView::addViewTransitionGroup(const AtomString& name, RenderBox& group)
{
    m_viewTransitionGroups.set(name, &group);
}

void RenderView::removeViewTransitionGroup(const AtomString& name)
{
    m_viewTransitionGroups.remove(name);
}

RenderBox* RenderView::viewTransitionGroupForName(const AtomString& name)
{
    return m_viewTransitionGroups.get(name).get();
}

} // namespace WebCore
