/*
 * Copyright (C) 2006-2024 Apple Inc. All rights reserved.
 * Copyright (C) 2013-2014 Google Inc. All rights reserved.
 * Copyright (C) 2019 Adobe. All rights reserved.
 * Copyright (c) 2020, 2021, 2022 Igalia S.L.
 *
 * Portions are Copyright (C) 1998 Netscape Communications Corporation.
 *
 * Other contributors:
 *   Robert O'Callahan <roc+@cs.cmu.edu>
 *   David Baron <dbaron@fas.harvard.edu>
 *   Christian Biesinger <cbiesinger@web.de>
 *   Randall Jesup <rjesup@wgate.com>
 *   Roland Mainz <roland.mainz@informatik.med.uni-giessen.de>
 *   Josh Soref <timeless@mac.com>
 *   Boris Zbarsky <bzbarsky@mit.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Alternatively, the contents of this file may be used under the terms
 * of either the Mozilla Public License Version 1.1, found at
 * http://www.mozilla.org/MPL/ (the "MPL") or the GNU General Public
 * License Version 2.0, found at http://www.fsf.org/copyleft/gpl.html
 * (the "GPL"), in which case the provisions of the MPL or the GPL are
 * applicable instead of those above.  If you wish to allow use of your
 * version of this file only under the terms of one of those two
 * licenses (the MPL or the GPL) and not to allow others to use your
 * version of this file under the LGPL, indicate your decision by
 * deletingthe provisions above and replace them with the notice and
 * other provisions required by the MPL or the GPL, as the case may be.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under any of the LGPL, the MPL or the GPL.
 */

#include "config.h"
#include "RenderLayer.h"

#include "AccessibilityRegionContext.h"
#include "BitmapImage.h"
#include "BorderShape.h"
#include "BoxLayoutShape.h"
#include "ContainerNodeInlines.h"
#include "CSSFilter.h"
#include "CSSPropertyNames.h"
#include "Chrome.h"
#include "DebugPageOverlays.h"
#include "Document.h"
#include "DocumentMarkerController.h"
#include "Editor.h"
#include "Element.h"
#include "ElementInlines.h"
#include "EventHandler.h"
#include "FEColorMatrix.h"
#include "FEMerge.h"
#include "FloatConversion.h"
#include "FloatPoint3D.h"
#include "FloatRect.h"
#include "FloatRoundedRect.h"
#include "FocusController.h"
#include "FrameLoader.h"
#include "FrameSelection.h"
#include "FrameTree.h"
#include "Gradient.h"
#include "GraphicsContext.h"
#include "HTMLCanvasElement.h"
#include "HTMLFormControlElement.h"
#include "HTMLFrameElement.h"
#include "HTMLFrameOwnerElement.h"
#include "HTMLIFrameElement.h"
#include "HTMLNames.h"
#include "HitTestRequest.h"
#include "HitTestResult.h"
#include "HitTestingTransformState.h"
#include "ImageDocument.h"
#include "InspectorInstrumentation.h"
#include "LegacyRenderSVGForeignObject.h"
#include "LegacyRenderSVGImage.h"
#include "LegacyRenderSVGResourceClipper.h"
#include "LegacyRenderSVGRoot.h"
#include "LocalFrame.h"
#include "LocalFrameLoaderClient.h"
#include "LocalFrameView.h"
#include "Logging.h"
#include "OverflowEvent.h"
#include "OverlapTestRequestClient.h"
#include "Page.h"
#include "PlatformMouseEvent.h"
#include "ReferencedSVGResources.h"
#include "RenderAncestorIterator.h"
#include "RenderBoxInlines.h"
#include "RenderElementInlines.h"
#include "RenderFlexibleBox.h"
#include "RenderFragmentContainer.h"
#include "RenderFragmentedFlow.h"
#include "RenderHTMLCanvas.h"
#include "RenderImage.h"
#include "RenderInline.h"
#include "RenderIterator.h"
#include "RenderLayerBacking.h"
#include "RenderLayerCompositor.h"
#include "RenderLayerFilters.h"
#include "RenderLayerInlines.h"
#include "RenderLayerScrollableArea.h"
#include "RenderMarquee.h"
#include "RenderMultiColumnFlow.h"
#include "RenderObjectInlines.h"
#include "RenderReplica.h"
#include "RenderSVGForeignObject.h"
#include "RenderSVGHiddenContainer.h"
#include "RenderSVGInline.h"
#include "RenderSVGModelObject.h"
#include "RenderSVGResourceClipper.h"
#include "RenderSVGRoot.h"
#include "RenderSVGText.h"
#include "RenderSVGViewportContainer.h"
#include "RenderScrollbar.h"
#include "RenderScrollbarPart.h"
#include "RenderStyleSetters.h"
#include "RenderTableCell.h"
#include "RenderTableRow.h"
#include "RenderText.h"
#include "RenderTheme.h"
#include "RenderTreeAsText.h"
#include "RenderTreeMutationDisallowedScope.h"
#include "RenderView.h"
#include "SVGClipPathElement.h"
#include "SVGNames.h"
#include "ScaleTransformOperation.h"
#include "ScrollAnimator.h"
#include "ScrollSnapOffsetsInfo.h"
#include "Scrollbar.h"
#include "ScrollbarTheme.h"
#include "ScrollingCoordinator.h"
#include "Settings.h"
#include "ShadowRoot.h"
#include "SourceGraphic.h"
#include "StyleAttributeMutationScope.h"
#include "StyleProperties.h"
#include "StyleResolver.h"
#include "Styleable.h"
#include "TransformOperationData.h"
#include "TransformationMatrix.h"
#include "TranslateTransformOperation.h"
#include "ViewTransition.h"
#include "WheelEventTestMonitor.h"
#include <stdio.h>
#include <wtf/HexNumber.h>
#include <wtf/MonotonicTime.h>
#include <wtf/StdLibExtras.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/TextStream.h>

namespace WebCore {

using namespace HTMLNames;

class ClipRects : public RefCounted<ClipRects> {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(ClipRects);
public:
    static Ref<ClipRects> create()
    {
        return adoptRef(*new ClipRects);
    }

    static Ref<ClipRects> create(const ClipRects& other)
    {
        return adoptRef(*new ClipRects(other));
    }

    void reset()
    {
        m_overflowClipRect.reset();
        m_fixedClipRect.reset();
        m_posClipRect.reset();
        m_fixed = false;
    }

    const ClipRect& overflowClipRect() const { return m_overflowClipRect; }
    void setOverflowClipRect(const ClipRect& clipRect) { m_overflowClipRect = clipRect; }

    const ClipRect& fixedClipRect() const { return m_fixedClipRect; }
    void setFixedClipRect(const ClipRect& clipRect) { m_fixedClipRect = clipRect; }

    const ClipRect& posClipRect() const { return m_posClipRect; }
    void setPosClipRect(const ClipRect& clipRect) { m_posClipRect = clipRect; }

    bool fixed() const { return m_fixed; }
    void setFixed(bool fixed) { m_fixed = fixed; }

    void setOverflowClipRectAffectedByRadius() { m_overflowClipRect.setAffectedByRadius(true); }

    bool operator==(const ClipRects& other) const
    {
        return m_overflowClipRect == other.overflowClipRect()
            && m_fixedClipRect == other.fixedClipRect()
            && m_posClipRect == other.posClipRect()
            && m_fixed == other.fixed();
    }

    ClipRects& operator=(const ClipRects& other)
    {
        m_overflowClipRect = other.overflowClipRect();
        m_fixedClipRect = other.fixedClipRect();
        m_posClipRect = other.posClipRect();
        m_fixed = other.fixed();
        return *this;
    }

private:
    ClipRects() = default;

    ClipRects(const LayoutRect& clipRect)
        : m_overflowClipRect(clipRect)
        , m_fixedClipRect(clipRect)
        , m_posClipRect(clipRect)
    {
    }

    ClipRects(const ClipRects& other)
        : RefCounted()
        , m_fixed(other.fixed())
        , m_overflowClipRect(other.overflowClipRect())
        , m_fixedClipRect(other.fixedClipRect())
        , m_posClipRect(other.posClipRect())
    {
    }

    bool m_fixed { false };
    ClipRect m_overflowClipRect;
    ClipRect m_fixedClipRect;
    ClipRect m_posClipRect;
};

class ClipRectsCache {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(ClipRectsCache);
public:
    ClipRectsCache()
    {
#if ASSERT_ENABLED
        for (int i = 0; i < NumCachedClipRectsTypes; ++i) {
            m_clipRectsRoot[i] = nullptr;
        }
#endif

    }

    ClipRects* getClipRects(const RenderLayer::ClipRectsContext& context) const
    {
        return m_clipRects[getIndex(context.clipRectsType, context.respectOverflowClip())].get();
    }

    void setClipRects(ClipRectsType clipRectsType, bool respectOverflowClip, RefPtr<ClipRects>&& clipRects)
    {
        m_clipRects[getIndex(clipRectsType, respectOverflowClip)] = WTFMove(clipRects);
    }

#if ASSERT_ENABLED
    std::array<const RenderLayer*, NumCachedClipRectsTypes> m_clipRectsRoot;
#endif
private:
    unsigned getIndex(ClipRectsType clipRectsType, bool respectOverflowClip) const
    {
        unsigned index = static_cast<unsigned>(clipRectsType);
        if (respectOverflowClip)
            index += static_cast<unsigned>(NumCachedClipRectsTypes);
        ASSERT_WITH_SECURITY_IMPLICATION(index < NumCachedClipRectsTypes * 2);
        return index;
    }

    std::array<RefPtr<ClipRects>, NumCachedClipRectsTypes * 2> m_clipRects;
};

void makeMatrixRenderable(TransformationMatrix& matrix, bool has3DRendering)
{
    if (!has3DRendering)
        matrix.makeAffine();
}

#if !LOG_DISABLED
static TextStream& operator<<(TextStream& ts, const ClipRects& clipRects)
{
    TextStream::GroupScope scope(ts);
    ts << indent << "ClipRects\n"_s;
    ts << indent << "  overflow  : "_s << clipRects.overflowClipRect() << '\n';
    ts << indent << "  fixed     : "_s << clipRects.fixedClipRect() << '\n';
    ts << indent << "  positioned: "_s << clipRects.posClipRect() << '\n';

    return ts;
}

#endif

static ScrollingScope nextScrollingScope()
{
    static ScrollingScope currentScope = 0;
    return ++currentScope;
}

WTF_MAKE_PREFERABLY_COMPACT_TZONE_OR_ISO_ALLOCATED_IMPL(RenderLayer);

RenderLayer::RenderLayer(RenderLayerModelObject& renderer)
    : m_isRenderViewLayer(renderer.isRenderView())
    , m_forcedStackingContext(renderer.isRenderMedia())
    , m_isNormalFlowOnly(false)
    , m_isCSSStackingContext(false)
    , m_canBeBackdropRoot(false)
    , m_hasBackdropFilterDescendantsWithoutRoot(false)
    , m_isOpportunisticStackingContext(false)
    , m_zOrderListsDirty(false)
    , m_normalFlowListDirty(true)
    , m_hadNegativeZOrderList(false)
    , m_inResizeMode(false)
    , m_hasSelfPaintingLayerDescendant(false)
    , m_hasSelfPaintingLayerDescendantDirty(false)
    , m_hasViewportConstrainedDescendant(false)
    , m_hasViewportConstrainedDescendantStatusDirty(false)
    , m_usedTransparency(false)
    , m_paintingInsideReflection(false)
    , m_visibleContentStatusDirty(true)
    , m_hasVisibleContent(false)
    , m_visibleDescendantStatusDirty(false)
    , m_hasVisibleDescendant(false)
    , m_isFixedIntersectingViewport(false)
    , m_behavesAsFixed(false)
    , m_3DTransformedDescendantStatusDirty(true)
    , m_has3DTransformedDescendant(false)
    , m_hasCompositingDescendant(false)
    , m_hasCompositedNonContainedDescendants(false)
    , m_hasCompositedScrollingAncestor(false)
    , m_hasFixedContainingBlockAncestor(false)
    , m_hasTransformedAncestor(false)
    , m_has3DTransformedAncestor(false)
    , m_insideSVGForeignObject(false)
    , m_indirectCompositingReason(static_cast<unsigned>(IndirectCompositingReason::None))
    , m_viewportConstrainedNotCompositedReason(NoNotCompositedReason)
#if ASSERT_ENABLED
    , m_layerListMutationAllowed(true)
#endif
    , m_blendMode(static_cast<unsigned>(BlendMode::Normal))
    , m_hasNotIsolatedCompositedBlendingDescendants(false)
    , m_hasNotIsolatedBlendingDescendants(false)
    , m_hasNotIsolatedBlendingDescendantsStatusDirty(false)
    , m_renderer(renderer)
{
    setIsNormalFlowOnly(shouldBeNormalFlowOnly());
    setIsCSSStackingContext(shouldBeCSSStackingContext());
    setCanBeBackdropRoot(computeCanBeBackdropRoot());
    setNeedsPositionUpdate();

    m_isSelfPaintingLayer = shouldBeSelfPaintingLayer();

    if (isRenderViewLayer())
        m_boxScrollingScope = m_contentsScrollingScope = nextScrollingScope();

    auto needsVisibleContentStatusUpdate = [&]() {
        if (renderer.firstChild())
            return false;

        // Leave m_visibleContentStatusDirty = true in any case. The associated renderer needs to be inserted into the
        // render tree, before we can determine the visible content status. The visible content status of a SVG renderer
        // depends on its ancestors (all children of RenderSVGHiddenContainer are recursively invisible, no matter what).
        if (renderer.isSVGLayerAwareRenderer() && renderer.document().settings().layerBasedSVGEngineEnabled())
            return false;

        //  We need the parent to know if we have skipped content or content-visibility root.
        if (renderer.style().isSkippedRootOrSkippedContent() && !renderer.parent())
            return false;
        return true;
    }();

    if (needsVisibleContentStatusUpdate) {
        m_visibleContentStatusDirty = false;
        m_hasVisibleContent = renderer.style().usedVisibility() == Visibility::Visible;
    }
}

RenderLayer::~RenderLayer()
{
    if (inResizeMode())
        renderer().frame().eventHandler().resizeLayerDestroyed();

    if (m_reflection)
        removeReflection();

    clearLayerScrollableArea();
    clearLayerFilters();
    clearLayerClipPath();

    // Child layers will be deleted by their corresponding render objects, so
    // we don't need to delete them ourselves.

    clearBacking({ }, true);

    removeClipperClientIfNeeded();

    // Layer and all its children should be removed from the tree before destruction.
    RELEASE_ASSERT_WITH_SECURITY_IMPLICATION(renderer().renderTreeBeingDestroyed() || !parent());
    RELEASE_ASSERT_WITH_SECURITY_IMPLICATION(renderer().renderTreeBeingDestroyed() || !firstChild());
}

RenderLayer::PaintedContentRequest::PaintedContentRequest(const RenderLayer& owningLayer)
{
#if HAVE(SUPPORT_HDR_DISPLAY)
    if (owningLayer.renderer().document().drawsHDRContent())
        makeHDRContentUnknown();
    else
        makeHDRContentFalse();
#else
    UNUSED_PARAM(owningLayer);
#endif
}

void RenderLayer::removeClipperClientIfNeeded() const
{
    WTF::switchOn(renderer().style().clipPath(),
        [&](const Style::ReferencePath& clipPath) {
            if (auto* clipperRenderer = ReferencedSVGResources::referencedClipperRenderer(renderer().treeScopeForSVGReferences(), clipPath))
                clipperRenderer->removeClientFromCache(renderer());
        },
        [](const auto&) { }
    );
}

void RenderLayer::addChild(RenderLayer& child, RenderLayer* beforeChild)
{
    RenderLayer* prevSibling = beforeChild ? beforeChild->previousSibling() : lastChild();
    if (prevSibling) {
        child.setPreviousSibling(prevSibling);
        prevSibling->setNextSibling(&child);
        ASSERT(prevSibling != &child);
    } else
        setFirstChild(&child);

    if (beforeChild) {
        beforeChild->setPreviousSibling(&child);
        child.setNextSibling(beforeChild);
        ASSERT(beforeChild != &child);
    } else
        setLastChild(&child);

    child.m_parent = this;
    child.setSelfAndDescendantsNeedPositionUpdate();

    dirtyPaintOrderListsOnChildChange(child);

    child.updateAncestorDependentState();
    dirtyAncestorChainVisibleDescendantStatus();
    child.updateDescendantDependentFlags();

    if (child.isSelfPaintingLayer() || child.hasSelfPaintingLayerDescendant())
        setAncestorChainHasSelfPaintingLayerDescendant();

    if (child.isViewportConstrained() || child.m_hasViewportConstrainedDescendant)
        setAncestorChainHasViewportConstrainedDescendant();

    if (compositor().hasContentCompositingLayers())
        setDescendantsNeedCompositingRequirementsTraversal();

    if (child.hasDescendantNeedingCompositingRequirementsTraversal() || child.needsCompositingRequirementsTraversal())
        child.setAncestorsHaveCompositingDirtyFlag(Compositing::HasDescendantNeedingRequirementsTraversal);

    if (child.hasDescendantNeedingUpdateBackingOrHierarchyTraversal() || child.needsUpdateBackingOrHierarchyTraversal())
        child.setAncestorsHaveCompositingDirtyFlag(Compositing::HasDescendantNeedingBackingOrHierarchyTraversal);

    if (child.hasBlendMode() || (child.hasNotIsolatedBlendingDescendants() && !child.isolatesBlending()))
        updateAncestorChainHasBlendingDescendants(); // Why not just dirty?

#if ENABLE(ASYNC_SCROLLING)
    if (child.hasDescendantNeedingEventRegionUpdate() || (child.isComposited() && child.backing()->needsEventRegionUpdate()))
        child.setAncestorsHaveDescendantNeedingEventRegionUpdate();
#endif
}

void RenderLayer::removeChild(RenderLayer& oldChild)
{
    if (!renderer().renderTreeBeingDestroyed())
        compositor().layerWillBeRemoved(*this, oldChild);

    // remove the child
    if (oldChild.previousSibling())
        oldChild.previousSibling()->setNextSibling(oldChild.nextSibling());
    if (oldChild.nextSibling())
        oldChild.nextSibling()->setPreviousSibling(oldChild.previousSibling());

    if (m_first == &oldChild)
        m_first = oldChild.nextSibling();
    if (m_last == &oldChild)
        m_last = oldChild.previousSibling();

    dirtyPaintOrderListsOnChildChange(oldChild);

    oldChild.setPreviousSibling(nullptr);
    oldChild.setNextSibling(nullptr);
    oldChild.m_parent = nullptr;

    oldChild.updateDescendantDependentFlags();
    if (oldChild.m_hasVisibleContent || oldChild.m_hasVisibleDescendant)
        dirtyAncestorChainVisibleDescendantStatus();

    if (oldChild.isSelfPaintingLayer() || oldChild.hasSelfPaintingLayerDescendant())
        dirtyAncestorChainHasSelfPaintingLayerDescendantStatus();

    if (oldChild.isViewportConstrained() || oldChild.m_hasViewportConstrainedDescendant)
        dirtyAncestorChainHasViewportConstrainedDescendantStatus();

    if (compositor().hasContentCompositingLayers())
        setDescendantsNeedCompositingRequirementsTraversal();

    if (oldChild.hasBlendMode() || (oldChild.hasNotIsolatedBlendingDescendants() && !oldChild.isolatesBlending()))
        dirtyAncestorChainHasBlendingDescendants();
    if (renderer().style().usedVisibility() != Visibility::Visible)
        dirtyVisibleContentStatus();
}

void RenderLayer::dirtyPaintOrderListsOnChildChange(RenderLayer& child)
{
    if (child.isNormalFlowOnly())
        dirtyNormalFlowList();

    if (!child.isNormalFlowOnly() || child.firstChild()) {
        // Dirty the z-order list in which we are contained. The stackingContext() can be null in the
        // case where we're building up generated content layers. This is ok, since the lists will start
        // off dirty in that case anyway.
        child.dirtyStackingContextZOrderLists();
    }
}

void RenderLayer::insertOnlyThisLayer()
{
    if (!m_parent && renderer().parent()) {
        // We need to connect ourselves when our renderer() has a parent.
        // Find our enclosingLayer and add ourselves.
        auto* parentLayer = renderer().layerParent();
        if (!parentLayer)
            return;

        auto* beforeChild = parentLayer->reflectionLayer() != this ? renderer().layerNextSibling(*parentLayer) : nullptr;
        parentLayer->addChild(*this, beforeChild);
    }

    // Remove all descendant layers from the hierarchy and add them to the new position.
    for (auto& child : childrenOfType<RenderElement>(renderer()))
        child.moveLayers(*this);

    // Clear out all the clip rects.
    clearClipRectsIncludingDescendants();
}

void RenderLayer::removeOnlyThisLayer()
{
    if (!m_parent)
        return;

    compositor().layerWillBeRemoved(*m_parent, *this);

    // Dirty the clip rects.
    clearClipRectsIncludingDescendants();

    RenderLayer* nextSib = nextSibling();

    // Remove the child reflection layer before moving other child layers.
    // The reflection layer should not be moved to the parent.
    if (auto* reflectionLayer = this->reflectionLayer())
        removeChild(*reflectionLayer);

    // Now walk our kids and reattach them to our parent.
    RenderLayer* current = m_first;
    while (current) {
        RenderLayer* next = current->nextSibling();
        removeChild(*current);
        m_parent->addChild(*current, nextSib);
        current->setRepaintStatus(RepaintStatus::NeedsFullRepaint);
        if (isComposited())
            current->computeRepaintRectsIncludingDescendants();
        current = next;
    }

    // Remove us from the parent.
    m_parent->removeChild(*this);
    renderer().destroyLayer();
}

static bool canCreateStackingContext(const RenderLayer& layer)
{
    auto& renderer = layer.renderer();
    return renderer.hasTransformRelatedProperty()
        || renderer.hasClipPath()
        || renderer.hasFilter()
        || renderer.hasMask()
        || renderer.hasBackdropFilter()
#if HAVE(CORE_MATERIAL)
        || renderer.hasAppleVisualEffect()
#endif
        || renderer.hasBlendMode()
        || renderer.isTransparent()
        || renderer.requiresRenderingConsolidationForViewTransition()
        || renderer.isRenderViewTransitionCapture()
        || renderer.isPositioned() // Note that this only creates stacking context in conjunction with explicit z-index.
        || renderer.hasReflection()
        || renderer.style().hasIsolation()
        || renderer.shouldApplyPaintContainment()
        || !renderer.style().hasAutoUsedZIndex()
        || (renderer.style().willChange() && renderer.style().willChange()->canCreateStackingContext())
        || layer.establishesTopLayer();
}

bool RenderLayer::shouldBeNormalFlowOnly() const
{
    if (canCreateStackingContext(*this))
        return false;

    return renderer().hasNonVisibleOverflow()
        || renderer().isRenderHTMLCanvas()
        || renderer().isRenderVideo()
        || renderer().isRenderEmbeddedObject()
        || renderer().isRenderIFrame()
        || (renderer().style().specifiesColumns() && !isRenderViewLayer())
        || renderer().isRenderFragmentedFlow();
}

bool RenderLayer::shouldBeCSSStackingContext() const
{
    return !renderer().style().hasAutoUsedZIndex() || renderer().shouldApplyLayoutContainment() || renderer().shouldApplyPaintContainment() || renderer().requiresRenderingConsolidationForViewTransition() || renderer().isRenderViewTransitionCapture() ||  renderer().isViewTransitionRoot() || renderer().isViewTransitionContainingBlock() || isRenderViewLayer();
}

bool RenderLayer::computeCanBeBackdropRoot() const
{
    if (!renderer().settings().cssUnprefixedBackdropFilterEnabled())
        return false;

    // In order to match other impls and not the spec, the document element should
    // only be a backdrop root (and be isolated from the base background color) if
    // another group rendering effect is present.
    // https://github.com/w3c/fxtf-drafts/issues/557
    return isRenderViewLayer()
        || renderer().isTransparent()
        || renderer().hasBackdropFilter()
#if HAVE(CORE_MATERIAL)
        || renderer().hasAppleVisualEffect()
#endif
        || renderer().hasClipPath()
        || renderer().hasFilter()
        || renderer().hasBlendMode()
        || renderer().hasMask()
        || (renderer().requiresRenderingConsolidationForViewTransition() && !renderer().isDocumentElementRenderer())
        || (renderer().style().willChange() && renderer().style().willChange()->canBeBackdropRoot());
}

bool RenderLayer::setIsNormalFlowOnly(bool isNormalFlowOnly)
{
    if (isNormalFlowOnly == m_isNormalFlowOnly)
        return false;

    m_isNormalFlowOnly = isNormalFlowOnly;

    if (auto* p = parent())
        p->dirtyNormalFlowList();
    dirtyStackingContextZOrderLists();
    return true;
}

void RenderLayer::isStackingContextChanged()
{
    dirtyStackingContextZOrderLists();
    setSelfAndDescendantsNeedPositionUpdate();
    if (isStackingContext())
        dirtyZOrderLists();
    else
        clearZOrderLists();
}

bool RenderLayer::setIsOpportunisticStackingContext(bool isStacking)
{
    bool wasStacking = isStackingContext();
    m_isOpportunisticStackingContext = isStacking;
    if (wasStacking == isStackingContext())
        return false;

    isStackingContextChanged();
    return true;
}

bool RenderLayer::setIsCSSStackingContext(bool isCSSStackingContext)
{
    bool wasStacking = isStackingContext();
    m_isCSSStackingContext = isCSSStackingContext;
    if (wasStacking == isStackingContext())
        return false;

    isStackingContextChanged();
    return true;
}

bool RenderLayer::setCanBeBackdropRoot(bool canBeBackdropRoot)
{
    if (m_canBeBackdropRoot == canBeBackdropRoot)
        return false;
    m_canBeBackdropRoot = canBeBackdropRoot;
    return true;
}

RenderLayer* RenderLayer::stackingContext() const
{
    auto* layer = parent();
    while (layer && !layer->isStackingContext())
        layer = layer->parent();

    ASSERT(!layer || layer->isStackingContext());
    ASSERT_IMPLIES(establishesTopLayer(), !layer || layer == renderer().view().layer());
    return layer;
}

void RenderLayer::dirtyZOrderLists()
{
    ASSERT(layerListMutationAllowed());
    ASSERT(isStackingContext());

    if (m_posZOrderList)
        m_posZOrderList->clear();
    if (m_negZOrderList)
        m_negZOrderList->clear();
    m_zOrderListsDirty = true;

    // FIXME: Ideally, we'd only dirty if the lists changed.
    if (hasCompositingDescendant())
        setNeedsCompositingPaintOrderChildrenUpdate();
}

void RenderLayer::dirtyStackingContextZOrderLists()
{
    if (auto* sc = stackingContext())
        sc->dirtyZOrderLists();
}

void RenderLayer::dirtyHiddenStackingContextAncestorZOrderLists()
{
    for (auto* sc = stackingContext(); sc; sc = sc->stackingContext()) {
        sc->dirtyZOrderLists();
        if (sc->hasVisibleContent())
            break;
    }
}

bool RenderLayer::willCompositeClipPath() const
{
    if (!isComposited())
        return false;

    if (!renderer().style().hasClipPath())
        return false;

    if (renderer().hasMask())
        return false;

    return GraphicsLayer::supportsLayerType(GraphicsLayer::Type::Shape);
}

void RenderLayer::dirtyNormalFlowList()
{
    ASSERT(layerListMutationAllowed());

    if (m_normalFlowList)
        m_normalFlowList->clear();
    m_normalFlowListDirty = true;

    if (hasCompositingDescendant())
        setNeedsCompositingPaintOrderChildrenUpdate();
}

void RenderLayer::updateNormalFlowList()
{
    if (!m_normalFlowListDirty)
        return;

    ASSERT(layerListMutationAllowed());

    for (RenderLayer* child = firstChild(); child; child = child->nextSibling()) {
        // Ignore non-overflow layers and reflections.
        if (child->isNormalFlowOnly() && !isReflectionLayer(*child)) {
            if (!m_normalFlowList)
                m_normalFlowList = makeUnique<Vector<RenderLayer*>>();
            m_normalFlowList->append(child);
            child->setWasIncludedInZOrderTree();
        }
    }

    if (m_normalFlowList)
        m_normalFlowList->shrinkToFit();

    m_normalFlowListDirty = false;
}

void RenderLayer::rebuildZOrderLists()
{
    ASSERT(layerListMutationAllowed());
    ASSERT(isDirtyStackingContext());

    OptionSet<Compositing> childDirtyFlags;
    rebuildZOrderLists(m_posZOrderList, m_negZOrderList, childDirtyFlags);
    m_zOrderListsDirty = false;

    bool hasNegativeZOrderList = m_negZOrderList && m_negZOrderList->size();
    // Having negative z-order lists affect whether a compositing layer needs a foreground layer.
    // Ideally we'd only trigger this when having z-order children changes, but we blow away the old z-order
    // lists on dirtying so we don't know the old state.
    if (hasNegativeZOrderList != m_hadNegativeZOrderList) {
        m_hadNegativeZOrderList = hasNegativeZOrderList;
        if (isComposited())
            setNeedsCompositingConfigurationUpdate();
    }

    // Building lists may have added layers with dirty flags, so make sure we propagate dirty bits up the tree.
    if (m_compositingDirtyBits.containsAll({ Compositing::DescendantsNeedRequirementsTraversal, Compositing::DescendantsNeedBackingAndHierarchyTraversal }))
        return;

    if (childDirtyFlags.containsAny(computeCompositingRequirementsFlags()))
        setDescendantsNeedCompositingRequirementsTraversal();

    if (childDirtyFlags.containsAny(updateBackingOrHierarchyFlags()))
        setDescendantsNeedUpdateBackingAndHierarchyTraversal();
}

void RenderLayer::rebuildZOrderLists(std::unique_ptr<Vector<RenderLayer*>>& posZOrderList, std::unique_ptr<Vector<RenderLayer*>>& negZOrderList, OptionSet<Compositing>& accumulatedDirtyFlags)
{
    for (RenderLayer* child = firstChild(); child; child = child->nextSibling()) {
        if (!isReflectionLayer(*child))
            child->collectLayers(posZOrderList, negZOrderList, accumulatedDirtyFlags);
    }

    auto compareZIndex = [] (const RenderLayer* first, const RenderLayer* second) -> bool {
        return first->zIndex() < second->zIndex();
    };

    // Sort the two lists.
    if (posZOrderList) {
        std::stable_sort(posZOrderList->begin(), posZOrderList->end(), compareZIndex);
        posZOrderList->shrinkToFit();
    }

    if (negZOrderList) {
        std::stable_sort(negZOrderList->begin(), negZOrderList->end(), compareZIndex);
        negZOrderList->shrinkToFit();
    }

    if (isRenderViewLayer() && renderer().document().hasTopLayerElement()) {
        auto topLayerLayers = topLayerRenderLayers(renderer().view());
        if (topLayerLayers.size()) {
            if (!posZOrderList)
                posZOrderList = makeUnique<Vector<RenderLayer*>>();
            posZOrderList->appendVector(topLayerLayers);
        }
    }

    if (isRenderViewLayer() && renderer().document().hasViewTransitionPseudoElementTree()) {
        if (WeakPtr viewTransitionContainingBlock = renderer().view().viewTransitionContainingBlock(); viewTransitionContainingBlock && viewTransitionContainingBlock->hasLayer()) {
            if (!posZOrderList)
                posZOrderList = makeUnique<Vector<RenderLayer*>>();
            posZOrderList->append(viewTransitionContainingBlock->layer());
        }
    }
}

void RenderLayer::removeSelfFromCompositor()
{
    if (parent())
        compositor().layerWillBeRemoved(*parent(), *this);
    clearBacking({ });
}

void RenderLayer::removeDescendantsFromCompositor()
{
    for (RenderLayer* child = firstChild(); child; child = child->nextSibling()) {
        child->removeSelfFromCompositor();
        child->removeDescendantsFromCompositor();
    }
}

void RenderLayer::setWasOmittedFromZOrderTree()
{
    if (m_wasOmittedFromZOrderTree)
        return;

    ASSERT(!isNormalFlowOnly());
    removeSelfFromCompositor();

    // Omitting a stacking context removes the whole subtree, otherwise collectLayers will
    // visit and omit/include descendants separately.
    if (isStackingContext())
        removeDescendantsFromCompositor();

    if (compositor().hasContentCompositingLayers() && parent())
        parent()->setDescendantsNeedCompositingRequirementsTraversal();

    m_wasOmittedFromZOrderTree = true;
}

void RenderLayer::collectLayers(std::unique_ptr<Vector<RenderLayer*>>& positiveZOrderList, std::unique_ptr<Vector<RenderLayer*>>& negativeZOrderList, OptionSet<Compositing>& accumulatedDirtyFlags)
{
    ASSERT(!descendantDependentFlagsAreDirty());
    if (establishesTopLayer() || renderer().isViewTransitionContainingBlock())
        return;

    bool isStacking = isStackingContext();
    bool layerOrDescendantsAreVisible = m_hasVisibleContent || m_alwaysIncludedInZOrderLists || m_hasVisibleDescendant || m_hasAlwaysIncludedInZOrderListsDescendants;
    layerOrDescendantsAreVisible |= page().hasEverSetVisibilityAdjustment();
    // Normal flow layers are just painted by their enclosing layers, so they don't get put in zorder lists.
    if (!isNormalFlowOnly()) {
        if (layerOrDescendantsAreVisible) {
            auto& layerList = (zIndex() >= 0) ? positiveZOrderList : negativeZOrderList;
            if (!layerList)
                layerList = makeUnique<Vector<RenderLayer*>>();
            layerList->append(this);
            accumulatedDirtyFlags.add(m_compositingDirtyBits);
            setWasIncludedInZOrderTree();
        } else
            setWasOmittedFromZOrderTree();
    }

    // Recur into our children to collect more layers, but only if we don't establish
    // a stacking context/container.
    if (!isStacking) {
        for (RenderLayer* child = firstChild(); child; child = child->nextSibling()) {
            // Ignore reflections.
            if (!isReflectionLayer(*child))
                child->collectLayers(positiveZOrderList, negativeZOrderList, accumulatedDirtyFlags);
        }
    }
}

void RenderLayer::setNeedsPositionUpdate()
{
    m_layerPositionDirtyBits.add(LayerPositionUpdates::NeedsPositionUpdate);
    for (auto* layer = parent(); layer; layer = layer->parent()) {
        if (layer->m_layerPositionDirtyBits.contains(LayerPositionUpdates::DescendantNeedsPositionUpdate))
            break;
        layer->m_layerPositionDirtyBits.add(LayerPositionUpdates::DescendantNeedsPositionUpdate);
    }
}

bool RenderLayer::needsPositionUpdate() const
{
    if (m_layerPositionDirtyBits.containsAny({ LayerPositionUpdates::NeedsPositionUpdate, LayerPositionUpdates::DescendantNeedsPositionUpdate }))
        return true;
    if (parent() && parent()->m_layerPositionDirtyBits.contains(LayerPositionUpdates::AllChildrenNeedPositionUpdate))
        return true;
    return false;
}

void RenderLayer::setSelfAndChildrenNeedPositionUpdate()
{
    setNeedsPositionUpdate();
    m_layerPositionDirtyBits.add({ LayerPositionUpdates::DescendantNeedsPositionUpdate, LayerPositionUpdates::AllChildrenNeedPositionUpdate });
}

void RenderLayer::setSelfAndDescendantsNeedPositionUpdate()
{
    setNeedsPositionUpdate();
    m_layerPositionDirtyBits.add({ LayerPositionUpdates::DescendantNeedsPositionUpdate, LayerPositionUpdates::AllDescendantsNeedPositionUpdate });
}

void RenderLayer::setAncestorsHaveCompositingDirtyFlag(Compositing flag)
{
    for (auto* layer = paintOrderParent(); layer; layer = layer->paintOrderParent()) {
        if (layer->m_compositingDirtyBits.contains(flag))
            break;
        layer->m_compositingDirtyBits.add(flag);
    }
}

void RenderLayer::updateLayerListsIfNeeded()
{
    updateDescendantDependentFlags();
    updateZOrderLists();
    updateNormalFlowList();

    if (RenderLayer* reflectionLayer = this->reflectionLayer()) {
        reflectionLayer->updateZOrderLists();
        reflectionLayer->updateNormalFlowList();
    }
}

String RenderLayer::name() const
{
    if (!isReflection())
        return renderer().debugDescription();
    return makeString(renderer().debugDescription(), " (reflection)"_s);
}

RenderLayerCompositor& RenderLayer::compositor() const
{
    return renderer().view().compositor();
}

void RenderLayer::contentChanged(ContentChangeType changeType)
{
    if (changeType == ContentChangeType::Canvas || changeType == ContentChangeType::Video || changeType == ContentChangeType::FullScreen || changeType == ContentChangeType::Model || changeType == ContentChangeType::HDRImage) {
        setNeedsPostLayoutCompositingUpdate();
        setNeedsCompositingConfigurationUpdate();
    }

    if (auto* backing = this->backing())
        backing->contentChanged(changeType);
}

bool RenderLayer::canRender3DTransforms() const
{
    return compositor().canRender3DTransforms();
}

bool RenderLayer::shouldPaintWithFilters(OptionSet<PaintBehavior> paintBehavior) const
{
    const auto& filter = renderer().style().filter();
    if (filter.isEmpty())
        return false;

    if (renderer().isRenderOrLegacyRenderSVGRoot() && filter.isReferenceFilter())
        return false;

    if (RenderLayerFilters::isIdentity(renderer()))
        return false;

    if (paintBehavior & PaintBehavior::FlattenCompositingLayers)
        return true;

    if (isComposited() && m_backing->canCompositeFilters())
        return false;

    return true;
}

bool RenderLayer::requiresFullLayerImageForFilters() const
{
    if (!shouldPaintWithFilters())
        return false;

    return m_filters && m_filters->hasFilterThatMovesPixels();
}

OptionSet<RenderLayer::UpdateLayerPositionsFlag> RenderLayer::flagsForUpdateLayerPositions(RenderLayer& startingLayer)
{
    OptionSet<UpdateLayerPositionsFlag> flags = { CheckForRepaint };

    if (auto* parent = startingLayer.parent()) {
        if (parent->hasFixedContainingBlockAncestor() || (!parent->isRenderViewLayer() && parent->renderer().canContainFixedPositionObjects()))
            flags.add(SeenFixedContainingBlockLayer);

        if (parent->hasTransformedAncestor() || parent->transform())
            flags.add(SeenTransformedLayer);

        if (parent->has3DTransformedAncestor() || (parent->transform() && !parent->transform()->isAffine()))
            flags.add(Seen3DTransformedLayer);

        if (parent->behavesAsFixed() || (parent->renderer().isFixedPositioned() && !parent->hasFixedContainingBlockAncestor()))
            flags.add(SeenFixedLayer);

        if (parent->renderer().isStickilyPositioned())
            flags.add(SeenStickyLayer);

        if (parent->hasCompositedScrollingAncestor() || parent->hasCompositedScrollableOverflow())
            flags.add(SeenCompositedScrollingLayer);
    }

    return flags;
}

void RenderLayer::willUpdateLayerPositions()
{
    if (CheckedPtr markers = renderer().document().markersIfExists())
        markers->invalidateRectsForAllMarkers();
}

#if !LOG_DISABLED || ENABLE(TREE_DEBUGGING)
static inline bool compositingLogEnabledRenderLayer()
{
    return LogCompositing.state == WTFLogChannelState::On;
}
#endif

void RenderLayer::updateLayerPositionsAfterStyleChange(bool environmentChanged)
{
    LOG(Compositing, "RenderLayer %p updateLayerPositionsAfterStyleChange - before", this);
#if ENABLE(TREE_DEBUGGING)
    if (compositingLogEnabledRenderLayer())
        showLayerPositionTree(this);
#endif

    auto updateLayerPositionFlags = [&](bool environmentChanged) {
        auto flags = flagsForUpdateLayerPositions(*this);
        if (environmentChanged)
            flags.add(RenderLayer::EnvironmentChanged);
        return flags;
    };

    if (environmentChanged)
        setSelfAndDescendantsNeedPositionUpdate();

    willUpdateLayerPositions();
    recursiveUpdateLayerPositions(updateLayerPositionFlags(environmentChanged));

    LOG(Compositing, "RenderLayer %p updateLayerPositionsAfterStyleChange - after", this);
#if ENABLE(TREE_DEBUGGING)
    if (compositingLogEnabledRenderLayer())
        showLayerPositionTree(this);
#endif
}

#if ASSERT_ENABLED
uint32_t gUpdatePositionsCount = 0;
uint32_t gVerifyPositionsCount = 0;
uint32_t gVisitedPositionsCount = 0;
#endif

void RenderLayer::updateLayerPositionsAfterLayout(bool didFullRepaint, bool environmentChanged)
{
    ASSERT(isRenderViewLayer());

    auto updateLayerPositionFlags = [&](bool didFullRepaint, bool environmentChanged) {
        auto flags = flagsForUpdateLayerPositions(*this);
        if (didFullRepaint) {
            flags.remove(RenderLayer::CheckForRepaint);
            flags.add(RenderLayer::NeedsFullRepaintInBacking);
        }
        if (environmentChanged)
            flags.add(RenderLayer::EnvironmentChanged);
        return flags;
    };

#if ASSERT_ENABLED
    gUpdatePositionsCount = 0;
    gVerifyPositionsCount = 0;
    gVisitedPositionsCount = 0;
#endif

    LOG_WITH_STREAM(Compositing, stream << "RenderLayer " << this << " updateLayerPositionsAfterLayout (environment changed " << environmentChanged << " - before");
#if ENABLE(TREE_DEBUGGING)
    if (compositingLogEnabledRenderLayer())
        showLayerPositionTree(root());
#endif

    if (environmentChanged)
        setSelfAndDescendantsNeedPositionUpdate();

    willUpdateLayerPositions();

    recursiveUpdateLayerPositions(updateLayerPositionFlags(didFullRepaint, environmentChanged));

    LOG(Compositing, "RenderLayer %p updateLayerPositionsAfterLayout - after", this);
#if ASSERT_ENABLED
    LOG_WITH_STREAM(Compositing, stream << "Visited " << gVisitedPositionsCount << ", updated " << gUpdatePositionsCount << " layers, and verified " << gVerifyPositionsCount << " layers");
#endif
#if ENABLE(TREE_DEBUGGING)
    if (compositingLogEnabledRenderLayer())
        showLayerPositionTree(root());
#endif
}

bool RenderLayer::ancestorLayerPositionStateChanged(OptionSet<UpdateLayerPositionsFlag> flags)
{
    return m_hasFixedContainingBlockAncestor != flags.contains(SeenFixedContainingBlockLayer)
        || m_hasTransformedAncestor != flags.contains(SeenTransformedLayer)
        || m_has3DTransformedAncestor != flags.contains(Seen3DTransformedLayer)
        || m_hasFixedAncestor != flags.contains(SeenFixedLayer)
        || m_hasPaginatedAncestor != flags.contains(UpdatePagination)
        || m_hasCompositedScrollingAncestor != flags.contains(SeenCompositedScrollingLayer)
        || m_hasPaginatedAncestor != flags.contains(UpdatePagination);
}

#define LAYER_POSITIONS_ASSERT_ENABLED ASSERT_ENABLED || ENABLE(CONJECTURE_ASSERT)
#if ASSERT_ENABLED
#if ENABLE(TREE_DEBUGGING)
#define LAYER_POSITIONS_ASSERT(assertion, ...) do { \
    if (!(assertion)) [[unlikely]] \
        showLayerPositionTree(root(), this); \
    ASSERT(assertion, __VA_ARGS__); \
} while (0)
#define LAYER_POSITIONS_ASSERT_IMPLIES(condition, assertion) do { \
    if (condition && !(assertion)) [[unlikely]] \
        showLayerPositionTree(root(), this); \
    ASSERT_IMPLIES(condition, assertion); \
} while (0)
#else
#define LAYER_POSITIONS_ASSERT(assertion, ...) ASSERT(assertion, __VA_ARGS__)
#define LAYER_POSITIONS_ASSERT_IMPLIES(condition, assertion) ASSERT_IMPLIES(condition, assertion)
#endif // ENABLE(TREE_DEBUGGING)
#elif ENABLE(CONJECTURE_ASSERT)
#define LAYER_POSITIONS_ASSERT(assertion, ...) CONJECTURE_ASSERT(assertion, __VAR_ARGS__)
#define LAYER_POSITIONS_ASSERT_IMPLIES(condition, assertion) CONJECTURE_ASSERT_IMPLIES(condition, assertion)
#else
#define LAYER_POSITIONS_ASSERT(assertion, ...) ((void)0)
#define LAYER_POSITIONS_ASSERT_IMPLIES(condition, assertion) ((void)0)
#endif

template<RenderLayer::UpdateLayerPositionsMode mode>
void RenderLayer::recursiveUpdateLayerPositions(OptionSet<UpdateLayerPositionsFlag> flags)
{
#if ASSERT_ENABLED
    if (mode == Write)
        gVisitedPositionsCount++;
#endif

    if (ancestorLayerPositionStateChanged(flags))
        flags.add(SubtreeNeedsUpdate);

    if (mode == Write && !needsPositionUpdate() && !flags.containsAny(invalidationLayerPositionsFlags())) {
#if LAYER_POSITIONS_ASSERT_ENABLED
        recursiveUpdateLayerPositions<Verify>(flags);
#endif
        return;
    }

    if (m_layerPositionDirtyBits.contains(LayerPositionUpdates::AllDescendantsNeedPositionUpdate)) {
        LAYER_POSITIONS_ASSERT(mode != Verify);
        flags.add(SubtreeNeedsUpdate);
    }

    if (updateLayerPosition(&flags, mode))
        flags.add(SubtreeNeedsUpdate);

#if ASSERT_ENABLED
    if (mode == Write)
        gUpdatePositionsCount++;
    else
        gVerifyPositionsCount++;
#endif

    if (m_scrollableArea) {
        LAYER_POSITIONS_ASSERT_IMPLIES(mode == Verify, !m_scrollableArea->hasPostLayoutScrollPosition());
        m_scrollableArea->applyPostLayoutScrollPositionIfNeeded();
    }

    // Clear our cached clip rect information.
    if (mode == Write)
        clearClipRects();
    else
        verifyClipRects();

    if (mode == Write && m_scrollableArea && m_scrollableArea->hasOverflowControls()) {
        // FIXME: It looks suspicious to call convertToLayerCoords here
        // as canUseOffsetFromAncestor may be true for an ancestor layer.
        auto offsetFromRoot = offsetFromAncestor(root());
#if LAYER_POSITIONS_ASSERT_ENABLED
        bool changed =
#endif
        m_scrollableArea->positionOverflowControls(roundedIntSize(offsetFromRoot));
        LAYER_POSITIONS_ASSERT_IMPLIES(mode == Verify, !changed);
    }

    if (mode == Write)
        updateDescendantDependentFlags();
    else {
        LAYER_POSITIONS_ASSERT(!m_visibleDescendantStatusDirty);
        LAYER_POSITIONS_ASSERT(!m_hasSelfPaintingLayerDescendantDirty);
        LAYER_POSITIONS_ASSERT(!m_hasViewportConstrainedDescendantStatusDirty);
        LAYER_POSITIONS_ASSERT(!hasNotIsolatedBlendingDescendantsStatusDirty());
        LAYER_POSITIONS_ASSERT(!m_visibleContentStatusDirty);
    }

    if (flags & UpdatePagination) {
#if LAYER_POSITIONS_ASSERT_ENABLED
        CheckedPtr oldEnclosingPaginationLayer = m_enclosingPaginationLayer.get();
#endif
        updatePagination();
        LAYER_POSITIONS_ASSERT_IMPLIES(mode == Verify, m_enclosingPaginationLayer.get() ==  oldEnclosingPaginationLayer);
    } else if (renderer().isRenderFragmentedFlow()) {
        LAYER_POSITIONS_ASSERT_IMPLIES(mode == Verify, m_enclosingPaginationLayer.get() == this);
        m_enclosingPaginationLayer = *this;
        flags.add(UpdatePagination);
    } else {
        LAYER_POSITIONS_ASSERT_IMPLIES(mode == Verify, !m_enclosingPaginationLayer.get());
        m_enclosingPaginationLayer = nullptr;
    }

    if (renderer().isSVGLayerAwareRenderer() && renderer().document().settings().layerBasedSVGEngineEnabled()) {
        if (!is<RenderSVGRoot>(renderer())) {
            ASSERT(!renderer().isFixedPositioned());
            if (mode == Write)
                m_repaintStatus = RepaintStatus::NeedsFullRepaint;
        }

        // Only the outermost <svg> and / <foreignObject> are potentially scrollable.
        ASSERT_IMPLIES(is<RenderSVGModelObject>(renderer()) || is<RenderSVGText>(renderer()) || is<RenderSVGInline>(renderer()), !m_scrollableArea);
    }

    auto repaintIfNecessary = [&](bool checkForRepaint) {
        if (mode == Verify) {
            WeakPtr repaintContainer = renderer().containerForRepaint().renderer.get();
            LAYER_POSITIONS_ASSERT(repaintRects() || (isVisibilityHiddenOrOpacityZero() || !isSelfPaintingLayer()));
            if (isVisibilityHiddenOrOpacityZero())
                LAYER_POSITIONS_ASSERT(!m_repaintContainer);
            else
                LAYER_POSITIONS_ASSERT(m_repaintContainer == repaintContainer);
            LAYER_POSITIONS_ASSERT_IMPLIES(repaintRects(), *repaintRects() == renderer().rectsForRepaintingAfterLayout(repaintContainer.get(), RepaintOutlineBounds::Yes));
            return;
        }

        // FIXME: Paint offset cache does not work with RenderLayers as there is not a 1-to-1
        // mapping between them and the RenderObjects. It would be neat to enable
        // LayoutState outside the layout() phase and use it here.
        ASSERT(!renderer().view().frameView().layoutContext().isPaintOffsetCacheEnabled());

        WeakPtr repaintContainer = renderer().containerForRepaint().renderer.get();

        auto oldRects = repaintRects();
        computeRepaintRects(repaintContainer.get());
        auto newRects = repaintRects();

        if (checkForRepaint && shouldRepaintAfterLayout() && newRects) {
            auto needsFullRepaint = repaintStatus() == RepaintStatus::NeedsFullRepaint ? RequiresFullRepaint::Yes : RequiresFullRepaint::No;
            auto resolvedOldRects = valueOrDefault(oldRects);
            renderer().repaintAfterLayoutIfNeeded(WTFMove(repaintContainer), needsFullRepaint, resolvedOldRects, *newRects);
        }
    };

    repaintIfNecessary(flags.contains(CheckForRepaint));

#define UPDATE_OR_VERIFY_STATE_BIT(dest, source) \
    if (mode == Write) \
        dest = source; \
    else \
        LAYER_POSITIONS_ASSERT(dest == source);
    UPDATE_OR_VERIFY_STATE_BIT(this->m_repaintStatus, RepaintStatus::NeedsNormalRepaint);
    UPDATE_OR_VERIFY_STATE_BIT(m_hasFixedContainingBlockAncestor, flags.contains(SeenFixedContainingBlockLayer));
    UPDATE_OR_VERIFY_STATE_BIT(m_hasTransformedAncestor, flags.contains(SeenTransformedLayer));
    UPDATE_OR_VERIFY_STATE_BIT(m_has3DTransformedAncestor, flags.contains(Seen3DTransformedLayer));
    UPDATE_OR_VERIFY_STATE_BIT(m_hasFixedAncestor, flags.contains(SeenFixedLayer));
    UPDATE_OR_VERIFY_STATE_BIT(m_hasStickyAncestor, flags.contains(SeenStickyLayer))
    UPDATE_OR_VERIFY_STATE_BIT(m_hasPaginatedAncestor, flags.contains(UpdatePagination));
    UPDATE_OR_VERIFY_STATE_BIT(m_hasCompositedScrollingAncestor, flags.contains(SeenCompositedScrollingLayer));
#undef UPDATE_OR_VERIFY_STATE_BIT

    // Update the reflection's position and size.
    if (m_reflection) {
        if (mode == Write)
            m_reflection->layout();
        else
            LAYER_POSITIONS_ASSERT(!m_reflection->needsLayout());

    }

    if (!isRenderViewLayer()) {
        if (renderer().canContainFixedPositionObjects())
            flags.add(SeenFixedContainingBlockLayer);

        if (transform()) {
            flags.add(SeenTransformedLayer);
            if (!transform()->isAffine())
                flags.add(Seen3DTransformedLayer);
        }
    }

    // Fixed inside transform behaves like absolute (per spec).
    if (m_hasFixedAncestor || (renderer().isFixedPositioned() && !m_hasFixedContainingBlockAncestor)) {
        LAYER_POSITIONS_ASSERT_IMPLIES(mode == Verify, behavesAsFixed());
        setBehavesAsFixed(true);
        flags.add(SeenFixedLayer);
    }

    if (m_hasStickyAncestor || renderer().isStickilyPositioned())
        flags.add(SeenStickyLayer);

    if (hasCompositedScrollableOverflow())
        flags.add(SeenCompositedScrollingLayer);

    if (flags.containsAny(invalidationLayerPositionsFlags()) || m_layerPositionDirtyBits.contains(LayerPositionUpdates::DescendantNeedsPositionUpdate)) {
        for (RenderLayer* child = firstChild(); child; child = child->nextSibling())
            child->recursiveUpdateLayerPositions<mode>(flags);
    } else {
#if LAYER_POSITIONS_ASSERT_ENABLED
        for (RenderLayer* child = firstChild(); child; child = child->nextSibling())
            child->recursiveUpdateLayerPositions<Verify>(flags);
#endif
    }

    // FIXME: Verify?
    if (mode == Write && m_scrollableArea)
        m_scrollableArea->updateMarqueePosition();

    if (renderer().isFixedPositioned() && renderer().settings().acceleratedCompositingForFixedPositionEnabled()) {
        bool intersectsViewport = compositor().fixedLayerIntersectsViewport(*this);
        LAYER_POSITIONS_ASSERT_IMPLIES(mode == Verify, intersectsViewport == m_isFixedIntersectingViewport);
        if (intersectsViewport != m_isFixedIntersectingViewport) {
            m_isFixedIntersectingViewport = intersectsViewport;
            setNeedsPostLayoutCompositingUpdate();
        }
    }

    LAYER_POSITIONS_ASSERT_IMPLIES(mode == Verify, !flags.contains(ContainingClippingLayerChangedSize));
    LAYER_POSITIONS_ASSERT_IMPLIES(mode == Verify, !flags.contains(NeedsFullRepaintInBacking));
    if (mode == Write && isComposited())
        backing()->updateAfterLayout(flags.contains(ContainingClippingLayerChangedSize), flags.contains(NeedsFullRepaintInBacking));

    LAYER_POSITIONS_ASSERT_IMPLIES(mode == Verify, m_layerPositionDirtyBits.isEmpty());
    ASSERT(m_backingProviderLayer == m_backingProviderLayerAtEndOfCompositingUpdate);
    clearLayerPositionDirtyBits();
}

LayoutRect RenderLayer::repaintRectIncludingNonCompositingDescendants() const
{
    LayoutRect repaintRect;
    if (m_repaintRectsValid)
        repaintRect = m_repaintRects.clippedOverflowRect;
    for (RenderLayer* child = firstChild(); child; child = child->nextSibling()) {
        // Don't include repaint rects for composited child layers; they will paint themselves and have a different origin.
        if (child->isComposited())
            continue;

        repaintRect.uniteIfNonZero(child->repaintRectIncludingNonCompositingDescendants());
    }
    return repaintRect;
}

void RenderLayer::setRepaintStatus(RepaintStatus status)
{
    if (status != m_repaintStatus) {
        m_repaintStatus = status;
        setNeedsPositionUpdate();
    }
}

void RenderLayer::setAncestorChainHasSelfPaintingLayerDescendant()
{
    for (RenderLayer* layer = this; layer; layer = layer->parent()) {
        if (renderer().shouldApplyPaintContainment()) {
            m_hasSelfPaintingLayerDescendant = true;
            m_hasSelfPaintingLayerDescendantDirty = false;
            break;
        }
        if (!layer->m_hasSelfPaintingLayerDescendantDirty && layer->hasSelfPaintingLayerDescendant())
            break;

        layer->m_hasSelfPaintingLayerDescendantDirty = false;
        layer->m_hasSelfPaintingLayerDescendant = true;
    }
}

void RenderLayer::dirtyAncestorChainHasSelfPaintingLayerDescendantStatus()
{
    for (RenderLayer* layer = this; layer; layer = layer->parent()) {
        if (layer->m_hasSelfPaintingLayerDescendantDirty)
            break;

        layer->m_hasSelfPaintingLayerDescendantDirty = true;
        layer->setNeedsPositionUpdate();
    }
}

void RenderLayer::setAncestorChainHasViewportConstrainedDescendant()
{
    for (CheckedPtr layer = this; layer; layer = layer->parent()) {
        if (!layer->m_hasViewportConstrainedDescendantStatusDirty && layer->m_hasViewportConstrainedDescendant)
            break;

        layer->m_hasViewportConstrainedDescendant = true;
        layer->m_hasViewportConstrainedDescendantStatusDirty = false;
    }
}

void RenderLayer::dirtyAncestorChainHasViewportConstrainedDescendantStatus()
{
    for (CheckedPtr layer = this; layer; layer = layer->parent()) {
        if (layer->m_hasViewportConstrainedDescendantStatusDirty)
            break;

        layer->m_hasViewportConstrainedDescendantStatusDirty = true;
    }
}

std::optional<LayoutRect> RenderLayer::cachedClippedOverflowRect() const
{
    if (!m_repaintRectsValid)
        return std::nullopt;

    return m_repaintRects.clippedOverflowRect;
}

void RenderLayer::computeRepaintRects(const RenderLayerModelObject* repaintContainer)
{
    ASSERT(!m_visibleContentStatusDirty);

    if (isVisibilityHiddenOrOpacityZero() || !isSelfPaintingLayer())
        clearRepaintRects();
    else
        setRepaintRects(renderer().rectsForRepaintingAfterLayout(repaintContainer, RepaintOutlineBounds::Yes));

    if (isVisibilityHiddenOrOpacityZero())
        m_repaintContainer = nullptr;
    else
        m_repaintContainer = repaintContainer;
}

void RenderLayer::computeRepaintRectsIncludingDescendants()
{
    // FIXME: computeRepaintRects() has to walk up the parent chain for every layer to compute the rects.
    // We should make this more efficient.
    computeRepaintRects(renderer().containerForRepaint().renderer.get());
    clearClipRects(PaintingClipRects);

    for (RenderLayer* layer = firstChild(); layer; layer = layer->nextSibling())
        layer->computeRepaintRectsIncludingDescendants();
}

void RenderLayer::compositingStatusChanged(LayoutUpToDate layoutUpToDate)
{
    updateDescendantDependentFlags();
    if (parent() || isRenderViewLayer())
        computeRepaintRectsIncludingDescendants();
    if (layoutUpToDate == LayoutUpToDate::No)
        setSelfAndDescendantsNeedPositionUpdate();
}

void RenderLayer::setRepaintRects(const RenderObject::RepaintRects& rects)
{
    m_repaintRects = rects;
    m_repaintRectsValid = true;
}

void RenderLayer::clearRepaintRects()
{
    m_repaintRectsValid = false;
}

void RenderLayer::updateLayerPositionsAfterOverflowScroll()
{
    willUpdateLayerPositions();

    // FIXME: why is it OK to not check the ancestors of this layer in order to
    // initialize the HasSeenViewportConstrainedAncestor and HasSeenAncestorWithOverflowClip flags?
    recursiveUpdateLayerPositionsAfterScroll(RenderLayer::IsOverflowScroll);
}

void RenderLayer::updateLayerPositionsAfterDocumentScroll()
{
    ASSERT(isRenderViewLayer());
    LOG(Scrolling, "RenderLayer::updateLayerPositionsAfterDocumentScroll");

    willUpdateLayerPositions();
    recursiveUpdateLayerPositionsAfterScroll();
}

void RenderLayer::recursiveUpdateLayerPositionsAfterScroll(OptionSet<UpdateLayerPositionsAfterScrollFlag> flags)
{
    // FIXME: This shouldn't be needed, but there are some corner cases where
    // these flags are still dirty. Update so that the check below is valid.
    updateDescendantDependentFlags();

    // If we have no visible content and no visible descendants, there is no point recomputing
    // our rectangles as they will be empty. If our visibility changes, we are expected to
    // recompute all our positions anyway.
    if (!m_hasVisibleDescendant && !m_hasVisibleContent)
        return;

    bool positionChanged = updateLayerPosition();
    if (positionChanged)
        flags.add(HasChangedAncestor);

    if (flags.containsAny({ HasChangedAncestor, HasSeenViewportConstrainedAncestor, IsOverflowScroll }))
        clearClipRects();

    if (renderer().style().hasViewportConstrainedPosition())
        flags.add(HasSeenViewportConstrainedAncestor);

    if (renderer().hasNonVisibleOverflow())
        flags.add(HasSeenAncestorWithOverflowClip);
    
    bool shouldComputeRepaintRects = (flags.contains(HasSeenViewportConstrainedAncestor) || flags.containsAll({ IsOverflowScroll, HasSeenAncestorWithOverflowClip })) && isSelfPaintingLayer();
    // FIXME: We could track the repaint container as we walk down the tree.
    if (shouldComputeRepaintRects)
        computeRepaintRects(renderer().containerForRepaint().renderer.get());

    for (auto* child = firstChild(); child; child = child->nextSibling())
        child->recursiveUpdateLayerPositionsAfterScroll(flags);

    // We don't update our reflection as scrolling is a translation which does not change the size()
    // of an object, thus RenderReplica will still repaint itself properly as the layer position was
    // updated above.

    if (m_scrollableArea)
        m_scrollableArea->updateMarqueePosition();
}

void RenderLayer::updateBlendMode()
{
    bool hadBlendMode = static_cast<BlendMode>(m_blendMode) != BlendMode::Normal;
    if (parent() && hadBlendMode != hasBlendMode()) {
        if (hasBlendMode())
            parent()->updateAncestorChainHasBlendingDescendants();
        else
            parent()->dirtyAncestorChainHasBlendingDescendants();
    }

    BlendMode newBlendMode = renderer().style().blendMode();
    if (newBlendMode != static_cast<BlendMode>(m_blendMode))
        m_blendMode = static_cast<unsigned>(newBlendMode);
}

void RenderLayer::willRemoveChildWithBlendMode()
{
    parent()->dirtyAncestorChainHasBlendingDescendants();
}

void RenderLayer::updateAncestorChainHasBlendingDescendants()
{
    for (auto* layer = this; layer; layer = layer->parent()) {
        if (!layer->hasNotIsolatedBlendingDescendantsStatusDirty() && layer->hasNotIsolatedBlendingDescendants())
            break;
        layer->m_hasNotIsolatedBlendingDescendants = true;
        layer->m_hasNotIsolatedBlendingDescendantsStatusDirty = false;

        layer->updateSelfPaintingLayer();

        if (layer->isCSSStackingContext())
            break;
    }
}

void RenderLayer::dirtyAncestorChainHasBlendingDescendants()
{
    for (auto* layer = this; layer; layer = layer->parent()) {
        if (layer->hasNotIsolatedBlendingDescendantsStatusDirty())
            break;
        
        layer->m_hasNotIsolatedBlendingDescendantsStatusDirty = true;
        layer->setNeedsPositionUpdate();
    }
}

void RenderLayer::setIntrinsicallyComposited(bool composited)
{
    m_intrinsicallyComposited = composited;
    updateAlwaysIncludedInZOrderLists();
}

void RenderLayer::updateAlwaysIncludedInZOrderLists()
{
    bool alwaysIncludedInZOrderLists = m_intrinsicallyComposited  || renderer().hasViewTransitionName();
    if (m_alwaysIncludedInZOrderLists != alwaysIncludedInZOrderLists) {
        m_alwaysIncludedInZOrderLists = alwaysIncludedInZOrderLists;
        if (alwaysIncludedInZOrderLists)
            updateAncestorChainHasAlwaysIncludedInZOrderListsDescendants();
        else
            dirtyAncestorChainHasAlwaysIncludedInZOrderListsDescendants();
        if (!hasVisibleContent() && !isNormalFlowOnly())
            dirtyHiddenStackingContextAncestorZOrderLists();
    }
}

void RenderLayer::updateAncestorChainHasAlwaysIncludedInZOrderListsDescendants()
{
    for (auto* layer = this; layer; layer = layer->parent()) {
        if (!layer->m_hasAlwaysIncludedInZOrderListsDescendantsStatusDirty && layer->m_hasAlwaysIncludedInZOrderListsDescendants)
            break;
        layer->m_hasAlwaysIncludedInZOrderListsDescendants = true;
        layer->m_hasAlwaysIncludedInZOrderListsDescendantsStatusDirty = false;
    }
}

void RenderLayer::dirtyAncestorChainHasAlwaysIncludedInZOrderListsDescendants()
{
    for (auto* layer = this; layer; layer = layer->parent()) {
        if (layer->m_hasAlwaysIncludedInZOrderListsDescendantsStatusDirty)
            break;

        layer->m_hasAlwaysIncludedInZOrderListsDescendantsStatusDirty = true;
    }
}

FloatRect RenderLayer::referenceBoxRectForClipPath(CSSBoxType boxType, const LayoutSize& offsetFromRoot, const LayoutRect& rootRelativeBounds) const
{
    bool isReferenceBox = false;

    if (renderer().document().settings().layerBasedSVGEngineEnabled() && renderer().isSVGLayerAwareRenderer())
        isReferenceBox = true;
    else
        isReferenceBox = renderer().isRenderBox();

    // FIXME: Support different reference boxes for inline content.
    // https://bugs.webkit.org/show_bug.cgi?id=129047
    if (!isReferenceBox)
        return rootRelativeBounds;

    auto referenceBoxRect = renderer().referenceBoxRect(boxType);
    referenceBoxRect.move(offsetFromRoot);
    return referenceBoxRect;
}

void RenderLayer::updateTransformFromStyle(TransformationMatrix& transform, const RenderStyle& style, OptionSet<RenderStyle::TransformOperationOption> options) const
{
    // https://drafts.csswg.org/css-anchor-position-1/#default-scroll-shift
    // > After layout has been performed for abspos, it is additionally shifted by
    // > the default scroll shift, as if affected by a transform
    // > ** (before any other transforms). **
    if (m_snapshottedScrollOffsetForAnchorPositioning)
        transform.translate(m_snapshottedScrollOffsetForAnchorPositioning->width(), m_snapshottedScrollOffsetForAnchorPositioning->height());

    auto referenceBoxRect = snapRectToDevicePixelsIfNeeded(renderer().transformReferenceBoxRect(style), renderer());
    renderer().applyTransform(transform, style, referenceBoxRect, options);

    makeMatrixRenderable(transform, canRender3DTransforms());
}

void RenderLayer::updateTransform()
{
    bool hasTransform = isTransformed();
    bool had3DTransform = has3DTransform();

    std::unique_ptr<TransformationMatrix> oldTransform;
    if (m_transform && hasTransform)
        oldTransform = makeUnique<TransformationMatrix>(*m_transform);
    if (hasTransform != !!m_transform) {
        if (hasTransform)
            m_transform = makeUnique<TransformationMatrix>();
        else
            m_transform = nullptr;

        // Layers with transforms act as clip rects roots, so clear the cached clip rects here.
        clearClipRectsIncludingDescendants();
        setSelfAndDescendantsNeedPositionUpdate();
        LOG_WITH_STREAM(Compositing, stream << "Changed transform for " << this);
    }
    
    if (hasTransform) {
        m_transform->makeIdentity();
        updateTransformFromStyle(*m_transform, renderer().style(), RenderStyle::allTransformOperations());
    }

    if (had3DTransform != has3DTransform()) {
        dirty3DTransformedDescendantStatus();
        // Having a 3D transform affects whether enclosing perspective and preserve-3d layers composite, so trigger an update.
        setNeedsPostLayoutCompositingUpdateOnAncestors();
    }

    if (oldTransform && m_transform && *oldTransform != *m_transform) {
        LOG_WITH_STREAM(Compositing, stream << "Changed transform value for " << this << " from " << *oldTransform << " to " << *m_transform);
        setSelfAndDescendantsNeedPositionUpdate();
    }
}

void RenderLayer::forceStackingContextIfNeeded()
{
    if (setIsCSSStackingContext(true)) {
        setIsNormalFlowOnly(false);
        if (parent()) {
            if (!hasNotIsolatedBlendingDescendantsStatusDirty() && hasNotIsolatedBlendingDescendants())
                parent()->dirtyAncestorChainHasBlendingDescendants();
        }
    }
}

TransformationMatrix RenderLayer::currentTransform(OptionSet<RenderStyle::TransformOperationOption> options) const
{
    if (!m_transform)
        return { };

    // m_transform includes transform-origin and is affected by the choice of the transform-box.
    // Therefore we can only use the cached m_transform, if the animation doesn't alter transform-box or excludes transform-origin.

    // Query the animatedStyle() to obtain the current transformation, when accelerated transform animations are running.
    auto styleable = Styleable::fromRenderer(renderer());
    if ((styleable && styleable->isRunningAcceleratedAnimationOfProperty(CSSPropertyTransform)) || !options.contains(RenderStyle::TransformOperationOption::TransformOrigin)) {
        std::unique_ptr<RenderStyle> animatedStyle = renderer().animatedStyle();

        TransformationMatrix transform;
        updateTransformFromStyle(transform, *animatedStyle, options);
        return transform;
    }

    return *m_transform;
}

TransformationMatrix RenderLayer::currentTransform() const
{
    return currentTransform(RenderStyle::allTransformOperations());
}

TransformationMatrix RenderLayer::renderableTransform(OptionSet<PaintBehavior> paintBehavior) const
{
    if (!m_transform)
        return TransformationMatrix();
    
    if (paintBehavior & PaintBehavior::FlattenCompositingLayers) {
        TransformationMatrix matrix = *m_transform;
        makeMatrixRenderable(matrix, false /* flatten 3d */);
        return matrix;
    }

    return *m_transform;
}

RenderLayer* RenderLayer::enclosingOverflowClipLayer(IncludeSelfOrNot includeSelf) const
{
    const RenderLayer* layer = (includeSelf == IncludeSelf) ? this : parent();
    while (layer) {
        if (layer->renderer().hasPotentiallyScrollableOverflow())
            return const_cast<RenderLayer*>(layer);

        layer = layer->parent();
    }
    return nullptr;
}

// FIXME: This is terrible. Bring back a cached bit for this someday. This crawl is going to slow down all
// painting of content inside paginated layers.
bool RenderLayer::hasCompositedLayerInEnclosingPaginationChain() const
{
    // No enclosing layer means no compositing in the chain.
    if (!m_enclosingPaginationLayer)
        return false;
    
    // If the enclosing layer is composited, we don't have to check anything in between us and that
    // layer.
    if (m_enclosingPaginationLayer->isComposited())
        return true;

    // If we are the enclosing pagination layer, then we can't be composited or we'd have passed the
    // previous check.
    if (m_enclosingPaginationLayer == this)
        return false;

    // The enclosing paginated layer is our ancestor and is not composited, so we have to check
    // intermediate layers between us and the enclosing pagination layer. Start with our own layer.
    if (isComposited())
        return true;
    
    // For normal flow layers, we can recur up the layer tree.
    if (isNormalFlowOnly())
        return parent()->hasCompositedLayerInEnclosingPaginationChain();
    
    // Otherwise we have to go up the containing block chain. Find the first enclosing
    // containing block layer ancestor, and check that.
    for (const auto* containingBlock = renderer().containingBlock(); containingBlock && !is<RenderView>(*containingBlock); containingBlock = containingBlock->containingBlock()) {
        if (containingBlock->hasLayer())
            return containingBlock->layer()->hasCompositedLayerInEnclosingPaginationChain();
    }
    return false;
}

void RenderLayer::updatePagination()
{
    m_enclosingPaginationLayer = nullptr;
    
    if (!parent())
        return;
    
    // Each layer that is inside a multicolumn flow thread has to be checked individually and
    // genuinely know if it is going to have to split itself up when painting only its contents (and not any other descendant
    // layers). We track an enclosingPaginationLayer instead of using a simple bit, since we want to be able to get back
    // to that layer easily.
    if (renderer().isRenderFragmentedFlow()) {
        m_enclosingPaginationLayer = *this;
        return;
    }

    if (isNormalFlowOnly()) {
        // Content inside a transform is not considered to be paginated, since we simply
        // paint the transform multiple times in each column, so we don't have to use
        // fragments for the transformed content.
        if (parent()->isTransformed())
            m_enclosingPaginationLayer = nullptr;
        else
            m_enclosingPaginationLayer = parent()->enclosingPaginationLayer(IncludeCompositedPaginatedLayers);
        return;
    }

    // For the new columns code, we want to walk up our containing block chain looking for an enclosing layer. Once
    // we find one, then we just check its pagination status.
    for (const auto* containingBlock = renderer().containingBlock(); containingBlock && !is<RenderView>(*containingBlock); containingBlock = containingBlock->containingBlock()) {
        if (containingBlock->hasLayer()) {
            // Content inside a transform is not considered to be paginated, since we simply
            // paint the transform multiple times in each column, so we don't have to use
            // fragments for the transformed content.
            if (containingBlock->layer()->isTransformed())
                m_enclosingPaginationLayer = nullptr;
            else
                m_enclosingPaginationLayer = containingBlock->layer()->enclosingPaginationLayer(IncludeCompositedPaginatedLayers);
            return;
        }
    }
}

void RenderLayer::setBehavesAsFixed(bool behavesAsFixed)
{
    if (m_behavesAsFixed != behavesAsFixed && renderer().isFixedPositioned())
        setNeedsCompositingConfigurationUpdate();

    m_behavesAsFixed = behavesAsFixed;
}

void RenderLayer::setHasVisibleContent()
{ 
    if (m_hasVisibleContent && !m_visibleContentStatusDirty) {
        ASSERT(!parent() || parent()->m_visibleDescendantStatusDirty || parent()->hasVisibleDescendant());
        return;
    }

    m_visibleContentStatusDirty = false; 
    m_hasVisibleContent = true;
    computeRepaintRects(renderer().containerForRepaint().renderer.get());
    setNeedsPositionUpdate();
    if (!isNormalFlowOnly()) {
        // We don't collect invisible layers in z-order lists if they are not composited.
        // As we became visible, we need to dirty our stacking containers ancestors to be properly
        // collected.
        dirtyHiddenStackingContextAncestorZOrderLists();
    }

    if (parent())
        parent()->dirtyAncestorChainVisibleDescendantStatus();
}

void RenderLayer::dirtyVisibleContentStatus() 
{ 
    m_visibleContentStatusDirty = true;
    setNeedsPositionUpdate();
    if (parent())
        parent()->dirtyAncestorChainVisibleDescendantStatus();
}

void RenderLayer::dirtyAncestorChainVisibleDescendantStatus()
{
    setNeedsPositionUpdate();
    for (auto* layer = this; layer; layer = layer->parent()) {
        if (layer->m_visibleDescendantStatusDirty)
            break;

        layer->m_visibleDescendantStatusDirty = true;
    }
}

void RenderLayer::updateAncestorDependentState()
{
    m_enclosingSVGHiddenOrResourceContainer = nullptr;
    auto determineSVGAncestors = [&] (const RenderElement& renderer) {
        for (auto* ancestor = renderer.parent(); ancestor; ancestor = ancestor->parent()) {
            if (auto* container = dynamicDowncast<RenderSVGHiddenContainer>(ancestor)) {
                m_enclosingSVGHiddenOrResourceContainer = container;
                return;
            }
        }
    };
    if (renderer().document().settings().layerBasedSVGEngineEnabled())
        determineSVGAncestors(renderer());

    bool insideSVGForeignObject = false;
    if (renderer().document().mayHaveRenderedSVGForeignObjects()) {
        if (ancestorsOfType<LegacyRenderSVGForeignObject>(renderer()).first())
            insideSVGForeignObject = true;
        else if (renderer().document().settings().layerBasedSVGEngineEnabled() && ancestorsOfType<RenderSVGForeignObject>(renderer()).first())
            insideSVGForeignObject = true;
    }

    if (insideSVGForeignObject == m_insideSVGForeignObject)
        return;

    m_insideSVGForeignObject = insideSVGForeignObject;
    updateSelfPaintingLayer();
}

void RenderLayer::updateDescendantDependentFlags()
{
    if (m_visibleDescendantStatusDirty || m_hasSelfPaintingLayerDescendantDirty || hasNotIsolatedBlendingDescendantsStatusDirty() || m_hasAlwaysIncludedInZOrderListsDescendantsStatusDirty || m_hasViewportConstrainedDescendantStatusDirty) {
        bool hasVisibleDescendant = false;
        bool hasSelfPaintingLayerDescendant = false;
        bool hasNotIsolatedBlendingDescendants = false;
        bool hasAlwaysIncludedInZOrderListsDescendants = false;
        bool hasViewportConstrainedDescendant = false;

        if (m_hasNotIsolatedBlendingDescendantsStatusDirty) {
            m_hasNotIsolatedBlendingDescendantsStatusDirty = false;
            updateSelfPaintingLayer();
        }

        for (RenderLayer* child = firstChild(); child; child = child->nextSibling()) {
            child->updateDescendantDependentFlags();

            hasVisibleDescendant |= child->m_hasVisibleContent || child->m_hasVisibleDescendant;
            hasSelfPaintingLayerDescendant |= child->isSelfPaintingLayer() || child->hasSelfPaintingLayerDescendant();
            hasNotIsolatedBlendingDescendants |= child->hasBlendMode() || (child->hasNotIsolatedBlendingDescendants() && !child->isolatesBlending());
            hasAlwaysIncludedInZOrderListsDescendants |= child->alwaysIncludedInZOrderLists() || child->m_hasAlwaysIncludedInZOrderListsDescendants;
            hasViewportConstrainedDescendant |= child->m_hasViewportConstrainedDescendant || child->isViewportConstrained();
        }

        m_hasVisibleDescendant = hasVisibleDescendant;
        m_visibleDescendantStatusDirty = false;
        m_hasSelfPaintingLayerDescendant = hasSelfPaintingLayerDescendant;
        m_hasSelfPaintingLayerDescendantDirty = false;
        m_hasAlwaysIncludedInZOrderListsDescendants = hasAlwaysIncludedInZOrderListsDescendants;
        m_hasAlwaysIncludedInZOrderListsDescendantsStatusDirty = false;
        m_hasViewportConstrainedDescendant = hasViewportConstrainedDescendant;
        m_hasViewportConstrainedDescendantStatusDirty = false;

        m_hasNotIsolatedBlendingDescendants = hasNotIsolatedBlendingDescendants;
    }

    if (m_visibleContentStatusDirty) {
        //  We need the parent to know if we have skipped content or content-visibility root.
        if (renderer().style().isSkippedRootOrSkippedContent() && !renderer().parent())
            return;
        bool hasVisibleContent = computeHasVisibleContent();
        if (hasVisibleContent != m_hasVisibleContent) {
            m_hasVisibleContent = hasVisibleContent;
            if (!isNormalFlowOnly()) {
                // We don't collect invisible layers in z-order lists if they are not composited.
                // As we change visibility, we need to dirty our stacking containers ancestors to be properly
                // collected.
                dirtyHiddenStackingContextAncestorZOrderLists();
            }
        }
        m_visibleContentStatusDirty = false;
    }

    ASSERT(!descendantDependentFlagsAreDirty());
}

bool RenderLayer::computeHasVisibleContent() const
{
    if (renderer().isAnonymous() && is<RenderSVGViewportContainer>(renderer()))
        return false;

    if (m_isHiddenByOverflowTruncation)
        return false;

    if (renderer().isSkippedContent())
        return false;

    if (renderer().style().usedVisibility() == Visibility::Visible)
        return true;

    // Layer's renderer has visibility:hidden, but some non-layer child may have visibility:visible.
    auto nextRenderer = [&] (auto& renderer) -> const RenderObject* {
        for (auto* ancestor = &renderer; ancestor && ancestor != &this->renderer(); ancestor = ancestor->parent()) {
            if (auto* sibling = ancestor->nextSibling())
                return sibling;
        }
        return { };
    };
    const auto* renderer = this->renderer().firstChild();
    while (renderer) {
        if (CheckedPtr renderElement = dynamicDowncast<RenderElement>(renderer); renderElement && !renderElement->hasLayer()) {
            if (renderElement->style().usedVisibility() == Visibility::Visible)
                return true;
            if (auto* firstChild = renderElement->firstChild()) {
                renderer = firstChild;
                continue;
            }
        }
        renderer = nextRenderer(*renderer);
    }
    return false;
}

static LayoutRect computeLayerPositionAndIntegralSize(const RenderLayerModelObject& renderer)
{
    if (auto* inlineRenderer = dynamicDowncast<RenderInline>(renderer); inlineRenderer && inlineRenderer->isInline())
        return { LayoutPoint(), inlineRenderer->linesBoundingBox().size() };

    if (auto* boxRenderer = dynamicDowncast<RenderBox>(renderer)) {
        const auto& frameRect = boxRenderer->frameRect();
        return { boxRenderer->topLeftLocation(), snappedIntSize(frameRect.size(), frameRect.location()) };
    }

    if (auto* svgModelObjectRenderer = dynamicDowncast<RenderSVGModelObject>(renderer)) {
        const auto& frameRect = svgModelObjectRenderer->frameRectEquivalent();
        return { svgModelObjectRenderer->topLeftLocationEquivalent(), enclosingIntRect(frameRect).size() };
    }

    ASSERT_NOT_REACHED();
    return { };
}

void RenderLayer::dirty3DTransformedDescendantStatus()
{
    RenderLayer* curr = stackingContext();
    if (curr)
        curr->m_3DTransformedDescendantStatusDirty = true;
        
    // This propagates up through preserve-3d hierarchies to the enclosing flattening layer.
    // Note that preserves3D() creates stacking context, so we can just run up the stacking containers.
    while (curr && curr->preserves3D()) {
        curr->m_3DTransformedDescendantStatusDirty = true;
        curr = curr->stackingContext();
    }
}

// Return true if this layer or any preserve-3d descendants have 3d.
bool RenderLayer::update3DTransformedDescendantStatus()
{
    if (m_3DTransformedDescendantStatusDirty) {
        m_has3DTransformedDescendant = false;

        updateZOrderLists();

        // Transformed or preserve-3d descendants can only be in the z-order lists, not
        // in the normal flow list, so we only need to check those.
        for (auto* layer : positiveZOrderLayers())
            m_has3DTransformedDescendant |= layer->update3DTransformedDescendantStatus();

        // Now check our negative z-index children.
        for (auto* layer : negativeZOrderLayers())
            m_has3DTransformedDescendant |= layer->update3DTransformedDescendantStatus();
        
        m_3DTransformedDescendantStatusDirty = false;
    }
    
    // If we live in a 3d hierarchy, then the layer at the root of that hierarchy needs
    // the m_has3DTransformedDescendant set.
    if (preserves3D())
        return has3DTransform() || m_has3DTransformedDescendant;

    return has3DTransform();
}

bool RenderLayer::updateLayerPosition(OptionSet<UpdateLayerPositionsFlag>* flags, UpdateLayerPositionsMode mode)
{
    auto layerRect = computeLayerPositionAndIntegralSize(renderer());
    auto localPoint = layerRect.location();

    bool geometryChanged = false;
    if (IntSize newSize(layerRect.width().toInt(), layerRect.height().toInt()); newSize != size()) {
        geometryChanged = true;
        setSize(newSize);

#if LAYER_POSITIONS_ASSERT_ENABLED
        LAYER_POSITIONS_ASSERT(mode == Write);
#else
        UNUSED_PARAM(mode);
#endif

        if (flags && renderer().hasNonVisibleOverflow())
            flags->add(ContainingClippingLayerChangedSize);

        // Trigger RenderLayerCompositor::requiresCompositingForFrame() which depends on the contentBoxRect size.
        if (compositor().hasCompositedWidgetContents(renderer()))
            setNeedsPostLayoutCompositingUpdate();
    }

    if (!renderer().isOutOfFlowPositioned()) {
        auto* ancestor = renderer().parent();
        // We must adjust our position by walking up the render tree looking for the
        // nearest enclosing object with a layer.
        while (ancestor && !ancestor->hasLayer()) {
            if (auto* boxRenderer = dynamicDowncast<RenderBox>(ancestor)) {
                // Rows and cells share the same coordinate space (that of the section).
                // Omit them when computing our xpos/ypos.
                if (!is<RenderTableRow>(boxRenderer))
                    localPoint += boxRenderer->topLeftLocationOffset();
            }
            ancestor = ancestor->parent();
        }
        if (auto* tableRow = dynamicDowncast<RenderTableRow>(ancestor)) {
            // Put ourselves into the row coordinate space.
            localPoint -= tableRow->topLeftLocationOffset();
        }
    }
    
    // Subtract our parent's scroll offset.
#if LAYER_POSITIONS_ASSERT_ENABLED
    std::optional<ScrollingScope> oldBoxScrollingScope = m_boxScrollingScope;
    std::optional<ScrollingScope> oldContentsScrollingScope = m_contentsScrollingScope;
#endif
    RenderLayer* positionedParent;
    if (renderer().isOutOfFlowPositioned() && (positionedParent = enclosingAncestorForPosition(renderer().style().position()))) {
        // For positioned layers, we subtract out the enclosing positioned layer's scroll offset.
        if (positionedParent->renderer().hasNonVisibleOverflow()) {
            if (auto* positionedParentScrollableArea = positionedParent->scrollableArea())
                localPoint -= toLayoutSize(positionedParentScrollableArea->scrollPosition());
        }
        if (positionedParent->renderer().isInFlowPositioned()) {
            if (auto* inlinePositionedParent = dynamicDowncast<RenderInline>(positionedParent->renderer()))
                localPoint += inlinePositionedParent->offsetForInFlowPositionedInline(renderBox());
        }

        ASSERT(positionedParent->contentsScrollingScope());
        m_boxScrollingScope = positionedParent->contentsScrollingScope();
    } else if (auto* parentLayer = parent()) {
        if (parentLayer->renderer().hasNonVisibleOverflow()) {
            if (auto* parentLayerScrollableArea = parentLayer->scrollableArea())
                localPoint -= toLayoutSize(parentLayerScrollableArea->scrollPosition());
        }

        ASSERT(parentLayer->contentsScrollingScope());
        m_boxScrollingScope = parentLayer->contentsScrollingScope();
    }

    if (hasCompositedScrollableOverflow()) {
        if (!m_contentsScrollingScope || m_contentsScrollingScope == m_boxScrollingScope)
            m_contentsScrollingScope = nextScrollingScope();
    } else if (!m_contentsScrollingScope || m_contentsScrollingScope != m_boxScrollingScope)
        m_contentsScrollingScope = m_boxScrollingScope;

    if (renderer().isInFlowPositioned()) {
        if (auto* boxModelObject = dynamicDowncast<RenderBoxModelObject>(renderer())) {
            auto newOffset = boxModelObject->offsetForInFlowPosition();
            geometryChanged |= newOffset != m_offsetForPosition;
            m_offsetForPosition = newOffset;
            localPoint.move(m_offsetForPosition);
        }
    }

    geometryChanged |= location() != localPoint;
    LAYER_POSITIONS_ASSERT_IMPLIES(mode == Verify, !geometryChanged);
    LAYER_POSITIONS_ASSERT_IMPLIES(mode == Verify, oldBoxScrollingScope == m_boxScrollingScope);
    LAYER_POSITIONS_ASSERT_IMPLIES(mode == Verify, oldContentsScrollingScope == m_contentsScrollingScope);
    setLocation(localPoint);
    
    if (geometryChanged && compositor().hasContentCompositingLayers()) {
        if (isComposited())
            setNeedsCompositingGeometryUpdate();
        // This layer's footprint can affect the location of a composited descendant (which may be a sibling in z-order),
        // so trigger a descendant walk from the enclosing stacking context.
        if (auto* sc = stackingContext()) {
            sc->setDescendantsNeedCompositingRequirementsTraversal();
            sc->setDescendantsNeedUpdateBackingAndHierarchyTraversal();
        }
    }

    return geometryChanged;
}

TransformationMatrix RenderLayer::perspectiveTransform() const
{
    if (!renderer().hasTransformRelatedProperty())
        return { };

    const auto& style = renderer().style();
    if (!style.hasPerspective())
        return { };

    auto transformReferenceBoxRect = snapRectToDevicePixelsIfNeeded(renderer().transformReferenceBoxRect(style), renderer());
    auto perspectiveOrigin = style.computePerspectiveOrigin(transformReferenceBoxRect);

    // In the regular case of a non-clipped, non-scrolled GraphicsLayer, all transformations
    // (via CSS 'transform' / 'perspective') are applied with respect to a predefined anchor point,
    // which depends on the chosen CSS 'transform-box' / 'transform-origin' properties.
    //
    // A transformation given by the CSS 'transform' property is applied, by translating
    // to the 'transform origin', applying the transformation, and translating back.
    // When an element specifies a CSS 'perspective' property, the perspective transformation matrix
    // that's computed here is propagated to the GraphicsLayer by calling setChildrenTransform().
    //
    // However the GraphicsLayer platform implementations (e.g. CA on macOS) apply the children transform,
    // defined on the parent, with respect to the anchor point of the parent, when rendering child elements.
    // This is wrong, as the perspective transformation (applied to a child of the element defining the
    // 3d effect), must be independant of the chosen transform-origin (the parents transform origin
    // must not affect its children).
    //
    // To circumvent this, explicitely remove the transform-origin dependency in the perspective matrix.
    auto transformOrigin = transformOriginPixelSnappedIfNeeded();

    TransformationMatrix transform;
    style.unapplyTransformOrigin(transform, transformOrigin);
    style.applyPerspective(transform, perspectiveOrigin);
    style.applyTransformOrigin(transform, transformOrigin);
    return transform;
}

FloatPoint3D RenderLayer::transformOriginPixelSnappedIfNeeded() const
{
    if (!renderer().hasTransformRelatedProperty())
        return { };

    const auto& style = renderer().style();
    auto referenceBoxRect = renderer().transformReferenceBoxRect(style);

    auto origin = style.computeTransformOrigin(referenceBoxRect);
    if (rendererNeedsPixelSnapping(renderer()))
        origin.setXY(roundPointToDevicePixels(LayoutPoint(origin.xy()), renderer().document().deviceScaleFactor()));
    return origin;
}

FloatPoint RenderLayer::perspectiveOrigin() const
{
    if (!renderer().hasTransformRelatedProperty())
        return { };
    return Style::evaluate(renderer().style().perspectiveOrigin(), renderer().transformReferenceBoxRect(renderer().style()).size());
}

static inline bool isContainerForPositioned(RenderLayer& layer, PositionType position, bool establishesTopLayer)
{
    if (establishesTopLayer)
        return layer.isRenderViewLayer();

    switch (position) {
    case PositionType::Fixed:
        return layer.renderer().canContainFixedPositionObjects();

    case PositionType::Absolute:
        return layer.renderer().canContainAbsolutelyPositionedObjects();
    
    default:
        ASSERT_NOT_REACHED();
        return false;
    }
}

bool RenderLayer::ancestorLayerIsInContainingBlockChain(const RenderLayer& ancestor, const RenderLayer* checkLimit) const
{
    if (&ancestor == this)
        return true;

    for (const auto* currentBlock = renderer().containingBlock(); currentBlock && !is<RenderView>(*currentBlock); currentBlock = currentBlock->containingBlock()) {
        auto* currLayer = currentBlock->layer();
        if (currLayer == &ancestor)
            return true;
        
        if (currLayer && currLayer == checkLimit)
            return false;
    }
    
    return false;
}

RenderLayer* RenderLayer::enclosingAncestorForPosition(PositionType position) const
{
    auto* curr = parent();
    while (curr && !isContainerForPositioned(*curr, position, establishesTopLayer()))
        curr = curr->parent();

    ASSERT_IMPLIES(establishesTopLayer(), !curr || curr == renderer().view().layer());
    return curr;
}

RenderLayer* RenderLayer::enclosingLayerInContainingBlockOrder() const
{
    for (const auto* currentBlock = renderer().containingBlock(); currentBlock; currentBlock = currentBlock->containingBlock()) {
        if (auto* layer = currentBlock->layer())
            return layer;
    }

    return nullptr;
}

RenderLayer* RenderLayer::enclosingFrameRenderLayer() const
{
    auto* ownerElement = renderer().document().ownerElement();
    if (!ownerElement)
        return nullptr;

    auto* ownerRenderer = ownerElement->renderer();
    if (!ownerRenderer)
        return nullptr;

    return ownerRenderer->enclosingLayer();
}

RenderLayer* RenderLayer::enclosingContainingBlockLayer(CrossFrameBoundaries crossFrameBoundaries) const
{
    if (auto* ancestor = enclosingLayerInContainingBlockOrder())
        return ancestor;

    if (crossFrameBoundaries == CrossFrameBoundaries::No)
        return nullptr;

    return enclosingFrameRenderLayer();
}

RenderLayer* RenderLayer::enclosingScrollableLayer(IncludeSelfOrNot includeSelf, CrossFrameBoundaries crossFrameBoundaries) const
{
    RenderTreeMutationDisallowedScope renderTreeMutationDisallowedScope;

    auto isConsideredScrollable = [](const RenderLayer& layer) {
        auto* box = dynamicDowncast<RenderBox>(layer.renderer());
        return box && box->canBeScrolledAndHasScrollableArea();
    };

    if (includeSelf == IncludeSelfOrNot::IncludeSelf && isConsideredScrollable(*this))
        return const_cast<RenderLayer*>(this);
    
    for (auto* nextLayer = enclosingContainingBlockLayer(crossFrameBoundaries); nextLayer; nextLayer = nextLayer->enclosingContainingBlockLayer(crossFrameBoundaries)) {
        if (isConsideredScrollable(*nextLayer))
            return nextLayer;
    }

    return nullptr;
}

RenderLayer* RenderLayer::enclosingTransformedAncestor() const
{
    RenderLayer* curr = parent();
    while (curr && !curr->isRenderViewLayer() && !curr->transform())
        curr = curr->parent();

    return curr;
}

bool RenderLayer::shouldRepaintAfterLayout() const
{
    // The SVG containers themselves never trigger repaints, only their contents are allowed to.
    // SVG container sizes/positions are only ever determined by their children, so they will
    // change as a reaction on a re-position/re-sizing of the children - which already properly
    // trigger repaints.
    if (is<RenderSVGContainer>(renderer()) && !shouldPaintWithFilters())
        return false;

    if (m_repaintStatus == RepaintStatus::NeedsNormalRepaint || m_repaintStatus == RepaintStatus::NeedsFullRepaint)
        return true;

    // Composited layers that were moved during a positioned movement only
    // layout, don't need to be repainted. They just need to be recomposited.
    ASSERT(m_repaintStatus == RepaintStatus::NeedsFullRepaintForOutOfFlowMovementLayout);
    return !isComposited() || backing()->paintsIntoCompositedAncestor();
}

void RenderLayer::setBackingProviderLayer(RenderLayer* backingProvider, OptionSet<UpdateBackingSharingFlags> flags)
{
    if (backingProvider == m_backingProviderLayer) {
        ASSERT_IMPLIES(!flags.contains(UpdateBackingSharingFlags::DuringCompositingUpdate), backingProvider == m_backingProviderLayerAtEndOfCompositingUpdate);
        return;
    }

    if (!renderer().renderTreeBeingDestroyed())
        clearClipRectsIncludingDescendants();

    m_backingProviderLayer = backingProvider;
    if (!flags.contains(UpdateBackingSharingFlags::DuringCompositingUpdate))
        m_backingProviderLayerAtEndOfCompositingUpdate = backingProvider;
}

void RenderLayer::disconnectFromBackingProviderLayer(OptionSet<UpdateBackingSharingFlags> flags)
{
    if (!m_backingProviderLayer)
        return;
    
    ASSERT(m_backingProviderLayer->isComposited());
    if (m_backingProviderLayer->isComposited())
        m_backingProviderLayer->backing()->removeBackingSharingLayer(*this, flags);
}

bool compositedWithOwnBackingStore(const RenderLayer& layer)
{
    return layer.isComposited() && !layer.backing()->paintsIntoCompositedAncestor();
}

RenderLayer* RenderLayer::enclosingCompositingLayer(IncludeSelfOrNot includeSelf) const
{
    if (includeSelf == IncludeSelf && isComposited())
        return const_cast<RenderLayer*>(this);

    for (const RenderLayer* curr = paintOrderParent(); curr; curr = curr->paintOrderParent()) {
        if (curr->isComposited())
            return const_cast<RenderLayer*>(curr);
    }
         
    return nullptr;
}

RenderLayer::EnclosingCompositingLayerStatus RenderLayer::enclosingCompositingLayerForRepaint(IncludeSelfOrNot includeSelf) const
{
    auto repaintTargetForLayer = [](const RenderLayer& layer) -> RenderLayer* {
        if (compositedWithOwnBackingStore(layer))
            return const_cast<RenderLayer*>(&layer);
        
        if (layer.paintsIntoProvidedBacking())
            return layer.backingProviderLayer();
        
        return nullptr;
    };
    auto isEligibleForFullRepaintCheck = [&](const auto& layer) {
        return layer.isSelfPaintingLayer() && !layer.renderer().hasPotentiallyScrollableOverflow() && !is<RenderView>(layer.renderer());
    };

    auto fullRepaintAlreadyScheduled = isEligibleForFullRepaintCheck(*this) && needsFullRepaint();
    RenderLayer* repaintTarget = nullptr;
    if (includeSelf == IncludeSelf && (repaintTarget = repaintTargetForLayer(*this)))
        return { fullRepaintAlreadyScheduled, repaintTarget };

    for (const RenderLayer* curr = paintOrderParent(); curr; curr = curr->paintOrderParent()) {
        fullRepaintAlreadyScheduled = fullRepaintAlreadyScheduled || (isEligibleForFullRepaintCheck(*curr) && curr->needsFullRepaint());
        if ((repaintTarget = repaintTargetForLayer(*curr)))
            return { fullRepaintAlreadyScheduled, repaintTarget };
    }
         
    return { };
}

RenderLayer* RenderLayer::enclosingFilterLayer(IncludeSelfOrNot includeSelf) const
{
    const RenderLayer* curr = (includeSelf == IncludeSelf) ? this : parent();
    for (; curr; curr = curr->parent()) {
        if (curr->requiresFullLayerImageForFilters())
            return const_cast<RenderLayer*>(curr);
    }
    
    return nullptr;
}

RenderLayer* RenderLayer::enclosingFilterRepaintLayer() const
{
    for (const RenderLayer* curr = this; curr; curr = curr->parent()) {
        if ((curr != this && curr->requiresFullLayerImageForFilters()) || compositedWithOwnBackingStore(*curr) || curr->isRenderViewLayer())
            return const_cast<RenderLayer*>(curr);
    }
    return nullptr;
}

// FIXME: This needs a better name.
void RenderLayer::setFilterBackendNeedsRepaintingInRect(const LayoutRect& rect)
{
    ASSERT(requiresFullLayerImageForFilters());
    ASSERT(m_filters);

    if (rect.isEmpty())
        return;
    
    LayoutRect rectForRepaint = rect;
    rectForRepaint.expand(toLayoutBoxExtent(filterOutsets()));

    m_filters->expandDirtySourceRect(rectForRepaint);
    
    RenderLayer* parentLayer = enclosingFilterRepaintLayer();
    ASSERT(parentLayer);
    FloatQuad repaintQuad(rectForRepaint);
    LayoutRect parentLayerRect = renderer().localToContainerQuad(repaintQuad, &parentLayer->renderer()).enclosingBoundingBox();

    if (parentLayer->isComposited()) {
        if (!parentLayer->backing()->paintsIntoWindow()) {
            parentLayer->setBackingNeedsRepaintInRect(parentLayerRect);
            return;
        }
        // If the painting goes to window, redirect the painting to the parent RenderView.
        parentLayer = renderer().view().layer();
        parentLayerRect = renderer().localToContainerQuad(repaintQuad, &parentLayer->renderer()).enclosingBoundingBox();
    }

    if (parentLayer->shouldPaintWithFilters()) {
        parentLayer->setFilterBackendNeedsRepaintingInRect(parentLayerRect);
        return;        
    }
    
    if (parentLayer->isRenderViewLayer()) {
        downcast<RenderView>(parentLayer->renderer()).repaintViewRectangle(parentLayerRect);
        return;
    }
    
    ASSERT_NOT_REACHED();
}

bool RenderLayer::hasAncestorWithFilterOutsets() const
{
    for (const RenderLayer* curr = this; curr; curr = curr->parent()) {
        if (curr->hasFilterOutsets())
            return true;
    }
    return false;
}

RenderLayer* RenderLayer::clippingRootForPainting() const
{
    if (isComposited())
        return const_cast<RenderLayer*>(this);

    if (paintsIntoProvidedBacking())
        return backingProviderLayer();

    const RenderLayer* current = this;
    while (current) {
        if (current->isRenderViewLayer())
            return const_cast<RenderLayer*>(current);

        current = current->paintOrderParent();
        ASSERT(current);
        if (current->transform() || compositedWithOwnBackingStore(*current))
            return const_cast<RenderLayer*>(current);

        if (renderer().settings().css3DTransformBackfaceVisibilityInteroperabilityEnabled() && current->participatesInPreserve3D() && current->renderer().style().backfaceVisibility() == BackfaceVisibility::Hidden)
            return const_cast<RenderLayer*>(current);

        if (current->paintsIntoProvidedBacking())
            return current->backingProviderLayer();
    }

    ASSERT_NOT_REACHED();
    return nullptr;
}

LayoutPoint RenderLayer::absoluteToContents(const LayoutPoint& absolutePoint) const
{
    // We don't use convertToLayerCoords because it doesn't know about transforms
    return LayoutPoint(renderer().absoluteToLocal(absolutePoint, UseTransforms));
}

bool RenderLayer::cannotBlitToWindow() const
{
    if (isTransparent() || hasReflection() || isTransformed())
        return true;
    if (!parent())
        return false;
    return parent()->cannotBlitToWindow();
}

RenderLayer* RenderLayer::transparentPaintingAncestor(const LayerPaintingInfo& info)
{
    if (this == info.rootLayer || isComposited() || paintsIntoProvidedBacking())
        return nullptr;
    for (auto* ancestor = parent(); ancestor; ancestor = ancestor->parent()) {
        if (ancestor->isStackingContext()) {
            if (ancestor->isComposited() || ancestor->paintsIntoProvidedBacking())
                return nullptr;
            if (ancestor->isTransparent())
                return ancestor;
        }
        if (ancestor == info.rootLayer)
            return nullptr;
    }
    return nullptr;
}

enum TransparencyClipBoxBehavior {
    PaintingTransparencyClipBox,
    HitTestingTransparencyClipBox
};

enum TransparencyClipBoxMode {
    DescendantsOfTransparencyClipBox,
    RootOfTransparencyClipBox
};

static LayoutRect transparencyClipBox(const RenderLayer&, const RenderLayer* rootLayer, TransparencyClipBoxBehavior, TransparencyClipBoxMode, OptionSet<PaintBehavior> = { }, const LayoutRect* paintDirtyRect = nullptr);

static void expandClipRectForDescendantsAndReflection(LayoutRect& clipRect, const RenderLayer& layer, const RenderLayer* rootLayer,
    TransparencyClipBoxBehavior transparencyBehavior, OptionSet<PaintBehavior> paintBehavior, const LayoutRect* paintDirtyRect)
{
    // If we have a mask, then the clip is limited to the border box area (and there is
    // no need to examine child layers).
    if (!layer.renderer().hasMask()) {
        // Note: we don't have to walk z-order lists since transparent elements always establish
        // a stacking container. This means we can just walk the layer tree directly.
        for (RenderLayer* curr = layer.firstChild(); curr; curr = curr->nextSibling()) {
            if (!layer.isReflectionLayer(*curr))
                clipRect.unite(transparencyClipBox(*curr, rootLayer, transparencyBehavior, DescendantsOfTransparencyClipBox, paintBehavior, paintDirtyRect));
        }
    }

    // If we have a reflection, then we need to account for that when we push the clip.  Reflect our entire
    // current transparencyClipBox to catch all child layers.
    // FIXME: Accelerated compositing will eventually want to do something smart here to avoid incorporating this
    // size into the parent layer.
    if (layer.renderer().isRenderBox() && layer.renderer().hasReflection()) {
        LayoutSize delta = layer.offsetFromAncestor(rootLayer);
        clipRect.move(-delta);
        clipRect.unite(layer.renderBox()->reflectedRect(clipRect));
        clipRect.move(delta);
    }
}

static LayoutRect transparencyClipBox(const RenderLayer& layer, const RenderLayer* rootLayer, TransparencyClipBoxBehavior transparencyBehavior,
    TransparencyClipBoxMode transparencyMode, OptionSet<PaintBehavior> paintBehavior, const LayoutRect* paintDirtyRect)
{
    // FIXME: Although this function completely ignores CSS-imposed clipping, we did already intersect with the
    // paintDirtyRect, and that should cut down on the amount we have to paint.  Still it
    // would be better to respect clips.
    
    if (rootLayer != &layer && ((transparencyBehavior == PaintingTransparencyClipBox && layer.paintsWithTransform(paintBehavior))
        || (transparencyBehavior == HitTestingTransparencyClipBox && layer.isTransformed()))) {
        // The best we can do here is to use enclosed bounding boxes to establish a "fuzzy" enough clip to encompass
        // the transformed layer and all of its children.
        RenderLayer::PaginationInclusionMode mode = transparencyBehavior == HitTestingTransparencyClipBox ? RenderLayer::IncludeCompositedPaginatedLayers : RenderLayer::ExcludeCompositedPaginatedLayers;
        const RenderLayer* paginationLayer = transparencyMode == DescendantsOfTransparencyClipBox ? layer.enclosingPaginationLayer(mode) : nullptr;
        const RenderLayer* rootLayerForTransform = paginationLayer ? paginationLayer : rootLayer;
        LayoutSize delta = layer.offsetFromAncestor(rootLayerForTransform);

        TransformationMatrix transform;
        transform.translate(delta.width(), delta.height());
        transform.multiply(*layer.transform());

        // We don't use fragment boxes when collecting a transformed layer's bounding box, since it always
        // paints unfragmented.
        LayoutRect clipRect = layer.boundingBox(&layer);
        expandClipRectForDescendantsAndReflection(clipRect, layer, &layer, transparencyBehavior, paintBehavior, paintDirtyRect);
        clipRect.expand(toLayoutBoxExtent(layer.filterOutsets()));
        LayoutRect result = transform.mapRect(clipRect);
        if (!paginationLayer) {
            if (paintDirtyRect)
                result = intersection(result, *paintDirtyRect);
            return result;
        }

        // We have to break up the transformed extent across our columns.
        // Split our box up into the actual fragment boxes that render in the columns/pages and unite those together to
        // get our true bounding box.
        auto& enclosingFragmentedFlow = downcast<RenderFragmentedFlow>(paginationLayer->renderer());
        result = enclosingFragmentedFlow.fragmentsBoundingBox(result);
        result.move(paginationLayer->offsetFromAncestor(rootLayer));
        if (paintDirtyRect)
            result = intersection(result, *paintDirtyRect);
        return result;
    }

    OptionSet<RenderLayer::CalculateLayerBoundsFlag> flags = transparencyBehavior == HitTestingTransparencyClipBox ? RenderLayer::UseFragmentBoxesIncludingCompositing : RenderLayer::UseFragmentBoxesExcludingCompositing;
    flags.add(RenderLayer::IncludeRootBackgroundPaintingArea);
    auto clipRect = layer.boundingBox(rootLayer, layer.offsetFromAncestor(rootLayer), flags);
    expandClipRectForDescendantsAndReflection(clipRect, layer, rootLayer, transparencyBehavior, paintBehavior, paintDirtyRect);
    clipRect.expand(toLayoutBoxExtent(layer.filterOutsets()));

    if (paintDirtyRect)
        clipRect = intersection(clipRect, *paintDirtyRect);

    return clipRect;
}

void RenderLayer::beginTransparencyLayers(GraphicsContext& context, const LayerPaintingInfo& paintingInfo, const LayoutRect& dirtyRect)
{
    if (context.paintingDisabled() || (paintsWithTransparency(paintingInfo.paintBehavior) && m_usedTransparency))
        return;

    if (auto* ancestor = transparentPaintingAncestor(paintingInfo))
        ancestor->beginTransparencyLayers(context, paintingInfo, dirtyRect);

    if (paintsWithTransparency(paintingInfo.paintBehavior)) {
        ASSERT(isStackingContext());
        m_usedTransparency = true;
        if (canPaintTransparencyWithSetOpacity()) {
            m_savedAlphaForTransparency = context.alpha();
            context.setAlpha(context.alpha() * renderer().opacity());
            return;
        }
        context.save();
        LayoutRect adjustedClipRect = transparencyClipBox(*this, paintingInfo.rootLayer, PaintingTransparencyClipBox, RootOfTransparencyClipBox, paintingInfo.paintBehavior, &dirtyRect);
        adjustedClipRect.move(paintingInfo.subpixelOffset);
        auto snappedClipRect = snapRectToDevicePixelsIfNeeded(adjustedClipRect, renderer());
        context.clip(snappedClipRect);

        bool usesCompositeOperation = hasBlendMode() && !(renderer().isLegacyRenderSVGRoot() && parent() && parent()->isRenderViewLayer());
        if (usesCompositeOperation)
            context.setCompositeOperation(context.compositeOperation(), blendMode());

        context.beginTransparencyLayer(renderer().opacity());

        if (usesCompositeOperation)
            context.setCompositeOperation(context.compositeOperation(), BlendMode::Normal);

#ifdef REVEAL_TRANSPARENCY_LAYERS
        context.setFillColor(SRGBA<uint8_t> { 0, 0, 128, 51 });
        context.fillRect(pixelSnappedClipRect);
#endif
    }
}

bool RenderLayer::isDescendantOf(const RenderLayer& layer) const
{
    for (auto* ancestor = this; ancestor; ancestor = ancestor->parent()) {
        if (&layer == ancestor)
            return true;
    }
    return false;
}

static RenderLayer* findCommonAncestor(const RenderLayer& firstLayer, const RenderLayer& secondLayer)
{
    if (&firstLayer == &secondLayer)
        return const_cast<RenderLayer*>(&firstLayer);

    SingleThreadWeakHashSet<const RenderLayer> ancestorChain;
    for (auto* currLayer = &firstLayer; currLayer; currLayer = currLayer->parent())
        ancestorChain.add(*currLayer);

    for (auto* currLayer = &secondLayer; currLayer; currLayer = currLayer->parent()) {
        if (ancestorChain.contains(*currLayer))
            return const_cast<RenderLayer*>(currLayer);
    }
    return nullptr;
}

RenderLayer* RenderLayer::commonAncestorWithLayer(const RenderLayer& layer) const
{
    return findCommonAncestor(*this, layer);
}

void RenderLayer::convertToPixelSnappedLayerCoords(const RenderLayer* ancestorLayer, IntPoint& roundedLocation, ColumnOffsetAdjustment adjustForColumns) const
{
    LayoutPoint location = convertToLayerCoords(ancestorLayer, roundedLocation, adjustForColumns);
    roundedLocation = roundedIntPoint(location);
}

// Returns the layer reached on the walk up towards the ancestor.
static inline const RenderLayer* accumulateOffsetTowardsAncestor(const RenderLayer* layer, const RenderLayer* ancestorLayer, LayoutPoint& location, RenderLayer::ColumnOffsetAdjustment adjustForColumns)
{
    ASSERT(ancestorLayer != layer);

    const auto& renderer = layer->renderer();
    auto position = renderer.style().position();

    // FIXME: Positioning of out-of-flow(fixed, absolute) elements collected in a RenderFragmentedFlow
    // may need to be revisited in a future patch.
    // If the fixed renderer is inside a RenderFragmentedFlow, we should not compute location using localToAbsolute,
    // since localToAbsolute maps the coordinates from named flow to regions coordinates and regions can be
    // positioned in a completely different place in the viewport (RenderView).
    if (position == PositionType::Fixed && (!ancestorLayer || ancestorLayer == renderer.view().layer())) {
        // If the fixed layer's container is the root, just add in the offset of the view. We can obtain this by calling
        // localToAbsolute() on the RenderView.
        location.moveBy(LayoutPoint(renderer.localToAbsolute({ }, IsFixed)));
        return ancestorLayer;
    }

    // For the fixed positioned elements inside a render flow thread, we should also skip the code path below
    // Otherwise, for the case of ancestorLayer == rootLayer and fixed positioned element child of a transformed
    // element in render flow thread, we will hit the fixed positioned container before hitting the ancestor layer.
    if (position == PositionType::Fixed) {
        // For a fixed layers, we need to walk up to the root to see if there's a fixed position container
        // (e.g. a transformed layer). It's an error to call offsetFromAncestor() across a layer with a transform,
        // so we should always find the ancestor at or before we find the fixed position container, if
        // the container is transformed.
        RenderLayer* fixedPositionContainerLayer = nullptr;
        bool foundAncestor = false;
        for (auto* currLayer = layer->parent(); currLayer; currLayer = currLayer->parent()) {
            if (currLayer == ancestorLayer)
                foundAncestor = true;

            if (isContainerForPositioned(*currLayer, PositionType::Fixed, layer->establishesTopLayer())) {
                fixedPositionContainerLayer = currLayer;
                // A layer that has a transform-related property but not a
                // transform still acts as a fixed-position container.
                // Accumulating offsets across such layers is allowed.
                if (currLayer->transform())
                    ASSERT_UNUSED(foundAncestor, foundAncestor);
                break;
            }
        }
        
        ASSERT(fixedPositionContainerLayer); // We should have hit the RenderView's layer at least.

        if (fixedPositionContainerLayer != ancestorLayer) {
            auto fixedContainerCoords = layer->offsetFromAncestor(fixedPositionContainerLayer);
            auto ancestorCoords = foundAncestor ? ancestorLayer->offsetFromAncestor(fixedPositionContainerLayer) : LayoutSize { };
            location.move(fixedContainerCoords - ancestorCoords);
            return foundAncestor ? ancestorLayer : fixedPositionContainerLayer;
        }
    }

    if (position == PositionType::Fixed) {
        ASSERT(ancestorLayer);
        if (ancestorLayer == renderer.view().layer()) {
            // Add location in flow thread coordinates.
            location.moveBy(layer->location());

            // Add flow thread offset in view coordinates since the view may be scrolled.
            location.moveBy(LayoutPoint(renderer.view().localToAbsolute({ }, IsFixed)));
            return ancestorLayer;
        }
    }

    RenderLayer* parentLayer;
    if (position == PositionType::Absolute || position == PositionType::Fixed) {
        // Do what enclosingAncestorForPosition() does, but check for ancestorLayer along the way.
        parentLayer = layer->parent();
        bool foundAncestorFirst = false;
        while (parentLayer) {
            // RenderFragmentedFlow is a positioned container, child of RenderView, positioned at (0,0).
            // This implies that, for out-of-flow positioned elements inside a RenderFragmentedFlow,
            // we are bailing out before reaching root layer.
            if (isContainerForPositioned(*parentLayer, position, layer->establishesTopLayer()))
                break;

            if (parentLayer == ancestorLayer) {
                foundAncestorFirst = true;
                break;
            }

            parentLayer = parentLayer->parent();
        }

        // We should not reach RenderView layer past the RenderFragmentedFlow layer for any
        // children of the RenderFragmentedFlow.
        if (renderer.enclosingFragmentedFlow())
            ASSERT(parentLayer != renderer.view().layer());

        if (foundAncestorFirst) {
            // Found ancestorLayer before the abs. positioned container, so compute offset of both relative
            // to enclosingAncestorForPosition and subtract.
            auto* positionedAncestor = parentLayer->enclosingAncestorForPosition(position);
            auto thisCoords = layer->offsetFromAncestor(positionedAncestor);
            auto ancestorCoords = ancestorLayer->offsetFromAncestor(positionedAncestor);
            location.move(thisCoords - ancestorCoords);
            return ancestorLayer;
        }
    } else
        parentLayer = layer->parent();
    
    if (!parentLayer)
        return nullptr;

    location.moveBy(layer->location());

    if (adjustForColumns == RenderLayer::AdjustForColumns) {
        if (auto* parentLayer = layer->parent(); parentLayer && parentLayer != ancestorLayer) {
            if (auto* multiColumnFlow = dynamicDowncast<RenderMultiColumnFlow>(parentLayer->renderer())) {
                if (auto* fragment = multiColumnFlow->physicalTranslationFromFlowToFragment(location))
                    location.move(fragment->topLeftLocation() - parentLayer->renderBox()->topLeftLocation());
            }
        }
    }

    return parentLayer;
}

LayoutPoint RenderLayer::convertToLayerCoords(const RenderLayer* ancestorLayer, const LayoutPoint& location, ColumnOffsetAdjustment adjustForColumns) const
{
    if (ancestorLayer == this)
        return location;

    const auto* currLayer = this;
    auto locationInLayerCoords = location;
    while (currLayer && currLayer != ancestorLayer)
        currLayer = accumulateOffsetTowardsAncestor(currLayer, ancestorLayer, locationInLayerCoords, adjustForColumns);

    // Pixel snap the whole SVG subtree as one "block" -- not individual layers down the SVG render tree.
    if (renderer().isRenderSVGRoot())
        return LayoutPoint(roundPointToDevicePixels(locationInLayerCoords, renderer().document().deviceScaleFactor()));

    return locationInLayerCoords;
}

LayoutSize RenderLayer::offsetFromAncestor(const RenderLayer* ancestorLayer, ColumnOffsetAdjustment adjustForColumns) const
{
    return toLayoutSize(convertToLayerCoords(ancestorLayer, LayoutPoint(), adjustForColumns));
}

bool RenderLayer::shouldTryToScrollForScrollIntoView() const
{
    if (!renderer().isRenderBox() || !renderer().hasNonVisibleOverflow())
        return false;

    // Don't scroll to reveal an overflow layer that is restricted by the -webkit-line-clamp property.
    // FIXME: Is this still needed? It used to be relevant for Safari RSS.
    if (renderer().parent() && !renderer().parent()->style().lineClamp().isNone())
        return false;

    auto& box = *renderBox();

    if (box.frame().eventHandler().autoscrollInProgress()) {
        // The "programmatically" here is misleading; this asks whether the box has scrollable overflow,
        // or is a special case like a form control.
        return box.canBeProgramaticallyScrolled();
    }

    // Programmatic scrolls can scroll overflow: hidden but not overflow: clip.
    return box.hasPotentiallyScrollableOverflow() && (box.hasHorizontalOverflow() || box.hasVerticalOverflow());
}

void RenderLayer::autoscroll(const IntPoint& positionInWindow)
{
    IntPoint currentDocumentPosition = renderer().view().frameView().windowToContents(positionInWindow);
    LocalFrameView::scrollRectToVisible(LayoutRect(currentDocumentPosition, LayoutSize(1, 1)), renderer(), false, { SelectionRevealMode::Reveal, ScrollAlignment::alignToEdgeIfNeeded, ScrollAlignment::alignToEdgeIfNeeded, ShouldAllowCrossOriginScrolling::Yes });
}

bool RenderLayer::canResize() const
{
    // We need a special case for <iframe> because they never have
    // hasNonVisibleOverflow(). However, they do "implicitly" clip their contents, so
    // we want to allow resizing them also.
    return (renderer().hasNonVisibleOverflow() || renderer().isRenderIFrame()) && renderer().style().resize() != Resize::None;
}

LayoutSize RenderLayer::minimumSizeForResizing(float zoomFactor) const
{
    // Use the resizer size as the strict minimum size
    auto resizerRect = overflowControlsRects().resizer;
    LayoutUnit minWidth = Style::evaluateMinimum(renderer().style().minWidth(), renderer().containingBlock()->width());
    LayoutUnit minHeight = Style::evaluateMinimum(renderer().style().minHeight(), renderer().containingBlock()->height());
    minWidth = std::max(LayoutUnit(minWidth / zoomFactor), LayoutUnit(resizerRect.width()));
    minHeight = std::max(LayoutUnit(minHeight / zoomFactor), LayoutUnit(resizerRect.height()));
    return LayoutSize(minWidth, minHeight);
}

void RenderLayer::resize(const PlatformMouseEvent& evt, const LayoutSize& oldOffset)
{
    // FIXME: This should be possible on generated content but is not right now.
    if (!inResizeMode() || !canResize())
        return;

    // FIXME: This should be possible on all elements but is not right now.
    RefPtr styledElement = dynamicDowncast<StyledElement>(renderer().element());
    if (!styledElement)
        return;

    // FIXME: The only case where renderer->element()->renderer() != renderer is with continuations. Do they matter here?
    // If they do it would still be better to deal with them explicitly.
    CheckedPtr renderer = downcast<RenderBox>(styledElement->renderer());

    Ref document = styledElement->document();
    if (!document->frame()->eventHandler().mousePressed())
        return;

    float zoomFactor = renderer->style().usedZoom();

    auto absolutePoint = document->view()->windowToContents(evt.position());
    auto localPoint = roundedIntPoint(absoluteToContents(absolutePoint));

    LayoutSize newOffset = offsetFromResizeCorner(localPoint);
    newOffset.setWidth(newOffset.width() / zoomFactor);
    newOffset.setHeight(newOffset.height() / zoomFactor);

    LayoutSize currentSize = LayoutSize(renderer->width() / zoomFactor, renderer->height() / zoomFactor);

    LayoutSize adjustedOldOffset = LayoutSize(oldOffset.width() / zoomFactor, oldOffset.height() / zoomFactor);
    if (renderer->shouldPlaceVerticalScrollbarOnLeft()) {
        newOffset.setWidth(-newOffset.width());
        adjustedOldOffset.setWidth(-adjustedOldOffset.width());
    }

    LayoutSize difference = (currentSize + newOffset - adjustedOldOffset).expandedTo(minimumSizeForResizing(zoomFactor)) - currentSize;

    StyleAttributeMutationScope mutationScope { styledElement.get() };
    bool isBoxSizingBorder = renderer->style().boxSizing() == BoxSizing::BorderBox;

    Resize resize = renderer->style().resize();
    bool canResizeWidth = resize == Resize::Horizontal || resize == Resize::Both
        || (renderer->isHorizontalWritingMode() ? resize == Resize::Inline : resize == Resize::Block);
    if (canResizeWidth && difference.width()) {
        if (is<HTMLFormControlElement>(*styledElement)) {
            // Make implicit margins from the theme explicit (see <http://bugs.webkit.org/show_bug.cgi?id=9547>).
            styledElement->setInlineStyleProperty(CSSPropertyMarginLeft, renderer->marginLeft() / zoomFactor, CSSUnitType::CSS_PX);
            styledElement->setInlineStyleProperty(CSSPropertyMarginRight, renderer->marginRight() / zoomFactor, CSSUnitType::CSS_PX);
        }
        LayoutUnit baseWidth = renderer->width() - (isBoxSizingBorder ? 0_lu : renderer->horizontalBorderAndPaddingExtent());
        baseWidth = baseWidth / zoomFactor;
        styledElement->setInlineStyleProperty(CSSPropertyWidth, roundToInt(baseWidth + difference.width()), CSSUnitType::CSS_PX);

        mutationScope.enqueueMutationRecord();
    }

    bool canResizeHeight = resize == Resize::Vertical || resize == Resize::Both
        || (renderer->isHorizontalWritingMode() ? resize == Resize::Block : resize == Resize::Inline);
    if (canResizeHeight && difference.height()) {
        if (is<HTMLFormControlElement>(*styledElement)) {
            // Make implicit margins from the theme explicit (see <http://bugs.webkit.org/show_bug.cgi?id=9547>).
            styledElement->setInlineStyleProperty(CSSPropertyMarginTop, renderer->marginTop() / zoomFactor, CSSUnitType::CSS_PX);
            styledElement->setInlineStyleProperty(CSSPropertyMarginBottom, renderer->marginBottom() / zoomFactor, CSSUnitType::CSS_PX);
        }
        LayoutUnit baseHeight = renderer->height() - (isBoxSizingBorder ? 0_lu : renderer->verticalBorderAndPaddingExtent());
        baseHeight = baseHeight / zoomFactor;
        styledElement->setInlineStyleProperty(CSSPropertyHeight, roundToInt(baseHeight + difference.height()), CSSUnitType::CSS_PX);

        mutationScope.enqueueMutationRecord();
    }

    document->updateLayout();

    // FIXME (Radar 4118564): We should also autoscroll the window as necessary to keep the point under the cursor in view.
}

IntSize RenderLayer::visibleSize() const
{
    RenderBox* box = renderBox();
    if (!box)
        return IntSize();

    return IntSize(roundToInt(box->clientWidth()), roundToInt(box->clientHeight()));
}

RenderLayer::OverflowControlRects RenderLayer::overflowControlsRects() const
{
    if (m_scrollableArea)
        return m_scrollableArea->overflowControlsRects();

    auto& renderBox = downcast<RenderBox>(renderer());
    // Scrollbars sit inside the border box.
    auto overflowControlsPositioningRect = snappedIntRect(renderBox.paddingBoxRectIncludingScrollbar());

    bool placeVerticalScrollbarOnTheLeft = renderBox.shouldPlaceVerticalScrollbarOnLeft();
    bool haveResizer = renderer().style().resize() != Resize::None && renderer().style().pseudoElementType() == PseudoId::None;

    OverflowControlRects result;
    auto cornerRect = [&](IntSize cornerSize) {
        if (placeVerticalScrollbarOnTheLeft) {
            auto bottomLeftCorner = overflowControlsPositioningRect.minXMaxYCorner();
            return IntRect { { bottomLeftCorner.x(), bottomLeftCorner.y() - cornerSize.height(), }, cornerSize };
        }
        return IntRect { overflowControlsPositioningRect.maxXMaxYCorner() - cornerSize, cornerSize };
    };

    if (haveResizer) {
        auto scrollbarThickness = ScrollbarTheme::theme().scrollbarThickness();
        result.resizer = cornerRect({ scrollbarThickness, scrollbarThickness });
    }

    return result;
}

String RenderLayer::debugDescription() const
{
    String compositedDescription;
    if (isComposited()) {
        TextStream stream;
        stream << " " << *backing();
        compositedDescription = stream.release();
    }

    return makeString("RenderLayer 0x"_s, hex(reinterpret_cast<uintptr_t>(this), Lowercase),
        ' ', size().width(), 'x', size().height(),
        transform() ? " has transform"_s : ""_s,
        hasFilter() ? " has filter"_s : ""_s,
        hasBackdropFilter() ? " has backdrop filter"_s : ""_s,
#if HAVE(CORE_MATERIAL)
        hasAppleVisualEffect() ? " has apple visual effect"_s : ""_s,
#endif
        hasBlendMode() ? " has blend mode"_s : ""_s,
        isolatesBlending() ? " isolates blending"_s : ""_s,
        compositedDescription);
}

IntSize RenderLayer::offsetFromResizeCorner(const IntPoint& localPoint) const
{
    auto resizerRect = overflowControlsRects().resizer;
    auto resizeCorner = renderer().shouldPlaceVerticalScrollbarOnLeft() ? resizerRect.minXMaxYCorner() : resizerRect.maxXMaxYCorner();
    return localPoint - resizeCorner;
}

int RenderLayer::scrollWidth() const
{
    if (m_scrollableArea)
        return m_scrollableArea->scrollWidth();

    RenderBox* box = renderBox();
    ASSERT(box);
    LayoutRect overflowRect(box->layoutOverflowRect());
    box->flipForWritingMode(overflowRect);
    return roundToInt(overflowRect.maxX() - overflowRect.x());
}

int RenderLayer::scrollHeight() const
{
    if (m_scrollableArea)
        return m_scrollableArea->scrollHeight();

    RenderBox* box = renderBox();
    ASSERT(box);
    LayoutRect overflowRect(box->layoutOverflowRect());
    box->flipForWritingMode(overflowRect);
    return roundToInt(overflowRect.maxY() - overflowRect.y());
}

void RenderLayer::updateScrollInfoAfterLayout()
{
    updateLayerScrollableArea();
    if (m_scrollableArea)
        m_scrollableArea->updateScrollInfoAfterLayout();
}

void RenderLayer::updateScrollbarSteps()
{
    if (m_scrollableArea)
        m_scrollableArea->updateScrollbarSteps();
}

bool RenderLayer::canUseCompositedScrolling() const
{
    if (m_scrollableArea)
        return m_scrollableArea->canUseCompositedScrolling();
    return false;
}

bool RenderLayer::hasCompositedScrollableOverflow() const
{
    if (m_scrollableArea)
        return m_scrollableArea->hasCompositedScrollableOverflow();
    return false;
}

void RenderLayer::computeHasCompositedScrollableOverflow(LayoutUpToDate layoutUpToDate)
{
    if (m_scrollableArea)
        m_scrollableArea->computeHasCompositedScrollableOverflow(layoutUpToDate);
}

bool RenderLayer::hasOverlayScrollbars() const
{
    if (m_scrollableArea)
        return m_scrollableArea->hasOverlayScrollbars();
    return false;
}

bool RenderLayer::usesCompositedScrolling() const
{
    if (m_scrollableArea)
        return m_scrollableArea->usesCompositedScrolling();
    return false;
}

bool RenderLayer::isPointInResizeControl(IntPoint localPoint) const
{
    if (!canResize())
        return false;

    return overflowControlsRects().resizer.contains(localPoint);
}

void RenderLayer::paint(GraphicsContext& context, const LayoutRect& damageRect, const LayoutSize& subpixelOffset, OptionSet<PaintBehavior> paintBehavior, RenderObject* subtreePaintRoot, OptionSet<PaintLayerFlag> paintFlags, SecurityOriginPaintPolicy paintPolicy, RegionContext* regionContext)
{
    OverlapTestRequestMap overlapTestRequests;

    LayerPaintingInfo paintingInfo(this, enclosingIntRect(damageRect), paintBehavior, subpixelOffset, subtreePaintRoot, &overlapTestRequests, paintPolicy == SecurityOriginPaintPolicy::AccessibleOriginOnly);
    if (regionContext) {
        paintingInfo.regionContext = regionContext;
        if (is<EventRegionContext>(regionContext))
            paintFlags.add(RenderLayer::PaintLayerFlag::CollectingEventRegion);
    }
    paintLayer(context, paintingInfo, paintFlags);

    for (auto& widget : overlapTestRequests.keys())
        widget->setOverlapTestResult(false);
}

void RenderLayer::clipToRect(GraphicsContext& context, GraphicsContextStateSaver& stateSaver, RegionContextStateSaver& regionContextStateSaver, const LayerPaintingInfo& paintingInfo, OptionSet<PaintBehavior> paintBehavior, const ClipRect& clipRect, BorderRadiusClippingRule rule)
{
    float deviceScaleFactor = renderer().document().deviceScaleFactor();
    bool needsClipping = !clipRect.isInfinite() && clipRect.rect() != paintingInfo.paintDirtyRect;
    if (needsClipping || clipRect.affectedByRadius())
        stateSaver.save();

    if (needsClipping) {
        LayoutRect adjustedClipRect = clipRect.rect();
        adjustedClipRect.move(paintingInfo.subpixelOffset);
        auto snappedClipRect = snapRectToDevicePixelsIfNeeded(adjustedClipRect, renderer());
        context.clip(snappedClipRect);
        regionContextStateSaver.pushClip(enclosingIntRect(snappedClipRect));
    }

    if (clipRect.affectedByRadius()) {
        // If the clip rect has been tainted by a border radius, then we have to walk up our layer chain applying the clips from
        // any layers with overflow. The condition for being able to apply these clips is that the overflow object be in our
        // containing block chain so we check that also.
        for (RenderLayer* layer = rule == IncludeSelfForBorderRadius ? this : parent(); layer; layer = layer->parent()) {
            if (paintBehavior.contains(PaintBehavior::CompositedOverflowScrollContent) && layer->usesCompositedScrolling())
                break;
        
            if (layer->renderer().hasNonVisibleOverflow() && layer->renderer().style().hasBorderRadius() && ancestorLayerIsInContainingBlockChain(*layer)) {
                auto adjustedClipRect = LayoutRect { LayoutPoint { layer->offsetFromAncestor(paintingInfo.rootLayer, AdjustForColumns) }, layer->rendererBorderBoxRect().size() };
                adjustedClipRect.move(paintingInfo.subpixelOffset);
                auto borderShape = BorderShape::shapeForBorderRect(layer->renderer().style(), adjustedClipRect);
                if (borderShape.innerShapeContains(paintingInfo.paintDirtyRect))
                    context.clip(snapRectToDevicePixels(intersection(paintingInfo.paintDirtyRect, adjustedClipRect), deviceScaleFactor));
                else
                    borderShape.clipToInnerShape(context, deviceScaleFactor);
            }
            
            if (layer == paintingInfo.rootLayer)
                break;
        }
    }
}

static void performOverlapTests(OverlapTestRequestMap& overlapTestRequests, const RenderLayer* rootLayer, const RenderLayer* layer)
{
    if (overlapTestRequests.isEmpty())
        return;

    Vector<OverlapTestRequestClient*> overlappedRequestClients;
    LayoutRect boundingBox = layer->boundingBox(rootLayer, layer->offsetFromAncestor(rootLayer));
    for (auto& request : overlapTestRequests) {
        if (!boundingBox.intersects(request.value))
            continue;

        request.key->setOverlapTestResult(true);
        overlappedRequestClients.append(request.key);
    }
    for (auto* client : overlappedRequestClients)
        overlapTestRequests.remove(client);
}

static inline bool shouldDoSoftwarePaint(const RenderLayer* layer, bool paintingReflection)
{
    return paintingReflection && !layer->has3DTransform();
}

static inline bool shouldSuppressPaintingLayer(RenderLayer* layer)
{
    // Avoid painting all layers if the document is in a state where visual updates aren't allowed.
    // A full repaint will occur in Document::setVisualUpdatesAllowed(bool) if painting is suppressed here.
    if (!layer->renderer().document().visualUpdatesAllowed())
        return true;

    return false;
}

void RenderLayer::paintSVGResourceLayer(GraphicsContext& context, const AffineTransform& layerContentTransform)
{
    bool wasPaintingSVGResourceLayer = m_isPaintingSVGResourceLayer;
    m_isPaintingSVGResourceLayer = true;
    context.concatCTM(layerContentTransform);

    auto localPaintDirtyRect = LayoutRect::infiniteRect();

    auto* rootPaintingLayer = [&] () {
        auto* curr = parent();
        while (curr && !(curr->renderer().isAnonymous() && is<RenderSVGViewportContainer>(curr->renderer())))
            curr = curr->parent();
        return curr;
    }();
    ASSERT(rootPaintingLayer);

    LayerPaintingInfo paintingInfo(rootPaintingLayer, localPaintDirtyRect, PaintBehavior::Normal, LayoutSize());

    OptionSet<PaintLayerFlag> flags { PaintLayerFlag::TemporaryClipRects };
    if (!renderer().hasNonVisibleOverflow())
        flags.add({ PaintLayerFlag::PaintingOverflowContents, PaintLayerFlag::PaintingOverflowContentsRoot });

    paintLayer(context, paintingInfo, flags);

    m_isPaintingSVGResourceLayer = wasPaintingSVGResourceLayer;
}

static inline bool paintForFixedRootBackground(const RenderLayer* layer, OptionSet<RenderLayer::PaintLayerFlag> paintFlags)
{
    return layer->renderer().isDocumentElementRenderer() && (paintFlags & RenderLayer::PaintLayerFlag::PaintingRootBackgroundOnly);
}

void RenderLayer::paintLayer(GraphicsContext& context, const LayerPaintingInfo& paintingInfo, OptionSet<PaintLayerFlag> paintFlags)
{
    auto shouldContinuePaint = [&] () {
        return backing()->paintsIntoWindow()
            || backing()->paintsIntoCompositedAncestor()
            || shouldDoSoftwarePaint(this, paintFlags.contains(PaintLayerFlag::PaintingReflection))
            || paintForFixedRootBackground(this, paintFlags);
    };

    auto paintsIntoDifferentCompositedDestination = [&]() {
        if (paintsIntoProvidedBacking())
            return true;
    
        if (isComposited() && !shouldContinuePaint())
            return true;

        return false;
    };
    
    if (paintsIntoDifferentCompositedDestination()) {
        if (!context.performingPaintInvalidation() && !(paintingInfo.paintBehavior & PaintBehavior::FlattenCompositingLayers))
            return;

        paintFlags.add(PaintLayerFlag::TemporaryClipRects);
    }

    if (viewportConstrainedNotCompositedReason() == NotCompositedForBoundsOutOfView && !(paintingInfo.paintBehavior & PaintBehavior::Snapshotting)) {
        // Don't paint out-of-view viewport constrained layers (when doing prepainting) because they will never be visible
        // unless their position or viewport size is changed.
        ASSERT(renderer().isFixedPositioned());
        return;
    }

    paintLayerWithEffects(context, paintingInfo, paintFlags);
}

void RenderLayer::paintLayerWithEffects(GraphicsContext& context, const LayerPaintingInfo& paintingInfo, OptionSet<PaintLayerFlag> paintFlags)
{
    // Non self-painting leaf layers don't need to be painted as their renderer() should properly paint itself.
    if (!isSelfPaintingLayer() && !hasSelfPaintingLayerDescendant())
        return;

    if (shouldSuppressPaintingLayer(this))
        return;

    // If this layer is totally invisible then there is nothing to paint.
    if (!renderer().opacity() && !is<AccessibilityRegionContext>(paintingInfo.regionContext)) {
        // However, we do want to continue painting for accessibility paints, as we still need accurate
        // geometry for opacity:0 things. It's very common to make form controls "screenreader-only" via
        // CSS, often involving opacity:0, while positioning some other visual-only / mouse-only control in
        // its place. Having the correct geometry is vital for ensuring VoiceOver can still press these controls.
        return;
    }

    if (paintsWithTransparency(paintingInfo.paintBehavior))
        paintFlags.add(PaintLayerFlag::HaveTransparency);

    // PaintLayerFlag::AppliedTransform is used in RenderReplica, to avoid applying the transform twice.
    if (paintsWithTransform(paintingInfo.paintBehavior) && !(paintFlags & PaintLayerFlag::AppliedTransform)) {
        TransformationMatrix layerTransform = renderableTransform(paintingInfo.paintBehavior);
        // If the transform can't be inverted, then don't paint anything.
        if (!layerTransform.isInvertible())
            return;

        // If we have a transparency layer enclosing us and we are the root of a transform, then we need to establish the transparency
        // layer from the parent now, assuming there is a parent
        if (paintFlags & PaintLayerFlag::HaveTransparency) {
            if (this != paintingInfo.rootLayer && parent())
                parent()->beginTransparencyLayers(context, paintingInfo, paintingInfo.paintDirtyRect);
            else
                beginTransparencyLayers(context, paintingInfo, paintingInfo.paintDirtyRect);
        }

        if (enclosingPaginationLayer(ExcludeCompositedPaginatedLayers)) {
            paintTransformedLayerIntoFragments(context, paintingInfo, paintFlags);
            return;
        }

        // Make sure the parent's clip rects have been calculated.
        ClipRect clipRect = paintingInfo.paintDirtyRect;
        GraphicsContextStateSaver stateSaver(context, false);
        RegionContextStateSaver regionContextStateSaver(paintingInfo.regionContext);
        if (parent()) {
            auto options = paintFlags.contains(PaintLayerFlag::PaintingOverflowContents) ? clipRectOptionsForPaintingOverflowContents : clipRectDefaultOptions;
            if (shouldHaveFiltersForPainting(context, paintFlags, paintingInfo.paintBehavior))
                options.add(ClipRectsOption::OutsideFilter);
            if (paintFlags & PaintLayerFlag::TemporaryClipRects)
                options.add(ClipRectsOption::Temporary);
            auto clipRectsContext = ClipRectsContext(paintingInfo.rootLayer, PaintingClipRects, options);
            clipRect = backgroundClipRect(clipRectsContext);
            clipRect.intersect(paintingInfo.paintDirtyRect);
        
            OptionSet<PaintBehavior> paintBehavior = PaintBehavior::Normal;
            if (paintFlags.contains(PaintLayerFlag::PaintingOverflowContents))
                paintBehavior.add(PaintBehavior::CompositedOverflowScrollContent);

            // Always apply SVG viewport clipping in coordinate system before the SVG viewBox transformation is applied.
            if (CheckedPtr svgRoot = dynamicDowncast<RenderSVGRoot>(renderer())) {
                if (svgRoot->shouldApplyViewportClip()) {
                    auto newRect = svgRoot->borderBoxRect();

                    auto offsetFromParent = offsetFromAncestor(clipRectsContext.rootLayer);
                    auto offsetForThisLayer = offsetFromParent + paintingInfo.subpixelOffset;
                    auto devicePixelSnappedOffsetForThisLayer = toFloatSize(roundPointToDevicePixels(toLayoutPoint(offsetForThisLayer), renderer().document().deviceScaleFactor()));
                    newRect.move(devicePixelSnappedOffsetForThisLayer.width(), devicePixelSnappedOffsetForThisLayer.height());

                    clipRect.intersect(newRect);
                }
            }

            // Push the parent coordinate space's clip.
            parent()->clipToRect(context, stateSaver, regionContextStateSaver, paintingInfo, paintBehavior, clipRect);
        }

        paintLayerByApplyingTransform(context, paintingInfo, paintFlags);
        return;
    }
    
    paintLayerContentsAndReflection(context, paintingInfo, paintFlags);
}

void RenderLayer::paintLayerContentsAndReflection(GraphicsContext& context, const LayerPaintingInfo& paintingInfo, OptionSet<PaintLayerFlag> paintFlags)
{
    ASSERT(isSelfPaintingLayer() || hasSelfPaintingLayerDescendant());

    auto localPaintFlags = paintFlags - OptionSet<PaintLayerFlag> { PaintLayerFlag::AppliedTransform, PaintLayerFlag::PaintingOverflowContentsRoot };

    // Paint the reflection first if we have one.
    if (m_reflection && !m_paintingInsideReflection) {
        // Mark that we are now inside replica painting.
        m_paintingInsideReflection = true;
        reflectionLayer()->paintLayer(context, paintingInfo, localPaintFlags | PaintLayerFlag::PaintingReflection);
        m_paintingInsideReflection = false;
    }

    localPaintFlags.add(paintLayerPaintingCompositingAllPhasesFlags());
    paintLayerContents(context, paintingInfo, localPaintFlags);
}

bool RenderLayer::setupFontSubpixelQuantization(GraphicsContext& context, bool& didQuantizeFonts)
{
    if (context.paintingDisabled())
        return false;

    bool scrollingOnMainThread = true;
#if ENABLE(ASYNC_SCROLLING)
    if (RefPtr scrollingCoordinator = page().scrollingCoordinator())
        scrollingOnMainThread = scrollingCoordinator->shouldUpdateScrollLayerPositionSynchronously(renderer().view().frameView());
#endif

    // FIXME: We shouldn't have to disable subpixel quantization for overflow clips or subframes once we scroll those
    // things on the scrolling thread.
    bool contentsScrollByPainting = (renderer().hasNonVisibleOverflow() && !usesCompositedScrolling()) || (renderer().frame().ownerElement());
    bool isZooming = !page().chrome().client().hasStablePageScaleFactor();
    if (scrollingOnMainThread || contentsScrollByPainting || isZooming) {
        didQuantizeFonts = context.shouldSubpixelQuantizeFonts();
        context.setShouldSubpixelQuantizeFonts(false);
        return true;
    }
    return false;
}

std::pair<Path, WindRule> RenderLayer::computeClipPath(const LayoutSize& offsetFromRoot, const LayoutRect& rootRelativeBoundsForNonBoxes) const
{
    auto& style = renderer().style();

    return WTF::switchOn(style.clipPath(),
        [&](const Style::BasicShapePath& clipPath) -> std::pair<Path, WindRule> {
            auto referenceBoxRect = referenceBoxRectForClipPath(clipPath.referenceBox(), offsetFromRoot, rootRelativeBoundsForNonBoxes);
            auto snappedReferenceBoxRect = snapRectToDevicePixelsIfNeeded(referenceBoxRect, renderer());
            return { Style::path(clipPath.shape(), snappedReferenceBoxRect), Style::windRule(clipPath.shape()) };
        },
        [&](const Style::BoxPath& clipPath) -> std::pair<Path, WindRule> {
            CheckedPtr box = dynamicDowncast<RenderBox>(renderer());
            if (box) {
                auto shapeRect = computeRoundedRectForBoxShape(clipPath.referenceBox(), *box).pixelSnappedRoundedRectForPainting(renderer().document().deviceScaleFactor());
                shapeRect.move(offsetFromRoot);
                return { shapeRect.path(), WindRule::NonZero };
            }
            return { Path(), WindRule::NonZero };
        },
        [&](const auto&) -> std::pair<Path, WindRule> {
            return { Path(), WindRule::NonZero };
        }
    );
}

void RenderLayer::setupClipPath(GraphicsContext& context, GraphicsContextStateSaver& stateSaver, RegionContextStateSaver& regionContextStateSaver, const LayerPaintingInfo& paintingInfo, OptionSet<PaintLayerFlag>& paintFlags, const LayoutSize& offsetFromRoot)
{
    bool isCollectingEventRegion = paintFlags.contains(PaintLayerFlag::CollectingEventRegion);
    if (!renderer().hasClipPath() || (context.paintingDisabled() && !isCollectingEventRegion) || paintingInfo.paintDirtyRect.isEmpty())
        return;

    // Applying clip-path on <clipPath> enforces us to use mask based clipping, so return false here to disable path based clipping.
    // Furthermore if we're the child of a resource container (<clipPath> / <mask> / ...) disabled path based clipping.
    if (is<RenderSVGResourceClipper>(m_enclosingSVGHiddenOrResourceContainer)) {
        // If m_isPaintingSVGResourceLayer is true, this function was invoked via paintSVGResourceLayer() -- clipping on <clipPath> is already
        // handled in RenderSVGResourceClipper::applyMaskClipping(), so do not set paintSVGClippingMask to true here.
        paintFlags.set(PaintLayerFlag::PaintingSVGClippingMask, !m_isPaintingSVGResourceLayer);
        return;
    }

    auto clippedContentBounds = calculateLayerBounds(paintingInfo.rootLayer, offsetFromRoot, { UseLocalClipRectIfPossible });

    auto& style = renderer().style();
    LayoutSize paintingOffsetFromRoot = LayoutSize(snapSizeToDevicePixel(offsetFromRoot + paintingInfo.subpixelOffset, LayoutPoint(), renderer().document().deviceScaleFactor()));
    ASSERT(!WTF::holdsAlternative<CSS::Keyword::None>(style.clipPath()));
    if (WTF::holdsAlternative<Style::BasicShapePath>(style.clipPath()) || (WTF::holdsAlternative<Style::BoxPath>(style.clipPath()) && is<RenderBox>(renderer()))) {
        // clippedContentBounds is used as the reference box for inlines, which is also poorly specified: https://github.com/w3c/csswg-drafts/issues/6383.
        auto [path, windRule] = computeClipPath(paintingOffsetFromRoot, clippedContentBounds);

        if (isCollectingEventRegion) {
            regionContextStateSaver.pushClip(path);
            return;
        }

        stateSaver.save();
        context.clipPath(path, windRule);
        return;
    }

    if (auto* svgClipper = renderer().svgClipperResourceFromStyle()) {
        RefPtr graphicsElement = svgClipper->shouldApplyPathClipping();
        if (!graphicsElement) {
            paintFlags.add(PaintLayerFlag::PaintingSVGClippingMask);
            return;
        }

        stateSaver.save();
        FloatRect svgReferenceBox;
        FloatSize coordinateSystemOriginTranslation;
        if (renderer().isSVGLayerAwareRenderer()) {
            ASSERT(paintingInfo.subpixelOffset.isZero());
            auto boundingBoxTopLeftCorner = renderer().nominalSVGLayoutLocation();
            svgReferenceBox = renderer().objectBoundingBox();
            coordinateSystemOriginTranslation = toLayoutPoint(offsetFromRoot) - boundingBoxTopLeftCorner;
        } else {
            auto clipPathObjectBoundingBox = referenceBoxRectForClipPath(CSSBoxType::BorderBox, offsetFromRoot, clippedContentBounds);
            svgReferenceBox = snapRectToDevicePixels(LayoutRect(clipPathObjectBoundingBox), renderer().document().deviceScaleFactor());
        }

        if (!coordinateSystemOriginTranslation.isZero())
            context.translate(coordinateSystemOriginTranslation);

        svgClipper->applyPathClipping(context, renderer(), svgReferenceBox, *graphicsElement);

        if (!coordinateSystemOriginTranslation.isZero())
            context.translate(-coordinateSystemOriginTranslation);
        return;
    }

    if (auto* svgClipper = renderer().legacySVGClipperResourceFromStyle()) {
        // Use the border box as the reference box, even though this is not clearly specified: https://github.com/w3c/csswg-drafts/issues/5786.
        // clippedContentBounds is used as the reference box for inlines, which is also poorly specified: https://github.com/w3c/csswg-drafts/issues/6383.
        auto referenceBox = referenceBoxRectForClipPath(CSSBoxType::BorderBox, offsetFromRoot, clippedContentBounds);
        auto snappedReferenceBox = snapRectToDevicePixelsIfNeeded(referenceBox, renderer());
        auto offset = snappedReferenceBox.location();

        auto snappedClippingBounds = snapRectToDevicePixelsIfNeeded(clippedContentBounds, renderer());
        snappedClippingBounds.moveBy(-offset);

        stateSaver.save();
        context.translate(offset);
        svgClipper->applyClippingToContext(context, renderer(), { { }, referenceBox.size() }, snappedClippingBounds, renderer().style().usedZoom());
        context.translate(-offset);

        // FIXME: Support event regions.
    }
}

void RenderLayer::clearLayerClipPath()
{
    if (auto* svgClipper = renderer().legacySVGClipperResourceFromStyle())
        svgClipper->removeClientFromCache(renderer());
}

bool RenderLayer::shouldHaveFiltersForPainting(GraphicsContext& context, OptionSet<PaintLayerFlag> paintFlags, OptionSet<PaintBehavior> paintBehavior) const
{
    if (context.paintingDisabled())
        return false;

    if (paintFlags & PaintLayerFlag::PaintingOverlayScrollbars)
        return false;

    if (!shouldPaintWithFilters(paintBehavior))
        return false;

    return true;
}

RenderLayerFilters* RenderLayer::filtersForPainting(GraphicsContext& context, OptionSet<PaintLayerFlag> paintFlags, OptionSet<PaintBehavior> paintBehavior)
{
    if (!shouldHaveFiltersForPainting(context, paintFlags, paintBehavior))
        return nullptr;

    return &ensureLayerFilters();
}

GraphicsContext* RenderLayer::setupFilters(GraphicsContext& destinationContext, LayerPaintingInfo& paintingInfo, OptionSet<PaintLayerFlag>& paintFlags, const LayoutSize& offsetFromRoot, const ClipRect& backgroundRect)
{
    auto* paintingFilters = filtersForPainting(destinationContext, paintFlags, paintingInfo.paintBehavior);
    if (!paintingFilters)
        return nullptr;

    LayoutRect filterRepaintRect = paintingFilters->dirtySourceRect();
    filterRepaintRect.move(offsetFromRoot);

    auto rootRelativeBounds = calculateLayerBounds(paintingInfo.rootLayer, offsetFromRoot, { RenderLayer::PreserveAncestorFlags });

    GraphicsContext* filterContext = paintingFilters->beginFilterEffect(renderer(), destinationContext, enclosingIntRect(rootRelativeBounds), enclosingIntRect(paintingInfo.paintDirtyRect), enclosingIntRect(filterRepaintRect), backgroundRect.rect());
    if (!filterContext)
        return nullptr;

    paintingInfo.paintDirtyRect = paintingFilters->repaintRect();
    if (paintingFilters->hasFilterThatMovesPixels()) {
        m_suppressAncestorClippingInsideFilter = true;
        paintFlags.add(PaintLayerFlag::TemporaryClipRects);
    }
    paintingInfo.requireSecurityOriginAccessForWidgets = paintingFilters->hasFilterThatShouldBeRestrictedBySecurityOrigin();

    return filterContext;
}

void RenderLayer::applyFilters(GraphicsContext& originalContext, const LayerPaintingInfo& paintingInfo, OptionSet<PaintBehavior> behavior, const ClipRect& backgroundRect)
{
    GraphicsContextStateSaver stateSaver(originalContext, false);
    bool needsClipping = m_filters->hasSourceImage();

    m_suppressAncestorClippingInsideFilter = false;

    if (needsClipping) {
        RegionContextStateSaver regionContextStateSaver(paintingInfo.regionContext);

        clipToRect(originalContext, stateSaver, regionContextStateSaver, paintingInfo, behavior, backgroundRect);
    }

    m_filters->applyFilterEffect(originalContext);
}

void RenderLayer::paintLayerContents(GraphicsContext& context, const LayerPaintingInfo& paintingInfo, OptionSet<PaintLayerFlag> paintFlags)
{
    ASSERT(isSelfPaintingLayer() || hasSelfPaintingLayerDescendant());

    if (context.detectingContentfulPaint() && context.contentfulPaintDetected())
        return;

    auto localPaintFlags = paintFlags - PaintLayerFlag::AppliedTransform;

    bool haveTransparency = localPaintFlags.contains(PaintLayerFlag::HaveTransparency);
    bool isPaintingOverlayScrollbars = localPaintFlags.contains(PaintLayerFlag::PaintingOverlayScrollbars);
    bool isPaintingCompositedForeground = localPaintFlags.contains(PaintLayerFlag::PaintingCompositingForegroundPhase);
    bool isPaintingCompositedBackground = localPaintFlags.contains(PaintLayerFlag::PaintingCompositingBackgroundPhase);
    bool isPaintingOverflowContents = localPaintFlags.contains(PaintLayerFlag::PaintingOverflowContents);
    bool isCollectingEventRegion = localPaintFlags.contains(PaintLayerFlag::CollectingEventRegion);
    bool isCollectingAccessibilityRegion = is<AccessibilityRegionContext>(paintingInfo.regionContext);

    bool isSelfPaintingLayer = this->isSelfPaintingLayer();
    bool isInsideSkippedSubtree = renderer().isSkippedContent();

    auto hasVisibleContent = [&]() -> bool {
        if (isInsideSkippedSubtree)
            return false;

        if (!m_hasVisibleContent)
            return false;

        if (!m_enclosingSVGHiddenOrResourceContainer)
            return true;

        // Hidden SVG containers (<defs> / <symbol> ...) and their children are never painted directly.
        if (!is<RenderSVGResourceContainer>(m_enclosingSVGHiddenOrResourceContainer))
            return false;

        // SVG resource layers and their children are only painted indirectly, via paintSVGResourceLayer().
        ASSERT(m_enclosingSVGHiddenOrResourceContainer->hasLayer());
        return m_enclosingSVGHiddenOrResourceContainer->layer()->isPaintingSVGResourceLayer();
    };

    auto shouldSkipNonFixedTopDocumentContent = [&] {
        if (!paintingInfo.paintBehavior.contains(PaintBehavior::FixedAndStickyLayersOnly))
            return false;

        if (hasFixedAncestor() || m_hasStickyAncestor)
            return false;

        if (isViewportConstrained())
            return false;

        if (!m_renderer->frame().isMainFrame())
            return false;

        return true;
    };

    bool shouldPaintContent = hasVisibleContent()
        && isSelfPaintingLayer
        && !isPaintingOverlayScrollbars
        && !isCollectingEventRegion
        && !isCollectingAccessibilityRegion
        && !shouldSkipNonFixedTopDocumentContent();

    bool shouldPaintOutline = [&]() {
        if (!isSelfPaintingLayer)
            return false;

        if (!shouldPaintContent)
            return false;

        if (isPaintingOverlayScrollbars || isCollectingEventRegion || isCollectingAccessibilityRegion)
            return false;

        // For the current layer, the outline has been painted by the primary GraphicsLayer.
        if (localPaintFlags.contains(PaintLayerFlag::PaintingOverflowContentsRoot))
            return false;

        // Paint outlines in the background phase for a scroll container so that they don't scroll with the content.
        // FIXME: inset outlines will have the wrong z-ordering with scrolled content. See also webkit.org/b/249457.
        if (localPaintFlags.contains(PaintLayerFlag::PaintingOverflowContainer))
            return isPaintingCompositedBackground;

        return isPaintingCompositedForeground;
    }();

    bool shouldPaintNegativeZIndexChildren = [&]() {
        if (localPaintFlags.contains(PaintLayerFlag::PaintingOverflowContainer))
            return false;

        if (localPaintFlags.contains(PaintLayerFlag::PaintingOverflowContents)) {
            // Overflow contents has the "PaintingCompositingForegroundPhase" phase,
            // but we need to paint negative z-index layers here so they scroll with the content.
            return true;
        }

        return isPaintingCompositedBackground;
    }();

    auto shouldExcludeBasedOnContainingBlock = [&]() {
        if (CheckedPtr rootAsBlock = dynamicDowncast<RenderBlock>(paintingInfo.subtreePaintRoot))
            return !rootAsBlock->isContainingBlockAncestorFor(renderer());
        return false;
    };

    if (paintingInfo.paintBehavior.contains(PaintBehavior::DraggableSnapshot) && paintingInfo.subtreePaintRoot) {
        if (paintingInfo.subtreePaintRoot->hasLayer()) {
            CheckedPtr subtreeRootLayer = paintingInfo.subtreePaintRoot->enclosingLayer();
            bool isLayerInSubtree = (this == subtreeRootLayer) || isDescendantOf(*subtreeRootLayer);

            if (isLayerInSubtree && (paintingInfo.subtreePaintRoot != &renderer() && shouldExcludeBasedOnContainingBlock()))
                shouldPaintContent = false;
        } else if (renderer().isAbsolutelyPositioned() && paintingInfo.subtreePaintRoot != &renderer() && shouldExcludeBasedOnContainingBlock()) {
            shouldPaintContent = false;
        }
    }

    if (localPaintFlags.contains(PaintLayerFlag::PaintingRootBackgroundOnly) && !renderer().isRenderView() && !renderer().isDocumentElementRenderer()) {
        // If beginTransparencyLayers was called prior to this, ensure the transparency state is cleaned up before returning.
        if (haveTransparency && m_usedTransparency && !m_paintingInsideReflection) {
            if (m_savedAlphaForTransparency) {
                context.setAlpha(*m_savedAlphaForTransparency);
                m_savedAlphaForTransparency = std::nullopt;
            } else {
                context.endTransparencyLayer();
                context.restore();
            }
            m_usedTransparency = false;
        }

        return;
    }

    updateLayerListsIfNeeded();

    LayoutSize offsetFromRoot = offsetFromAncestor(paintingInfo.rootLayer);

    // FIXME: We shouldn't have to disable subpixel quantization for overflow clips or subframes once we scroll those
    // things on the scrolling thread.
    bool didQuantizeFonts = true;
    bool needToAdjustSubpixelQuantization = setupFontSubpixelQuantization(context, didQuantizeFonts);

    // Apply clip-path to context.
    LayoutSize columnAwareOffsetFromRoot = offsetFromRoot;
    if (renderer().enclosingFragmentedFlow() && (renderer().hasClipPath() || shouldHaveFiltersForPainting(context, paintFlags, paintingInfo.paintBehavior)))
        columnAwareOffsetFromRoot = toLayoutSize(convertToLayerCoords(paintingInfo.rootLayer, LayoutPoint(), AdjustForColumns));

    GraphicsContextStateSaver stateSaver(context, false);
    RegionContextStateSaver regionContextStateSaver(paintingInfo.regionContext);

    if (shouldApplyClipPath(paintingInfo.paintBehavior, localPaintFlags))
        setupClipPath(context, stateSaver, regionContextStateSaver, paintingInfo, localPaintFlags, columnAwareOffsetFromRoot);

    bool applySVGClippingMask = localPaintFlags.contains(PaintLayerFlag::PaintingSVGClippingMask);
    if (applySVGClippingMask)
        localPaintFlags.remove(PaintLayerFlag::PaintingSVGClippingMask);

    bool selectionAndBackgroundsOnly = paintingInfo.paintBehavior.contains(PaintBehavior::SelectionAndBackgroundsOnly);
    bool selectionOnly = paintingInfo.paintBehavior.contains(PaintBehavior::SelectionOnly);

    m_paintFrequencyTracker.track(page().lastRenderingUpdateTimestamp());

    LayerFragments layerFragments;
    RenderObject* subtreePaintRootForRenderer = nullptr;

    auto paintBehavior = [&]() {
        static constexpr OptionSet flagsToCopy {
            PaintBehavior::FlattenCompositingLayers,
            PaintBehavior::Snapshotting,
            PaintBehavior::ExcludeSelection,
            PaintBehavior::ExcludeReplacedContentExceptForIFrames,
            PaintBehavior::ExcludeText,
            PaintBehavior::FixedAndStickyLayersOnly,
            PaintBehavior::DrawsHDRContent,
        };
        OptionSet<PaintBehavior> paintBehavior = paintingInfo.paintBehavior & flagsToCopy;

        if (localPaintFlags.contains(PaintLayerFlag::PaintingSkipRootBackground))
            paintBehavior.add(PaintBehavior::SkipRootBackground);
        else if (localPaintFlags.contains(PaintLayerFlag::PaintingRootBackgroundOnly))
            paintBehavior.add(PaintBehavior::RootBackgroundOnly);

        // FIXME: This seems wrong. We should retain the DefaultAsynchronousImageDecode flag for all RenderLayers painted into the root tile cache.
        if ((paintingInfo.paintBehavior & PaintBehavior::DefaultAsynchronousImageDecode) && isRenderViewLayer())
            paintBehavior.add(PaintBehavior::DefaultAsynchronousImageDecode);

        if (isPaintingOverflowContents)
            paintBehavior.add(PaintBehavior::CompositedOverflowScrollContent);

        if (isCollectingEventRegion) {
            paintBehavior = paintBehavior & PaintBehavior::CompositedOverflowScrollContent;
            if (isPaintingCompositedForeground)
                paintBehavior.add(PaintBehavior::EventRegionIncludeForeground);
            if (isPaintingCompositedBackground)
                paintBehavior.add(PaintBehavior::EventRegionIncludeBackground);
        }

        return paintBehavior;
    }();

    { // Scope for filter-related state changes.
        ClipRect backgroundRect;

        if (shouldHaveFiltersForPainting(context, paintFlags, paintBehavior)) {
            // When we called collectFragments() last time, paintDirtyRect was reset to represent the filter bounds.
            // Now we need to compute the backgroundRect uncontaminated by filters, in order to clip the filtered result.
            // Note that we also use paintingInfo here, not localPaintingInfo which filters also contaminated.
            LayerFragments layerFragments;
            auto clipRectOptions = isPaintingOverflowContents ? clipRectOptionsForPaintingOverflowContents : clipRectDefaultOptions;
            clipRectOptions.add(ClipRectsOption::OutsideFilter);
            if (localPaintFlags & PaintLayerFlag::TemporaryClipRects)
                clipRectOptions.add(ClipRectsOption::Temporary);
            collectFragments(layerFragments, paintingInfo.rootLayer, paintingInfo.paintDirtyRect, ExcludeCompositedPaginatedLayers, PaintingClipRects, clipRectOptions, offsetFromRoot);
            updatePaintingInfoForFragments(layerFragments, paintingInfo, localPaintFlags, shouldPaintContent, offsetFromRoot);

            // FIXME: Handle more than one fragment.
            backgroundRect = layerFragments.isEmpty() ? ClipRect() : layerFragments[0].backgroundRect;

            if (haveTransparency) {
                // If we have a filter and transparency, we have to eagerly start a transparency layer here, rather than risk a child layer lazily starts one with the wrong context.
                beginTransparencyLayers(context, paintingInfo, paintingInfo.paintDirtyRect);
            }
        }

        LayerPaintingInfo localPaintingInfo(paintingInfo);
        GraphicsContext* filterContext = setupFilters(context, localPaintingInfo, localPaintFlags, columnAwareOffsetFromRoot, backgroundRect);
        GraphicsContext& currentContext = filterContext ? *filterContext : context;

        if (filterContext)
            localPaintingInfo.paintBehavior.add(PaintBehavior::DontShowVisitedLinks);

        // If this layer's renderer is a child of the subtreePaintRoot, we render unconditionally, which
        // is done by passing a nil subtreePaintRoot down to our renderer (as if no subtreePaintRoot was ever set).
        // Otherwise, our renderer tree may or may not contain the subtreePaintRoot root, so we pass that root along
        // so it will be tested against as we descend through the renderers.
        if (localPaintingInfo.subtreePaintRoot && !renderer().isDescendantOf(localPaintingInfo.subtreePaintRoot))
            subtreePaintRootForRenderer = localPaintingInfo.subtreePaintRoot;

        if (localPaintingInfo.overlapTestRequests && isSelfPaintingLayer)
            performOverlapTests(*localPaintingInfo.overlapTestRequests, localPaintingInfo.rootLayer, this);

        LayoutRect paintDirtyRect = localPaintingInfo.paintDirtyRect;
        if (shouldPaintContent || shouldPaintOutline || isPaintingOverlayScrollbars || isCollectingEventRegion || isCollectingAccessibilityRegion) {
            // Collect the fragments. This will compute the clip rectangles and paint offsets for each layer fragment, as well as whether or not the content of each
            // fragment should paint.
            auto clipRectOptions = isPaintingOverflowContents ? clipRectOptionsForPaintingOverflowContents : clipRectDefaultOptions;
            if (localPaintFlags & PaintLayerFlag::TemporaryClipRects)
                clipRectOptions.add(ClipRectsOption::Temporary);
            collectFragments(layerFragments, localPaintingInfo.rootLayer, paintDirtyRect, ExcludeCompositedPaginatedLayers, PaintingClipRects, clipRectOptions, offsetFromRoot);
            updatePaintingInfoForFragments(layerFragments, localPaintingInfo, localPaintFlags, shouldPaintContent, offsetFromRoot);
        }
        
        if (isPaintingCompositedBackground) {
            // Paint only the backgrounds for all of the fragments of the layer.
            if (shouldPaintContent && !selectionOnly) {
                paintBackgroundForFragments(layerFragments, currentContext, context, paintingInfo.paintDirtyRect, haveTransparency,
                    localPaintingInfo, paintBehavior, subtreePaintRootForRenderer);
            }
        }

        // Now walk the sorted list of children with negative z-indices.
        if (shouldPaintNegativeZIndexChildren)
            paintList(negativeZOrderLayers(), currentContext, paintingInfo, localPaintFlags);
        
        if (isPaintingCompositedForeground && shouldPaintContent)
            paintForegroundForFragments(layerFragments, currentContext, context, paintingInfo.paintDirtyRect, haveTransparency, localPaintingInfo, paintBehavior, subtreePaintRootForRenderer);

        if (isCollectingEventRegion && !isInsideSkippedSubtree)
            collectEventRegionForFragments(layerFragments, currentContext, localPaintingInfo, paintBehavior);

        if (isCollectingAccessibilityRegion)
            collectAccessibilityRegionsForFragments(layerFragments, currentContext, localPaintingInfo, paintBehavior);

        if (shouldPaintOutline)
            paintOutlineForFragments(layerFragments, currentContext, localPaintingInfo, paintBehavior, subtreePaintRootForRenderer);

        if (isPaintingCompositedForeground) {
            // Paint any child layers that have overflow.
            paintList(normalFlowLayers(), currentContext, paintingInfo, localPaintFlags);

            // Now walk the sorted list of children with positive z-indices.
            paintList(positiveZOrderLayers(), currentContext, localPaintingInfo, localPaintFlags);
        }

        if (m_scrollableArea) {
            if (isPaintingOverlayScrollbars && m_scrollableArea->hasScrollbars())
                paintOverflowControlsForFragments(layerFragments, currentContext, localPaintingInfo);
        }

        if (filterContext) {
            applyFilters(context, paintingInfo, paintBehavior, backgroundRect);
            // Painting a snapshot might have temporarily overriden the filter painting strategy,
            // make sure it gets reset.
            updateFilterPaintingStrategy();
        }
    }
    
    if (shouldPaintContent && !(selectionOnly || selectionAndBackgroundsOnly)) {
        if (shouldPaintMask(paintingInfo.paintBehavior, localPaintFlags)) {
            // Paint the mask for the fragments.
            paintMaskForFragments(layerFragments, context, paintingInfo, paintBehavior, subtreePaintRootForRenderer);
        }

        if (applySVGClippingMask || (!(paintFlags & PaintLayerFlag::PaintingCompositingMaskPhase) && (paintFlags & PaintLayerFlag::PaintingCompositingClipPathPhase))) {
            // Re-use paintChildClippingMaskForFragments to paint black for the compositing clipping mask.
            paintChildClippingMaskForFragments(layerFragments, context, paintingInfo, paintBehavior, subtreePaintRootForRenderer);
        }

        if (localPaintFlags & PaintLayerFlag::PaintingChildClippingMaskPhase) {
            // Paint the border radius mask for the fragments.
            paintChildClippingMaskForFragments(layerFragments, context, paintingInfo, paintBehavior, subtreePaintRootForRenderer);
        }
    }

    // End our transparency layer
    if (haveTransparency && m_usedTransparency && !m_paintingInsideReflection) {
        if (m_savedAlphaForTransparency) {
            context.setAlpha(*m_savedAlphaForTransparency);
            m_savedAlphaForTransparency = std::nullopt;
        } else {
            context.endTransparencyLayer();
            context.restore();
        }
        m_usedTransparency = false;
    }

    // Re-set this to whatever it was before we painted the layer.
    if (needToAdjustSubpixelQuantization)
        context.setShouldSubpixelQuantizeFonts(didQuantizeFonts);
}

void RenderLayer::paintLayerByApplyingTransform(GraphicsContext& context, const LayerPaintingInfo& paintingInfo, OptionSet<PaintLayerFlag> paintFlags, const LayoutSize& translationOffset)
{
    // This involves subtracting out the position of the layer in our current coordinate space, but preserving
    // the accumulated error for sub-pixel layout.
    // Note: The pixel-snapping logic is disabled for the whole SVG render tree, except the outermost <svg>.
    float deviceScaleFactor = renderer().document().deviceScaleFactor();
    LayoutSize offsetFromParent = offsetFromAncestor(paintingInfo.rootLayer);
    offsetFromParent += translationOffset;
    TransformationMatrix transform(renderableTransform(paintingInfo.paintBehavior));
    // Add the subpixel accumulation to the current layer's offset so that we can always snap the translateRight value to where the renderer() is supposed to be painting.
    LayoutSize offsetForThisLayer = offsetFromParent + paintingInfo.subpixelOffset;
    FloatSize alignedOffsetForThisLayer = rendererNeedsPixelSnapping(renderer()) ? toFloatSize(roundPointToDevicePixels(toLayoutPoint(offsetForThisLayer), deviceScaleFactor)) : offsetForThisLayer;
    // We handle accumulated subpixels through nested layers here. Since the context gets translated to device pixels,
    // all we need to do is add the delta to the accumulated pixels coming from ancestor layers.
    // Translate the graphics context to the snapping position to avoid off-device-pixel positing.
    transform.translateRight(alignedOffsetForThisLayer.width(), alignedOffsetForThisLayer.height());
    // Apply the transform.
    auto oldTransform = context.getCTM();
    auto affineTransform = transform.toAffineTransform();
    context.concatCTM(affineTransform);

    if (paintingInfo.regionContext)
        paintingInfo.regionContext->pushTransform(affineTransform);

    // Only propagate the subpixel offsets to the descendant layers, if we're not the root
    // of a SVG subtree, where no pixel snapping is applied -- only the outermost <svg> layer
    // is pixel-snapped "as whole", if it's part of a compound document, e.g. inline SVG in HTML.
    LayoutSize adjustedSubpixelOffset;
    if (rendererNeedsPixelSnapping(renderer()) && !renderer().isRenderSVGRoot())
        adjustedSubpixelOffset = offsetForThisLayer - LayoutSize(alignedOffsetForThisLayer);

    // Now do a paint with the root layer shifted to be us.
    LayerPaintingInfo transformedPaintingInfo(paintingInfo);
    transformedPaintingInfo.rootLayer = this;
    if (!transformedPaintingInfo.paintDirtyRect.isInfinite())
        transformedPaintingInfo.paintDirtyRect = LayoutRect(encloseRectToDevicePixels(valueOrDefault(transform.inverse()).mapRect(paintingInfo.paintDirtyRect), deviceScaleFactor));

    paintFlags.remove(PaintLayerFlag::PaintingOverflowContents);

    transformedPaintingInfo.subpixelOffset = adjustedSubpixelOffset;
    paintLayerContentsAndReflection(context, transformedPaintingInfo, paintFlags);

    if (paintingInfo.regionContext)
        paintingInfo.regionContext->popTransform();

    context.setCTM(oldTransform);
}

void RenderLayer::paintList(LayerList layerIterator, GraphicsContext& context, const LayerPaintingInfo& paintingInfo, OptionSet<PaintLayerFlag> paintFlags)
{
    if (layerIterator.begin() == layerIterator.end())
        return;

    if (!hasSelfPaintingLayerDescendant())
        return;

#if ASSERT_ENABLED
    LayerListMutationDetector mutationChecker(*this);
#endif

    for (auto* childLayer : layerIterator) {
        if (paintFlags.contains(PaintLayerFlag::PaintingSkipDescendantViewTransition)) {
            if (childLayer->renderer().effectiveCapturedInViewTransition())
                continue;
            if (childLayer->renderer().isViewTransitionPseudo())
                continue;
        }
        childLayer->paintLayer(context, paintingInfo, paintFlags);
    }
}

RenderLayer* RenderLayer::enclosingPaginationLayerInSubtree(const RenderLayer* rootLayer, PaginationInclusionMode mode) const
{
    // If we don't have an enclosing layer, or if the root layer is the same as the enclosing layer,
    // then just return the enclosing pagination layer (it will be 0 in the former case and the rootLayer in the latter case).
    RenderLayer* paginationLayer = enclosingPaginationLayer(mode);
    if (!paginationLayer || rootLayer == paginationLayer)
        return paginationLayer;
    
    // Walk up the layer tree and see which layer we hit first. If it's the root, then the enclosing pagination
    // layer isn't in our subtree and we return nullptr. If we hit the enclosing pagination layer first, then
    // we can return it.
    for (const RenderLayer* layer = this; layer; layer = layer->parent()) {
        if (layer == rootLayer)
            return nullptr;
        if (layer == paginationLayer)
            return paginationLayer;
    }
    
    // This should never be reached, since an enclosing layer should always either be the rootLayer or be
    // our enclosing pagination layer.
    ASSERT_NOT_REACHED();
    return nullptr;
}

void RenderLayer::collectFragments(LayerFragments& fragments, const RenderLayer* rootLayer, const LayoutRect& dirtyRect, PaginationInclusionMode inclusionMode,
    ClipRectsType clipRectsType, OptionSet<ClipRectsOption> clipRectOptions, const LayoutSize& offsetFromRoot,
    const LayoutRect* layerBoundingBox, ShouldApplyRootOffsetToFragments applyRootOffsetToFragments)
{
    RenderLayer* paginationLayer = enclosingPaginationLayerInSubtree(rootLayer, inclusionMode);
    if (!paginationLayer || isTransformed()) {
        // For unpaginated layers, there is only one fragment.
        LayerFragment fragment;
        ClipRectsContext clipRectsContext(rootLayer, clipRectsType, clipRectOptions);
        calculateRects(clipRectsContext, dirtyRect, fragment.layerBounds, fragment.backgroundRect, fragment.foregroundRect, offsetFromRoot);
        fragments.append(fragment);
        return;
    }
    
    // Compute our offset within the enclosing pagination layer.
    LayoutSize offsetWithinPaginatedLayer = offsetFromAncestor(paginationLayer);
    
    // Calculate clip rects relative to the enclosingPaginationLayer. The purpose of this call is to determine our bounds clipped to intermediate
    // layers between us and the pagination context. It's important to minimize the number of fragments we need to create and this helps with that.
    clipRectOptions.add(ClipRectsOption::Temporary);
    ClipRectsContext paginationClipRectsContext(paginationLayer, PaintingClipRects, clipRectOptions);
    LayoutRect layerBoundsInFragmentedFlow;
    ClipRect backgroundRectInFragmentedFlow;
    ClipRect foregroundRectInFragmentedFlow;
    calculateRects(paginationClipRectsContext, LayoutRect::infiniteRect(), layerBoundsInFragmentedFlow, backgroundRectInFragmentedFlow, foregroundRectInFragmentedFlow,
        offsetWithinPaginatedLayer);
    
    // Take our bounding box within the flow thread and clip it.
    LayoutRect layerBoundingBoxInFragmentedFlow = layerBoundingBox ? *layerBoundingBox : boundingBox(paginationLayer, offsetWithinPaginatedLayer);
    layerBoundingBoxInFragmentedFlow.intersect(backgroundRectInFragmentedFlow.rect());
    
    auto& enclosingFragmentedFlow = downcast<RenderFragmentedFlow>(paginationLayer->renderer());
    RenderLayer* parentPaginationLayer = paginationLayer->parent()->enclosingPaginationLayerInSubtree(rootLayer, inclusionMode);
    LayerFragments ancestorFragments;
    if (parentPaginationLayer) {
        // Compute a bounding box accounting for fragments.
        LayoutRect layerFragmentBoundingBoxInParentPaginationLayer = enclosingFragmentedFlow.fragmentsBoundingBox(layerBoundingBoxInFragmentedFlow);
        
        // Convert to be in the ancestor pagination context's coordinate space.
        LayoutSize offsetWithinParentPaginatedLayer = paginationLayer->offsetFromAncestor(parentPaginationLayer);
        layerFragmentBoundingBoxInParentPaginationLayer.move(offsetWithinParentPaginatedLayer);
        
        // Now collect ancestor fragments.
        parentPaginationLayer->collectFragments(ancestorFragments, rootLayer, dirtyRect, inclusionMode, clipRectsType, clipRectOptions,
            offsetFromAncestor(rootLayer), &layerFragmentBoundingBoxInParentPaginationLayer, ApplyRootOffsetToFragments);
        
        if (ancestorFragments.isEmpty())
            return;
        
        for (auto& ancestorFragment : ancestorFragments) {
            // Shift the dirty rect into flow thread coordinates.
            LayoutRect dirtyRectInFragmentedFlow(dirtyRect);
            dirtyRectInFragmentedFlow.move(-offsetWithinParentPaginatedLayer - ancestorFragment.paginationOffset);
            
            size_t oldSize = fragments.size();
            
            // Tell the flow thread to collect the fragments. We pass enough information to create a minimal number of fragments based off the pages/columns
            // that intersect the actual dirtyRect as well as the pages/columns that intersect our layer's bounding box.
            enclosingFragmentedFlow.collectLayerFragments(fragments, layerBoundingBoxInFragmentedFlow, dirtyRectInFragmentedFlow);
            
            size_t newSize = fragments.size();
            
            if (oldSize == newSize)
                continue;

            for (size_t i = oldSize; i < newSize; ++i) {
                LayerFragment& fragment = fragments.at(i);
                
                // Set our four rects with all clipping applied that was internal to the flow thread.
                fragment.setRects(layerBoundsInFragmentedFlow, backgroundRectInFragmentedFlow, foregroundRectInFragmentedFlow, layerBoundingBoxInFragmentedFlow);
                
                // Shift to the root-relative physical position used when painting the flow thread in this fragment.
                fragment.moveBy(toLayoutPoint(ancestorFragment.paginationOffset + fragment.paginationOffset + offsetWithinParentPaginatedLayer));

                // Intersect the fragment with our ancestor's background clip so that e.g., columns in an overflow:hidden block are
                // properly clipped by the overflow.
                fragment.intersect(ancestorFragment.paginationClip);
                
                // Now intersect with our pagination clip. This will typically mean we're just intersecting the dirty rect with the column
                // clip, so the column clip ends up being all we apply.
                fragment.intersect(fragment.paginationClip);
                
                if (applyRootOffsetToFragments == ApplyRootOffsetToFragments)
                    fragment.paginationOffset = fragment.paginationOffset + offsetWithinParentPaginatedLayer;
            }
        }
        
        return;
    }
    
    // Shift the dirty rect into flow thread coordinates.
    LayoutSize offsetOfPaginationLayerFromRoot = enclosingPaginationLayer(inclusionMode)->offsetFromAncestor(rootLayer);
    LayoutRect dirtyRectInFragmentedFlow(dirtyRect);
    dirtyRectInFragmentedFlow.move(-offsetOfPaginationLayerFromRoot);

    // Tell the flow thread to collect the fragments. We pass enough information to create a minimal number of fragments based off the pages/columns
    // that intersect the actual dirtyRect as well as the pages/columns that intersect our layer's bounding box.
    enclosingFragmentedFlow.collectLayerFragments(fragments, layerBoundingBoxInFragmentedFlow, dirtyRectInFragmentedFlow);
    
    if (fragments.isEmpty())
        return;
    
    // Get the parent clip rects of the pagination layer, since we need to intersect with that when painting column contents.
    ClipRect ancestorClipRect = dirtyRect;
    if (paginationLayer->parent()) {
        ClipRectsContext clipRectsContext(rootLayer, clipRectsType, clipRectOptions);
        ancestorClipRect = paginationLayer->backgroundClipRect(clipRectsContext);
        ancestorClipRect.intersect(dirtyRect);
    }

    for (auto& fragment : fragments) {
        // Set our four rects with all clipping applied that was internal to the flow thread.
        fragment.setRects(layerBoundsInFragmentedFlow, backgroundRectInFragmentedFlow, foregroundRectInFragmentedFlow, layerBoundingBoxInFragmentedFlow);
        
        // Shift to the root-relative physical position used when painting the flow thread in this fragment.
        fragment.moveBy(toLayoutPoint(fragment.paginationOffset + offsetOfPaginationLayerFromRoot));

        // Intersect the fragment with our ancestor's background clip so that e.g., columns in an overflow:hidden block are
        // properly clipped by the overflow.
        fragment.intersect(ancestorClipRect);

        // Now intersect with our pagination clip. This will typically mean we're just intersecting the dirty rect with the column
        // clip, so the column clip ends up being all we apply.
        fragment.intersect(fragment.paginationClip);
        
        if (applyRootOffsetToFragments == ApplyRootOffsetToFragments)
            fragment.paginationOffset = fragment.paginationOffset + offsetOfPaginationLayerFromRoot;
    }
}

void RenderLayer::updatePaintingInfoForFragments(LayerFragments& fragments, const LayerPaintingInfo& localPaintingInfo, OptionSet<PaintLayerFlag> localPaintFlags,
    bool shouldPaintContent, const LayoutSize& offsetFromRoot)
{
    for (auto& fragment : fragments) {
        fragment.shouldPaintContent = shouldPaintContent;
        if (this != localPaintingInfo.rootLayer || !(localPaintFlags & PaintLayerFlag::PaintingOverflowContents)) {
            LayoutSize newOffsetFromRoot = offsetFromRoot + fragment.paginationOffset;
            fragment.shouldPaintContent &= intersectsDamageRect(fragment.layerBounds, fragment.backgroundRect.rect(), localPaintingInfo.rootLayer, newOffsetFromRoot, fragment.boundingBox);
        }
    }
}

void RenderLayer::paintTransformedLayerIntoFragments(GraphicsContext& context, const LayerPaintingInfo& paintingInfo, OptionSet<PaintLayerFlag> paintFlags)
{
    LayerFragments enclosingPaginationFragments;
    LayoutSize offsetOfPaginationLayerFromRoot;
    RenderLayer* paginatedLayer = enclosingPaginationLayer(ExcludeCompositedPaginatedLayers);
    LayoutRect transformedExtent = transparencyClipBox(*this, paginatedLayer, PaintingTransparencyClipBox, RootOfTransparencyClipBox, paintingInfo.paintBehavior);

    auto clipRectOptions = paintFlags.contains(PaintLayerFlag::PaintingOverflowContents) ? clipRectOptionsForPaintingOverflowContents : clipRectDefaultOptions;
    if (shouldHaveFiltersForPainting(context, paintFlags, paintingInfo.paintBehavior))
        clipRectOptions.add(ClipRectsOption::OutsideFilter);
    if (paintFlags & PaintLayerFlag::TemporaryClipRects)
        clipRectOptions.add(ClipRectsOption::Temporary);
    paginatedLayer->collectFragments(enclosingPaginationFragments, paintingInfo.rootLayer, paintingInfo.paintDirtyRect, ExcludeCompositedPaginatedLayers, PaintingClipRects, clipRectOptions, offsetOfPaginationLayerFromRoot, &transformedExtent);

    for (const auto& fragment : enclosingPaginationFragments) {
        // Apply the page/column clip for this fragment, as well as any clips established by layers in between us and
        // the enclosing pagination layer.
        LayoutRect clipRect = fragment.backgroundRect.rect();
        
        // Now compute the clips within a given fragment
        if (parent() != paginatedLayer) {
            offsetOfPaginationLayerFromRoot = toLayoutSize(paginatedLayer->convertToLayerCoords(paintingInfo.rootLayer, toLayoutPoint(offsetOfPaginationLayerFromRoot)));

            auto clipRectsContext = ClipRectsContext(paginatedLayer, PaintingClipRects, clipRectOptions);
            LayoutRect parentClipRect = backgroundClipRect(clipRectsContext).rect();
            parentClipRect.move(fragment.paginationOffset + offsetOfPaginationLayerFromRoot);
            clipRect.intersect(parentClipRect);
        }

        OptionSet<PaintBehavior> paintBehavior = PaintBehavior::Normal;
        if (paintFlags.contains(PaintLayerFlag::PaintingOverflowContents))
            paintBehavior.add(PaintBehavior::CompositedOverflowScrollContent);

        GraphicsContextStateSaver stateSaver(context, false);
        RegionContextStateSaver regionContextStateSaver(paintingInfo.regionContext);

        parent()->clipToRect(context, stateSaver, regionContextStateSaver, paintingInfo, paintBehavior, clipRect);
        paintLayerByApplyingTransform(context, paintingInfo, paintFlags, fragment.paginationOffset);
    }
}

void RenderLayer::paintBackgroundForFragments(const LayerFragments& layerFragments, GraphicsContext& context, GraphicsContext& contextForTransparencyLayer,
    const LayoutRect& transparencyPaintDirtyRect, bool haveTransparency, const LayerPaintingInfo& localPaintingInfo, OptionSet<PaintBehavior> paintBehavior,
    RenderObject* subtreePaintRootForRenderer)
{
    for (const auto& fragment : layerFragments) {
        if (!fragment.shouldPaintContent)
            continue;

        // Begin transparency layers lazily now that we know we have to paint something.
        if (haveTransparency)
            beginTransparencyLayers(contextForTransparencyLayer, localPaintingInfo, transparencyPaintDirtyRect);
    
        GraphicsContextStateSaver stateSaver(context, false);
        RegionContextStateSaver regionContextStateSaver(localPaintingInfo.regionContext);

        // Paint our background first, before painting any child layers.
        // Establish the clip used to paint our background.
        clipToRect(context, stateSaver, regionContextStateSaver, localPaintingInfo, paintBehavior, fragment.backgroundRect, DoNotIncludeSelfForBorderRadius); // Background painting will handle clipping to self.

        // Paint the background.
        // FIXME: Eventually we will collect the region from the fragment itself instead of just from the paint info.
        PaintInfo paintInfo(context, fragment.backgroundRect.rect(), PaintPhase::BlockBackground, paintBehavior, subtreePaintRootForRenderer, nullptr, nullptr, &localPaintingInfo.rootLayer->renderer(), this);
        renderer().paint(paintInfo, paintOffsetForRenderer(fragment, localPaintingInfo));
    }
}

void RenderLayer::paintForegroundForFragments(const LayerFragments& layerFragments, GraphicsContext& context, GraphicsContext& contextForTransparencyLayer,
    const LayoutRect& transparencyPaintDirtyRect, bool haveTransparency, const LayerPaintingInfo& localPaintingInfo, OptionSet<PaintBehavior> paintBehavior,
    RenderObject* subtreePaintRootForRenderer)
{
    // Begin transparency if we have something to paint.
    if (haveTransparency) {
        for (const auto& fragment : layerFragments) {
            if (fragment.shouldPaintContent && !fragment.foregroundRect.isEmpty()) {
                beginTransparencyLayers(contextForTransparencyLayer, localPaintingInfo, transparencyPaintDirtyRect);
                break;
            }
        }
    }

    OptionSet<PaintBehavior> localPaintBehavior;
    if (localPaintingInfo.paintBehavior & PaintBehavior::ForceBlackText)
        localPaintBehavior = PaintBehavior::ForceBlackText;
    else if (localPaintingInfo.paintBehavior & PaintBehavior::ForceWhiteText)
        localPaintBehavior = PaintBehavior::ForceWhiteText;
    else
        localPaintBehavior = paintBehavior;

    // FIXME: It's unclear if this flag copying is necessary.
    static constexpr OptionSet flagsToCopy {
        PaintBehavior::ExcludeSelection,
        PaintBehavior::Snapshotting,
        PaintBehavior::DefaultAsynchronousImageDecode,
        PaintBehavior::CompositedOverflowScrollContent,
        PaintBehavior::ForceSynchronousImageDecode,
        PaintBehavior::ExcludeReplacedContentExceptForIFrames,
        PaintBehavior::ExcludeText,
        PaintBehavior::FixedAndStickyLayersOnly,
        PaintBehavior::DontShowVisitedLinks,
        PaintBehavior::DrawsHDRContent,
    };
    localPaintBehavior.add(localPaintingInfo.paintBehavior & flagsToCopy);

    GraphicsContextStateSaver stateSaver(context, false);
    RegionContextStateSaver regionContextStateSaver(localPaintingInfo.regionContext);

    // Optimize clipping for the single fragment case.
    bool shouldClip = layerFragments.size() == 1 && layerFragments[0].shouldPaintContent && !layerFragments[0].foregroundRect.isEmpty();
    if (shouldClip)
        clipToRect(context, stateSaver, regionContextStateSaver, localPaintingInfo, localPaintBehavior, layerFragments[0].foregroundRect);

    // We have to loop through every fragment multiple times, since we have to repaint in each specific phase in order for
    // interleaving of the fragments to work properly.
    bool selectionOnly = localPaintingInfo.paintBehavior.contains(PaintBehavior::SelectionOnly);
    bool selectionAndBackgroundsOnly = localPaintingInfo.paintBehavior.contains(PaintBehavior::SelectionAndBackgroundsOnly);

    if (is<RenderSVGModelObject>(renderer()) && !is<RenderSVGContainer>(renderer())) {
        // SVG containers need to propagate paint phases. This could be saved if we remember somewhere if a SVG subtree
        // contains e.g. LegacyRenderSVGForeignObject objects that do need the individual paint phases. For SVG shapes & SVG images
        // we can avoid the multiple paintForegroundForFragmentsWithPhase() calls.
        if (selectionOnly || selectionAndBackgroundsOnly)
            return;

        paintForegroundForFragmentsWithPhase(PaintPhase::Foreground, layerFragments, context, localPaintingInfo, localPaintBehavior, subtreePaintRootForRenderer);
        return;
    }

    if (!selectionOnly)
        paintForegroundForFragmentsWithPhase(PaintPhase::ChildBlockBackgrounds, layerFragments, context, localPaintingInfo, localPaintBehavior, subtreePaintRootForRenderer);

    if (selectionOnly || selectionAndBackgroundsOnly)
        paintForegroundForFragmentsWithPhase(PaintPhase::Selection, layerFragments, context, localPaintingInfo, localPaintBehavior, subtreePaintRootForRenderer);
    else {
        paintForegroundForFragmentsWithPhase(PaintPhase::Float, layerFragments, context, localPaintingInfo, localPaintBehavior, subtreePaintRootForRenderer);
        paintForegroundForFragmentsWithPhase(PaintPhase::Foreground, layerFragments, context, localPaintingInfo, localPaintBehavior, subtreePaintRootForRenderer);
        paintForegroundForFragmentsWithPhase(PaintPhase::ChildOutlines, layerFragments, context, localPaintingInfo, localPaintBehavior, subtreePaintRootForRenderer);
    }
}

void RenderLayer::paintForegroundForFragmentsWithPhase(PaintPhase phase, const LayerFragments& layerFragments, GraphicsContext& context,
    const LayerPaintingInfo& localPaintingInfo, OptionSet<PaintBehavior> paintBehavior, RenderObject* subtreePaintRootForRenderer)
{
    bool shouldClip = layerFragments.size() > 1;

    for (const auto& fragment : layerFragments) {
        if (!fragment.shouldPaintContent || fragment.foregroundRect.isEmpty())
            continue;

        GraphicsContextStateSaver stateSaver(context, false);
        RegionContextStateSaver regionContextStateSaver(localPaintingInfo.regionContext);

        if (shouldClip)
            clipToRect(context, stateSaver, regionContextStateSaver, localPaintingInfo, paintBehavior, fragment.foregroundRect);

        PaintInfo paintInfo(context, fragment.foregroundRect.rect(), phase, paintBehavior, subtreePaintRootForRenderer, nullptr, nullptr, &localPaintingInfo.rootLayer->renderer(), this, localPaintingInfo.requireSecurityOriginAccessForWidgets);
        if (phase == PaintPhase::Foreground)
            paintInfo.overlapTestRequests = localPaintingInfo.overlapTestRequests;
        renderer().paint(paintInfo, paintOffsetForRenderer(fragment, localPaintingInfo));
    }
}

void RenderLayer::paintOutlineForFragments(const LayerFragments& layerFragments, GraphicsContext& context, const LayerPaintingInfo& localPaintingInfo,
    OptionSet<PaintBehavior> paintBehavior, RenderObject* subtreePaintRootForRenderer)
{
    for (const auto& fragment : layerFragments) {
        if (fragment.backgroundRect.isEmpty())
            continue;

        // Paint our own outline
        PaintInfo paintInfo(context, fragment.backgroundRect.rect(), PaintPhase::SelfOutline, paintBehavior, subtreePaintRootForRenderer, nullptr, nullptr, &localPaintingInfo.rootLayer->renderer(), this);

        GraphicsContextStateSaver stateSaver(context, false);
        RegionContextStateSaver regionContextStateSaver(localPaintingInfo.regionContext);

        clipToRect(context, stateSaver, regionContextStateSaver, localPaintingInfo, paintBehavior, fragment.backgroundRect, DoNotIncludeSelfForBorderRadius);
        renderer().paint(paintInfo, paintOffsetForRenderer(fragment, localPaintingInfo));
    }
}

void RenderLayer::paintMaskForFragments(const LayerFragments& layerFragments, GraphicsContext& context, const LayerPaintingInfo& localPaintingInfo,
    OptionSet<PaintBehavior> paintBehavior, RenderObject* subtreePaintRootForRenderer)
{
    for (const auto& fragment : layerFragments) {
        if (!fragment.shouldPaintContent)
            continue;

        GraphicsContextStateSaver stateSaver(context, false);
        RegionContextStateSaver regionContextStateSaver(localPaintingInfo.regionContext);

        clipToRect(context, stateSaver, regionContextStateSaver, localPaintingInfo, paintBehavior, fragment.backgroundRect, DoNotIncludeSelfForBorderRadius); // Mask painting will handle clipping to self.

        // Paint the mask.
        // FIXME: Eventually we will collect the region from the fragment itself instead of just from the paint info.
        PaintInfo paintInfo(context, fragment.backgroundRect.rect(), PaintPhase::Mask, paintBehavior, subtreePaintRootForRenderer, nullptr, nullptr, &localPaintingInfo.rootLayer->renderer(), this);
        renderer().paint(paintInfo, paintOffsetForRenderer(fragment, localPaintingInfo));
    }
}

void RenderLayer::paintChildClippingMaskForFragments(const LayerFragments& layerFragments, GraphicsContext& context, const LayerPaintingInfo& localPaintingInfo, OptionSet<PaintBehavior> paintBehavior, RenderObject* subtreePaintRootForRenderer)
{
    for (const auto& fragment : layerFragments) {
        if (!fragment.shouldPaintContent)
            continue;

        GraphicsContextStateSaver stateSaver(context, false);
        RegionContextStateSaver regionContextStateSaver(localPaintingInfo.regionContext);

        clipToRect(context, stateSaver, regionContextStateSaver, localPaintingInfo, paintBehavior, fragment.foregroundRect, IncludeSelfForBorderRadius); // Child clipping mask painting will handle clipping to self.

        // Paint the clipped mask.
        PaintInfo paintInfo(context, fragment.backgroundRect.rect(), PaintPhase::ClippingMask, paintBehavior, subtreePaintRootForRenderer, nullptr, nullptr, &localPaintingInfo.rootLayer->renderer(), this);
        renderer().paint(paintInfo, paintOffsetForRenderer(fragment, localPaintingInfo));
    }
}

void RenderLayer::paintOverflowControlsForFragments(const LayerFragments& layerFragments, GraphicsContext& context, const LayerPaintingInfo& localPaintingInfo)
{
    ASSERT(m_scrollableArea);

    for (const auto& fragment : layerFragments) {
        if (fragment.backgroundRect.isEmpty())
            continue;

        GraphicsContextStateSaver stateSaver(context, false);
        RegionContextStateSaver regionContextStateSaver(localPaintingInfo.regionContext);

        clipToRect(context, stateSaver, regionContextStateSaver, localPaintingInfo, { }, fragment.backgroundRect);
        m_scrollableArea->paintOverflowControls(context, localPaintingInfo.paintBehavior, roundedIntPoint(paintOffsetForRenderer(fragment, localPaintingInfo)), snappedIntRect(fragment.backgroundRect.rect()), true);
    }
}

void RenderLayer::collectEventRegionForFragments(const LayerFragments& layerFragments, GraphicsContext& context, const LayerPaintingInfo& localPaintingInfo, OptionSet<PaintBehavior> paintBehavior)
{
    ASSERT(is<EventRegionContext>(localPaintingInfo.regionContext));
    for (const auto& fragment : layerFragments) {
        PaintInfo paintInfo(context, fragment.foregroundRect.rect(), PaintPhase::EventRegion, paintBehavior);
        paintInfo.regionContext = localPaintingInfo.regionContext;
        paintInfo.regionContext->pushClip(enclosingIntRect(fragment.backgroundRect.rect()));

        renderer().paint(paintInfo, paintOffsetForRenderer(fragment, localPaintingInfo));
        paintInfo.regionContext->popClip();
    }
}

void RenderLayer::collectAccessibilityRegionsForFragments(const LayerFragments& layerFragments, GraphicsContext& context, const LayerPaintingInfo& localPaintingInfo, OptionSet<PaintBehavior> paintBehavior)
{
    ASSERT(is<AccessibilityRegionContext>(localPaintingInfo.regionContext));
    for (const auto& fragment : layerFragments) {
        PaintInfo paintInfo(context, fragment.foregroundRect.rect(), PaintPhase::Accessibility, paintBehavior);
        paintInfo.regionContext = localPaintingInfo.regionContext;
        renderer().paint(paintInfo, paintOffsetForRenderer(fragment, localPaintingInfo));
    }
}

bool RenderLayer::hitTest(const HitTestRequest& request, HitTestResult& result)
{
    return hitTest(request, result.hitTestLocation(), result);
}

bool RenderLayer::hitTest(const HitTestRequest& request, const HitTestLocation& hitTestLocation, HitTestResult& result)
{
    ASSERT(isSelfPaintingLayer() || hasSelfPaintingLayerDescendant());
    ASSERT(!renderer().view().needsLayout());
    
    ASSERT(!isRenderFragmentedFlow());
    LayoutRect hitTestArea = renderer().view().documentRect();
    if (!request.ignoreClipping()) {
        const auto& settings = renderer().settings();
        if (settings.visualViewportEnabled() && settings.clientCoordinatesRelativeToLayoutViewport()) {
            auto& frameView = renderer().view().frameView();
            LayoutRect absoluteLayoutViewportRect = frameView.layoutViewportRect();
            auto scaleFactor = frameView.frame().frameScaleFactor();
            if (scaleFactor > 1)
                absoluteLayoutViewportRect.scale(scaleFactor);
            hitTestArea.intersect(absoluteLayoutViewportRect);
        } else
            hitTestArea.intersect(renderer().view().frameView().visibleContentRect(ScrollableArea::LegacyIOSDocumentVisibleRect));
    }

    auto insideLayer = hitTestLayer(this, nullptr, request, result, hitTestArea, hitTestLocation, false);
    if (!insideLayer.layer) {
        // We didn't hit any layer. If we are the root layer and the mouse is -- or just was -- down,
        // return ourselves. We do this so mouse events continue getting delivered after a drag has 
        // exited the WebView, and so hit testing over a scrollbar hits the content document.
        // In addtion, it is possible for the mouse to stay in the document but there is no element.
        // At that time, the events of the mouse should be fired.
        LayoutPoint hitPoint = hitTestLocation.point();
        bool moveRequestIsOverDocument = request.move() && hitTestArea.contains(hitPoint);
        if (!request.isChildFrameHitTest() && (request.active() || request.release() || moveRequestIsOverDocument) && isRenderViewLayer()) {
            renderer().updateHitTestResult(result, downcast<RenderView>(renderer()).flipForWritingMode(hitTestLocation.point()));
            insideLayer = { this };
        }
    }

    // Now determine if the result is inside an anchor - if the urlElement isn't already set.
    Node* node = result.innerNode();
    if (node && !result.URLElement())
        result.setURLElement(node->enclosingLinkEventParentOrSelf());

    // Now return whether we were inside this layer (this will always be true for the root
    // layer).
    return insideLayer.layer;
}

Element* RenderLayer::enclosingElement() const
{
    for (RenderElement* r = &renderer(); r; r = r->parent()) {
        if (Element* e = r->element())
            return e;
    }
    return nullptr;
}

Vector<RenderLayer*> RenderLayer::topLayerRenderLayers(const RenderView& renderView)
{
    Vector<RenderLayer*> layers;
    for (auto& element : renderView.document().topLayerElements()) {
        auto* renderer = element->renderer();
        if (!renderer)
            continue;

        auto backdropRenderer = renderer->backdropRenderer();
        if (backdropRenderer && backdropRenderer->hasLayer() && backdropRenderer->layer()->parent())
            layers.append(backdropRenderer->layer());

        if (renderer->hasLayer()) {
            auto& modelObject = downcast<RenderLayerModelObject>(*renderer);
            if (modelObject.layer()->parent())
                layers.append(modelObject.layer());
        }
    }
    return layers;
}

bool RenderLayer::establishesTopLayer() const
{
    return isInTopLayerOrBackdrop(renderer().style(), renderer().element());
}

void RenderLayer::establishesTopLayerWillChange()
{
    compositor().establishesTopLayerWillChangeForLayer(*this);

    if (auto* parentLayer = parent())
        parentLayer->removeChild(*this);
}

void RenderLayer::establishesTopLayerDidChange()
{
    if (auto* parentLayer = renderer().layerParent()) {
        setIsNormalFlowOnly(shouldBeNormalFlowOnly());
        auto* beforeChild = renderer().layerNextSibling(*parentLayer);
        parentLayer->addChild(*this, beforeChild);
    }
}

RenderLayer* RenderLayer::enclosingFragmentedFlowAncestor() const
{
    RenderLayer* curr = parent();
    for (; curr && !curr->isRenderFragmentedFlow(); curr = curr->parent()) {
        if (curr->isStackingContext() && curr->isComposited()) {
            // We only adjust the position of the first level of layers.
            return nullptr;
        }
    }
    return curr;
}

// Compute the z-offset of the point in the transformState.
// This is effectively projecting a ray normal to the plane of ancestor, finding where that
// ray intersects target, and computing the z delta between those two points.
static double computeZOffset(const HitTestingTransformState& transformState)
{
    // We got an affine transform, so no z-offset
    if (transformState.m_accumulatedTransform.isAffine())
        return 0;

    // Flatten the point into the target plane
    FloatPoint targetPoint = transformState.mappedPoint();
    
    // Now map the point back through the transform, which computes Z.
    FloatPoint3D backmappedPoint = transformState.m_accumulatedTransform.mapPoint(FloatPoint3D(targetPoint));
    return backmappedPoint.z();
}

Ref<HitTestingTransformState> RenderLayer::createLocalTransformState(RenderLayer* rootLayer, RenderLayer* containerLayer,
                                        const LayoutRect& hitTestRect, const HitTestLocation& hitTestLocation,
                                        const HitTestingTransformState* containerTransformState,
                                        const LayoutSize& translationOffset) const
{
    RefPtr<HitTestingTransformState> transformState;
    LayoutSize offset;
    if (containerTransformState) {
        // If we're already computing transform state, then it's relative to the container (which we know is non-null).
        transformState = HitTestingTransformState::create(*containerTransformState);
        offset = offsetFromAncestor(containerLayer);
    } else {
        // If this is the first time we need to make transform state, then base it off of hitTestLocation,
        // which is relative to rootLayer.
        transformState = HitTestingTransformState::create(hitTestLocation.transformedPoint(), hitTestLocation.transformedRect(), FloatQuad(hitTestRect));
        offset = offsetFromAncestor(rootLayer);
    }
    offset += translationOffset;

    if (renderer().shouldUseTransformFromContainer(containerLayer ? &containerLayer->renderer() : nullptr)) {
        TransformationMatrix containerTransform;
        renderer().getTransformFromContainer(offset, containerTransform);
        transformState->applyTransform(containerTransform);
    } else {
        transformState->translate(offset.width(), offset.height());
    }
    
    return transformState.releaseNonNull();
}

static RefPtr<Element> flattenedParent(Element* element)
{
    if (!element)
        return nullptr;
    RefPtr parent = element->parentElementInComposedTree();
    while (parent) {
        if (!parent->isConnected() || parent->computedStyle()->display() != DisplayType::Contents)
            break;
        parent = parent->parentElementInComposedTree();
    }
    return parent;
}

bool RenderLayer::ancestorLayerIsDOMParent(const RenderLayer* ancestor) const
{
    if (!ancestor)
        return false;
    auto parent = flattenedParent(renderer().element());
    if (parent && ancestor->renderer().element() == parent)
        return true;

    std::optional<PseudoId> parentPseudoId = parentPseudoElement(renderer().style().pseudoElementType());
    return parentPseudoId && *parentPseudoId == ancestor->renderer().style().pseudoElementType();
}

bool RenderLayer::participatesInPreserve3D() const
{
    return ancestorLayerIsDOMParent(parent()) && parent()->preserves3D() && (transform() || renderer().style().backfaceVisibility() == BackfaceVisibility::Hidden || preserves3D());
}

void RenderLayer::setSnapshottedScrollOffsetForAnchorPositioning(LayoutSize offset)
{
    if (m_snapshottedScrollOffsetForAnchorPositioning == offset)
        return;

    // FIXME: Scroll offset should be adjusted in the scrolling tree so layers stay exactly in sync.
    m_snapshottedScrollOffsetForAnchorPositioning = offset;
    updateTransform();

    if (isComposited())
        setNeedsCompositingGeometryUpdate();
}

void RenderLayer::clearSnapshottedScrollOffsetForAnchorPositioning()
{
    if (!m_snapshottedScrollOffsetForAnchorPositioning)
        return;

    m_snapshottedScrollOffsetForAnchorPositioning = { };
    updateTransform();

    if (isComposited())
        setNeedsCompositingGeometryUpdate();
}

// hitTestLocation and hitTestRect are relative to rootLayer.
// A 'flattening' layer is one preserves3D() == false.
// transformState.m_accumulatedTransform holds the transform from the containing flattening layer.
// transformState.m_lastPlanarPoint is the hitTestLocation in the plane of the containing flattening layer.
// transformState.m_lastPlanarQuad is the hitTestRect as a quad in the plane of the containing flattening layer.
// 
// If zOffset is non-null (which indicates that the caller wants z offset information), 
//  *zOffset on return is the z offset of the hit point relative to the containing flattening layer.
RenderLayer::HitLayer RenderLayer::hitTestLayer(RenderLayer* rootLayer, RenderLayer* containerLayer, const HitTestRequest& request, HitTestResult& result,
    const LayoutRect& hitTestRect, const HitTestLocation& hitTestLocation, bool appliedTransform,
    const HitTestingTransformState* transformState, double* zOffset)
{
    updateLayerListsIfNeeded();

    if (!isSelfPaintingLayer() && !hasSelfPaintingLayerDescendant())
        return { };

    // Renderers that are captured in a view transition are not hit tested.
    if (renderer().effectiveCapturedInViewTransition())
        return { };

    // If we're hit testing 'SVG clip content' (aka. RenderSVGResourceClipper) do not early exit.
    if (!request.svgClipContent()) {
        // SVG resource layers and their children are never hit tested.
        if (is<RenderSVGResourceContainer>(m_enclosingSVGHiddenOrResourceContainer))
            return { };

        // Hidden SVG containers (<defs> / <symbol> ...) are never hit tested directly.
        if (is<RenderSVGHiddenContainer>(renderer()))
            return { };
    }

    bool skipLayerForFixedContainerSampling = [&] {
        if (!request.isForFixedContainerSampling())
            return false;

        if (!m_hasViewportConstrainedDescendant && !isViewportConstrained() && !hasFixedAncestor() && !m_hasStickyAncestor)
            return true;

        if (hasCompositedScrollableOverflow() && !renderer().hasBackground())
            return true;

        return false;
    }();

    if (skipLayerForFixedContainerSampling)
        return { };

    // The natural thing would be to keep HitTestingTransformState on the stack, but it's big, so we heap-allocate.

    // Apply a transform if we have one.
    if (transform() && !appliedTransform) {
        if (enclosingPaginationLayer(IncludeCompositedPaginatedLayers))
            return hitTestTransformedLayerInFragments(rootLayer, containerLayer, request, result, hitTestRect, hitTestLocation, transformState, zOffset);

        // Make sure the parent's clip rects have been calculated.
        if (parent()) {
            ClipRectsContext clipRectsContext(rootLayer, RootRelativeClipRects, { ClipRectsOption::RespectOverflowClip });
            ClipRect clipRect = backgroundClipRect(clipRectsContext);
            // Test the enclosing clip now.
            if (!clipRect.intersects(hitTestLocation))
                return { };
        }

        return hitTestLayerByApplyingTransform(rootLayer, containerLayer, request, result, hitTestRect, hitTestLocation, transformState, zOffset);
    }

    // Ensure our lists and 3d status are up-to-date.
    update3DTransformedDescendantStatus();

    RefPtr<HitTestingTransformState> localTransformState;
    if (appliedTransform) {
        // We computed the correct state in the caller (above code), so just reference it.
        ASSERT(transformState);
        localTransformState = const_cast<HitTestingTransformState*>(transformState);
    } else if (transformState || has3DTransformedDescendant() || preserves3D()) {
        // We need transform state for the first time, or to offset the container state, so create it here.
        localTransformState = createLocalTransformState(rootLayer, containerLayer, hitTestRect, hitTestLocation, transformState);
    }

    // Check for hit test on backface if backface-visibility is 'hidden'
    if (localTransformState && renderer().style().backfaceVisibility() == BackfaceVisibility::Hidden) {
        std::optional<TransformationMatrix> invertedMatrix = localTransformState->m_accumulatedTransform.inverse();
        // If the z-vector of the matrix is negative, the back is facing towards the viewer.
        if (invertedMatrix && invertedMatrix.value().m33() < 0)
            return { };
    }

    // The following are used for keeping track of the z-depth of the hit point of 3d-transformed
    // descendants.
    double localZOffset = -std::numeric_limits<double>::infinity();
    double* zOffsetForDescendantsPtr = nullptr;

    bool depthSortDescendants = false;
    if (preserves3D()) {
        depthSortDescendants = true;
        // Our layers can depth-test with our container, so share the z depth pointer with the container, if it passed one down.
        zOffsetForDescendantsPtr = zOffset ? zOffset : &localZOffset;
    } else if (zOffset)
        zOffsetForDescendantsPtr = nullptr;

    double selfZOffset = localTransformState ? computeZOffset(*localTransformState) : 0;

    // This variable tracks which layer the mouse ends up being inside.
    auto candidateLayer = HitLayer { nullptr, -std::numeric_limits<double>::infinity() };
#if ASSERT_ENABLED
    LayerListMutationDetector mutationChecker(*this);
#endif

    auto offsetFromRoot = offsetFromAncestor(rootLayer);
    // FIXME: We need to correctly hit test the clip-path when we have a RenderInline too.
    if (auto* rendererBox = this->renderBox(); rendererBox && !rendererBox->hitTestClipPath(hitTestLocation, toLayoutPoint(offsetFromRoot - toLayoutSize(rendererLocation()))))
        return { };

    // Begin by walking our list of positive layers from highest z-index down to the lowest z-index.
    auto hitLayer = hitTestList(positiveZOrderLayers(), rootLayer, request, result, hitTestRect, hitTestLocation, localTransformState.get(), zOffsetForDescendantsPtr, depthSortDescendants);
    if (hitLayer.layer) {
        if (!depthSortDescendants)
            return hitLayer;
        if (hitLayer.zOffset > candidateLayer.zOffset)
            candidateLayer = hitLayer;
    }

    // Now check our overflow objects.
    {
        HitTestResult tempResult(result.hitTestLocation());
        hitLayer = hitTestList(normalFlowLayers(), rootLayer, request, tempResult, hitTestRect, hitTestLocation, localTransformState.get(), zOffsetForDescendantsPtr, depthSortDescendants);

        if (request.resultIsElementList())
            result.append(tempResult, request);

        if (hitLayer.layer) {
            if (!depthSortDescendants || hitLayer.zOffset > candidateLayer.zOffset) {
                if (!request.resultIsElementList())
                    result = tempResult;

                candidateLayer = hitLayer;
            }

            if (!depthSortDescendants)
                return hitLayer;
        }
    }

    // Collect the fragments. This will compute the clip rectangles for each layer fragment.
    LayerFragments layerFragments;
    collectFragments(layerFragments, rootLayer, hitTestRect, IncludeCompositedPaginatedLayers, RootRelativeClipRects, { ClipRectsOption::RespectOverflowClip }, offsetFromRoot);

    LayoutPoint localPoint;
    if (canResize() && m_scrollableArea && m_scrollableArea->hitTestResizerInFragments(layerFragments, hitTestLocation, localPoint)) {
        renderer().updateHitTestResult(result, localPoint);
        return { this, selfZOffset };
    }

    auto isHitCandidate = [&]() {
        return !depthSortDescendants || selfZOffset > candidateLayer.zOffset;
    };

    // Next we want to see if the mouse pos is inside the child RenderObjects of the layer. Check
    // every fragment in reverse order.
    if (isSelfPaintingLayer()) {
        // Hit test with a temporary HitTestResult, because we only want to commit to 'result' if we know we're frontmost.
        HitTestResult tempResult(result.hitTestLocation());
        bool insideFragmentForegroundRect = false;
        if (hitTestContentsForFragments(layerFragments, request, tempResult, hitTestLocation, HitTestDescendants, insideFragmentForegroundRect) && isHitCandidate()) {
            if (request.resultIsElementList())
                result.append(tempResult, request);
            else
                result = tempResult;

            if (!depthSortDescendants)
                return { this, selfZOffset };

            // Foreground can depth-sort with descendant layers, so keep this as a candidate.
            candidateLayer = { this, selfZOffset };
        } else if (insideFragmentForegroundRect && request.resultIsElementList())
            result.append(tempResult, request);
    }

    // Now check our negative z-index children.
    {
        HitTestResult tempResult(result.hitTestLocation());
        hitLayer = hitTestList(negativeZOrderLayers(), rootLayer, request, tempResult, hitTestRect, hitTestLocation, localTransformState.get(), zOffsetForDescendantsPtr, depthSortDescendants);

        if (request.resultIsElementList())
            result.append(tempResult, request);

        if (hitLayer.layer) {
            if (!depthSortDescendants || hitLayer.zOffset > candidateLayer.zOffset) {
                if (!request.resultIsElementList())
                    result = tempResult;

                candidateLayer = hitLayer;
            }

            if (!depthSortDescendants)
                return hitLayer;
        }
    }

    // If we found a layer, return. Child layers, and foreground always render in front of background.
    if (candidateLayer.layer && !depthSortDescendants)
        return candidateLayer;

    if (isSelfPaintingLayer()) {
        HitTestResult tempResult(result.hitTestLocation());
        bool insideFragmentBackgroundRect = false;
        if (hitTestContentsForFragments(layerFragments, request, tempResult, hitTestLocation, HitTestSelf, insideFragmentBackgroundRect) && isHitCandidate()) {
            if (request.resultIsElementList())
                result.append(tempResult, request);
            else
                result = tempResult;

            if (!depthSortDescendants)
                return { this, selfZOffset };

            candidateLayer = { this, selfZOffset };
        }

        if (insideFragmentBackgroundRect && request.resultIsElementList())
            result.append(tempResult, request);
    }

    return candidateLayer;
}

bool RenderLayer::hitTestContentsForFragments(const LayerFragments& layerFragments, const HitTestRequest& request, HitTestResult& result,
    const HitTestLocation& hitTestLocation, HitTestFilter hitTestFilter, bool& insideClipRect) const
{
    if (layerFragments.isEmpty())
        return false;

    for (int i = layerFragments.size() - 1; i >= 0; --i) {
        const auto& fragment = layerFragments.at(i);
        if ((hitTestFilter == HitTestSelf && !fragment.backgroundRect.intersects(hitTestLocation))
            || (hitTestFilter == HitTestDescendants && !fragment.foregroundRect.intersects(hitTestLocation)))
            continue;
        insideClipRect = true;
        if (hitTestContents(request, result, fragment.layerBounds, hitTestLocation, hitTestFilter))
            return true;
    }
    
    return false;
}

RenderLayer::HitLayer RenderLayer::hitTestTransformedLayerInFragments(RenderLayer* rootLayer, RenderLayer* containerLayer, const HitTestRequest& request, HitTestResult& result,
    const LayoutRect& hitTestRect, const HitTestLocation& hitTestLocation, const HitTestingTransformState* transformState, double* zOffset)
{
    LayerFragments enclosingPaginationFragments;
    LayoutSize offsetOfPaginationLayerFromRoot;
    RenderLayer* paginatedLayer = enclosingPaginationLayer(IncludeCompositedPaginatedLayers);
    LayoutRect transformedExtent = transparencyClipBox(*this, paginatedLayer, HitTestingTransparencyClipBox, RootOfTransparencyClipBox);
    paginatedLayer->collectFragments(enclosingPaginationFragments, rootLayer, hitTestRect, IncludeCompositedPaginatedLayers,
        RootRelativeClipRects, { ClipRectsOption::RespectOverflowClip }, offsetOfPaginationLayerFromRoot, &transformedExtent);

    for (int i = enclosingPaginationFragments.size() - 1; i >= 0; --i) {
        const LayerFragment& fragment = enclosingPaginationFragments.at(i);
        
        // Apply the page/column clip for this fragment, as well as any clips established by layers in between us and
        // the enclosing pagination layer.
        LayoutRect clipRect = fragment.backgroundRect.rect();

        // Now compute the clips within a given fragment
        if (parent() != paginatedLayer) {
            offsetOfPaginationLayerFromRoot = toLayoutSize(paginatedLayer->convertToLayerCoords(rootLayer, toLayoutPoint(offsetOfPaginationLayerFromRoot)));
    
            ClipRectsContext clipRectsContext(paginatedLayer, RootRelativeClipRects, { ClipRectsOption::RespectOverflowClip });
            LayoutRect parentClipRect = backgroundClipRect(clipRectsContext).rect();
            parentClipRect.move(fragment.paginationOffset + offsetOfPaginationLayerFromRoot);
            clipRect.intersect(parentClipRect);
        }
        
        if (!hitTestLocation.intersects(clipRect))
            continue;

        auto hitLayer = hitTestLayerByApplyingTransform(rootLayer, containerLayer, request, result, hitTestRect, hitTestLocation,
            transformState, zOffset, fragment.paginationOffset);
        if (hitLayer.layer)
            return hitLayer;
    }
    
    return { };
}

RenderLayer::HitLayer RenderLayer::hitTestLayerByApplyingTransform(RenderLayer* rootLayer, RenderLayer* containerLayer, const HitTestRequest& request, HitTestResult& result,
    const LayoutRect& hitTestRect, const HitTestLocation& hitTestLocation, const HitTestingTransformState* transformState, double* zOffset, const LayoutSize& translationOffset)
{
    // Create a transform state to accumulate this transform.
    Ref<HitTestingTransformState> newTransformState = createLocalTransformState(rootLayer, containerLayer, hitTestRect, hitTestLocation, transformState, translationOffset);

    // If the transform can't be inverted, then don't hit test this layer at all.
    if (!newTransformState->m_accumulatedTransform.isInvertible())
        return { };

    // Compute the point and the hit test rect in the coords of this layer by using the values
    // from the transformState, which store the point and quad in the coords of the last flattened
    // layer, and the accumulated transform which lets up map through preserve-3d layers.
    //
    // We can't just map hitTestLocation and hitTestRect because they may have been flattened (losing z)
    // by our container.
    auto localPoint = newTransformState->mappedPoint();
    auto localHitTestRect = newTransformState->boundsOfMappedArea();
    HitTestLocation newHitTestLocation;
    if (hitTestLocation.isRectBasedTest()) {
        auto localPointQuad = newTransformState->mappedQuad();
        newHitTestLocation = HitTestLocation(localPoint, localPointQuad);
    } else {
        auto localPointQuad = newTransformState->boundsOfMappedQuad();
        newHitTestLocation = HitTestLocation(localPoint, FloatRect { localPointQuad });
    }

    // Now do a hit test with the root layer shifted to be us.
    return hitTestLayer(this, containerLayer, request, result, localHitTestRect, newHitTestLocation, true, newTransformState.ptr(), zOffset);
}

bool RenderLayer::hitTestContents(const HitTestRequest& request, HitTestResult& result, const LayoutRect& layerBounds, const HitTestLocation& hitTestLocation, HitTestFilter hitTestFilter) const
{
    ASSERT(isSelfPaintingLayer() || hasSelfPaintingLayerDescendant());

    if (!renderer().hitTest(request, result, hitTestLocation, toLayoutPoint(layerBounds.location() - rendererLocation()), hitTestFilter)) {
        // It's wrong to set innerNode, but then claim that you didn't hit anything, unless it is
        // a rect-based test.
        ASSERT(!result.innerNode() || (request.resultIsElementList() && result.listBasedTestResult().size()));
        return false;
    }

    // For positioned generated content, we might still not have a
    // node by the time we get to the layer level, since none of
    // the content in the layer has an element. So just walk up
    // the tree.
    if (!result.innerNode() || !result.innerNonSharedNode()) {
        Element* e = enclosingElement();
        if (!result.innerNode())
            result.setInnerNode(e);
        if (!result.innerNonSharedNode())
            result.setInnerNonSharedNode(e);
    }
        
    return true;
}

RenderLayer::HitLayer RenderLayer::hitTestList(LayerList layerIterator, RenderLayer* rootLayer, const HitTestRequest& request, HitTestResult& result, const LayoutRect& hitTestRect, const HitTestLocation& hitTestLocation, const HitTestingTransformState* transformState, double* zOffsetForDescendants, bool depthSortDescendants)
{
    if (layerIterator.begin() == layerIterator.end())
        return { };

    if (!hasSelfPaintingLayerDescendant())
        return { };

    if (CheckedPtr renderBox = this->renderBox(); renderBox && isSkippedContentRoot(*renderBox))
        return { };

    auto resultLayer = HitLayer { nullptr, -std::numeric_limits<double>::infinity() };

    RefPtr<HitTestingTransformState> flattenedTransformState;
    double unflattenedZOffset = 0;
    for (auto iter = layerIterator.rbegin(); iter != layerIterator.rend(); ++iter) {
        auto* childLayer = *iter;

        // If we're about to cross a flattening boundary, then pass the (lazily-initialized)
        // flattened transfomState to the child layer.
        auto* transformStateForChild = transformState;
        if (transformState && !childLayer->participatesInPreserve3D()) {
            if (!flattenedTransformState) {
                flattenedTransformState = HitTestingTransformState::create(*transformState);
                flattenedTransformState->flatten();
                unflattenedZOffset = computeZOffset(*transformState);
            }
            transformStateForChild = flattenedTransformState.get();
        }

        HitTestResult tempResult(result.hitTestLocation());
        auto hitLayer = childLayer->hitTestLayer(rootLayer, this, request, tempResult, hitTestRect, hitTestLocation, false, transformStateForChild, zOffsetForDescendants);

        // If it is a list-based test, we can safely append the temporary result since it might had hit
        // nodes but not necessarily had hitLayer set.
        ASSERT(!result.isRectBasedTest() || request.resultIsElementList());
        if (request.resultIsElementList())
            result.append(tempResult, request);

        if (hitLayer.layer) {
            // If the child was flattened, then override the returned depth with the depth of the
            // plane we flattened into (ourselves) instead.
            if (transformStateForChild == flattenedTransformState)
                hitLayer.zOffset = unflattenedZOffset;

            if (!depthSortDescendants || hitLayer.zOffset > resultLayer.zOffset) {
                resultLayer = hitLayer;
                if (!request.resultIsElementList())
                    result = tempResult;
                if (!depthSortDescendants)
                    break;
            }
        }
    }

    return resultLayer;
}

void RenderLayer::verifyClipRects()
{
#ifdef CHECK_CACHED_CLIP_RECTS
    if (!m_clipRectsCache)
        return;

    for (int i = 0; i < NumCachedClipRectsTypes; ++i) {
        if (!m_clipRectsCache->m_clipRectsRoot[i])
            continue;

        ClipRectsContext clipRectsContext(m_clipRectsCache->m_clipRectsRoot[i], (ClipRectsType)i, { });
        verifyClipRect(clipRectsContext);

        clipRectsContext.options.add(ClipRectsOption::RespectOverflowClip);
        verifyClipRect(clipRectsContext);
    }
#endif
}

void RenderLayer::verifyClipRect(const ClipRectsContext& clipRectsContext)
{
#ifdef CHECK_CACHED_CLIP_RECTS
    ASSERT(m_clipRectsCache);
    if (auto* clipRects = m_clipRectsCache->getClipRects(clipRectsContext)) {

        // This code is useful to check cached clip rects, but is too expensive to leave enabled in debug builds by default.
        ClipRectsContext tempContext(clipRectsContext);
        tempContext.options.add(ClipRectsOption::Temporary);
        Ref<ClipRects> tempClipRects = ClipRects::create();
        calculateClipRects(tempContext, tempClipRects);
        ASSERT(tempClipRects.get() == *clipRects);
    }
#else
    UNUSED_PARAM(clipRectsContext);
#endif
}


Ref<ClipRects> RenderLayer::updateClipRects(const ClipRectsContext& clipRectsContext)
{
    ClipRectsType clipRectsType = clipRectsContext.clipRectsType;
    ASSERT(clipRectsType < NumCachedClipRectsTypes);
    ASSERT(!clipRectsContext.options.contains(ClipRectsOption::Temporary));
    ASSERT(!clipRectsContext.options.contains(ClipRectsOption::OutsideFilter));
    if (m_clipRectsCache) {
        if (auto* clipRects = m_clipRectsCache->getClipRects(clipRectsContext)) {
            ASSERT(clipRectsContext.rootLayer == m_clipRectsCache->m_clipRectsRoot[clipRectsType]);
            verifyClipRect(clipRectsContext);
            return *clipRects; // We have the correct cached value.
        }
    }
    
    if (!m_clipRectsCache)
        m_clipRectsCache = makeUnique<ClipRectsCache>();

#if ASSERT_ENABLED
    m_clipRectsCache->m_clipRectsRoot[clipRectsType] = clipRectsContext.rootLayer;
#endif
    ASSERT(enumToUnderlyingType(clipRectsContext.overlayScrollbarSizeRelevancy()) == (clipRectsContext.clipRectsType == RootRelativeClipRects));

    RefPtr<ClipRects> parentClipRects = this->parentClipRects(clipRectsContext);

    auto clipRects = ClipRects::create();
    calculateClipRects(clipRectsContext, clipRects);

    if (parentClipRects && *parentClipRects == clipRects) {
        m_clipRectsCache->setClipRects(clipRectsType, clipRectsContext.respectOverflowClip(), parentClipRects.copyRef());
        return parentClipRects.releaseNonNull();
    }

    m_clipRectsCache->setClipRects(clipRectsType, clipRectsContext.respectOverflowClip(), clipRects.copyRef());
    return clipRects;
}

ClipRects* RenderLayer::clipRects(const ClipRectsContext& context) const
{
    ASSERT(context.clipRectsType < NumCachedClipRectsTypes);
    ASSERT(!context.options.contains(ClipRectsOption::Temporary));
    ASSERT(!context.options.contains(ClipRectsOption::OutsideFilter));
    if (!m_clipRectsCache)
        return nullptr;
    return m_clipRectsCache->getClipRects(context);
}

bool RenderLayer::clipCrossesPaintingBoundary() const
{
    return parent()->enclosingPaginationLayer(IncludeCompositedPaginatedLayers) != enclosingPaginationLayer(IncludeCompositedPaginatedLayers)
        || parent()->enclosingCompositingLayerForRepaint().layer != enclosingCompositingLayerForRepaint().layer;
}

void RenderLayer::calculateClipRects(const ClipRectsContext& clipRectsContext, ClipRects& clipRects) const
{
    if (!parent()) {
        // The root layer's clip rect is always infinite.
        clipRects.reset();
        return;
    }

    if (auto parentClipRects = this->parentClipRects(clipRectsContext))
        clipRects = *parentClipRects;
    else
        clipRects.reset();

    // A fixed object is essentially the root of its containing block hierarchy, so when
    // we encounter such an object, we reset our clip rects to the fixedClipRect.
    if (renderer().isFixedPositioned()) {
        clipRects.setPosClipRect(clipRects.fixedClipRect());
        clipRects.setOverflowClipRect(clipRects.fixedClipRect());
        clipRects.setFixed(true);
    } else if (renderer().isInFlowPositioned())
        clipRects.setPosClipRect(clipRects.overflowClipRect());
    else if (renderer().shouldUsePositionedClipping())
        clipRects.setOverflowClipRect(clipRects.posClipRect());

    // Update the clip rects that will be passed to child layers.
#if PLATFORM(IOS_FAMILY)
    if (renderer().hasClipOrNonVisibleOverflow() && (clipRectsContext.respectOverflowClip() || this != clipRectsContext.rootLayer)) {
#else
    if ((renderer().hasNonVisibleOverflow() && (clipRectsContext.respectOverflowClip() || this != clipRectsContext.rootLayer)) || renderer().hasClip()) {
#endif
        // This layer establishes a clip of some kind.

        // FIXME: Transforming a clip doesn't make a whole lot of sense, since it we have to round out to the
        // bounding box of the transformed quad.
        // It would be better for callers to transform rects into the coordinate space of the nearest clipped layer, apply
        // the clip in local space, and then repeat until the required coordinate space is reached.
        bool needsTransform = clipRectsContext.clipRectsType == AbsoluteClipRects ? m_hasTransformedAncestor || !canUseOffsetFromAncestor() : !canUseOffsetFromAncestor(*clipRectsContext.rootLayer);

        LayoutPoint offset;
        if (!needsTransform)
            offset = toLayoutPoint(offsetFromAncestor(clipRectsContext.rootLayer, AdjustForColumns));

        if (clipRects.fixed() && &clipRectsContext.rootLayer->renderer() == &renderer().view())
            offset -= toLayoutSize(renderer().view().frameView().scrollPositionForFixedPosition());

        if (renderer().hasNonVisibleOverflow()) {
            ClipRect newOverflowClip = rendererOverflowClipRectForChildLayers({ }, clipRectsContext.overlayScrollbarSizeRelevancy());
            if (needsTransform)
                newOverflowClip = LayoutRect(renderer().localToContainerQuad(FloatRect(newOverflowClip.rect()), &clipRectsContext.rootLayer->renderer()).boundingBox());
            newOverflowClip.moveBy(offset);
            newOverflowClip.setAffectedByRadius(renderer().style().hasBorderRadius());
            clipRects.setOverflowClipRect(intersection(newOverflowClip, clipRects.overflowClipRect()));
            if (renderer().canContainAbsolutelyPositionedObjects())
                clipRects.setPosClipRect(intersection(newOverflowClip, clipRects.posClipRect()));
            if (renderer().canContainFixedPositionObjects())
                clipRects.setFixedClipRect(intersection(newOverflowClip, clipRects.fixedClipRect()));
        }
        if (renderer().hasClip()) {
            if (CheckedPtr box = dynamicDowncast<RenderBox>(renderer())) {
                LayoutRect newPosClip = box->clipRect({ });
                if (needsTransform)
                    newPosClip = LayoutRect(renderer().localToContainerQuad(FloatRect(newPosClip), &clipRectsContext.rootLayer->renderer()).boundingBox());
                newPosClip.moveBy(offset);
                clipRects.setPosClipRect(intersection(newPosClip, clipRects.posClipRect()));
                clipRects.setOverflowClipRect(intersection(newPosClip, clipRects.overflowClipRect()));
                clipRects.setFixedClipRect(intersection(newPosClip, clipRects.fixedClipRect()));
            }
        }
    } else if (renderer().hasNonVisibleOverflow() && transform() && renderer().style().hasBorderRadius())
        clipRects.setOverflowClipRectAffectedByRadius();

    LOG_WITH_STREAM(ClipRects, stream << "RenderLayer " << this << " calculateClipRects " << clipRectsContext << " computed " << clipRects);
}

RefPtr<ClipRects> RenderLayer::parentClipRects(const ClipRectsContext& clipRectsContext) const
{
    auto containerLayer = parent();
    if (clipRectsContext.rootLayer == this || !parent())
        return nullptr;

    if (clipRectsContext.clipRectsType == PaintingClipRects && m_suppressAncestorClippingInsideFilter && !clipRectsContext.options.contains(ClipRectsOption::OutsideFilter))
        return nullptr;

    auto temporaryParentClipRects = [&](const ClipRectsContext& clipContext) {
        auto parentClipRects = ClipRects::create();
        containerLayer->calculateClipRects(clipContext, parentClipRects);
        return parentClipRects;
    };

    if (clipRectsContext.options.contains(ClipRectsOption::Temporary) || clipRectsContext.options.contains(ClipRectsOption::OutsideFilter))
        return temporaryParentClipRects(clipRectsContext);

    if (clipRectsContext.clipRectsType != AbsoluteClipRects && clipCrossesPaintingBoundary()) {
        ClipRectsContext tempClipRectsContext(clipRectsContext);
        tempClipRectsContext.options.add(ClipRectsOption::Temporary);
        return temporaryParentClipRects(tempClipRectsContext);
    }

    return containerLayer->updateClipRects(clipRectsContext);
}

static inline ClipRect backgroundClipRectForPosition(const ClipRects& parentRects, PositionType position)
{
    if (position == PositionType::Fixed)
        return parentRects.fixedClipRect();

    if (position == PositionType::Absolute)
        return parentRects.posClipRect();

    return parentRects.overflowClipRect();
}

ClipRect RenderLayer::backgroundClipRect(const ClipRectsContext& clipRectsContext) const
{
    ASSERT(parent());
    ClipRect backgroundClipRect;
    auto parentRects = parentClipRects(clipRectsContext);
    if (!parentRects) {
        backgroundClipRect.reset();
        return backgroundClipRect;
    }
    backgroundClipRect = backgroundClipRectForPosition(*parentRects, renderer().style().position());
    RenderView& view = renderer().view();
    // Note: infinite clipRects should not be scrolled here, otherwise they will accidentally no longer be considered infinite.
    if (parentRects->fixed() && &clipRectsContext.rootLayer->renderer() == &view && !backgroundClipRect.isInfinite())
        backgroundClipRect.moveBy(view.frameView().scrollPositionForFixedPosition());

    LOG_WITH_STREAM(ClipRects, stream << "RenderLayer " << this << " backgroundClipRect with context " << clipRectsContext << " returning " << backgroundClipRect);
    return backgroundClipRect;
}

void RenderLayer::calculateRects(const ClipRectsContext& clipRectsContext, const LayoutRect& paintDirtyRect, LayoutRect& layerBounds,
    ClipRect& backgroundRect, ClipRect& foregroundRect, const LayoutSize& offsetFromRoot) const
{
    if (clipRectsContext.rootLayer != this && parent()) {
        backgroundRect = backgroundClipRect(clipRectsContext);
        backgroundRect.intersect(paintDirtyRect);
    } else
        backgroundRect = paintDirtyRect;

    LayoutSize offsetFromRootLocal = offsetFromRoot;

    layerBounds = LayoutRect(toLayoutPoint(offsetFromRootLocal), size());

    foregroundRect = backgroundRect;

    bool shouldApplyClip = clipRectsContext.clipRectsType != PaintingClipRects || !m_suppressAncestorClippingInsideFilter || clipRectsContext.options.contains(ClipRectsOption::OutsideFilter);
    if (renderer().hasClip() && shouldApplyClip) {
        if (CheckedPtr box = dynamicDowncast<RenderBox>(renderer())) {
            // Clip applies to *us* as well, so update the damageRect.
            LayoutRect newPosClip = box->clipRect(toLayoutPoint(offsetFromRootLocal));
            backgroundRect.intersect(newPosClip);
            foregroundRect.intersect(newPosClip);
        }
    }

    if (clipRectsContext.options.contains(ClipRectsOption::OutsideFilter))
        return;

    // Update the clip rects that will be passed to child layers.
    if (renderer().hasClipOrNonVisibleOverflow()) {
        // This layer establishes a clip of some kind.
        if (renderer().hasNonVisibleOverflow()) {
            if (this != clipRectsContext.rootLayer || clipRectsContext.respectOverflowClip()) {
                LayoutRect overflowClipRect = rendererOverflowClipRect(toLayoutPoint(offsetFromRootLocal), clipRectsContext.overlayScrollbarSizeRelevancy());
                foregroundRect.intersect(overflowClipRect);
                foregroundRect.setAffectedByRadius(true);
            } else if (transform() && renderer().style().hasBorderRadius())
                foregroundRect.setAffectedByRadius(true);
        }

        // If we establish a clip at all, then make sure our background rect is intersected with our layer's bounds including our visual overflow,
        // since any visual overflow like box-shadow or border-outset is not clipped by overflow:auto/hidden.
        if (rendererHasVisualOverflow()) {
            // FIXME: Does not do the right thing with CSS regions yet, since we don't yet factor in the
            // individual region boxes as overflow.
            LayoutRect layerBoundsWithVisualOverflow = rendererVisualOverflowRect();
            if (renderer().isRenderBox())
                renderBox()->flipForWritingMode(layerBoundsWithVisualOverflow); // Layers are in physical coordinates, so the overflow has to be flipped.
            layerBoundsWithVisualOverflow.move(offsetFromRootLocal);
            if (this != clipRectsContext.rootLayer || clipRectsContext.respectOverflowClip())
                backgroundRect.intersect(layerBoundsWithVisualOverflow);
        } else {
            // Shift the bounds to be for our region only.
            LayoutRect bounds = rendererBorderBoxRect();

            bounds.move(offsetFromRootLocal);
            if (this != clipRectsContext.rootLayer || clipRectsContext.respectOverflowClip())
                backgroundRect.intersect(bounds);
        }
    }
}

LayoutRect RenderLayer::childrenClipRect() const
{
    // FIXME: border-radius not accounted for.
    // FIXME: Regions not accounted for.
    RenderLayer* clippingRootLayer = clippingRootForPainting();
    LayoutRect layerBounds;
    ClipRect backgroundRect;
    ClipRect foregroundRect;
    ClipRectsContext clipRectsContext(clippingRootLayer, PaintingClipRects, { ClipRectsOption::Temporary });
    // Need to use temporary clip rects, because the value of 'dontClipToOverflow' may be different from the painting path (<rdar://problem/11844909>).
    calculateRects(clipRectsContext, LayoutRect::infiniteRect(), layerBounds, backgroundRect, foregroundRect, offsetFromAncestor(clipRectsContext.rootLayer));
    if (foregroundRect.rect().isInfinite())
        return renderer().view().unscaledDocumentRect();

    auto absoluteClippingRect = clippingRootLayer->renderer().localToAbsoluteQuad(FloatQuad(foregroundRect.rect())).enclosingBoundingBox();
    return intersection(absoluteClippingRect, renderer().view().unscaledDocumentRect());
}

LayoutRect RenderLayer::clipRectRelativeToAncestor(const RenderLayer* ancestor, LayoutSize offsetFromAncestor, const LayoutRect& constrainingRect, bool temporaryClipRects) const
{
    LayoutRect layerBounds;
    ClipRect backgroundRect;
    ClipRect foregroundRect;
    auto options = clipRectDefaultOptions;
    if ((m_enclosingPaginationLayer && m_enclosingPaginationLayer != ancestor) || temporaryClipRects)
        options.add(ClipRectsOption::Temporary);
    ClipRectsContext clipRectsContext(ancestor, PaintingClipRects, options);
    calculateRects(clipRectsContext, constrainingRect, layerBounds, backgroundRect, foregroundRect, offsetFromAncestor);
    return backgroundRect.rect();
}

LayoutRect RenderLayer::selfClipRect() const
{
    // FIXME: border-radius not accounted for.
    // FIXME: Regions not accounted for.
    RenderLayer* clippingRootLayer = clippingRootForPainting();
    LayoutRect clipRect = clipRectRelativeToAncestor(clippingRootLayer, offsetFromAncestor(clippingRootLayer), renderer().view().documentRect());
    return clippingRootLayer->renderer().localToAbsoluteQuad(FloatQuad(clipRect)).enclosingBoundingBox();
}

LayoutRect RenderLayer::localClipRect(bool& clipExceedsBounds, LocalClipRectMode mode) const
{
    clipExceedsBounds = false;
    // FIXME: border-radius not accounted for.
    // FIXME: Regions not accounted for.
    const RenderLayer* clippingRootLayer = mode == LocalClipRectMode::ExcludeCompositingState ? this : clippingRootForPainting();
    LayoutSize offsetFromRoot = offsetFromAncestor(clippingRootLayer);
    LayoutRect clipRect = clipRectRelativeToAncestor(clippingRootLayer, offsetFromRoot, LayoutRect::infiniteRect());
    if (clipRect.isInfinite())
        return clipRect;

    if (renderer().hasClip()) {
        if (CheckedPtr box = dynamicDowncast<RenderBox>(renderer())) {
            // CSS clip may be larger than our border box.
            LayoutRect cssClipRect = box->clipRect({ });
            clipExceedsBounds = !cssClipRect.isEmpty() && (clipRect.width() < cssClipRect.width() || clipRect.height() < cssClipRect.height());
        }
    }

    clipRect.move(-offsetFromRoot);
    return clipRect;
}

void RenderLayer::addBlockSelectionGapsBounds(const LayoutRect& bounds)
{
    m_blockSelectionGapsBounds.unite(enclosingIntRect(bounds));
}

void RenderLayer::clearBlockSelectionGapsBounds()
{
    m_blockSelectionGapsBounds = IntRect();
    for (RenderLayer* child = firstChild(); child; child = child->nextSibling())
        child->clearBlockSelectionGapsBounds();
}

void RenderLayer::repaintBlockSelectionGaps()
{
    for (RenderLayer* child = firstChild(); child; child = child->nextSibling())
        child->repaintBlockSelectionGaps();

    if (m_blockSelectionGapsBounds.isEmpty())
        return;

    LayoutRect rect = m_blockSelectionGapsBounds;
    if (m_scrollableArea)
        rect.moveBy(-m_scrollableArea->scrollPosition());
    if (renderer().hasNonVisibleOverflow() && !usesCompositedScrolling())
        rect.intersect(downcast<RenderBox>(renderer()).overflowClipRect({ }));
    if (renderer().hasClip())
        rect.intersect(downcast<RenderBox>(renderer()).clipRect({ }));
    if (!rect.isEmpty())
        renderer().repaintRectangle(rect);
}

bool RenderLayer::intersectsDamageRect(const LayoutRect& layerBounds, const LayoutRect& damageRect, const RenderLayer* rootLayer, const LayoutSize& offsetFromRoot, const std::optional<LayoutRect>& cachedBoundingBox) const
{
    // Always examine the canvas and the root.
    // FIXME: Could eliminate the isDocumentElementRenderer() check if we fix background painting so that the RenderView
    // paints the root's background.
    if (isRenderViewLayer() || renderer().isDocumentElementRenderer())
        return true;

    if (damageRect.isInfinite())
        return true;

    if (damageRect.isEmpty())
        return false;

    // If we aren't an inline flow, and our layer bounds do intersect the damage rect, then we can return true.
    if (!renderer().isRenderInline() && layerBounds.intersects(damageRect))
        return true;

    // Otherwise we need to compute the bounding box of this single layer and see if it intersects
    // the damage rect. It's possible the fragment computed the bounding box already, in which case we
    // can use the cached value.
    if (cachedBoundingBox)
        return cachedBoundingBox->intersects(damageRect);

    return boundingBox(rootLayer, offsetFromRoot).intersects(damageRect);
}

LayoutRect RenderLayer::localBoundingBox(OptionSet<CalculateLayerBoundsFlag> flags) const
{
    // There are three special cases we need to consider.
    // (1) Inline Flows.  For inline flows we will create a bounding box that fully encompasses all of the lines occupied by the
    // inline.  In other words, if some <span> wraps to three lines, we'll create a bounding box that fully encloses the
    // line boxes of all three lines (including overflow on those lines).
    // (2) Left/Top Overflow.  The width/height of layers already includes right/bottom overflow.  However, in the case of left/top
    // overflow, we have to create a bounding box that will extend to include this overflow.
    // (3) Floats.  When a layer has overhanging floats that it paints, we need to make sure to include these overhanging floats
    // as part of our bounding box.  We do this because we are the responsible layer for both hit testing and painting those
    // floats.
    LayoutRect result;
    if (CheckedPtr renderInline = dynamicDowncast<RenderInline>(renderer()); renderInline && renderer().isInline())
        result = renderInline->linesVisualOverflowBoundingBox();
    else if (CheckedPtr modelObject = dynamicDowncast<RenderSVGModelObject>(renderer()))
        result = modelObject->visualOverflowRectEquivalent();
    else if (CheckedPtr tableRow = dynamicDowncast<RenderTableRow>(renderer())) {
        // Our bounding box is just the union of all of our cells' border/overflow rects.
        for (RenderTableCell* cell = tableRow->firstCell(); cell; cell = cell->nextCell()) {
            LayoutRect bbox = cell->borderBoxRect();
            result.unite(bbox);
            LayoutRect overflowRect = tableRow->visualOverflowRect();
            if (bbox != overflowRect)
                result.unite(overflowRect);
        }
    } else {
        RenderBox* box = renderBox();
        ASSERT(box);
        if (!(flags & DontConstrainForMask) && box->hasMask()) {
            result = box->maskClipRect(LayoutPoint());
            box->flipForWritingMode(result); // The mask clip rect is in physical coordinates, so we have to flip, since localBoundingBox is not.
        } else
            result = box->visualOverflowRect();

        if (flags.contains(IncludeRootBackgroundPaintingArea) && renderer().isDocumentElementRenderer()) {
            // If the root layer becomes composited (e.g. because some descendant with negative z-index is composited),
            // then it has to be big enough to cover the viewport in order to display the background. This is akin
            // to the code in RenderBox::paintRootBoxFillLayers().
            auto& frameView = renderer().view().frameView();
            result.setWidth(std::max(result.width(), frameView.contentsWidth() - result.x()));
            result.setHeight(std::max(result.height(), frameView.contentsHeight() - result.y()));
        }
    }
    return result;
}

LayoutRect RenderLayer::boundingBox(const RenderLayer* ancestorLayer, const LayoutSize& offsetFromRoot, OptionSet<CalculateLayerBoundsFlag> flags) const
{
    LayoutRect result = localBoundingBox(flags);
    if (renderer().view().frameView().hasFlippedBlockRenderers()) {
        if (renderer().isRenderBox())
            renderBox()->flipForWritingMode(result);
        else
            renderer().containingBlock()->flipForWritingMode(result);
    }

    PaginationInclusionMode inclusionMode = ExcludeCompositedPaginatedLayers;
    if (flags & UseFragmentBoxesIncludingCompositing)
        inclusionMode = IncludeCompositedPaginatedLayers;

    const RenderLayer* paginationLayer = nullptr;
    if (flags.containsAny({ UseFragmentBoxesExcludingCompositing, UseFragmentBoxesIncludingCompositing }))
        paginationLayer = enclosingPaginationLayerInSubtree(ancestorLayer, inclusionMode);

    const RenderLayer* childLayer = this;
    bool isPaginated = paginationLayer;
    while (paginationLayer) {
        // Split our box up into the actual fragment boxes that render in the columns/pages and unite those together to
        // get our true bounding box.
        result.move(childLayer->offsetFromAncestor(paginationLayer));

        auto& enclosingFragmentedFlow = downcast<RenderFragmentedFlow>(paginationLayer->renderer());
        result = enclosingFragmentedFlow.fragmentsBoundingBox(result);

        childLayer = paginationLayer;
        paginationLayer = paginationLayer->parent()->enclosingPaginationLayerInSubtree(ancestorLayer, inclusionMode);
    }

    if (isPaginated) {
        result.move(childLayer->offsetFromAncestor(ancestorLayer));
        return result;
    }

    result.move(offsetFromRoot);
    return result;
}

bool RenderLayer::getOverlapBoundsIncludingChildrenAccountingForTransformAnimations(LayoutRect& bounds, OptionSet<CalculateLayerBoundsFlag> additionalFlags) const
{
    // The animation will override the display transform, so don't include it.
    auto boundsFlags = additionalFlags | (defaultCalculateLayerBoundsFlags() - IncludeSelfTransform);

    bounds = calculateLayerBounds(this, LayoutSize(), boundsFlags);

    LayoutRect animatedBounds = bounds;
    if (auto styleable = Styleable::fromRenderer(renderer())) {
        if (styleable->computeAnimationExtent(animatedBounds)) {
            bounds = animatedBounds;
            return true;
        }
    }

    return false;
}

IntRect RenderLayer::absoluteBoundingBox() const
{
    const RenderLayer* rootLayer = root();
    return snappedIntRect(boundingBox(rootLayer, offsetFromAncestor(rootLayer)));
}

FloatRect RenderLayer::absoluteBoundingBoxForPainting() const
{
    const RenderLayer* rootLayer = root();
    return snapRectToDevicePixels(boundingBox(rootLayer, offsetFromAncestor(rootLayer)), renderer().document().deviceScaleFactor());
}

LayoutRect RenderLayer::overlapBounds() const
{
    if (overlapBoundsIncludeChildren())
        return calculateLayerBounds(this, { }, { UseLocalClipRectExcludingCompositingIfPossible, IncludeFilterOutsets, UseFragmentBoxesExcludingCompositing });

    return localBoundingBox();
}
LayoutRect RenderLayer::calculateLayerBounds(const RenderLayer* ancestorLayer, const LayoutSize& offsetFromRoot, OptionSet<CalculateLayerBoundsFlag> flags) const
{
    if (!isSelfPaintingLayer())
        return LayoutRect();

    // FIXME: This could be improved to do a check like hasVisibleNonCompositingDescendantLayers() (bug 92580).
    if ((flags & ExcludeHiddenDescendants) && this != ancestorLayer && !hasVisibleContent() && !hasVisibleDescendant())
        return LayoutRect();

    if ((flags & ExcludeViewTransitionCapturedDescendants) && this != ancestorLayer && renderer().capturedInViewTransition() && !renderer().isDocumentElementRenderer())
        return LayoutRect();

    if (isRenderViewLayer()) {
        // The root layer is always just the size of the document.
        return renderer().view().unscaledDocumentRect();
    }

    LayoutRect boundingBoxRect = localBoundingBox(flags | IncludeRootBackgroundPaintingArea);
    if (renderer().view().frameView().hasFlippedBlockRenderers()) {
        if (CheckedPtr box = dynamicDowncast<RenderBox>(renderer()))
            box->flipForWritingMode(boundingBoxRect);
        else
            renderer().containingBlock()->flipForWritingMode(boundingBoxRect);
    }

    LayoutRect unionBounds = boundingBoxRect;

    if (flags.containsAny({ UseLocalClipRectIfPossible, UseLocalClipRectExcludingCompositingIfPossible })) {
        bool clipExceedsBounds = false;
        LayoutRect localClipRect = this->localClipRect(clipExceedsBounds, flags.contains(UseLocalClipRectExcludingCompositingIfPossible) ? LocalClipRectMode::ExcludeCompositingState : LocalClipRectMode::IncludeCompositingState);
        if (!localClipRect.isInfinite() && !clipExceedsBounds) {
            if ((flags & IncludeSelfTransform) && paintsWithTransform(PaintBehavior::Normal))
                localClipRect = transform()->mapRect(localClipRect);

            localClipRect.move(offsetFromAncestor(ancestorLayer));
            return localClipRect;
        }
    }

    // FIXME: should probably just pass 'flags' down to descendants.
    auto descendantFlags = (flags & PreserveAncestorFlags) ? flags : defaultCalculateLayerBoundsFlags() | (flags & ExcludeHiddenDescendants) | (flags & IncludeCompositedDescendants);

    const_cast<RenderLayer*>(this)->updateLayerListsIfNeeded();

    if (RenderLayer* reflection = reflectionLayer()) {
        if (!reflection->isComposited()) {
            LayoutRect childUnionBounds = reflection->calculateLayerBounds(this, reflection->offsetFromAncestor(this), descendantFlags);
            unionBounds.unite(childUnionBounds);
        }
    }
    
    ASSERT(isStackingContext() || !positiveZOrderLayers().size());

#if ASSERT_ENABLED
    LayerListMutationDetector mutationChecker(const_cast<RenderLayer&>(*this));
#endif

    auto computeLayersUnion = [this, &unionBounds, flags, descendantFlags] (const RenderLayer& childLayer) {
        if (!(flags & IncludeCompositedDescendants) && (childLayer.isComposited() || childLayer.paintsIntoProvidedBacking()))
            return;
        LayoutRect childBounds = childLayer.calculateLayerBounds(this, childLayer.offsetFromAncestor(this), descendantFlags);
        // Ignore child layer (and behave as if we had overflow: hidden) when it is positioned off the parent layer so much
        // that we hit the max LayoutUnit value.
        unionBounds.checkedUnite(childBounds);
    };

    for (auto* childLayer : negativeZOrderLayers())
        computeLayersUnion(*childLayer);

    for (auto* childLayer : positiveZOrderLayers())
        computeLayersUnion(*childLayer);

    for (auto* childLayer : normalFlowLayers())
        computeLayersUnion(*childLayer);

    if (flags.contains(IncludeFilterOutsets) || (flags.contains(IncludePaintedFilterOutsets) && shouldPaintWithFilters(flags & IncludeCompositedDescendants ? PaintBehavior::FlattenCompositingLayers : OptionSet<PaintBehavior>())))
        unionBounds.expand(toLayoutBoxExtent(filterOutsets()));

    if ((flags & IncludeSelfTransform) && paintsWithTransform(PaintBehavior::Normal)) {
        TransformationMatrix* affineTrans = transform();
        boundingBoxRect = affineTrans->mapRect(boundingBoxRect);
        unionBounds = affineTrans->mapRect(unionBounds);
    }
    unionBounds.move(offsetFromRoot);
    return unionBounds;
}

void RenderLayer::clearClipRectsIncludingDescendants(ClipRectsType typeToClear)
{
    // FIXME: it's not clear how this layer not having clip rects guarantees that no descendants have any.
    if (!m_clipRectsCache)
        return;

    clearClipRects(typeToClear);
    
    for (RenderLayer* l = firstChild(); l; l = l->nextSibling())
        l->clearClipRectsIncludingDescendants(typeToClear);
}

void RenderLayer::clearClipRects(ClipRectsType typeToClear)
{
    if (typeToClear == AllClipRectTypes)
        m_clipRectsCache = nullptr;
    else if (m_clipRectsCache) {
        ASSERT(typeToClear < NumCachedClipRectsTypes);
        m_clipRectsCache->setClipRects(typeToClear, RespectOverflowClip, nullptr);
        m_clipRectsCache->setClipRects(typeToClear, IgnoreOverflowClip, nullptr);
    }
}

RenderLayerBacking* RenderLayer::ensureBacking()
{
    if (!m_backing) {
        m_backing = makeUnique<RenderLayerBacking>(*this);
        compositor().layerBecameComposited(*this);

        updateFilterPaintingStrategy();
    }
    return m_backing.get();
}

void RenderLayer::clearBacking(OptionSet<UpdateBackingSharingFlags> flags, bool layerBeingDestroyed)
{
    if (!m_backing)
        return;

    if (!renderer().renderTreeBeingDestroyed())
        compositor().layerBecameNonComposited(*this);
    
    m_backing->willBeDestroyed(flags);
    m_backing = nullptr;

    if (!layerBeingDestroyed)
        updateFilterPaintingStrategy();
}

bool RenderLayer::hasCompositedMask() const
{
    return m_backing && m_backing->hasMaskLayer();
}

bool RenderLayer::paintsWithTransform(OptionSet<PaintBehavior> paintBehavior) const
{
    bool paintsToWindow = !isComposited() || backing()->paintsIntoWindow();
    return transform() && ((paintBehavior & PaintBehavior::FlattenCompositingLayers) || paintsToWindow);
}

bool RenderLayer::shouldPaintMask(OptionSet<PaintBehavior> paintBehavior, OptionSet<PaintLayerFlag> paintFlags) const
{
    if (!renderer().hasMask())
        return false;

    bool paintsToWindow = !isComposited() || backing()->paintsIntoWindow();
    if (paintsToWindow || (paintBehavior & PaintBehavior::FlattenCompositingLayers))
        return true;

    return paintFlags.contains(PaintLayerFlag::PaintingCompositingMaskPhase);
}

bool RenderLayer::shouldApplyClipPath(OptionSet<PaintBehavior> paintBehavior, OptionSet<PaintLayerFlag> paintFlags) const
{
    if (!renderer().hasClipPath())
        return false;

    bool paintsToWindow = !isComposited() || backing()->paintsIntoWindow();
    if (paintsToWindow || (paintBehavior & PaintBehavior::FlattenCompositingLayers))
        return true;

    return paintFlags.containsAny({ PaintLayerFlag::PaintingCompositingClipPathPhase, PaintLayerFlag::CollectingEventRegion });
}

bool RenderLayer::backgroundIsKnownToBeOpaqueInRect(const LayoutRect& localRect) const
{
    if (!isSelfPaintingLayer() && !hasSelfPaintingLayerDescendant())
        return false;

    if (paintsWithTransparency(PaintBehavior::Normal))
        return false;

    if (renderer().isDocumentElementRenderer()) {
        // Normally the document element doens't have a layer.  If it does have a layer, its background propagates to the RenderView
        // so this layer doesn't draw it.
        return false;
    }

    // We can't use hasVisibleContent(), because that will be true if our renderer is hidden, but some child
    // is visible and that child doesn't cover the entire rect.
    if (renderer().style().usedVisibility() != Visibility::Visible)
        return false;

    if (shouldPaintWithFilters() && renderer().style().filter().hasFilterThatAffectsOpacity())
        return false;

    // FIXME: Handle simple transforms.
    if (paintsWithTransform(PaintBehavior::Normal))
        return false;

    // FIXME: Remove this check.
    // This function should not be called when layer-lists are dirty.
    // It is somehow getting triggered during style update.
    if (zOrderListsDirty() || normalFlowListDirty())
        return false;

    // Table painting is special; a table paints its sections.
    if (renderer().isTablePart())
        return false;

    // A fieldset with a legend will have an irregular shape, so can't be treated as opaque.
    if (renderer().isFieldset())
        return false;

    // FIXME: We currently only check the immediate renderer,
    // which will miss many cases.
    if (renderer().backgroundIsKnownToBeOpaqueInRect(localRect))
        return true;
    
    // We can't consult child layers if we clip, since they might cover
    // parts of the rect that are clipped out.
    if (renderer().hasNonVisibleOverflow())
        return false;
    
    return listBackgroundIsKnownToBeOpaqueInRect(positiveZOrderLayers(), localRect)
        || listBackgroundIsKnownToBeOpaqueInRect(negativeZOrderLayers(), localRect)
        || listBackgroundIsKnownToBeOpaqueInRect(normalFlowLayers(), localRect);
}

bool RenderLayer::listBackgroundIsKnownToBeOpaqueInRect(const LayerList& list, const LayoutRect& localRect) const
{
    if (list.begin() == list.end())
        return false;

    for (auto iter = list.rbegin(); iter != list.rend(); ++iter) {
        const auto* childLayer = *iter;
        if (childLayer->isComposited())
            continue;

        if (!childLayer->canUseOffsetFromAncestor())
            continue;

        LayoutRect childLocalRect(localRect);
        childLocalRect.move(-childLayer->offsetFromAncestor(this));

        if (childLayer->backgroundIsKnownToBeOpaqueInRect(childLocalRect))
            return true;
    }
    return false;
}

void RenderLayer::repaintIncludingDescendants()
{
    renderer().repaint();
    for (RenderLayer* current = firstChild(); current; current = current->nextSibling())
        current->repaintIncludingDescendants();
}

bool RenderLayer::canUseOffsetFromAncestor(const RenderLayer& ancestor) const
{
    for (auto* layer = this; layer && layer != &ancestor; layer = layer->parent()) {
        if (!layer->canUseOffsetFromAncestor())
            return false;
    }
    return true;
}

void RenderLayer::setBackingNeedsRepaint(GraphicsLayer::ShouldClipToLayer shouldClip)
{
    ASSERT(isComposited());
    if (backing()->paintsIntoWindow()) {
        // If we're trying to repaint the placeholder document layer, propagate the
        // repaint to the native view system.
        renderer().view().repaintViewRectangle(absoluteBoundingBox());
    } else
        backing()->setContentsNeedDisplay(shouldClip);
}

void RenderLayer::setBackingNeedsRepaintInRect(const LayoutRect& r, GraphicsLayer::ShouldClipToLayer shouldClip)
{
    // https://bugs.webkit.org/show_bug.cgi?id=61159 describes an unreproducible crash here,
    // so assert but check that the layer is composited.
    ASSERT(isComposited());
    if (!isComposited() || backing()->paintsIntoWindow()) {
        // If we're trying to repaint the placeholder document layer, propagate the
        // repaint to the native view system.
        LayoutRect absRect(r);
        absRect.move(offsetFromAncestor(root()));

        renderer().view().repaintViewRectangle(absRect);
    } else
        backing()->setContentsNeedDisplayInRect(r, shouldClip);
}

// Since we're only painting non-composited layers, we know that they all share the same repaintContainer.
void RenderLayer::repaintIncludingNonCompositingDescendants(const RenderLayerModelObject* repaintContainer)
{
    auto clippedOverflowRect = m_repaintRectsValid ? m_repaintRects.clippedOverflowRect : renderer().clippedOverflowRectForRepaint(repaintContainer);
    renderer().repaintUsingContainer(repaintContainer, clippedOverflowRect);

    for (RenderLayer* curr = firstChild(); curr; curr = curr->nextSibling()) {
        if (!curr->isComposited())
            curr->repaintIncludingNonCompositingDescendants(repaintContainer);
    }
}

bool RenderLayer::shouldBeSelfPaintingLayer() const
{
    if (!isNormalFlowOnly())
        return true;

    return hasOverlayScrollbars()
        || hasCompositedScrollableOverflow()
        || renderer().isRenderTableRow()
        || renderer().isRenderHTMLCanvas()
        || renderer().isRenderVideo()
        || renderer().isRenderEmbeddedObject()
        || renderer().isRenderIFrame()
        || renderer().isRenderFragmentedFlow();
}

void RenderLayer::updateSelfPaintingLayer()
{
    bool isSelfPaintingLayer = shouldBeSelfPaintingLayer();
    if (m_isSelfPaintingLayer == isSelfPaintingLayer)
        return;

    m_isSelfPaintingLayer = isSelfPaintingLayer;
    setNeedsPositionUpdate();

    if (!parent())
        return;

    if (isSelfPaintingLayer)
        parent()->setAncestorChainHasSelfPaintingLayerDescendant();
    else {
        parent()->dirtyAncestorChainHasSelfPaintingLayerDescendantStatus();
        clearRepaintRects();
        auto updateFloatBoxShouldPaintIfApplicable = [&] {
            auto* renderBox = this->renderBox();
            if (!renderBox || !renderBox->isFloating())
                return;
            renderBox->updateFloatPainterAfterSelfPaintingLayerChange();
        };
        updateFloatBoxShouldPaintIfApplicable();
    }
}

static bool hasVisibleBoxDecorationsOrBackground(const RenderElement& renderer)
{
    return renderer.hasVisibleBoxDecorations() || renderer.style().hasOutline();
}

#if HAVE(SUPPORT_HDR_DISPLAY)
static bool rendererHasHDRContent(const RenderElement& renderer)
{
    auto& style = renderer.style();

    if (CheckedPtr imageRenderer = dynamicDowncast<RenderImage>(renderer)) {
        if (auto* cachedImage = imageRenderer->cachedImage()) {
            if (cachedImage->hasHDRContent())
                return true;
        }
    } else if (CheckedPtr imageRenderer = dynamicDowncast<LegacyRenderSVGImage>(renderer)) {
        if (auto* cachedImage = imageRenderer->imageResource().cachedImage()) {
            if (cachedImage->hasHDRContent())
                return true;
        }
#if ENABLE(PIXEL_FORMAT_RGBA16F)
    } else if (CheckedPtr canavsRenderer = dynamicDowncast<RenderHTMLCanvas>(renderer)) {
        if (auto* renderingContext = canavsRenderer->canvasElement().renderingContext()) {
            if (renderingContext->isHDR())
                return true;
        }
#endif
    }

    auto styleHasHDRContent = [](const auto& style) {
        if (style.hasBackgroundImage()) {
            if (style.backgroundLayers().hasHDRContent())
                return true;
        }

        if (style.hasBorderImage()) {
            auto image = style.borderImage().image();
            if (auto* cachedImage = image ? image->cachedImage() : nullptr) {
                if (cachedImage->hasHDRContent())
                    return true;
            }
        }

        if (auto image = style.listStyleImage()) {
            if (auto* cachedImage = image->cachedImage()) {
                if (cachedImage->hasHDRContent())
                    return true;
            }
        }

        return false;
    };

    return styleHasHDRContent(style);
}
#endif

static void determineNonLayerDescendantsPaintedContent(const RenderElement& renderer, unsigned& renderersTraversed, RenderLayer::PaintedContentRequest& request)
{
    // Constrain the depth and breadth of the search for performance.
    static constexpr unsigned maxRendererTraversalCount = 200;

    for (const auto& child : childrenOfType<RenderObject>(renderer)) {
        if (++renderersTraversed > maxRendererTraversalCount) {
            if (!request.isPaintedContentSatisfied())
                request.makePaintedContentUndetermined();
            if (request.isSatisfied())
                return;
        }

        if (CheckedPtr renderText = dynamicDowncast<RenderText>(child)) {
            if (!renderText->hasRenderedText())
                continue;

            if (renderer.style().usedUserSelect() != UserSelect::None)
                request.setHasPaintedContent();

            if (!renderText->text().containsOnly<isASCIIWhitespace>())
                request.setHasPaintedContent();

            if (request.isSatisfied())
                return;
        }
        
        CheckedPtr childElement = dynamicDowncast<RenderElement>(child);
        if (!childElement)
            continue;

        if (auto* modelObject = dynamicDowncast<RenderLayerModelObject>(*childElement); modelObject && modelObject->hasSelfPaintingLayer())
            continue;

        if (hasVisibleBoxDecorationsOrBackground(*childElement)) {
            request.setHasPaintedContent();
            if (request.isSatisfied())
                return;
        }
        
        if (is<RenderReplaced>(*childElement)) {
            request.setHasPaintedContent();

            if (request.isSatisfied())
                return;
        }

#if HAVE(SUPPORT_HDR_DISPLAY)
        if (!request.isHDRContentSatisfied() && rendererHasHDRContent(*childElement)) {
            request.setHasHDRContent();

            if (request.isSatisfied())
                return;
        }
#endif

        determineNonLayerDescendantsPaintedContent(*childElement, renderersTraversed, request);
        if (request.isSatisfied())
            return;
    }
}

void RenderLayer::determineNonLayerDescendantsPaintedContent(PaintedContentRequest& request) const
{
    unsigned renderersTraversed = 0;
    WebCore::determineNonLayerDescendantsPaintedContent(renderer(), renderersTraversed, request);
}

#if HAVE(SUPPORT_HDR_DISPLAY)
bool RenderLayer::rendererHasHDRContent() const
{
    if (auto* imageDocument = dynamicDowncast<ImageDocument>(renderer().document()))
        return imageDocument->drawsHDRContent();
    return WebCore::rendererHasHDRContent(renderer());
}
#endif

bool RenderLayer::hasVisibleBoxDecorationsOrBackground() const
{
    return WebCore::hasVisibleBoxDecorationsOrBackground(renderer());
}

bool RenderLayer::hasVisibleBoxDecorations() const
{
    if (!hasVisibleContent())
        return false;

    return hasVisibleBoxDecorationsOrBackground() || (m_scrollableArea && m_scrollableArea->hasOverflowControls());
}

bool RenderLayer::isVisibilityHiddenOrOpacityZero() const
{
    return !hasVisibleContent() || renderer().style().opacity().isTransparent();
}

bool RenderLayer::isVisuallyNonEmpty(PaintedContentRequest* request) const
{
    ASSERT(!m_visibleContentStatusDirty);

    if (!hasVisibleContent() || renderer().style().opacity().isTransparent())
        return false;

    if (renderer().isRenderReplaced() || (m_scrollableArea && m_scrollableArea->hasOverflowControls())) {
        if (!request)
            return true;

        request->setHasPaintedContent();
        if (request->isSatisfied())
            return true;
    }

    if (hasVisibleBoxDecorationsOrBackground()) {
        if (!request)
            return true;

        request->setHasPaintedContent();
        if (request->isSatisfied())
            return true;
    }

    PaintedContentRequest localRequest;
    if (!request)
        request = &localRequest;

    determineNonLayerDescendantsPaintedContent(*request);
    return request->probablyHasPaintedContent();
}

void RenderLayer::styleChanged(StyleDifference diff, const RenderStyle* oldStyle)
{
    setIsNormalFlowOnly(shouldBeNormalFlowOnly());
    setCanBeBackdropRoot(computeCanBeBackdropRoot());

    if (setIsCSSStackingContext(shouldBeCSSStackingContext())) {
        if (parent()) {
            if (isCSSStackingContext()) {
                if (!hasNotIsolatedBlendingDescendantsStatusDirty() && hasNotIsolatedBlendingDescendants())
                    parent()->dirtyAncestorChainHasBlendingDescendants();
            } else {
                if (hasNotIsolatedBlendingDescendantsStatusDirty())
                    parent()->dirtyAncestorChainHasBlendingDescendants();
                else if (hasNotIsolatedBlendingDescendants())
                    parent()->updateAncestorChainHasBlendingDescendants();
            }
        }
    }

    updateLayerScrollableArea();

    // FIXME: RenderLayer already handles visibility changes through our visibility dirty bits. This logic could
    // likely be folded along with the rest.
    if (oldStyle) {
        bool visibilityChanged = oldStyle->usedVisibility() != renderer().style().usedVisibility();
        if (oldStyle->usedZIndex() != renderer().style().usedZIndex() || oldStyle->usedContentVisibility() != renderer().style().usedContentVisibility() || visibilityChanged) {
            dirtyStackingContextZOrderLists();
            if (isStackingContext())
                dirtyZOrderLists();
        }

        if (!oldStyle->viewTransitionName().isNone() != renderer().hasViewTransitionName())
            updateAlwaysIncludedInZOrderLists();

        // Visibility and scrollability are input to canUseCompositedScrolling().
        if (m_scrollableArea) {
            if (oldStyle->writingMode() != renderer().style().writingMode())
                m_scrollableArea->invalidateScrollCornerRect({ });
            if (visibilityChanged || oldStyle->isOverflowVisible() != renderer().style().isOverflowVisible())
                m_scrollableArea->computeHasCompositedScrollableOverflow(diff <= StyleDifference::RepaintLayer ? LayoutUpToDate::Yes : LayoutUpToDate::No);
        }

        if (oldStyle->isOverflowVisible() != renderer().style().isOverflowVisible())
            setSelfAndDescendantsNeedPositionUpdate();

        if (oldStyle->opacity().isTransparent() != renderer().style().opacity().isTransparent())
            setNeedsPositionUpdate();

        if (oldStyle->preserves3D() != preserves3D()) {
            dirty3DTransformedDescendantStatus();
            setNeedsPostLayoutCompositingUpdateOnAncestors();
        }
    }

    if (m_scrollableArea) {
        m_scrollableArea->createOrDestroyMarquee();
        m_scrollableArea->updateScrollbarsAfterStyleChange(oldStyle);
    }
    // Overlay scrollbars can make this layer self-painting so we need
    // to recompute the bit once scrollbars have been updated.
    updateSelfPaintingLayer();

    if (!hasReflection() && m_reflection)
        removeReflection();
    else if (hasReflection()) {
        if (!m_reflection)
            createReflection();
        else
            m_reflection->setStyle(createReflectionStyle());
    }
    
    // FIXME: Need to detect a swap from custom to native scrollbars (and vice versa).
    if (m_scrollableArea)
        m_scrollableArea->updateAllScrollbarRelatedStyle();

    updateDescendantDependentFlags();
    updateBlendMode();
    updateFiltersAfterStyleChange(diff, oldStyle);
    clearClipRects();

    compositor().layerStyleChanged(diff, *this, oldStyle);

    updateTransform();
    updateFilterPaintingStrategy();

    if (oldStyle && oldStyle->hasViewportConstrainedPosition() != isViewportConstrained())
        dirtyAncestorChainHasViewportConstrainedDescendantStatus();

#if PLATFORM(IOS_FAMILY) && ENABLE(TOUCH_EVENTS)
    if (diff == StyleDifference::RecompositeLayer || diff >= StyleDifference::LayoutOutOfFlowMovementOnly)
        renderer().document().invalidateRenderingDependentRegions();
#else
    UNUSED_PARAM(diff);
#endif
}

RenderLayer* RenderLayer::reflectionLayer() const
{
    return m_reflection ? m_reflection->layer() : nullptr;
}

bool RenderLayer::isReflectionLayer(const RenderLayer& layer) const
{
    return m_reflection ? &layer == m_reflection->layer() : false;
}

void RenderLayer::createReflection()
{
    ASSERT(!m_reflection);
    m_reflection = createRenderer<RenderReplica>(renderer().document(), createReflectionStyle());
    // FIXME: A renderer should be a child of its parent!
    m_reflection->m_parent = &renderer(); // We create a 1-way connection.
    m_reflection->initializeStyle();
}

void RenderLayer::removeReflection()
{
    if (!m_reflection->renderTreeBeingDestroyed()) {
        if (auto* layer = m_reflection->layer())
            removeChild(*layer);
    }

    m_reflection->m_parent = nullptr;
    m_reflection = nullptr;
}

RenderStyle RenderLayer::createReflectionStyle()
{
    auto newStyle = RenderStyle::create();
    newStyle.inheritFrom(renderer().style());
    
    // Map in our transform.
    Vector<Ref<TransformOperation>> operations;

    switch (renderer().style().boxReflect()->direction()) {
    case ReflectionDirection::Below:
        operations = {
            TranslateTransformOperation::create(Length(0, LengthType::Fixed), Length(100., LengthType::Percent), TransformOperation::Type::Translate),
            TranslateTransformOperation::create(Length(0, LengthType::Fixed), renderer().style().boxReflect()->offset(), TransformOperation::Type::Translate),
            ScaleTransformOperation::create(1.0, -1.0, ScaleTransformOperation::Type::Scale)
        };
        break;
    case ReflectionDirection::Above:
        operations = {
            ScaleTransformOperation::create(1.0, -1.0, ScaleTransformOperation::Type::Scale),
            TranslateTransformOperation::create(Length(0, LengthType::Fixed), Length(100., LengthType::Percent), TransformOperation::Type::Translate),
            TranslateTransformOperation::create(Length(0, LengthType::Fixed), renderer().style().boxReflect()->offset(), TransformOperation::Type::Translate)
        };
        break;
    case ReflectionDirection::Right:
        operations = {
            TranslateTransformOperation::create(Length(100., LengthType::Percent), Length(0, LengthType::Fixed), TransformOperation::Type::Translate),
            TranslateTransformOperation::create(renderer().style().boxReflect()->offset(), Length(0, LengthType::Fixed), TransformOperation::Type::Translate),
            ScaleTransformOperation::create(-1.0, 1.0, ScaleTransformOperation::Type::Scale)
        };
        break;
    case ReflectionDirection::Left:
        operations = {
            ScaleTransformOperation::create(-1.0, 1.0, ScaleTransformOperation::Type::Scale),
            TranslateTransformOperation::create(Length(100., LengthType::Percent), Length(0, LengthType::Fixed), TransformOperation::Type::Translate),
            TranslateTransformOperation::create(renderer().style().boxReflect()->offset(), Length(0, LengthType::Fixed), TransformOperation::Type::Translate)
        };
        break;
    }
    newStyle.setTransform(TransformOperations { WTFMove(operations) });

    // Map in our mask.
    newStyle.setMaskBorder(renderer().style().boxReflect()->mask());

    // Style has transform and mask, so needs to be stacking context.
    newStyle.setUsedZIndex(0);

    return newStyle;
}

RenderLayerFilters& RenderLayer::ensureLayerFilters()
{
    if (m_filters)
        return *m_filters;
    
    m_filters = makeUnique<RenderLayerFilters>(*this);
    m_filters->setPreferredFilterRenderingModes(renderer().page().preferredFilterRenderingModes());
    m_filters->setFilterScale({ page().deviceScaleFactor(), page().deviceScaleFactor() });
    return *m_filters;
}

void RenderLayer::clearLayerFilters()
{
    m_filters = nullptr;
}

RenderLayerScrollableArea* RenderLayer::ensureLayerScrollableArea()
{
    bool hadScrollableArea = scrollableArea();
    
    if (!m_scrollableArea)
        m_scrollableArea = makeUnique<RenderLayerScrollableArea>(*this);

    if (!hadScrollableArea) {
        if (renderer().settings().asyncOverflowScrollingEnabled())
            setNeedsCompositingConfigurationUpdate();

        m_scrollableArea->restoreScrollPosition();
    }

    return m_scrollableArea.get();
}

void RenderLayer::clearLayerScrollableArea()
{
    if (m_scrollableArea) {
        m_scrollableArea->clear();
        m_scrollableArea = nullptr;
    }
}

void RenderLayer::updateFiltersAfterStyleChange(StyleDifference diff, const RenderStyle* oldStyle)
{
    if (renderer().style().filter().hasReferenceFilter())
        ensureLayerFilters().updateReferenceFilterClients(renderer().style().filter());
    else if (!shouldPaintWithFilters())
        clearLayerFilters();
    else if (m_filters)
        m_filters->removeReferenceFilterClients();

    auto filterChanged = [&] {
        if (!m_filters)
            return false;
        if (diff < StyleDifference::RepaintLayer)
            return false;
        if (!oldStyle)
            return false;
        if (oldStyle->filter() != renderer().style().filter())
            return true;
        auto currentColorChanged = oldStyle->color() != renderer().style().color();
        if (currentColorChanged && oldStyle->filter().requiresRepaintForCurrentColorChange())
            return true;
        return false;
    };
    if (filterChanged())
        clearLayerFilters();
}

void RenderLayer::updateLayerScrollableArea()
{
    bool hasScrollableArea = scrollableArea();
    bool needsScrollableArea = [&] {
        auto* box = dynamicDowncast<RenderBox>(renderer());
        return box && box->requiresLayerWithScrollableArea();
    }();

    if (needsScrollableArea == hasScrollableArea)
        return;

    if (needsScrollableArea)
        ensureLayerScrollableArea();
    else {
        clearLayerScrollableArea();
        if (renderer().settings().asyncOverflowScrollingEnabled())
            setNeedsCompositingConfigurationUpdate();
    }

    InspectorInstrumentation::didAddOrRemoveScrollbars(m_renderer);
}

void RenderLayer::updateFilterPaintingStrategy()
{
    // RenderLayerFilters is only used to render the filters in software mode,
    // so we always need to run updateFilterPaintingStrategy() after the composited
    // mode might have changed for this layer.
    if (!shouldPaintWithFilters()) {
        // Don't delete the whole filter info here, because we might use it
        // for loading SVG reference filter files.
        if (m_filters)
            m_filters->clearFilter();

        // Early-return only if we *don't* have reference filters.
        // For reference filters, we still want the FilterEffect graph built
        // for us, even if we're composited.
        if (!renderer().style().filter().hasReferenceFilter())
            return;
    }

    ensureLayerFilters();
}

IntOutsets RenderLayer::filterOutsets() const
{
    if (m_filters)
        return m_filters->calculateOutsets(renderer(), localBoundingBox());
    return renderer().style().filterOutsets();
}

static RenderLayer* parentLayerCrossFrame(const RenderLayer& layer)
{
    if (auto* parent = layer.parent())
        return parent;

    return layer.enclosingFrameRenderLayer();
}

bool RenderLayer::isTransparentRespectingParentFrames() const
{
    static const double minimumVisibleOpacity = 0.01;

    float currentOpacity = 1;
    for (auto* layer = this; layer; layer = parentLayerCrossFrame(*layer)) {
        currentOpacity *= layer->renderer().style().opacity().value.value;
        if (currentOpacity < minimumVisibleOpacity)
            return true;
    }

    return false;
}

#if ENABLE(RE_DYNAMIC_CONTENT_SCALING)
bool RenderLayer::allowsDynamicContentScaling() const
{
    if (is<RenderHTMLCanvas>(renderer()))
        return false;

    if (isBitmapOnly())
        return false;

    return true;
}
#endif

bool RenderLayer::isBitmapOnly() const
{
    if (hasVisibleBoxDecorationsOrBackground())
        return false;

    if (is<RenderHTMLCanvas>(renderer()))
        return true;

    if (CheckedPtr imageRenderer = dynamicDowncast<RenderImage>(renderer())) {
        if (auto* cachedImage = imageRenderer->cachedImage()) {
            if (!cachedImage->hasImage())
                return false;
            return is<BitmapImage>(cachedImage->imageForRenderer(imageRenderer.get()));
        }
        return false;
    }

    return false;
}

void RenderLayer::simulateFrequentPaint()
{
    m_paintFrequencyTracker.track(page().lastRenderingUpdateTimestamp());
}

void RenderLayer::purgeFrontBufferForTesting()
{
    if (backing())
        backing()->purgeFrontBufferForTesting();
}

void RenderLayer::purgeBackBufferForTesting()
{
    if (backing())
        backing()->purgeBackBufferForTesting();
}

void RenderLayer::markFrontBufferVolatileForTesting()
{
    if (backing())
        backing()->markFrontBufferVolatileForTesting();
}

RenderLayerScrollableArea* RenderLayer::scrollableArea() const
{
    return m_scrollableArea.get();
}

CheckedPtr<RenderLayerScrollableArea> RenderLayer::checkedScrollableArea() const
{
    return scrollableArea();
}

#if ENABLE(ASYNC_SCROLLING) && !LOG_DISABLED
static TextStream& operator<<(TextStream& ts, RenderLayer::EventRegionInvalidationReason reason)
{
    switch (reason) {
    case RenderLayer::EventRegionInvalidationReason::Paint: ts << "Paint"_s; break;
    case RenderLayer::EventRegionInvalidationReason::SettingDidChange: ts << "SettingDidChange"_s; break;
    case RenderLayer::EventRegionInvalidationReason::Style: ts << "Style"_s; break;
    case RenderLayer::EventRegionInvalidationReason::NonCompositedFrame: ts << "NonCompositedFrame"_s; break;
    }
    return ts;
}
#endif // !LOG_DISABLED

void RenderLayer::setAncestorsHaveDescendantNeedingEventRegionUpdate()
{
    for (auto* layer = parent(); layer; layer = layer->parent()) {
        if (layer->m_hasDescendantNeedingEventRegionUpdate)
            break;

        layer->m_hasDescendantNeedingEventRegionUpdate = true;
    }
}

bool RenderLayer::invalidateEventRegion(EventRegionInvalidationReason reason)
{
#if ENABLE(ASYNC_SCROLLING)
    auto* compositingLayer = enclosingCompositingLayerForRepaint().layer;

    auto shouldInvalidate = [&] {
        if (!compositingLayer)
            return false;

        if (reason == EventRegionInvalidationReason::NonCompositedFrame)
            return true;

        return compositingLayer->backing()->maintainsEventRegion();
    };

    if (!shouldInvalidate())
        return false;

    LOG_WITH_STREAM(EventRegions, stream << this << " invalidateEventRegion for reason " << reason << " invalidating in compositing layer " << compositingLayer);

    compositingLayer->backing()->setNeedsEventRegionUpdate();
    compositingLayer->setAncestorsHaveDescendantNeedingEventRegionUpdate();

    if (reason == EventRegionInvalidationReason::NonCompositedFrame) {
        auto& view = renderer().view();
        LOG_WITH_STREAM(EventRegions, stream << " calling setNeedsEventRegionUpdateForNonCompositedFrame on " << view);
        view.setNeedsEventRegionUpdateForNonCompositedFrame();
        auto visibleDebugOverlayRegions = OptionSet<DebugOverlayRegions>::fromRaw(renderer().settings().visibleDebugOverlayRegions());
        if (visibleDebugOverlayRegions.containsAny({ DebugOverlayRegions::TouchActionRegion, DebugOverlayRegions::EditableElementRegion, DebugOverlayRegions::WheelEventHandlerRegion }))
            view.setNeedsRepaintHackAfterCompositingLayerUpdateForDebugOverlaysOnly();
        view.compositor().scheduleCompositingLayerUpdate();
    }
    return true;
#else
    UNUSED_PARAM(reason);
    return false;
#endif
}

TextStream& operator<<(WTF::TextStream& ts, ClipRectsType clipRectsType)
{
    switch (clipRectsType) {
    case PaintingClipRects: ts << "painting"_s; break;
    case RootRelativeClipRects: ts << "root-relative"_s; break;
    case AbsoluteClipRects: ts << "absolute"_s; break;
    case AllClipRectTypes: ts << "all"_s; break;
    case NumCachedClipRectsTypes:
        ts << '?';
        break;
    }
    return ts;
}

TextStream& operator<<(TextStream& ts, const RenderLayer& layer)
{
    ts << layer.debugDescription();
    return ts;
}

TextStream& operator<<(TextStream& ts, const RenderLayer::ClipRectsContext& context)
{
    ts.dumpProperty("root layer:"_s, context.rootLayer);
    ts.dumpProperty("type:"_s, context.clipRectsType);
    ts.dumpProperty("options:"_s, context.options);

    return ts;
}

TextStream& operator<<(WTF::TextStream& ts, RenderLayer::ClipRectsOption clipRectsOption)
{
    switch (clipRectsOption) {
    case RenderLayer::ClipRectsOption::RespectOverflowClip: ts << "respect-overflow-clip"_s; break;
    case RenderLayer::ClipRectsOption::IncludeOverlayScrollbarSize: ts << "include-overlay-scrollbar-size"_s; break;
    case RenderLayer::ClipRectsOption::Temporary: ts << "temporary"_s; break;
    case RenderLayer::ClipRectsOption::OutsideFilter: ts << "outside-filter"_s; break;
    }
    return ts;
}

TextStream& operator<<(TextStream& ts, IndirectCompositingReason reason)
{
    switch (reason) {
    case IndirectCompositingReason::None: ts << "none"_s; break;
    case IndirectCompositingReason::Clipping: ts << "clipping"_s; break;
    case IndirectCompositingReason::Stacking: ts << "stacking"_s; break;
    case IndirectCompositingReason::OverflowScrollPositioning: ts << "overflow positioning"_s; break;
    case IndirectCompositingReason::Overlap: ts << "overlap"_s; break;
    case IndirectCompositingReason::BackgroundLayer: ts << "background layer"_s; break;
    case IndirectCompositingReason::GraphicalEffect: ts << "graphical effect"_s; break;
    case IndirectCompositingReason::Perspective: ts << "perspective"_s; break;
    case IndirectCompositingReason::Preserve3D: ts << "preserve-3d"_s; break;
    }

    return ts;
}

TextStream& operator<<(TextStream& ts, PaintBehavior behavior)
{
    switch (behavior) {
    case PaintBehavior::Normal: ts << "Normal"_s; break;
    case PaintBehavior::SelectionOnly: ts << "SelectionOnly"_s; break;
    case PaintBehavior::SkipSelectionHighlight: ts << "SkipSelectionHighlight"_s; break;
    case PaintBehavior::ForceBlackText: ts << "ForceBlackText"_s; break;
    case PaintBehavior::ForceWhiteText: ts << "ForceWhiteText"_s; break;
    case PaintBehavior::ForceBlackBorder: ts << "ForceBlackBorder"_s; break;
    case PaintBehavior::RenderingSVGClipOrMask: ts << "RenderingSVGClipOrMask"_s; break;
    case PaintBehavior::SkipRootBackground: ts << "SkipRootBackground"_s; break;
    case PaintBehavior::RootBackgroundOnly: ts << "RootBackgroundOnly"_s; break;
    case PaintBehavior::SelectionAndBackgroundsOnly: ts << "SelectionAndBackgroundsOnly"_s; break;
    case PaintBehavior::ExcludeSelection: ts << "ExcludeSelection"_s; break;
    case PaintBehavior::FlattenCompositingLayers: ts << "FlattenCompositingLayers"_s; break;
    case PaintBehavior::ForceSynchronousImageDecode: ts << "ForceSynchronousImageDecode"_s; break;
    case PaintBehavior::DefaultAsynchronousImageDecode: ts << "DefaultAsynchronousImageDecode"_s; break;
    case PaintBehavior::CompositedOverflowScrollContent: ts << "CompositedOverflowScrollContent"_s; break;
    case PaintBehavior::AnnotateLinks: ts << "AnnotateLinks"_s; break;
    case PaintBehavior::EventRegionIncludeForeground: ts << "EventRegionIncludeForeground"_s; break;
    case PaintBehavior::EventRegionIncludeBackground: ts << "EventRegionIncludeBackground"_s; break;
    case PaintBehavior::Snapshotting: ts << "Snapshotting"_s; break;
    case PaintBehavior::DontShowVisitedLinks: ts << "DontShowVisitedLinks"_s; break;
    case PaintBehavior::ExcludeReplacedContentExceptForIFrames: ts << "ExcludeReplacedContentExceptForIFrames"_s; break;
    case PaintBehavior::ExcludeText: ts << "ExcludeText"_s; break;
    case PaintBehavior::FixedAndStickyLayersOnly: ts << "FixedAndStickyLayersOnly"_s; break;
    case PaintBehavior::DrawsHDRContent: ts << "DrawsHDRContent"_s; break;
    case PaintBehavior::DraggableSnapshot: ts << "DraggableSnapshot"_s; break;
    }

    return ts;
}

TextStream& operator<<(TextStream& ts, RenderLayer::PaintLayerFlag flag)
{
    switch (flag) {
    case RenderLayer::PaintLayerFlag::HaveTransparency: ts << "HaveTransparency"_s; break;
    case RenderLayer::PaintLayerFlag::AppliedTransform: ts << "AppliedTransform"_s; break;
    case RenderLayer::PaintLayerFlag::TemporaryClipRects: ts << "TemporaryClipRects"_s; break;
    case RenderLayer::PaintLayerFlag::PaintingReflection: ts << "PaintingReflection"_s; break;
    case RenderLayer::PaintLayerFlag::PaintingOverlayScrollbars: ts << "PaintingOverlayScrollbars"_s; break;
    case RenderLayer::PaintLayerFlag::PaintingCompositingBackgroundPhase: ts << "PaintingCompositingBackgroundPhase"_s; break;
    case RenderLayer::PaintLayerFlag::PaintingCompositingForegroundPhase: ts << "PaintingCompositingForegroundPhase"_s; break;
    case RenderLayer::PaintLayerFlag::PaintingCompositingMaskPhase: ts << "PaintingCompositingMaskPhase"_s; break;
    case RenderLayer::PaintLayerFlag::PaintingCompositingClipPathPhase: ts << "PaintingCompositingClipPathPhase"_s; break;
    case RenderLayer::PaintLayerFlag::PaintingOverflowContainer: ts << "PaintingOverflowContainer"_s; break;
    case RenderLayer::PaintLayerFlag::PaintingOverflowContents: ts << "PaintingOverflowContents"_s; break;
    case RenderLayer::PaintLayerFlag::PaintingOverflowContentsRoot: ts << "PaintingOverflowContentsRoot"_s; break;
    case RenderLayer::PaintLayerFlag::PaintingRootBackgroundOnly: ts << "PaintingRootBackgroundOnly"_s; break;
    case RenderLayer::PaintLayerFlag::PaintingSkipRootBackground: ts << "PaintingSkipRootBackground"_s; break;
    case RenderLayer::PaintLayerFlag::PaintingChildClippingMaskPhase: ts << "PaintingChildClippingMaskPhase"_s; break;
    case RenderLayer::PaintLayerFlag::PaintingSVGClippingMask: ts << "PaintingSVGClippingMask"_s; break;
    case RenderLayer::PaintLayerFlag::CollectingEventRegion: ts << "CollectingEventRegion"_s; break;
    case RenderLayer::PaintLayerFlag::PaintingSkipDescendantViewTransition: ts << "PaintingSkipDescendantViewTransition"_s; break;
    }

    return ts;
}


#undef LAYER_POSITIONS_ASSERT_ENABLED
#undef LAYER_POSITIONS_ASSERT
#undef LAYER_POSITIONS_ASSERT_IMPLIES

} // namespace WebCore

#if ENABLE(TREE_DEBUGGING)

void showLayerTree(const WebCore::RenderLayer* layer)
{
    if (!layer)
        return;

    String output = externalRepresentation(&layer->renderer().frame(), {
        WebCore::RenderAsTextFlag::ShowAllLayers,
        WebCore::RenderAsTextFlag::ShowLayerNesting,
        WebCore::RenderAsTextFlag::ShowCompositedLayers,
        WebCore::RenderAsTextFlag::ShowOverflow,
        WebCore::RenderAsTextFlag::ShowSVGGeometry,
        WebCore::RenderAsTextFlag::ShowLayerFragments,
        WebCore::RenderAsTextFlag::ShowAddresses,
        WebCore::RenderAsTextFlag::ShowIDAndClass,
        WebCore::RenderAsTextFlag::DontUpdateLayout,
        WebCore::RenderAsTextFlag::ShowLayoutState,
    });
    SAFE_FPRINTF(stderr, "\n%s\n", output.utf8());
}

void showLayerTree(const WebCore::RenderObject* renderer)
{
    if (!renderer)
        return;
    showLayerTree(renderer->enclosingLayer());
}

static void outputPaintOrderTreeLegend(TextStream& stream)
{
    stream.nextLine();
    stream << "(T)op layer, (S)tacking Context/(F)orced SC/O(P)portunistic SC, (N)ormal flow only, (O)verflow clip, (A)lpha (opacity or mask), has (B)lend mode, (I)solates blending, (T)ransform-ish, (F)ilter, Fi(X)ed position, Behaves as fi(x)ed, (C)omposited, (P)rovides backing/uses (p)rovided backing/paints to (a)ncestor, (c)omposited descendant, (s)scrolling ancestor, (t)transformed ancestor\n"
        "Dirty (z)-lists, Dirty (n)ormal flow lists\n"
        "Traversal needs: requirements (t)raversal on descendants, (b)acking or hierarchy traversal on descendants, (r)equirements traversal on all descendants, requirements traversal on all (s)ubsequent layers, (h)ierarchy traversal on all descendants, update of paint (o)rder children\n"
        "Update needs:    post-(l)ayout requirements, (g)eometry, (k)ids geometry, (c)onfig, layer conne(x)ion, (s)crolling tree\n"
        "Scrolling scope: box contents\n";
    stream.nextLine();
}

static void outputIndent(TextStream& stream, unsigned depth)
{
    unsigned i = 0;
    while (++i <= depth * 2)
        stream << " ";
}

static void outputPaintOrderTreeRecursive(TextStream& stream, const WebCore::RenderLayer& layer, ASCIILiteral prefix, unsigned depth = 0)
{
    stream << (layer.establishesTopLayer() ? "T"_s : "-"_s);
    stream << (layer.isCSSStackingContext() ? "S"_s : (layer.isForcedStackingContext() ? "F"_s : (layer.isOpportunisticStackingContext() ? "P"_s : "-"_s)));
    stream << (layer.isNormalFlowOnly() ? "N"_s : "-"_s);
    stream << (layer.renderer().hasNonVisibleOverflow() ? "O"_s : "-"_s);
    stream << (layer.isTransparent() ? "A"_s : "-"_s);
    stream << (layer.hasBlendMode() ? "B"_s : "-"_s);
    stream << (layer.isolatesBlending() ? "I"_s : "-"_s);
    stream << (layer.renderer().hasTransformRelatedProperty() ? "T"_s : "-"_s);
    stream << (layer.hasFilter() ? "F"_s : "-"_s);
    stream << (layer.renderer().isFixedPositioned() ? "X"_s : "-"_s);
    stream << (layer.behavesAsFixed() ? "x"_s : "-"_s);
    stream << (layer.isComposited() ? "C"_s : "-"_s);
    
    auto compositedPaintingDestinationString = [&layer]() {
        if (layer.paintsIntoProvidedBacking())
            return "p"_s;

        if (!layer.isComposited())
            return "-"_s;

        if (layer.backing()->hasBackingSharingLayers())
            return "P"_s;
        
        if (layer.backing()->paintsIntoCompositedAncestor())
            return "a"_s;

        return "-"_s;
    };

    stream << compositedPaintingDestinationString();
    stream << (layer.hasCompositingDescendant() ? "c"_s : "-"_s);
    stream << (layer.hasCompositedScrollingAncestor() ? "s"_s : "-"_s);
    stream << (layer.hasTransformedAncestor() ? "t"_s : "-"_s);

    stream << " "_s;

    stream << (layer.zOrderListsDirty() ? "z"_s : "-"_s);
    stream << (layer.normalFlowListDirty() ? "n"_s : "-"_s);

    stream << " "_s;

    stream << (layer.hasDescendantNeedingCompositingRequirementsTraversal() ? "t"_s : "-"_s);
    stream << (layer.hasDescendantNeedingUpdateBackingOrHierarchyTraversal() ? "b"_s : "-"_s);
    stream << (layer.descendantsNeedCompositingRequirementsTraversal() ? "r"_s : "-"_s);
    stream << (layer.subsequentLayersNeedCompositingRequirementsTraversal() ? "s"_s : "-"_s);
    stream << (layer.descendantsNeedUpdateBackingAndHierarchyTraversal() ? "h"_s : "-"_s);
    stream << (layer.needsCompositingPaintOrderChildrenUpdate() ? "o"_s : "-"_s);

    stream << " "_s;

    stream << (layer.needsPostLayoutCompositingUpdate() ? "l"_s : "-"_s);
    stream << (layer.needsCompositingGeometryUpdate() ? "g"_s : "-"_s);
    stream << (layer.childrenNeedCompositingGeometryUpdate() ? "k"_s : "-"_s);
    stream << (layer.needsCompositingConfigurationUpdate() ? "c"_s : "-"_s);
    stream << (layer.needsCompositingLayerConnection() ? "x"_s : "-"_s);
    stream << (layer.needsScrollingTreeUpdate() ? "s"_s : "-"_s);

    stream << " "_s;

    stream << layer.boxScrollingScope();
    stream << " "_s;
    stream << layer.contentsScrollingScope();

    stream << " "_s;

    outputIndent(stream, depth);

    stream << prefix;

    auto layerRect = layer.rect();

    stream << &layer << " "_s << layerRect;

    if (auto* scrollableArea = layer.scrollableArea())
        stream << " [SA "_s << scrollableArea << "]"_s;

    if (layer.isComposited()) {
        auto& backing = *layer.backing();
        stream << " (layerID "_s << (backing.graphicsLayer()->primaryLayerID() ? backing.graphicsLayer()->primaryLayerID()->object().toUInt64() : 0) << ")"_s;
        
        if (layer.indirectCompositingReason() != WebCore::IndirectCompositingReason::None)
            stream << " "_s << layer.indirectCompositingReason();

        auto scrollingNodeID = backing.scrollingNodeIDForRole(WebCore::ScrollCoordinationRole::Scrolling);
        auto frameHostingNodeID = backing.scrollingNodeIDForRole(WebCore::ScrollCoordinationRole::FrameHosting);
        auto pluginHostingNodeID = backing.scrollingNodeIDForRole(WebCore::ScrollCoordinationRole::PluginHosting);
        auto viewportConstrainedNodeID = backing.scrollingNodeIDForRole(WebCore::ScrollCoordinationRole::ViewportConstrained);
        auto positionedNodeID = backing.scrollingNodeIDForRole(WebCore::ScrollCoordinationRole::Positioning);

        if (scrollingNodeID || frameHostingNodeID || viewportConstrainedNodeID || positionedNodeID) {
            stream << " {"_s;
            bool first = true;
            if (scrollingNodeID) {
                stream << "sc "_s << scrollingNodeID;
                first = false;
            }

            if (frameHostingNodeID) {
                if (!first)
                    stream << ", "_s;
                stream << "fh "_s << frameHostingNodeID;
                first = false;
            }

            if (pluginHostingNodeID) {
                if (!first)
                    stream << ", "_s;
                stream << "ph "_s << pluginHostingNodeID;
                first = false;
            }

            if (viewportConstrainedNodeID) {
                if (!first)
                    stream << ", "_s;
                stream << "vc "_s << viewportConstrainedNodeID;
                first = false;
            }

            if (positionedNodeID) {
                if (!first)
                    stream << ", "_s;
                stream << "pos "_s << positionedNodeID;
            }

            stream << "}"_s;
        }

        if (backing.subpixelOffsetFromRenderer() != WebCore::LayoutSize())
            stream << " (subpixel offset "_s << backing.subpixelOffsetFromRenderer() << ")"_s;
    }
    stream << " "_s << layer.name();
    stream.nextLine();

    const_cast<WebCore::RenderLayer&>(layer).updateLayerListsIfNeeded();

    for (auto* child : layer.negativeZOrderLayers())
        outputPaintOrderTreeRecursive(stream, *child, "- "_s, depth + 1);

    for (auto* child : layer.normalFlowLayers())
        outputPaintOrderTreeRecursive(stream, *child, "n "_s, depth + 1);

    for (auto* child : layer.positiveZOrderLayers())
        outputPaintOrderTreeRecursive(stream, *child, "+ "_s, depth + 1);
}

void showPaintOrderTree(const WebCore::RenderLayer* layer)
{
    TextStream stream;
    outputPaintOrderTreeLegend(stream);
    if (layer)
        outputPaintOrderTreeRecursive(stream, *layer, ""_s);
    
    WTFLogAlways("%s", stream.release().utf8().data());
}

void showPaintOrderTree(const WebCore::RenderObject* renderer)
{
    if (!renderer)
        return;
    showPaintOrderTree(renderer->enclosingLayer());
}

static void outputLayerPositionTreeLegend(TextStream& stream)
{
    stream.nextLine();
    stream << "Dirty flags: NeedsPosition(U)pdate, (D)escendantNeedsPositionUpdate, All(C)hildrenNeedPositionUpdate, (A)llDescendantsNeedPositionUpdate\n";
    stream << "Repaint status: (-)NeedsNormalRepaint, Needs(F)ullRepaint, NeedsFullRepaintFor(P)ositionedMovementLayout\n";
    stream << "Layer state: has(P)aginatedAncestor, has(F)ixedAncestor,  hasFixedContaining(B)lockAncestor, has(T)ransformedAncestor, has(3)DTransformedAncestor, hasComposited(S)crollingAncestor, !is(V)isibilityHiddenOrOpacityZero(), isSelfPainting(L)ayer, (C)omposited, CompositedWithOwn(B)ackingStore\n";
    stream.nextLine();
}

void outputLayerPositionTreeRecursive(TextStream& stream, const WebCore::RenderLayer& layer, unsigned depth, const WebCore::RenderLayer* mark)
{
    if (&layer == mark)
        stream << "*"_s;
    else
        stream << " "_s;

    stream << (layer.m_layerPositionDirtyBits.contains(WebCore::RenderLayer::LayerPositionUpdates::NeedsPositionUpdate) ? "U"_s : "-"_s);
    stream << (layer.m_layerPositionDirtyBits.contains(WebCore::RenderLayer::LayerPositionUpdates::DescendantNeedsPositionUpdate) ? "D"_s : "-"_s);
    stream << (layer.m_layerPositionDirtyBits.contains(WebCore::RenderLayer::LayerPositionUpdates::AllChildrenNeedPositionUpdate) ? "C"_s : "-"_s);
    stream << (layer.m_layerPositionDirtyBits.contains(WebCore::RenderLayer::LayerPositionUpdates::AllDescendantsNeedPositionUpdate) ? "A"_s : "-"_s);

    stream << " "_s;

    if (layer.repaintStatus() == WebCore::RepaintStatus::NeedsFullRepaintForOutOfFlowMovementLayout)
        stream << "P";
    else if (layer.repaintStatus() == WebCore::RepaintStatus::NeedsFullRepaint)
        stream << "F";
    else
        stream << "-";

    stream << " "_s;

    stream << (layer.hasPaginatedAncestor() ? "P"_s : "-"_s);
    stream << (layer.hasFixedAncestor() ? "F"_s : "-"_s);
    stream << (layer.hasFixedContainingBlockAncestor() ? "B"_s : "-"_s);
    stream << (layer.hasTransformedAncestor() ? "T"_s : "-"_s);
    stream << (layer.has3DTransformedAncestor() ? "3"_s : "-"_s);
    stream << (layer.hasCompositedScrollingAncestor() ? "S"_s : "-"_s);
    stream << (!layer.isVisibilityHiddenOrOpacityZero() ? "V"_s : "-"_s);
    stream << (layer.isSelfPaintingLayer() ? "L"_s : "-"_s);
    stream << (layer.isComposited() ? "C"_s : "-"_s);
    stream << (compositedWithOwnBackingStore(layer) ? "B"_s : "-"_s);

    // FIXME: cached clip rects?

    stream << " "_s;

    outputIndent(stream, depth);

    auto layerRect = layer.rect();

    stream << &layer << " "_s << layerRect;

    stream << " "_s << layer.name();

    if (layer.paintOrderParent() != layer.parent())
        stream << " (paint order parent " << layer.paintOrderParent() << ")";

    if (layer.m_repaintContainer)
        stream << " (repaint container: " << layer.m_repaintContainer.get() << ")";

    if (layer.repaintRects())
        stream << " (repaint rects " << *layer.repaintRects() << ")";

    if (layer.paintsIntoProvidedBacking())
        stream << " (backing provider " << layer.backingProviderLayer() << ")";

    stream.nextLine();

    for (WebCore::RenderLayer* child = layer.firstChild(); child; child = child->nextSibling())
        outputLayerPositionTreeRecursive(stream, *child, depth + 1, mark);
}

void showLayerPositionTree(const WebCore::RenderLayer* root, const WebCore::RenderLayer* mark)
{
    TextStream stream;
    outputLayerPositionTreeLegend(stream);
    if (root)
        outputLayerPositionTreeRecursive(stream, *root, 0, mark);

    WTFLogAlways("%s", stream.release().utf8().data());
}

#endif
