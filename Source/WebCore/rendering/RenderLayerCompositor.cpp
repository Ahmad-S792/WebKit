/*
 * Copyright (C) 2009-2020 Apple Inc. All rights reserved.
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
#include "RenderLayerCompositor.h"

#include "AsyncScrollingCoordinator.h"
#include "BorderData.h"
#include "BorderShape.h"
#include "CSSPropertyNames.h"
#include "CanvasRenderingContext.h"
#include "Chrome.h"
#include "ChromeClient.h"
#include "DocumentFullscreen.h"
#include "GraphicsLayer.h"
#include "HTMLCanvasElement.h"
#include "HTMLIFrameElement.h"
#include "HTMLNames.h"
#include "HitTestResult.h"
#include "InspectorInstrumentation.h"
#include "KeyframeEffectStack.h"
#include "LayerAncestorClippingStack.h"
#include "LayerOverlapMap.h"
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "Logging.h"
#include "NodeList.h"
#include "Page.h"
#include "PageOverlayController.h"
#include "PathOperation.h"
#include "RemoteFrame.h"
#include "RenderBoxInlines.h"
#include "RenderElementInlines.h"
#include "RenderEmbeddedObject.h"
#include "RenderFragmentedFlow.h"
#include "RenderGeometryMap.h"
#include "RenderIFrame.h"
#include "RenderImage.h"
#include "RenderLayerBacking.h"
#include "RenderLayerInlines.h"
#include "RenderLayerScrollableArea.h"
#include "RenderObjectInlines.h"
#include "RenderStyleInlines.h"
#include "RenderVideo.h"
#include "RenderView.h"
#include "RenderViewTransitionCapture.h"
#include "RenderWidgetInlines.h"
#include "RotateTransformOperation.h"
#include "SVGGraphicsElement.h"
#include "ScaleTransformOperation.h"
#include "ScrollingConstraints.h"
#include "Settings.h"
#include "StyleOffsetRotate.h"
#include "TiledBacking.h"
#include "TransformState.h"
#include "TranslateTransformOperation.h"
#include "ViewTransition.h"
#include "WillChangeData.h"
#include <wtf/HexNumber.h>
#include <wtf/MemoryPressureHandler.h>
#include <wtf/ObjectIdentifier.h>
#include <wtf/Scope.h>
#include <wtf/SetForScope.h>
#include <wtf/SystemTracing.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/text/TextStream.h>

#if PLATFORM(IOS_FAMILY)
#include "LegacyTileCache.h"
#include "RenderScrollbar.h"
#endif

#if PLATFORM(MAC)
#include "LocalDefaultSystemAppearance.h"
#endif

#if ENABLE(TREE_DEBUGGING)
#include "RenderTreeAsText.h"
#endif

#if ENABLE(MODEL_ELEMENT)
#include "RenderModel.h"
#endif

#if !PLATFORM(MAC) && !PLATFORM(IOS_FAMILY) && !PLATFORM(GTK) && !PLATFORM(WPE)
#define USE_COMPOSITING_FOR_SMALL_CANVASES 1
#endif

namespace WebCore {

#if PLATFORM(IOS_FAMILY)
WTF_MAKE_TZONE_ALLOCATED_IMPL(LegacyWebKitScrollingLayerCoordinator);
#endif

WTF_MAKE_TZONE_ALLOCATED_IMPL(RenderLayerCompositor);

#if !USE(COMPOSITING_FOR_SMALL_CANVASES)
static const int canvasAreaThresholdRequiringCompositing = 50 * 100;
#endif

using namespace HTMLNames;

struct ScrollingTreeState {
    Markable<ScrollingNodeID> parentNodeID;
    bool hasParent { false };
    size_t nextChildIndex { 0 };
    bool needSynchronousScrollingReasonsUpdate { false };
};

struct RenderLayerCompositor::OverlapExtent {
    LayoutRect bounds;
    LayerOverlapMap::LayerAndBoundsVector clippingScopes;

    bool extentComputed { false };
    bool hasTransformAnimation { false };
    bool animationCausesExtentUncertainty { false };
    bool clippingScopesComputed { false };

    bool knownToBeHaveExtentUncertainty() const { return extentComputed && animationCausesExtentUncertainty; }
};

struct RenderLayerCompositor::CompositingState {
    CompositingState(RenderLayer* compAncestor, bool testOverlap = true)
        : compositingAncestor(compAncestor)
        , testingOverlap(testOverlap)
    {
    }
    
    CompositingState stateForPaintOrderChildren(RenderLayer& layer) const
    {
        UNUSED_PARAM(layer);
        CompositingState childState(compositingAncestor);
        if (layer.isStackingContext())
            childState.stackingContextAncestor = &layer;
        else
            childState.stackingContextAncestor = stackingContextAncestor;

        childState.backingSharingAncestor = backingSharingAncestor;
        childState.subtreeIsCompositing = false;
        childState.testingOverlap = testingOverlap;
        childState.fullPaintOrderTraversalRequired = fullPaintOrderTraversalRequired;
        childState.descendantsRequireCompositingUpdate = descendantsRequireCompositingUpdate;
        childState.ancestorHasTransformAnimation = ancestorHasTransformAnimation;
        childState.ancestorAllowsBackingStoreDetachingForFixed = ancestorAllowsBackingStoreDetachingForFixed;
        childState.hasCompositedNonContainedDescendants = false;
        childState.hasNotIsolatedCompositedBlendingDescendants = false; // FIXME: should this only be reset for stacking contexts?
        childState.hasBackdropFilterDescendantsWithoutRoot = false;
#if !LOG_DISABLED
        childState.depth = depth + 1;
#endif
        return childState;
    }

    void updateWithDescendantStateAndLayer(const CompositingState& childState, const RenderLayer& layer, const RenderLayer* ancestorLayer, const OverlapExtent& layerExtent, bool isUnchangedSubtree = false)
    {
        // Subsequent layers in the parent stacking context also need to composite.
        subtreeIsCompositing |= childState.subtreeIsCompositing | layer.isComposited();
        if (!isUnchangedSubtree)
            fullPaintOrderTraversalRequired |= childState.fullPaintOrderTraversalRequired;

        // Turn overlap testing off for later layers if it's already off, or if we have an animating transform.
        // Note that if the layer clips its descendants, there's no reason to propagate the child animation to the parent layers. That's because
        // we know for sure the animation is contained inside the clipping rectangle, which is already added to the overlap map.
        auto canReenableOverlapTesting = [&layer] {
            return layer.isComposited() && RenderLayerCompositor::clipsCompositingDescendants(layer);
        };
        if ((!childState.testingOverlap && !canReenableOverlapTesting()) || layerExtent.knownToBeHaveExtentUncertainty())
            testingOverlap = false;

        auto computeHasCompositedNonContainedDescendants = [&] {
            if (hasCompositedNonContainedDescendants)
                return true;
            if (!ancestorLayer)
                return false;
            if (!layer.isComposited())
                return false;
            if (!layer.renderer().isOutOfFlowPositioned())
                return false;
            if (layer.ancestorLayerIsInContainingBlockChain(*ancestorLayer))
                return false;
            return true;
        };

        hasCompositedNonContainedDescendants = computeHasCompositedNonContainedDescendants();

        if ((layer.isComposited() && layer.hasBlendMode()) || (layer.hasNotIsolatedCompositedBlendingDescendants() && !layer.isolatesCompositedBlending()))
            hasNotIsolatedCompositedBlendingDescendants = true;

        if ((layer.isComposited() && layer.hasBackdropFilter()) || (layer.hasBackdropFilterDescendantsWithoutRoot() && !layer.isBackdropRoot()))
            hasBackdropFilterDescendantsWithoutRoot = true;

#if HAVE(CORE_MATERIAL)
        if (layer.isComposited() && layer.hasAppleVisualEffectRequiringBackdropFilter())
            hasBackdropFilterDescendantsWithoutRoot = true;
#endif
    }

    bool hasNonRootCompositedAncestor() const
    {
        return compositingAncestor && !compositingAncestor->isRenderViewLayer();
    }

    RenderLayer* compositingAncestor;
    RenderLayer* backingSharingAncestor { nullptr };
    RenderLayer* stackingContextAncestor { nullptr };
    bool subtreeIsCompositing { false };
    bool testingOverlap { true };
    bool fullPaintOrderTraversalRequired { false };
    bool descendantsRequireCompositingUpdate { false };
    bool ancestorHasTransformAnimation { false };
    bool ancestorAllowsBackingStoreDetachingForFixed { false };
    bool hasCompositedNonContainedDescendants { false };
    bool hasNotIsolatedCompositedBlendingDescendants { false };
    bool hasBackdropFilterDescendantsWithoutRoot { false };
#if !LOG_DISABLED
    unsigned depth { 0 };
#endif
};

struct RenderLayerCompositor::UpdateBackingTraversalState {
    UpdateBackingTraversalState(RenderLayer* compAncestor = nullptr, Vector<RenderLayer*>* clippedLayers = nullptr, Vector<RenderLayer*>* overflowScrollers = nullptr)
        : compositingAncestor(compAncestor)
        , layersClippedByScrollers(clippedLayers)
        , overflowScrollLayers(overflowScrollers)
    {
    }

    UpdateBackingTraversalState stateForDescendants() const
    {
        UpdateBackingTraversalState state(compositingAncestor, layersClippedByScrollers, overflowScrollLayers);
#if !LOG_DISABLED
        state.depth = depth + 1;
#endif
        return state;
    }

    RenderLayer* compositingAncestor;

    // List of layers in the current stacking context that are clipped by ancestor scrollers.
    Vector<RenderLayer*>* layersClippedByScrollers;
    // List of layers with composited overflow:scroll.
    Vector<RenderLayer*>* overflowScrollLayers;

#if !LOG_DISABLED
    unsigned depth { 0 };
#endif
};

/*
    Backing sharing is used to reduce memory use by allowing multiple RenderLayers (normally siblings) which share the same
    stacking context ancestor to render into the same compositing layer. This has to be done in a way that preserves back-to-front
    paint order. The common case where this kicks in is a non-stacking context overflow:scroll with position:relative descendants.
    
    When we've determined that a layer can be composited, it becomes a candidate for backing sharing (i.e. layers later
    in paint order, with the same stacking context ancestor, might be able to paint into it).
    
    We maintain multiple backing provider candidates in order to have backing sharing work with sibling or nested
    overflow scrollers. When traversing layers that might be able to share with these providers, this is essentially
    a bucketing process. There are three cases to consider here:

    1. Sibling scrollers that don't overlap:
       In this case we can simply add later layers to the appropriate scroller (using scrolling scope to find the right one),
       since we know that we're traversing those layers in paint order and the scrollers don't overlap. Aswe assign layers to
       one or other candidate, paint order will be preserved. This is supported.

    2. Sibling scrollers that overlap:
       Here we can have layers share with the on-top scroller, but have to ensure that layers scrolled by the below scroller
       correctly overlap the border/background of the on-top scroller (i.e. they can't use sharing). So we can only do sharing
       with the last scroller. This is not currently supported.

    3. Nested scrollers:
       Similar to overlapping scrollers, we have to ensure that we add to the right provider (looking a scrolling scope),
       and don't break overlap with the nested scroller. This is not currently supported.

    We also track additional backing sharing providers that aren't clipped scrollers. These cannot be added to, since that could expand the bounds of the resulting layer.
    They are tracked so we can check them for overlap, and continue to add to the scroller backing sharing providers if the new content doesn't overlap.

    To debug sharing behavior, enable the "Compositing" log channel and look for the P/p in the hierarchy output.
 */

enum class BackingSharingSequenceIdentifierType { };
using BackingSharingSequenceIdentifier = ObjectIdentifier<BackingSharingSequenceIdentifierType>;

struct RenderLayerCompositor::BackingSharingSnapshot {
    BackingSharingSequenceIdentifier sequenceIdentifier;
    size_t providerCount { 0 };
};

class RenderLayerCompositor::BackingSharingState {
    WTF_MAKE_NONCOPYABLE(BackingSharingState);
public:
    BackingSharingState(bool allowOverlappingProviders)
        : m_allowOverlappingProviders(allowOverlappingProviders)
    { }

    struct Provider {
        SingleThreadWeakPtr<RenderLayer> providerLayer;
        SingleThreadWeakListHashSet<RenderLayer> sharingLayers;
        LayoutRect absoluteBounds;
    };

    auto& backingProviderCandidates() { return m_backingProviderCandidates; }

    const RenderLayer* firstProviderCandidateLayer() const
    {
        return !m_backingProviderCandidates.isEmpty() ? m_backingProviderCandidates.first().providerLayer.get() : nullptr;
    }

    RenderLayer* backingSharingStackingContext() const { return m_backingSharingStackingContext; }

    Provider* backingProviderCandidateForLayer(const RenderLayer&, const RenderLayerCompositor&, LayerOverlapMap&, OverlapExtent&);
    Provider* existingBackingProviderCandidateForLayer(const RenderLayer&);
    Provider* backingProviderForLayer(const RenderLayer&);

    // Add a layer that would repaint into a layer in m_backingSharingLayers.
    // That repaint has to wait until we've set the provider's backing-sharing layers.
    void addLayerNeedingRepaint(RenderLayer& layer)
    {
        m_layersPendingRepaint.add(layer);
    }

    void addBackingSharingCandidate(RenderLayer& candidateLayer, LayoutRect candidateAbsoluteBounds, RenderLayer& candidateStackingContext, const std::optional<BackingSharingSnapshot>&);
    bool isAdditionalProviderCandidate(RenderLayer&, LayoutRect candidateAbsoluteBounds, RenderLayer* stackingContextAncestor) const;
    void startBackingSharingSequence(RenderLayer& candidateLayer, LayoutRect candidateAbsoluteBounds, RenderLayer& candidateStackingContext);
    void endBackingSharingSequence(RenderLayer&);

    std::optional<BackingSharingSnapshot> snapshot() const
    {
        if (!m_backingSharingStackingContext)
            return std::nullopt;
        return BackingSharingSnapshot { m_sequenceIdentifier, m_backingProviderCandidates.size() };
    }
    BackingSharingSequenceIdentifier sequenceIdentifier() const { return m_sequenceIdentifier; }

private:
    void layerWillBeComposited(RenderLayer&);

    void issuePendingRepaints();

    Vector<Provider> m_backingProviderCandidates;
    RenderLayer* m_backingSharingStackingContext { nullptr };
    BackingSharingSequenceIdentifier m_sequenceIdentifier { BackingSharingSequenceIdentifier::generate() };
    SingleThreadWeakHashSet<RenderLayer> m_layersPendingRepaint;
    bool m_allowOverlappingProviders { false };
};

WTF::TextStream& operator<<(WTF::TextStream&, const RenderLayerCompositor::BackingSharingState::Provider&);

void RenderLayerCompositor::BackingSharingState::startBackingSharingSequence(RenderLayer& candidateLayer, LayoutRect candidateAbsoluteBounds, RenderLayer& candidateStackingContext)
{
    ASSERT(!m_backingSharingStackingContext);
    ASSERT(m_backingProviderCandidates.isEmpty());
    m_backingProviderCandidates.append({ &candidateLayer, { }, candidateAbsoluteBounds });
    m_backingSharingStackingContext = &candidateStackingContext;
}

void RenderLayerCompositor::BackingSharingState::addBackingSharingCandidate(RenderLayer& candidateLayer, LayoutRect candidateAbsoluteBounds, RenderLayer& candidateStackingContext, const std::optional<BackingSharingSnapshot>& backingSharingSnapshot)
{
    ASSERT_UNUSED(candidateStackingContext, m_backingSharingStackingContext == &candidateStackingContext);
    ASSERT(!m_backingProviderCandidates.containsIf([&](auto& candidate) { return candidate.providerLayer == &candidateLayer; }));

    // Inserts candidateLayer into the provider list in z-order, using the state snapshot that
    // was taken before any descendant layers were traversed.

    if (!backingSharingSnapshot || m_sequenceIdentifier != backingSharingSnapshot->sequenceIdentifier) {
        // If a new sharing sequence has been started since the snapshot was taken, then this candidate
        // will be before any of the current ones in z-order (which must have been added by descendants of this layer).
        m_backingProviderCandidates.insert(0, { &candidateLayer, { }, candidateAbsoluteBounds });
    } else
        // Otherwise insert it at the position captured in the snapshot
        m_backingProviderCandidates.insert(backingSharingSnapshot->providerCount, { &candidateLayer, { }, candidateAbsoluteBounds });
}

void RenderLayerCompositor::BackingSharingState::endBackingSharingSequence(RenderLayer& endLayer)
{
    ASSERT(m_backingSharingStackingContext);

    auto candidates = std::exchange(m_backingProviderCandidates, { });

    for (auto& candidate : candidates) {
        candidate.sharingLayers.remove(endLayer);
        candidate.providerLayer->backing()->setBackingSharingLayers(WTFMove(candidate.sharingLayers));
    }
    m_backingSharingStackingContext = nullptr;
    m_sequenceIdentifier = BackingSharingSequenceIdentifier::generate();

    issuePendingRepaints();
}

auto RenderLayerCompositor::BackingSharingState::backingProviderCandidateForLayer(const RenderLayer& layer, const RenderLayerCompositor& compositor, LayerOverlapMap& overlapMap, OverlapExtent& overlap) -> Provider*
{
    if (layer.hasReflection())
        return nullptr;

    if (!m_allowOverlappingProviders) {
        for (auto& candidate : m_backingProviderCandidates) {
            auto& providerLayer = *candidate.providerLayer;
            if (layer.ancestorLayerIsInContainingBlockChain(providerLayer))
                return &candidate;
        }

        return nullptr;
    }

    if (m_backingProviderCandidates.isEmpty())
        return nullptr;

    LOG_WITH_STREAM(Compositing, stream << "Looking for backing provider candidate for " << &layer);

    // First, find the frontmost provider that is an ancestor in the containing block chain.
    auto candidateIndex = m_backingProviderCandidates.reverseFindIf([&](auto& provider) {
        auto& providerLayer = *provider.providerLayer;

        if (&layer == &providerLayer) {
            LOG_WITH_STREAM(Compositing, stream << "Rejected subject layer " << &providerLayer);
            return false;
        }

        if (!layer.ancestorLayerIsInContainingBlockChain(providerLayer)) {
            LOG_WITH_STREAM(Compositing, stream << "Rejected non-containing block ancestor " << &providerLayer);
            return false;
        }

        LOG_WITH_STREAM(Compositing, stream << "Found candidate " << &providerLayer);
        return true;
    });

    if (candidateIndex == notFound)
        return nullptr;

    auto& candidate = m_backingProviderCandidates[candidateIndex];

    // Only allow adding to providers that clip their descendants, unless there's only a single provider.
    // Unclipped providers in-front are tracked for overlap testing only.
    // FIXME: We could accumulate the union of the overlap bounds for a provider and its sharing layers to avoid this restriction.
    if (m_backingProviderCandidates.size() > 1 && !candidate.providerLayer->canUseCompositedScrolling())
        return nullptr;

    if (candidateIndex == m_backingProviderCandidates.size() - 1) {
        // No other provider is in front of the candidate, so no need to check for overlap.
        return &candidate;
    }

    auto& providerLayer = *candidate.providerLayer;
    LayoutRect overlapBounds = candidate.absoluteBounds;
    if (CheckedPtr scrollableArea = providerLayer.scrollableArea(); scrollableArea && providerLayer.canUseCompositedScrolling() && scrollableArea->hasScrollableHorizontalOverflow() != scrollableArea->hasScrollableVerticalOverflow()) {
        // If the provider uses composited scrolling but only supports scrolling
        // in one axis, we can use the clipped overlap bounds in the other axis,
        // when checking for overlap.
        auto clippedOverlapBounds = compositor.computeClippedOverlapBounds(overlapMap, layer, overlap);
        LOG_WITH_STREAM(Compositing, stream << "Candidate provider supports composited scrolling in a single axis; using layer bounds in opposite axis: clippedOverlapBounds(" << clippedOverlapBounds << ")");
        if (scrollableArea->hasScrollableHorizontalOverflow()) {
            overlapBounds.setY(clippedOverlapBounds.y());
            overlapBounds.setHeight(clippedOverlapBounds.height());
        } else {
            overlapBounds.setX(clippedOverlapBounds.x());
            overlapBounds.setWidth(clippedOverlapBounds.width());
        }
    }

    LOG_WITH_STREAM(Compositing, stream << "Provider: composited scroll(" << providerLayer.canUseCompositedScrolling() << ") scrollableArea(" << providerLayer.scrollableArea() << ") horizontalOverflow(" << (providerLayer.scrollableArea() && providerLayer.scrollableArea()->hasScrollableHorizontalOverflow()) << ") verticalOverflow(" << (providerLayer.scrollableArea() && providerLayer.scrollableArea()->hasScrollableVerticalOverflow()) << ")" << " overlapBounds(" << overlapBounds << ")");

    // Check if any of the other candidates that are in front of the selected provider will
    // overlap the bounds of the layer to be added.
    for (auto& provider : m_backingProviderCandidates.subspan(candidateIndex + 1)) {
        LOG_WITH_STREAM(Compositing, stream << "Considering " << provider.providerLayer  << " with bounds " << provider.absoluteBounds);
        if (overlapBounds.intersects(provider.absoluteBounds)) {
            LOG_WITH_STREAM(Compositing, stream << "Aborting due to overlap");
            return nullptr;
        }
    }

    return &candidate;
}

auto RenderLayerCompositor::BackingSharingState::existingBackingProviderCandidateForLayer(const RenderLayer& layer) -> Provider*
{
    ASSERT(layer.paintsIntoProvidedBacking());
    for (auto& candidate : m_backingProviderCandidates) {
        if (layer.backingProviderLayer() == candidate.providerLayer.get())
            return &candidate;
    }
    return nullptr;
}

auto RenderLayerCompositor::BackingSharingState::backingProviderForLayer(const RenderLayer& layer) -> Provider*
{
    for (auto& candidate : m_backingProviderCandidates) {
        if (candidate.sharingLayers.contains(layer))
            return &candidate;
    }

    return nullptr;
}

bool RenderLayerCompositor::BackingSharingState::isAdditionalProviderCandidate(RenderLayer& candidateLayer, LayoutRect candidateAbsoluteBounds, RenderLayer* stackingContextAncestor) const
{
    ASSERT(!m_backingProviderCandidates.isEmpty());
    if (!stackingContextAncestor || stackingContextAncestor != m_backingSharingStackingContext)
        return false;

    if (!m_allowOverlappingProviders) {
        // Only allow multiple providers for overflow scroll, which we know clips its descendants.
        if (!(m_backingProviderCandidates[0].providerLayer->canUseCompositedScrolling() && candidateLayer.canUseCompositedScrolling()))
            return false;

        // Disallow overlap between backing providers.
        for (auto& candidate : m_backingProviderCandidates) {
            if (candidateAbsoluteBounds.intersects(candidate.absoluteBounds))
                return false;
        }
        return true;
    }

    if (!m_backingProviderCandidates[0].providerLayer->canUseCompositedScrolling())
        return false;

    if (m_backingProviderCandidates.size() >= 10)
        return false;
    return true;
}

void RenderLayerCompositor::BackingSharingState::issuePendingRepaints()
{
    for (auto& layer : m_layersPendingRepaint) {
        LOG_WITH_STREAM(Compositing, stream << "Issuing postponed repaint of layer " << &layer);
        layer.compositingStatusChanged(LayoutUpToDate::Yes);
        layer.compositor().repaintOnCompositingChange(layer, layer.repaintContainer());
    }

    m_layersPendingRepaint.clear();
}

#if !LOG_DISABLED || ENABLE(TREE_DEBUGGING)
static inline bool compositingLogEnabled()
{
    return LogCompositing.state == WTFLogChannelState::On;
}

static inline bool layersLogEnabled()
{
    return LogLayers.state == WTFLogChannelState::On;
}
#endif

static constexpr Seconds conservativeCompositingPolicyHysteresisDuration { 2_s };

RenderLayerCompositor::RenderLayerCompositor(RenderView& renderView)
    : m_renderView(renderView)
    , m_updateCompositingLayersTimer(*this, &RenderLayerCompositor::updateCompositingLayersTimerFired)
    , m_updateRenderingTimer(*this, &RenderLayerCompositor::scheduleRenderingUpdate)
    , m_compositingPolicyHysteresis([](PAL::HysteresisState) { }, conservativeCompositingPolicyHysteresisDuration)
{
#if PLATFORM(IOS_FAMILY)
    if (m_renderView.frameView().platformWidget())
        m_legacyScrollingLayerCoordinator = makeUnique<LegacyWebKitScrollingLayerCoordinator>(page().chrome().client(), isRootFrameCompositor());
#endif
}

RenderLayerCompositor::~RenderLayerCompositor()
{
    // Take care that the owned GraphicsLayers are deleted first as their destructors may call back here.
    GraphicsLayer::unparentAndClear(m_rootContentsLayer);
    
    GraphicsLayer::unparentAndClear(m_clipLayer);
    GraphicsLayer::unparentAndClear(m_scrollContainerLayer);
    GraphicsLayer::unparentAndClear(m_scrolledContentsLayer);

    GraphicsLayer::unparentAndClear(m_overflowControlsHostLayer);

    GraphicsLayer::unparentAndClear(m_layerForHorizontalScrollbar);
    GraphicsLayer::unparentAndClear(m_layerForVerticalScrollbar);
    GraphicsLayer::unparentAndClear(m_layerForScrollCorner);

#if HAVE(RUBBER_BANDING)
    GraphicsLayer::unparentAndClear(m_layerForOverhangAreas);
    GraphicsLayer::unparentAndClear(m_contentShadowLayer);
    GraphicsLayer::unparentAndClear(m_layerForTopOverhangColorExtension);
    GraphicsLayer::unparentAndClear(m_layerForTopOverhangImage);
    GraphicsLayer::unparentAndClear(m_layerForBottomOverhangArea);
    GraphicsLayer::unparentAndClear(m_layerForHeader);
    GraphicsLayer::unparentAndClear(m_layerForFooter);
#endif

    ASSERT(m_rootLayerAttachment == RootLayerUnattached);
}

void RenderLayerCompositor::enableCompositingMode(bool enable /* = true */)
{
    if (enable != m_compositing) {
        m_compositing = enable;
        
        if (m_compositing) {
            ensureRootLayer();
            notifyIFramesOfCompositingChange();
        } else
            destroyRootLayer();
        
        
        m_renderView.layer()->setNeedsPostLayoutCompositingUpdate();
    }
}

void RenderLayerCompositor::cacheAcceleratedCompositingFlags()
{
    Ref settings = m_renderView.settings();
    bool hasAcceleratedCompositing = settings->acceleratedCompositingEnabled();

    // We allow the chrome to override the settings, in case the page is rendered
    // on a chrome that doesn't allow accelerated compositing.
    if (hasAcceleratedCompositing) {
        m_compositingTriggers = page().chrome().client().allowedCompositingTriggers();
        hasAcceleratedCompositing = m_compositingTriggers;
    }

    bool showDebugBorders = settings->showDebugBorders();
    bool showRepaintCounter = settings->showRepaintCounter();
    bool acceleratedDrawingEnabled = settings->acceleratedDrawingEnabled();

    // forceCompositingMode for subframes can only be computed after layout.
    bool forceCompositingMode = m_forceCompositingMode;
    if (isRootFrameCompositor())
        forceCompositingMode = m_renderView.settings().forceCompositingMode() && hasAcceleratedCompositing; 
    
    if (hasAcceleratedCompositing != m_hasAcceleratedCompositing || showDebugBorders != m_showDebugBorders || showRepaintCounter != m_showRepaintCounter || forceCompositingMode != m_forceCompositingMode) {
        if (auto* rootLayer = m_renderView.layer()) {
            rootLayer->setNeedsCompositingConfigurationUpdate();
            rootLayer->setDescendantsNeedUpdateBackingAndHierarchyTraversal();
        }
    }

    bool debugBordersChanged = m_showDebugBorders != showDebugBorders;
    m_hasAcceleratedCompositing = hasAcceleratedCompositing;
    m_forceCompositingMode = forceCompositingMode;
    m_showDebugBorders = showDebugBorders;
    m_showRepaintCounter = showRepaintCounter;
    m_acceleratedDrawingEnabled = acceleratedDrawingEnabled;
    
    if (debugBordersChanged) {
        if (m_layerForHorizontalScrollbar)
            m_layerForHorizontalScrollbar->setShowDebugBorder(m_showDebugBorders);

        if (m_layerForVerticalScrollbar)
            m_layerForVerticalScrollbar->setShowDebugBorder(m_showDebugBorders);

        if (m_layerForScrollCorner)
            m_layerForScrollCorner->setShowDebugBorder(m_showDebugBorders);
    }
    
    if (updateCompositingPolicy())
        rootRenderLayer().setDescendantsNeedCompositingRequirementsTraversal();
}

void RenderLayerCompositor::cacheAcceleratedCompositingFlagsAfterLayout()
{
    cacheAcceleratedCompositingFlags();

    if (isRootFrameCompositor())
        return;

    auto frameContentRequiresCompositing = [&] {
        RequiresCompositingData queryData;
        if (requiresCompositingForScrollableFrame(queryData))
            return true;

#if HAVE(SUPPORT_HDR_DISPLAY)
        if (m_renderView.document().hasHDRContent())
            return true;
#endif

        return false;
    };

    bool forceCompositingMode = m_hasAcceleratedCompositing && m_renderView.settings().forceCompositingMode() && frameContentRequiresCompositing();
    if (forceCompositingMode != m_forceCompositingMode) {
        m_forceCompositingMode = forceCompositingMode;
        rootRenderLayer().setDescendantsNeedCompositingRequirementsTraversal();
    }
}

bool RenderLayerCompositor::updateCompositingPolicy()
{
    if (!usesCompositing())
        return false;

    auto currentPolicy = m_compositingPolicy;
    if (page().compositingPolicyOverride()) {
        m_compositingPolicy = page().compositingPolicyOverride().value();
        return m_compositingPolicy != currentPolicy;
    }

    if (!canUpdateCompositingPolicy())
        return false;

    const auto isCurrentlyUnderMemoryPressureOrWarning = [] {
        return MemoryPressureHandler::singleton().isUnderMemoryPressure() || MemoryPressureHandler::singleton().isUnderMemoryWarning();
    };

    static auto cachedMemoryPolicy = WTF::MemoryUsagePolicy::Unrestricted;
    bool nowUnderMemoryPressure = isCurrentlyUnderMemoryPressureOrWarning();
    static bool cachedIsUnderMemoryPressureOrWarning = nowUnderMemoryPressure;

    if (cachedIsUnderMemoryPressureOrWarning != nowUnderMemoryPressure) {
        cachedMemoryPolicy = MemoryPressureHandler::singleton().currentMemoryUsagePolicy();
        cachedIsUnderMemoryPressureOrWarning = nowUnderMemoryPressure;
    }

    m_compositingPolicy = cachedMemoryPolicy == WTF::MemoryUsagePolicy::Unrestricted ? CompositingPolicy::Normal : CompositingPolicy::Conservative;

    bool didChangePolicy = currentPolicy != m_compositingPolicy;
    if (didChangePolicy && m_compositingPolicy == CompositingPolicy::Conservative)
        m_compositingPolicyHysteresis.impulse();

    return didChangePolicy;
}

bool RenderLayerCompositor::canUpdateCompositingPolicy() const
{
    return m_compositingPolicyHysteresis.state() == PAL::HysteresisState::Stopped;
}

bool RenderLayerCompositor::canRender3DTransforms() const
{
    return hasAcceleratedCompositing() && (m_compositingTriggers & ChromeClient::ThreeDTransformTrigger);
}

void RenderLayerCompositor::willRecalcStyle()
{
    cacheAcceleratedCompositingFlags();
}

bool RenderLayerCompositor::didRecalcStyleWithNoPendingLayout()
{
    return updateCompositingLayers(CompositingUpdateType::AfterStyleChange);
}

void RenderLayerCompositor::customPositionForVisibleRectComputation(const GraphicsLayer* graphicsLayer, FloatPoint& position) const
{
    if (graphicsLayer != m_scrolledContentsLayer.get())
        return;

    FloatPoint scrollPosition = -position;
    Ref frameView = m_renderView.frameView();

    if (frameView->scrollBehaviorForFixedElements() == ScrollBehaviorForFixedElements::StickToDocumentBounds)
        scrollPosition = frameView->constrainScrollPositionForOverhang(roundedIntPoint(scrollPosition));

    position = -scrollPosition;
}

bool RenderLayerCompositor::shouldDumpPropertyForLayer(const GraphicsLayer* layer, ASCIILiteral propertyName, OptionSet<LayerTreeAsTextOptions>) const
{
    if (propertyName == "anchorPoint"_s)
        return layer->anchorPoint() != FloatPoint3D(0.5f, 0.5f, 0);

    return true;
}

bool RenderLayerCompositor::backdropRootIsOpaque(const GraphicsLayer* layer) const
{
    if (layer != rootGraphicsLayer())
        return false;

    return !viewHasTransparentBackground();
}

void RenderLayerCompositor::notifyFlushRequired(const GraphicsLayer*)
{
    scheduleRenderingUpdate();
}

void RenderLayerCompositor::scheduleRenderingUpdate()
{
    ASSERT(!m_flushingLayers);

    protectedPage()->scheduleRenderingUpdate(RenderingUpdateStep::LayerFlush);
}

static inline ScrollableArea::VisibleContentRectIncludesScrollbars scrollbarInclusionForVisibleRect()
{
#if USE(COORDINATED_GRAPHICS)
    return ScrollableArea::VisibleContentRectIncludesScrollbars::Yes;
#else
    return ScrollableArea::VisibleContentRectIncludesScrollbars::No;
#endif
}

FloatRect RenderLayerCompositor::visibleRectForLayerFlushing() const
{
    const Ref frameView = m_renderView.frameView();
#if PLATFORM(IOS_FAMILY)
    return frameView->exposedContentRect();
#else

    // Having a m_scrolledContentsLayer indicates that we're doing scrolling via GraphicsLayers.
    FloatRect visibleRect = m_scrolledContentsLayer ? FloatRect({ }, frameView->sizeForVisibleContent(scrollbarInclusionForVisibleRect())) : frameView->visibleContentRect();

    if (auto exposedRect = frameView->viewExposedRect())
        visibleRect.intersect(*exposedRect);

    return visibleRect;
#endif
}

void RenderLayerCompositor::flushPendingLayerChanges(bool isFlushRoot)
{
    // LocalFrameView::flushCompositingStateIncludingSubframes() flushes each subframe,
    // but GraphicsLayer::flushCompositingState() will cross frame boundaries
    // if the GraphicsLayers are connected (the RootLayerAttachedViaEnclosingFrame case).
    // As long as we're not the root of the flush, we can bail.
    if (!isFlushRoot && rootLayerAttachment() == RootLayerAttachedViaEnclosingFrame)
        return;

    if (rootLayerAttachment() == RootLayerUnattached) {
        m_shouldFlushOnReattach = true;
        return;
    }

    ASSERT(!m_flushingLayers);
    {
        SetForScope flushingLayersScope(m_flushingLayers, true);

        if (RefPtr rootLayer = rootGraphicsLayer()) {
#if ENABLE(ASYNC_SCROLLING) && ENABLE(SCROLLING_THREAD)
            LayerTreeHitTestLocker layerLocker(scrollingCoordinator());
#endif
            FloatRect visibleRect = visibleRectForLayerFlushing();
            LOG_WITH_STREAM(Compositing,  stream << "\nRenderLayerCompositor " << this << " flushPendingLayerChanges (is root " << isFlushRoot << ") visible rect " << visibleRect);
            rootLayer->flushCompositingState(visibleRect);
        }
        
        ASSERT(m_flushingLayers);

#if ENABLE(TREE_DEBUGGING)
        if (layersLogEnabled()) {
            LOG(Layers, "RenderLayerCompositor::flushPendingLayerChanges");
            showGraphicsLayerTree(rootGraphicsLayer());
        }
#endif
    }

#if PLATFORM(IOS_FAMILY)
    updateScrollCoordinatedLayersAfterFlushIncludingSubframes();

    if (isFlushRoot)
        page().chrome().client().didFlushCompositingLayers();
#endif

    ++m_layerFlushCount;
}

void RenderLayerCompositor::setRenderingIsSuppressed(bool suppressed)
{
    if (auto* rootLayer = rootGraphicsLayer())
        rootLayer->setRenderingIsSuppressedIncludingDescendants(suppressed);
}

#if PLATFORM(IOS_FAMILY)
void RenderLayerCompositor::updateScrollCoordinatedLayersAfterFlushIncludingSubframes()
{
    updateScrollCoordinatedLayersAfterFlush();

    auto& frame = m_renderView.frameView().frame();
    for (auto* subframe = frame.tree().firstChild(); subframe; subframe = subframe->tree().traverseNext(&frame)) {
        auto* localFrame = dynamicDowncast<LocalFrame>(subframe);
        if (!localFrame)
            continue;
        auto* view = localFrame->contentRenderer();
        if (!view)
            continue;

        view->compositor().updateScrollCoordinatedLayersAfterFlush();
    }
}

void RenderLayerCompositor::updateScrollCoordinatedLayersAfterFlush()
{
    if (m_legacyScrollingLayerCoordinator) {
        m_legacyScrollingLayerCoordinator->registerAllViewportConstrainedLayers(*this);
        m_legacyScrollingLayerCoordinator->registerAllScrollingLayers();
    }
}
#endif

void RenderLayerCompositor::didChangePlatformLayerForLayer(RenderLayer& layer, const GraphicsLayer*)
{
    RefPtr scrollingCoordinator = this->scrollingCoordinator();
    if (!scrollingCoordinator)
        return;

    auto* backing = layer.backing();
    if (auto nodeID = backing->scrollingNodeIDForRole(ScrollCoordinationRole::Scrolling))
        updateScrollingNodeLayers(*nodeID, layer, *scrollingCoordinator);

    if (auto* clippingStack = layer.backing()->ancestorClippingStack())
        clippingStack->updateScrollingNodeLayers(*scrollingCoordinator);

    if (auto nodeID = backing->scrollingNodeIDForRole(ScrollCoordinationRole::ViewportConstrained)) {
        scrollingCoordinator->setNodeLayers(*nodeID, {
            .layer = backing->viewportClippingOrAnchorLayer(),
            .viewportAnchorLayer = backing->viewportAnchorLayer()
        });
    }

    if (auto nodeID = backing->scrollingNodeIDForRole(ScrollCoordinationRole::FrameHosting))
        scrollingCoordinator->setNodeLayers(*nodeID, { backing->graphicsLayer() });

    if (auto nodeID = backing->scrollingNodeIDForRole(ScrollCoordinationRole::Positioning))
        scrollingCoordinator->setNodeLayers(*nodeID, { backing->graphicsLayer() });
}

void RenderLayerCompositor::didPaintBacking(RenderLayerBacking*)
{
    Ref frameView = m_renderView.frameView();
    frameView->setLastPaintTime(MonotonicTime::now());
    if (frameView->milestonesPendingPaint())
        frameView->firePaintRelatedMilestonesIfNeeded();
}

void RenderLayerCompositor::didChangeVisibleRect()
{
    RefPtr rootLayer = rootGraphicsLayer();
    if (!rootLayer)
        return;

    FloatRect visibleRect = visibleRectForLayerFlushing();
    bool requiresFlush = rootLayer->visibleRectChangeRequiresFlush(visibleRect);
    LOG_WITH_STREAM(Compositing, stream << "RenderLayerCompositor::didChangeVisibleRect " << visibleRect << " requiresFlush " << requiresFlush);
    if (requiresFlush)
        scheduleRenderingUpdate();
}

void RenderLayerCompositor::notifySubsequentFlushRequired(const GraphicsLayer*)
{
    if (!m_updateRenderingTimer.isActive())
        m_updateRenderingTimer.startOneShot(0_s);
}

void RenderLayerCompositor::layerTiledBackingUsageChanged(const GraphicsLayer* graphicsLayer, bool usingTiledBacking)
{
    if (usingTiledBacking) {
        ++m_layersWithTiledBackingCount;
        graphicsLayer->tiledBacking()->setIsInWindow(page().isInWindow());
    } else {
        ASSERT(m_layersWithTiledBackingCount > 0);
        --m_layersWithTiledBackingCount;
    }
}

void RenderLayerCompositor::scheduleCompositingLayerUpdate()
{
    if (!m_updateCompositingLayersTimer.isActive())
        m_updateCompositingLayersTimer.startOneShot(0_s);
}

void RenderLayerCompositor::updateCompositingLayersTimerFired()
{
    updateCompositingLayers(CompositingUpdateType::AfterLayout);
}

void RenderLayerCompositor::cancelCompositingLayerUpdate()
{
    m_updateCompositingLayersTimer.stop();
}

template<typename ApplyFunctionType>
void RenderLayerCompositor::applyToCompositedLayerIncludingDescendants(RenderLayer& layer, const ApplyFunctionType& function)
{
    if (layer.isComposited())
        function(layer);
    for (auto* childLayer = layer.firstChild(); childLayer; childLayer = childLayer->nextSibling())
        applyToCompositedLayerIncludingDescendants(*childLayer, function);
}

void RenderLayerCompositor::updateEventRegionsRecursive(RenderLayer& layer)
{
#if ENABLE(ASYNC_SCROLLING)
    if (layer.isComposited())
        layer.backing()->updateEventRegion();

    if (!layer.hasDescendantNeedingEventRegionUpdate())
        return;

    for (auto* childLayer = layer.firstChild(); childLayer; childLayer = childLayer->nextSibling())
        updateEventRegionsRecursive(*childLayer);

    layer.clearHasDescendantNeedingEventRegionUpdate();
#else
    UNUSED_PARAM(layer);
#endif
}

void RenderLayerCompositor::updateEventRegions()
{
    updateEventRegionsRecursive(*m_renderView.layer());
    m_renderView.setNeedsEventRegionUpdateForNonCompositedFrame(false);
}

static std::optional<ScrollingNodeID> frameHostingNodeForFrame(LocalFrame& frame)
{
    if (!frame.document() || !frame.view())
        return { };

    // Find the frame's enclosing layer in our render tree.
    RefPtr ownerElement = frame.protectedDocument()->ownerElement();
    if (!ownerElement)
        return { };

    RefPtr widgetRenderer = dynamicDowncast<RenderWidget>(ownerElement->renderer());
    if (!widgetRenderer)
        return { };

    if (!widgetRenderer->hasLayer() || !widgetRenderer->layer()->isComposited()) {
        LOG(Scrolling, "frameHostingNodeForFrame: frame renderer has no layer or is not composited.");
        return { };
    }

    if (auto frameHostingNodeID = widgetRenderer->layer()->backing()->scrollingNodeIDForRole(ScrollCoordinationRole::FrameHosting))
        return frameHostingNodeID;

    return { };
}

// Returns true on a successful update.
bool RenderLayerCompositor::updateCompositingLayers(CompositingUpdateType updateType, RenderLayer* updateRoot)
{
    LOG_WITH_STREAM(Compositing, stream << "RenderLayerCompositor " << this << " [" << m_renderView.frameView() << "] updateCompositingLayers " << updateType << " contentLayersCount " << m_contentLayersCount);

    TraceScope tracingScope(CompositingUpdateStart, CompositingUpdateEnd);

#if ENABLE(TREE_DEBUGGING)
    if (compositingLogEnabled())
        showPaintOrderTree(m_renderView.layer());
    rootRenderLayer().updateLayerPositionsAfterLayout(false, false);
#endif

    if (updateType == CompositingUpdateType::AfterStyleChange || updateType == CompositingUpdateType::AfterLayout)
        cacheAcceleratedCompositingFlagsAfterLayout(); // Some flags (e.g. forceCompositingMode) depend on layout.

    m_updateCompositingLayersTimer.stop();

    ASSERT(m_renderView.document().backForwardCacheState() == Document::NotInBackForwardCache
        || m_renderView.document().backForwardCacheState() == Document::AboutToEnterBackForwardCache);
    
    // Compositing layers will be updated in Document::setVisualUpdatesAllowed(bool) if suppressed here.
    if (!m_renderView.document().visualUpdatesAllowed())
        return false;

    // Avoid updating the layers with old values. Compositing layers will be updated after the layout is finished.
    // This happens when m_updateCompositingLayersTimer fires before layout is updated.
    if (m_renderView.needsLayout()) {
        LOG_WITH_STREAM(Compositing, stream << "RenderLayerCompositor " << this << " updateCompositingLayers " << updateType << " - m_renderView.needsLayout, bailing ");
        return false;
    }

    if (!m_compositing && (m_forceCompositingMode || (isRootFrameCompositor() && page().pageOverlayController().overlayCount())))
        enableCompositingMode(true);

    bool isPageScroll = !updateRoot || updateRoot == &rootRenderLayer();
    updateRoot = &rootRenderLayer();

    if (updateType == CompositingUpdateType::OnScroll || updateType == CompositingUpdateType::OnCompositedScroll) {
        // We only get here if we didn't scroll on the scrolling thread, so this update needs to re-position viewport-constrained layers.
        if (m_renderView.settings().acceleratedCompositingForFixedPositionEnabled() && isPageScroll) {
            if (auto* viewportConstrainedObjects = m_renderView.frameView().viewportConstrainedObjects()) {
                for (auto& renderer : *viewportConstrainedObjects) {
                    if (auto* layer = renderer.layer())
                        layer->setNeedsCompositingGeometryUpdate();
                }
            }
        }

        // Scrolling can affect overlap. FIXME: avoid for page scrolling.
        updateRoot->setDescendantsNeedCompositingRequirementsTraversal();
    }

    if (updateType == CompositingUpdateType::AfterLayout) {
        // Ensure that post-layout updates push new scroll position and viewport rects onto the root node.
        rootRenderLayer().setNeedsScrollingTreeUpdate();
    }

    if (!updateRoot->hasDescendantNeedingCompositingRequirementsTraversal() && !m_compositing) {
        LOG_WITH_STREAM(Compositing, stream << " no compositing work to do");
        return true;
    }

    if (!updateRoot->needsAnyCompositingTraversal()) {
        LOG_WITH_STREAM(Compositing, stream << " updateRoot has no dirty child and doesn't need update");
        return true;
    }

    ++m_compositingUpdateCount;

#if !LOG_DISABLED
    MonotonicTime startTime;
    if (compositingLogEnabled()) {
        ++m_rootLayerUpdateCount;
        startTime = MonotonicTime::now();
    }

    if (compositingLogEnabled()) {
        m_obligateCompositedLayerCount = 0;
        m_secondaryCompositedLayerCount = 0;
        m_obligatoryBackingStoreBytes = 0;
        m_secondaryBackingStoreBytes = 0;

        auto& frame = m_renderView.frameView().frame();
        bool isRootFrame = isRootFrameCompositor();
        LOG_WITH_STREAM(Compositing, stream << "\nUpdate " << m_rootLayerUpdateCount << " of " << (isRootFrame ? "root frame"_s : makeString("frame "_s, frame.frameID().toUInt64())) << " - compositing policy is " << m_compositingPolicy);
    }
#endif

    // FIXME: optimize root-only update.
    if (updateRoot->hasDescendantNeedingCompositingRequirementsTraversal() || updateRoot->needsCompositingRequirementsTraversal()) {
        auto& rootLayer = rootRenderLayer();
        CompositingState compositingState(updateRoot);
        BackingSharingState backingSharingState(m_renderView.settings().overlappingBackingStoreProvidersEnabled());
        LayerOverlapMap overlapMap(rootLayer);

        computeCompositingRequirements(nullptr, rootLayer, overlapMap, compositingState, backingSharingState);
    }

    LOG(Compositing, "\nRenderLayerCompositor::updateCompositingLayers - mid");
#if ENABLE(TREE_DEBUGGING)
    if (compositingLogEnabled())
        showPaintOrderTree(m_renderView.layer());
    updateRoot->updateLayerPositionsAfterLayout(false, false);
#endif

    if (updateRoot->hasDescendantNeedingUpdateBackingOrHierarchyTraversal() || updateRoot->needsUpdateBackingOrHierarchyTraversal()) {
        ASSERT(m_layersWithUnresolvedRelations.isEmptyIgnoringNullReferences());
        ScrollingTreeState scrollingTreeState;
        scrollingTreeState.hasParent = true;

        if (!m_renderView.frame().isMainFrame()) {
            scrollingTreeState.parentNodeID = frameHostingNodeForFrame(m_renderView.protectedFrame());
            scrollingTreeState.hasParent = !!scrollingTreeState.parentNodeID;
        }

        RefPtr scrollingCoordinator = this->scrollingCoordinator();
        bool hadSubscrollers = scrollingCoordinator ? scrollingCoordinator->hasSubscrollers(m_renderView.frame().rootFrame().frameID()) : false;

        UpdateBackingTraversalState traversalState;
        Vector<Ref<GraphicsLayer>> childList;
        updateBackingAndHierarchy(*updateRoot, childList, traversalState, scrollingTreeState);

        if (scrollingTreeState.needSynchronousScrollingReasonsUpdate)
            updateSynchronousScrollingNodes();

        // Host the document layer in the RenderView's root layer.
        appendDocumentOverlayLayers(childList);
        // Even when childList is empty, don't drop out of compositing mode if there are
        // composited layers that we didn't hit in our traversal (e.g. because of visibility:hidden).
        if (childList.isEmpty() && !needsCompositingForContentOrOverlays())
            destroyRootLayer();
        else if (RefPtr rootContentsLayer = m_rootContentsLayer)
            rootContentsLayer->setChildren(WTFMove(childList));

        if (scrollingCoordinator && scrollingCoordinator->hasSubscrollers(m_renderView.frame().rootFrame().frameID()) != hadSubscrollers)
            invalidateEventRegionForAllFrames();

        resolveScrollingTreeRelationships();
    }

#if !LOG_DISABLED
    if (compositingLogEnabled()) {
        MonotonicTime endTime = MonotonicTime::now();
        LOG(Compositing, "Total layers   primary   secondary   obligatory backing (KB)   secondary backing(KB)   total backing (KB)  update time (ms)\n");

        LOG(Compositing, "%8d %11d %9d %20.2f %22.2f %22.2f %18.2f\n",
            m_obligateCompositedLayerCount + m_secondaryCompositedLayerCount, m_obligateCompositedLayerCount,
            m_secondaryCompositedLayerCount, m_obligatoryBackingStoreBytes / 1024, m_secondaryBackingStoreBytes / 1024, (m_obligatoryBackingStoreBytes + m_secondaryBackingStoreBytes) / 1024, (endTime - startTime).milliseconds());
    }
#endif

    // FIXME: Only do if dirty.
    updateRootLayerPosition();

#if ENABLE(TREE_DEBUGGING)
    if (compositingLogEnabled()) {
        LOG(Compositing, "RenderLayerCompositor::updateCompositingLayers - post");
        showPaintOrderTree(m_renderView.layer());
    }
#endif

    InspectorInstrumentation::layerTreeDidChange(protectedPage().ptr());

    if (m_renderView.needsRepaintHackAfterCompositingLayerUpdateForDebugOverlaysOnly()) {
        m_renderView.repaintRootContents();
        m_renderView.setNeedsRepaintHackAfterCompositingLayerUpdateForDebugOverlaysOnly(false);
    }

    if (m_scrolledContentsLayer)
        updateOverflowControlsLayers();


#if ENABLE(TREE_DEBUGGING)
    updateRoot->updateLayerPositionsAfterLayout(false, false);
#endif
    return true;
}

// Unchanged leaf compositing layers that clip their descendants can skip descendant
// traversal, since their descendants can't contribute any new overlap to the map.
static bool canSkipComputeCompositingRequirementsForSubtree(const RenderLayer& layer, bool willBeComposited)
{
    if (layer.needsCompositingRequirementsTraversal() || layer.hasDescendantNeedingCompositingRequirementsTraversal())
        return false;

    if (!layer.isComposited() || !willBeComposited || layer.hasCompositingDescendant() || !layer.isStackingContext())
        return false;

    return layer.renderer().hasNonVisibleOverflow();
}

bool RenderLayerCompositor::allowBackingStoreDetachingForFixedPosition(RenderLayer& layer, const LayoutRect& absoluteBounds)
{
    ASSERT_UNUSED(layer, layer.behavesAsFixed());

    // We'll allow detaching if the layer is outside the layout viewport. Fixed layers inside
    // the layout viewport can be revealed by async scrolling, so we want to pin their backing store.
    Ref frameView = m_renderView.frameView();
    LayoutRect fixedLayoutRect;
    if (frameView->useFixedLayout())
        fixedLayoutRect = m_renderView.unscaledDocumentRect();
    else
        fixedLayoutRect = frameView->rectForFixedPositionLayout();

    bool allowDetaching = !fixedLayoutRect.intersects(absoluteBounds);
    LOG_WITH_STREAM(Compositing, stream << "RenderLayerCompositor (layer " << &layer << ") allowsBackingStoreDetaching - absoluteBounds " << absoluteBounds << " layoutViewportRect " << fixedLayoutRect << ", allowDetaching " << allowDetaching);
    return allowDetaching;
}

void RenderLayerCompositor::computeCompositingRequirements(RenderLayer* ancestorLayer, RenderLayer& layer, LayerOverlapMap& overlapMap, CompositingState& compositingState, BackingSharingState& backingSharingState)
{
#if !LOG_DISABLED
    unsigned treeDepth = compositingState.depth;
#else
    unsigned treeDepth = 0;
#endif

    layer.updateDescendantDependentFlags();
    layer.updateLayerListsIfNeeded();

    if (!layer.hasDescendantNeedingCompositingRequirementsTraversal()
        && !layer.needsCompositingRequirementsTraversal()
        && !compositingState.fullPaintOrderTraversalRequired
        && !compositingState.descendantsRequireCompositingUpdate) {
        traverseUnchangedSubtree(ancestorLayer, layer, overlapMap, compositingState, backingSharingState);
        return;
    }

    LOG_WITH_STREAM(Compositing, stream << TextStream::Repeat(treeDepth * 2, ' ') << &layer << " computeCompositingRequirements (backing provider candidates " << backingSharingState.backingProviderCandidates() << ")");

    // FIXME: maybe we can avoid updating all remaining layers in paint order.
    compositingState.fullPaintOrderTraversalRequired |= layer.needsCompositingRequirementsTraversal();
    compositingState.descendantsRequireCompositingUpdate |= layer.descendantsNeedCompositingRequirementsTraversal();

    // We updated compositing for direct reasons in layerStyleChanged(). Here, check for compositing that can only be evaluated after layout.
    RequiresCompositingData queryData;
    bool wasComposited = layer.isComposited();
    bool willBeComposited = wasComposited;
    bool becameCompositedAfterDescendantTraversal = false;
    IndirectCompositingReason compositingReason = compositingState.subtreeIsCompositing ? IndirectCompositingReason::Stacking : IndirectCompositingReason::None;

    if (layer.needsPostLayoutCompositingUpdate() || compositingState.fullPaintOrderTraversalRequired || compositingState.descendantsRequireCompositingUpdate) {
        layer.setIndirectCompositingReason(IndirectCompositingReason::None);
        willBeComposited = needsToBeComposited(layer, queryData);
    }

    compositingState.fullPaintOrderTraversalRequired |= layer.subsequentLayersNeedCompositingRequirementsTraversal();

    OverlapExtent layerExtent;

    // Use the fact that we're composited as a hint to check for an animating transform.
    // FIXME: Maybe needsToBeComposited() should return a bitmask of reasons, to avoid the need to recompute things.
    if (willBeComposited && !layer.isRenderViewLayer())
        layerExtent.hasTransformAnimation = isRunningTransformAnimation(layer.renderer());

    bool respectTransforms = !layerExtent.hasTransformAnimation;
    overlapMap.geometryMap().pushMappingsToAncestor(&layer, ancestorLayer, respectTransforms);

    SingleThreadWeakPtr<RenderLayer> providedBackingLayer;
    if (!willBeComposited && compositingState.subtreeIsCompositing && canBeComposited(layer)) {
        if (auto* provider = backingSharingState.backingProviderCandidateForLayer(layer, *this, overlapMap, layerExtent)) {
            provider->sharingLayers.add(layer);
            LOG_WITH_STREAM(Compositing, stream << TextStream::Repeat(treeDepth * 2, ' ') << " " << &layer << " can share with " << backingSharingState.backingProviderCandidates());
            compositingReason = IndirectCompositingReason::None;
            providedBackingLayer = provider->providerLayer;
        }
    }

    // If we know for sure the layer is going to be composited, don't bother looking it up in the overlap map
    if (!willBeComposited && !providedBackingLayer && !overlapMap.isEmpty() && compositingState.testingOverlap) {
        // If we're testing for overlap, we only need to composite if we overlap something that is already composited.
        if (layerOverlaps(overlapMap, layer, layerExtent))
            compositingReason = IndirectCompositingReason::Overlap;
        else
            compositingReason = IndirectCompositingReason::None;
    }

#if ENABLE(VIDEO)
    // Video is special. It's the only RenderLayer type that can both have
    // RenderLayer children and whose children can't use its backing to render
    // into. These children (the controls) always need to be promoted into their
    // own layers to draw on top of the accelerated video.
    if (compositingState.compositingAncestor && compositingState.compositingAncestor->renderer().isRenderVideo())
        compositingReason = IndirectCompositingReason::Overlap;
#endif

    if (compositingReason != IndirectCompositingReason::None)
        layer.setIndirectCompositingReason(compositingReason);

    // Check if the computed indirect reason will force the layer to become composited.
    if (!willBeComposited && layer.mustCompositeForIndirectReasons() && canBeComposited(layer)) {
        LOG_WITH_STREAM(Compositing, stream << TextStream::Repeat(treeDepth * 2, ' ') << "layer " << &layer << " compositing for indirect reason " << layer.indirectCompositingReason() << " (was sharing: " << !!providedBackingLayer << ")");
        willBeComposited = true;
        providedBackingLayer = nullptr;
    }

    // The children of this layer don't need to composite, unless there is
    // a compositing layer among them, so start by inheriting the compositing
    // ancestor with subtreeIsCompositing set to false.
    CompositingState currentState = compositingState.stateForPaintOrderChildren(layer);
    bool didPushOverlapContainer = false;

    auto layerWillComposite = [&] {
        // This layer is going to be composited, so children can safely ignore the fact that there's an
        // animation running behind this layer, meaning they can rely on the overlap map testing again.
        currentState.testingOverlap = true;
        // This layer now acts as the ancestor for kids.
        currentState.compositingAncestor = &layer;
        // Compositing turns off backing sharing.
        currentState.backingSharingAncestor = nullptr;

        if (providedBackingLayer) {
            providedBackingLayer = nullptr;
            // providedBackingLayer was only valid for layers that would otherwise composite because of overlap. If we can
            // no longer share, put this this indirect reason back on the layer so that requiresOwnBackingStore() sees it.
            layer.setIndirectCompositingReason(IndirectCompositingReason::Overlap);
            LOG_WITH_STREAM(Compositing, stream << TextStream::Repeat(treeDepth * 2, ' ') << "layer " << &layer << " was sharing, now will composite");
        } else {
            if (!didPushOverlapContainer) {
                overlapMap.pushCompositingContainer(layer);
                didPushOverlapContainer = true;
                LOG_WITH_STREAM(CompositingOverlap, stream << TextStream::Repeat(treeDepth * 2, ' ') << "layer " << &layer << " will composite, pushed container " << overlapMap);
            }
        }

        willBeComposited = true;
    };


    // Unless we leave the containing block chain, or have an animated transform,
    // then we can continue to use the inherited backing store attachment.
    bool allowsBackingStoreDetachingForFixed = false;
    if (currentState.ancestorAllowsBackingStoreDetachingForFixed && ancestorLayer && layer.ancestorLayerIsInContainingBlockChain(*ancestorLayer) && !layerExtent.hasTransformAnimation)
        allowsBackingStoreDetachingForFixed = true;

    auto layerWillCompositePostDescendants = [&] {
        layerWillComposite();
        currentState.subtreeIsCompositing = true;
        becameCompositedAfterDescendantTraversal = true;
        if (layer.behavesAsFixed())
            allowsBackingStoreDetachingForFixed = allowBackingStoreDetachingForFixedPosition(layer, layerExtent.bounds);
    };

    if (willBeComposited) {
        layerWillComposite();

        computeExtent(overlapMap, layer, layerExtent);
        currentState.ancestorHasTransformAnimation |= layerExtent.hasTransformAnimation;

        if (!allowsBackingStoreDetachingForFixed && layer.behavesAsFixed())
            currentState.ancestorAllowsBackingStoreDetachingForFixed = allowsBackingStoreDetachingForFixed = allowBackingStoreDetachingForFixedPosition(layer, layerExtent.bounds);

        // Too hard to compute animated bounds if both us and some ancestor is animating transform.
        layerExtent.animationCausesExtentUncertainty |= layerExtent.hasTransformAnimation && compositingState.ancestorHasTransformAnimation;
    } else if (providedBackingLayer) {
        currentState.backingSharingAncestor = &layer;
        overlapMap.pushCompositingContainer(layer);
        didPushOverlapContainer = true;
        LOG_WITH_STREAM(CompositingOverlap, stream << TextStream::Repeat(treeDepth * 2, ' ') << "layer " << &layer << " will share, pushed container " << overlapMap);
    }

    auto backingSharingSnapshot = updateBackingSharingBeforeDescendantTraversal(backingSharingState, treeDepth, overlapMap, layer, layerExtent, willBeComposited, compositingState.stackingContextAncestor);

#if ASSERT_ENABLED
    LayerListMutationDetector mutationChecker(layer);
#endif

    bool descendantsAddedToOverlap = currentState.hasNonRootCompositedAncestor();

    if (!canSkipComputeCompositingRequirementsForSubtree(layer, willBeComposited)) {
        if (layer.hasNegativeZOrderLayers()) {
            // Speculatively push this layer onto the overlap map.
            bool didSpeculativelyPushOverlapContainer = false;
            if (!didPushOverlapContainer) {
                overlapMap.pushSpeculativeCompositingContainer(layer);
                didPushOverlapContainer = true;
                didSpeculativelyPushOverlapContainer = true;
            }

            for (auto* childLayer : layer.negativeZOrderLayers()) {
                computeCompositingRequirements(&layer, *childLayer, overlapMap, currentState, backingSharingState);

                // If we have to make a layer for this child, make one now so we can have a contents layer
                // (since we need to ensure that the -ve z-order child renders underneath our contents).
                if (!willBeComposited && currentState.subtreeIsCompositing) {
                    layer.setIndirectCompositingReason(IndirectCompositingReason::BackgroundLayer);
                    layerWillComposite();
                    overlapMap.confirmSpeculativeCompositingContainer();
                }
            }

            if (didSpeculativelyPushOverlapContainer) {
                if (overlapMap.maybePopSpeculativeCompositingContainer())
                    didPushOverlapContainer = false;
                else if (!willBeComposited) {
                    layer.setIndirectCompositingReason(IndirectCompositingReason::BackgroundLayer);
                    layerWillComposite();
                }
            }
        }

        for (auto* childLayer : layer.normalFlowLayers())
            computeCompositingRequirements(&layer, *childLayer, overlapMap, currentState, backingSharingState);

        for (auto* childLayer : layer.positiveZOrderLayers())
            computeCompositingRequirements(&layer, *childLayer, overlapMap, currentState, backingSharingState);

        // Set the flag to say that this layer has compositing children.
        layer.setHasCompositingDescendant(currentState.subtreeIsCompositing);
        layer.setHasCompositedNonContainedDescendants(currentState.hasCompositedNonContainedDescendants);
    }

    // If we just entered compositing mode, the root will have become composited (as long as accelerated compositing is enabled).
    if (layer.isRenderViewLayer()) {
        if (usesCompositing() && m_hasAcceleratedCompositing)
            willBeComposited = true;
    }

    bool isolatedCompositedBlending = layer.isolatesCompositedBlending();
    layer.setHasNotIsolatedCompositedBlendingDescendants(currentState.hasNotIsolatedCompositedBlendingDescendants);
    if (layer.isolatesCompositedBlending() != isolatedCompositedBlending) {
        // isolatedCompositedBlending affects the result of clippedByAncestor().
        layer.setChildrenNeedCompositingGeometryUpdate();
    }

    ASSERT(!layer.hasNotIsolatedCompositedBlendingDescendants() || layer.hasNotIsolatedBlendingDescendants());

    bool isBackdropRoot = layer.isBackdropRoot();
    layer.setHasBackdropFilterDescendantsWithoutRoot(currentState.hasBackdropFilterDescendantsWithoutRoot);
    if (layer.isBackdropRoot() != isBackdropRoot)
        layer.setNeedsCompositingConfigurationUpdate();

    // Now check for reasons to become composited that depend on the state of descendant layers.
    if (!willBeComposited && canBeComposited(layer)) {
        layer.update3DTransformedDescendantStatus();
        auto indirectReason = computeIndirectCompositingReason(layer, currentState.subtreeIsCompositing, layer.has3DTransformedDescendant(), !!providedBackingLayer);
        if (indirectReason != IndirectCompositingReason::None) {
            layer.setIndirectCompositingReason(indirectReason);
            layerWillCompositePostDescendants();
        }
    }

    if (layer.reflectionLayer()) {
        // FIXME: Shouldn't we call computeCompositingRequirements to handle a reflection overlapping with another renderer?
        layer.reflectionLayer()->setIndirectCompositingReason(willBeComposited ? IndirectCompositingReason::Stacking : IndirectCompositingReason::None);
    }

    // If we're back at the root, and no other layers need to be composited, and the root layer itself doesn't need
    // to be composited, then we can drop out of compositing mode altogether. However, don't drop out of compositing mode
    // if there are composited layers that we didn't hit in our traversal (e.g. because of visibility:hidden).
    RequiresCompositingData rootLayerQueryData;
    if (layer.isRenderViewLayer() && !currentState.subtreeIsCompositing && !requiresCompositingLayer(layer, rootLayerQueryData) && !m_forceCompositingMode && !needsCompositingForContentOrOverlays()) {
        // Don't drop out of compositing on iOS, because we may flash. See <rdar://problem/8348337>.
#if !PLATFORM(IOS_FAMILY)
        enableCompositingMode(false);
        willBeComposited = false;
#endif
    }

    ASSERT(willBeComposited == needsToBeComposited(layer, queryData));

    // Create or destroy backing here. However, we can't update geometry because layers above us may become composited
    // during post-order traversal (e.g. for clipping).
    bool needsCompositingStatusUpdate = false;
    if (updateBacking(layer, queryData, &backingSharingState, willBeComposited ? BackingRequired::Yes : BackingRequired::No)) {
        // This layer and all of its descendants have cached repaints rects that are relative to
        // the repaint container, so change when compositing changes; we need to update them here,
        // as long as shared backing isn't going to change our repaint container.
        needsCompositingStatusUpdate = true;
    }

    // Update layer state bits.
    if (layer.reflectionLayer() && updateReflectionCompositingState(*layer.reflectionLayer(), &layer, queryData))
        layer.setNeedsCompositingLayerConnection();

    // FIXME: clarify needsCompositingPaintOrderChildrenUpdate. If a composited layer gets a new ancestor, it needs geometry computations.
    if (layer.needsCompositingPaintOrderChildrenUpdate()) {
        layer.setChildrenNeedCompositingGeometryUpdate();
        layer.setNeedsCompositingLayerConnection();
    }

    layer.clearCompositingRequirementsTraversalState();

    // Compute state passed to the caller.
    compositingState.updateWithDescendantStateAndLayer(currentState, layer, ancestorLayer, layerExtent);
    updateBackingSharingAfterDescendantTraversal(backingSharingState, treeDepth, overlapMap, layer, layerExtent, compositingState.stackingContextAncestor, backingSharingSnapshot);

    needsCompositingStatusUpdate |= layer.backingProviderLayerAtEndOfCompositingUpdate() != providedBackingLayer;

    // Update the cached repaint rects now that we've finished updating backing
    // sharing state on descendants
    if (needsCompositingStatusUpdate) {
        // Repaint in the old container before we recompute the repaint container.
        if (!wasComposited && layer.repaintContainer() && layer.repaintContainer()->isComposited())
            repaintOnCompositingChange(layer, layer.repaintContainer());

        // Compute the new repaint container, and repaint our bounds in it (unless
        // this layer is newly compositing, in which case the layer will fully repaint already).
        if (!layer.isComposited()) {
            // If this layer is going to participate in backing sharing, defer until that's
            // complete, since repaint container computation depends on all the state being
            // in-place.
            if (layerRepaintTargetsBackingSharingLayer(layer, backingSharingState))
                backingSharingState.addLayerNeedingRepaint(layer);
            else {
                layer.compositingStatusChanged(LayoutUpToDate::Yes);
                repaintOnCompositingChange(layer, layer.repaintContainer());
            }
        } else
            layer.compositingStatusChanged(LayoutUpToDate::Yes);
    }

    layer.setBackingProviderLayerAtEndOfCompositingUpdate(providedBackingLayer.get());

    bool layerContributesToOverlap = (currentState.compositingAncestor && !currentState.compositingAncestor->isRenderViewLayer()) || currentState.backingSharingAncestor;
    updateOverlapMap(overlapMap, layer, layerExtent, didPushOverlapContainer, layerContributesToOverlap, becameCompositedAfterDescendantTraversal && !descendantsAddedToOverlap);

    if (layer.isComposited())
        layer.backing()->updateAllowsBackingStoreDetaching(allowsBackingStoreDetachingForFixed);

    overlapMap.geometryMap().popMappingsToAncestor(ancestorLayer);

    LOG_WITH_STREAM(Compositing, stream << TextStream::Repeat(treeDepth * 2, ' ') << &layer << " computeCompositingRequirements - willBeComposited " << willBeComposited << " (backing provider candidates " << backingSharingState.backingProviderCandidates() << ")");
}

// We have to traverse unchanged layers to fill in the overlap map.
void RenderLayerCompositor::traverseUnchangedSubtree(RenderLayer* ancestorLayer, RenderLayer& layer, LayerOverlapMap& overlapMap, CompositingState& compositingState, BackingSharingState& backingSharingState)
{
#if !LOG_DISABLED
    unsigned treeDepth = compositingState.depth;
#else
    unsigned treeDepth = 0;
#endif

    layer.updateDescendantDependentFlags();
    layer.updateLayerListsIfNeeded();

    ASSERT(!compositingState.fullPaintOrderTraversalRequired);
    ASSERT(!layer.hasDescendantNeedingCompositingRequirementsTraversal());
    ASSERT(!layer.needsCompositingRequirementsTraversal());

    LOG_WITH_STREAM(Compositing, stream << TextStream::Repeat(treeDepth * 2, ' ') << &layer << (layer.isNormalFlowOnly() ? " n" : " s") << " traverseUnchangedSubtree");

    bool layerIsComposited = layer.isComposited();
    bool layerPaintsIntoProvidedBacking = false;
    bool didPushOverlapContainer = false;

    OverlapExtent layerExtent;
    if (layerIsComposited && !layer.isRenderViewLayer())
        layerExtent.hasTransformAnimation = isRunningTransformAnimation(layer.renderer());

    bool respectTransforms = !layerExtent.hasTransformAnimation;
    overlapMap.geometryMap().pushMappingsToAncestor(&layer, ancestorLayer, respectTransforms);

    // If we know for sure the layer is going to be composited, don't bother looking it up in the overlap map
    if (!layerIsComposited && !overlapMap.isEmpty() && compositingState.testingOverlap)
        computeExtent(overlapMap, layer, layerExtent);

    if (layer.paintsIntoProvidedBacking()) {
        auto* provider = backingSharingState.existingBackingProviderCandidateForLayer(layer);
        ASSERT_WITH_SECURITY_IMPLICATION(provider);
        ASSERT_WITH_SECURITY_IMPLICATION(provider == backingSharingState.backingProviderCandidateForLayer(layer, *this, overlapMap, layerExtent));
        provider->sharingLayers.add(layer);
        layerPaintsIntoProvidedBacking = true;
    }

    CompositingState currentState = compositingState.stateForPaintOrderChildren(layer);

    if (layerIsComposited) {
        // This layer is going to be composited, so children can safely ignore the fact that there's an
        // animation running behind this layer, meaning they can rely on the overlap map testing again.
        currentState.testingOverlap = true;
        // This layer now acts as the ancestor for kids.
        currentState.compositingAncestor = &layer;
        currentState.backingSharingAncestor = nullptr;
        overlapMap.pushCompositingContainer(layer);
        didPushOverlapContainer = true;
        LOG_WITH_STREAM(CompositingOverlap, stream << "unchangedSubtree: layer " << &layer << " will composite, pushed container " << overlapMap);

        computeExtent(overlapMap, layer, layerExtent);
        currentState.ancestorHasTransformAnimation |= layerExtent.hasTransformAnimation;
        // Too hard to compute animated bounds if both us and some ancestor is animating transform.
        layerExtent.animationCausesExtentUncertainty |= layerExtent.hasTransformAnimation && compositingState.ancestorHasTransformAnimation;
    } else if (layerPaintsIntoProvidedBacking) {
        overlapMap.pushCompositingContainer(layer);
        currentState.backingSharingAncestor = &layer;
        didPushOverlapContainer = true;
        LOG_WITH_STREAM(CompositingOverlap, stream << "unchangedSubtree: layer " << &layer << " will share, pushed container " << overlapMap);
    }

    auto backingSharingSnapshot = updateBackingSharingBeforeDescendantTraversal(backingSharingState, treeDepth, overlapMap, layer, layerExtent, layerIsComposited, compositingState.stackingContextAncestor);

#if ASSERT_ENABLED
    LayerListMutationDetector mutationChecker(layer);
#endif

    if (!canSkipComputeCompositingRequirementsForSubtree(layer, layerIsComposited)) {
        for (auto* childLayer : layer.negativeZOrderLayers()) {
            traverseUnchangedSubtree(&layer, *childLayer, overlapMap, currentState, backingSharingState);
            if (currentState.subtreeIsCompositing)
                ASSERT(layerIsComposited);
        }

        for (auto* childLayer : layer.normalFlowLayers())
            traverseUnchangedSubtree(&layer, *childLayer, overlapMap, currentState, backingSharingState);

        for (auto* childLayer : layer.positiveZOrderLayers())
            traverseUnchangedSubtree(&layer, *childLayer, overlapMap, currentState, backingSharingState);

        // Set the flag to say that this layer has compositing children.
        ASSERT(layer.hasCompositingDescendant() == currentState.subtreeIsCompositing);
        ASSERT_IMPLIES(canBeComposited(layer) && clipsCompositingDescendants(layer), layerIsComposited);
    }

    ASSERT(!currentState.fullPaintOrderTraversalRequired);
    compositingState.updateWithDescendantStateAndLayer(currentState, layer, ancestorLayer, layerExtent, true);
    updateBackingSharingAfterDescendantTraversal(backingSharingState, treeDepth, overlapMap, layer, layerExtent, compositingState.stackingContextAncestor, backingSharingSnapshot);

    bool layerContributesToOverlap = (currentState.compositingAncestor && !currentState.compositingAncestor->isRenderViewLayer()) || currentState.backingSharingAncestor;
    updateOverlapMap(overlapMap, layer, layerExtent, didPushOverlapContainer, layerContributesToOverlap);

    overlapMap.geometryMap().popMappingsToAncestor(ancestorLayer);

    ASSERT(!layer.needsCompositingRequirementsTraversal());
}

void RenderLayerCompositor::collectViewTransitionNewContentLayers(RenderLayer& layer, Vector<Ref<GraphicsLayer>>& childList)
{
    if (layer.renderer().style().pseudoElementType() != PseudoId::ViewTransitionNew || !layer.hasVisibleContent())
        return;

    if (!downcast<RenderViewTransitionCapture>(layer.renderer()).canUseExistingLayers())
        return;

    RefPtr activeViewTransition = layer.renderer().protectedDocument()->activeViewTransition();
    if (!activeViewTransition)
        return;

    auto* capturedElement = activeViewTransition->namedElements().find(layer.renderer().style().pseudoElementNameArgument());
    if (!capturedElement)
        return;

    auto newStyleable = capturedElement->newElement.styleable();
    if (!newStyleable)
        return;

    CheckedPtr capturedRenderer = newStyleable->renderer();
    if (!capturedRenderer || !capturedRenderer->hasLayer())
        return;

    if (capturedRenderer->isDocumentElementRenderer()) {
        ASSERT(capturedRenderer->protectedDocument()->activeViewTransitionCapturedDocumentElement());
        capturedRenderer = &capturedRenderer->view();
        ASSERT(capturedRenderer->hasLayer());
    }

    auto& modelObject = downcast<RenderLayerModelObject>(*capturedRenderer);
    if (RenderLayerBacking* backing = modelObject.layer()->backing())
        childList.append(Ref { *backing->childForSuperlayersExcludingViewTransitions() });
}

void RenderLayerCompositor::updateBackingAndHierarchy(RenderLayer& layer, Vector<Ref<GraphicsLayer>>& childLayersOfEnclosingLayer, UpdateBackingTraversalState& traversalState, ScrollingTreeState& scrollingTreeState, OptionSet<UpdateLevel> updateLevel)
{
    layer.updateDescendantDependentFlags();
    layer.updateLayerListsIfNeeded();

    bool layerNeedsUpdate = !updateLevel.isEmpty();
    if (layer.descendantsNeedUpdateBackingAndHierarchyTraversal())
        updateLevel.add(UpdateLevel::AllDescendants);

    ScrollingTreeState scrollingStateForDescendants = scrollingTreeState;
    UpdateBackingTraversalState traversalStateForDescendants = traversalState.stateForDescendants();
    Vector<RenderLayer*> layersClippedByScrollers;
    Vector<RenderLayer*> compositedOverflowScrollLayers;
    
    if (layer.needsScrollingTreeUpdate())
        scrollingTreeState.needSynchronousScrollingReasonsUpdate = true;

    auto* layerBacking = layer.backing();
    if (layerBacking) {
        updateLevel.remove(UpdateLevel::CompositedChildren);

        // We updated the composited bounds in RenderLayerBacking::updateAfterLayout(), but it may have changed
        // based on which descendants are now composited.
        if (layerBacking->updateCompositedBounds()) {
            layer.setNeedsCompositingGeometryUpdate();
            // Our geometry can affect descendants.
            updateLevel.add(UpdateLevel::CompositedChildren);
        }
        
        if (layerNeedsUpdate || layer.needsCompositingConfigurationUpdate()) {
            if (layerBacking->updateConfiguration(traversalState.compositingAncestor)) {
                layerNeedsUpdate = true; // We also need to update geometry.
                layer.setNeedsCompositingLayerConnection();
            }

            layerBacking->updateDebugIndicators(m_showDebugBorders, m_showRepaintCounter);
        }
        
        OptionSet<ScrollingNodeChangeFlags> scrollingNodeChanges = { ScrollingNodeChangeFlags::Layer };
        if (layerNeedsUpdate || layer.needsCompositingGeometryUpdate()) {
            layerBacking->updateGeometry(traversalState.compositingAncestor);
            scrollingNodeChanges.add(ScrollingNodeChangeFlags::LayerGeometry);
        } else if (layer.needsScrollingTreeUpdate())
            scrollingNodeChanges.add(ScrollingNodeChangeFlags::LayerGeometry);

        if (auto* reflection = layer.reflectionLayer()) {
            if (auto* reflectionBacking = reflection->backing()) {
                reflectionBacking->updateCompositedBounds();
                reflectionBacking->updateGeometry(&layer);
                reflectionBacking->updateAfterDescendants();
            }
        }

        if (!layer.parent())
            updateRootLayerPosition();

        // FIXME: do based on dirty flags. Need to do this for changes of geometry, configuration and hierarchy.
        // Need to be careful to do the right thing when a scroll-coordinated layer loses a scroll-coordinated ancestor.
        scrollingStateForDescendants.parentNodeID = updateScrollCoordinationForLayer(layer, traversalState.compositingAncestor, scrollingTreeState, scrollingNodeChanges);
        scrollingStateForDescendants.hasParent = true;
        scrollingStateForDescendants.nextChildIndex = 0;
        
        traversalStateForDescendants.compositingAncestor = &layer;
        traversalStateForDescendants.layersClippedByScrollers = &layersClippedByScrollers;
        traversalStateForDescendants.overflowScrollLayers = &compositedOverflowScrollLayers;

#if !LOG_DISABLED
        logLayerInfo(layer, "updateBackingAndHierarchy"_s, traversalState.depth);
#endif
    }

    if (layer.childrenNeedCompositingGeometryUpdate())
        updateLevel.add(UpdateLevel::CompositedChildren);

    // If this layer has backing, then we are collecting its children, otherwise appending
    // to the compositing child list of an enclosing layer.
    Vector<Ref<GraphicsLayer>> layerChildren;
    auto& childList = layerBacking ? layerChildren : childLayersOfEnclosingLayer;

    bool requireDescendantTraversal = layer.hasDescendantNeedingUpdateBackingOrHierarchyTraversal()
        || (layer.hasCompositingDescendant() && (!layerBacking || layer.needsCompositingLayerConnection() || !updateLevel.isEmpty()));

    bool requiresChildRebuild = layerBacking && layer.needsCompositingLayerConnection() && !layer.hasCompositingDescendant();

#if ASSERT_ENABLED
    LayerListMutationDetector mutationChecker(layer);
#endif

    auto appendForegroundLayerIfNecessary = [&] {
        // If a negative z-order child is compositing, we get a foreground layer which needs to get parented.
        if (layer.negativeZOrderLayers().size()) {
            if (layerBacking && layerBacking->foregroundLayer())
                childList.append(Ref { *layerBacking->foregroundLayer() });
        }
    };

    if (requireDescendantTraversal) {
        for (auto* renderLayer : layer.negativeZOrderLayers())
            updateBackingAndHierarchy(*renderLayer, childList, traversalStateForDescendants, scrollingStateForDescendants, updateLevel);

        appendForegroundLayerIfNecessary();

        for (auto* renderLayer : layer.normalFlowLayers())
            updateBackingAndHierarchy(*renderLayer, childList, traversalStateForDescendants, scrollingStateForDescendants, updateLevel);
        
        for (auto* renderLayer : layer.positiveZOrderLayers())
            updateBackingAndHierarchy(*renderLayer, childList, traversalStateForDescendants, scrollingStateForDescendants, updateLevel);

        // Pass needSynchronousScrollingReasonsUpdate back up.
        scrollingTreeState.needSynchronousScrollingReasonsUpdate |= scrollingStateForDescendants.needSynchronousScrollingReasonsUpdate;
        if (scrollingTreeState.parentNodeID == scrollingStateForDescendants.parentNodeID)
            scrollingTreeState.nextChildIndex = scrollingStateForDescendants.nextChildIndex;

    } else if (requiresChildRebuild)
        appendForegroundLayerIfNecessary();

    if (layerBacking) {
        if (requireDescendantTraversal || requiresChildRebuild) {
            WidgetLayerAttachment widgetLayerAttachment;
            if (auto* renderWidget = dynamicDowncast<RenderWidget>(layer.renderer()))
                widgetLayerAttachment = attachWidgetContentLayersIfNecessary(*renderWidget);

            collectViewTransitionNewContentLayers(layer, childList);

            if (!widgetLayerAttachment.widgetLayersAttachedAsChildren) {
                // If the layer has a clipping layer the overflow controls layers will be siblings of the clipping layer.
                // Otherwise, the overflow control layers are normal children.
                if (!layerBacking->hasClippingLayer() && !layerBacking->hasScrollingLayer()) {
                    if (RefPtr overflowControlLayer = layerBacking->overflowControlsContainer())
                        layerChildren.append(*overflowControlLayer);
                }

                adjustOverflowScrollbarContainerLayers(layer, compositedOverflowScrollLayers, layersClippedByScrollers, layerChildren);
                RefPtr { layerBacking->parentForSublayers() }->setChildren(WTFMove(layerChildren));
            }
        }

        // Layers that are captured in a view transition get manually parented to their pseudo in collectViewTransitionNewContentLayers.
        // The view transition root (when the document element is captured) gets parented in RenderLayerBacking::childForSuperlayers.
        bool skipAddToEnclosing = layer.renderer().capturedInViewTransition() && !layer.renderer().isDocumentElementRenderer();
        if (layer.renderer().isViewTransitionContainingBlock() && layer.renderer().protectedDocument()->activeViewTransitionCapturedDocumentElement())
            skipAddToEnclosing = true;

        if (!skipAddToEnclosing)
            childLayersOfEnclosingLayer.append(Ref { *layerBacking->childForSuperlayers() });

        if (layerBacking->hasAncestorClippingLayers() && layerBacking->ancestorClippingStack()->hasAnyScrollingLayers())
            traversalState.layersClippedByScrollers->append(&layer);

        if (layer.hasCompositedScrollableOverflow())
            traversalState.overflowScrollLayers->append(&layer);

        layerBacking->updateAfterDescendants();
    }
    
    layer.clearUpdateBackingOrHierarchyTraversalState();
}

std::optional<RenderLayerCompositor::BackingSharingSnapshot> RenderLayerCompositor::updateBackingSharingBeforeDescendantTraversal(BackingSharingState& sharingState, unsigned depth, const LayerOverlapMap& overlapMap, RenderLayer& layer, OverlapExtent& layerExtent, bool willBeComposited, RenderLayer* stackingContextAncestor)
{
    UNUSED_PARAM(depth);

    layer.setBackingProviderLayer(nullptr, { UpdateBackingSharingFlags::DuringCompositingUpdate });

    LOG_WITH_STREAM(Compositing, stream << TextStream::Repeat(depth * 2, ' ') << &layer << " updateBackingSharingBeforeDescendantTraversal - will be composited " << willBeComposited);

    auto shouldEndSharingSequence = [&] {
        if (!sharingState.backingSharingStackingContext())
            return false;

        if (!willBeComposited)
            return false;

        // If this layer is composited, we can only continue the sequence if it's a new provider candidate.
        computeExtent(overlapMap, layer, layerExtent);
        return !sharingState.isAdditionalProviderCandidate(layer, layerExtent.bounds, stackingContextAncestor);
    }();

    // A layer that composites resets backing-sharing, since subsequent layers need to composite to overlap it.
    if (shouldEndSharingSequence) {
        LOG_WITH_STREAM(Compositing, stream << TextStream::Repeat(depth * 2, ' ') << " - ending sharing sequence on " << sharingState.backingProviderCandidates());
        sharingState.endBackingSharingSequence(layer);
    }

    return sharingState.snapshot();
}

void RenderLayerCompositor::updateBackingSharingAfterDescendantTraversal(BackingSharingState& sharingState, unsigned depth, const LayerOverlapMap& overlapMap, RenderLayer& layer, OverlapExtent& layerExtent, RenderLayer* stackingContextAncestor, const std::optional<BackingSharingSnapshot>& backingSharingSnapshot)
{
    UNUSED_PARAM(depth);
    LOG_WITH_STREAM(Compositing, stream << TextStream::Repeat(depth * 2, ' ') << &layer << " updateBackingSharingAfterDescendantTraversal for layer - is composited " << layer.isComposited() << " has composited descendant " << layer.hasCompositingDescendant());

    if (layer.isComposited()) {
        // If this layer is being composited, clean up sharing-related state.
        layer.disconnectFromBackingProviderLayer({ UpdateBackingSharingFlags::DuringCompositingUpdate });
        for (auto& candidate : sharingState.backingProviderCandidates())
            candidate.sharingLayers.remove(layer);
    }

    // Backing sharing is constrained to layers in the same stacking context.
    if (&layer == sharingState.backingSharingStackingContext()) {
        ASSERT(!sharingState.backingProviderCandidates().containsIf([&](auto& candidate) { return candidate.providerLayer == &layer; }));
        LOG_WITH_STREAM(Compositing, stream << TextStream::Repeat(depth * 2, ' ') << " - end of stacking context for backing provider " << sharingState.backingProviderCandidates());
        sharingState.endBackingSharingSequence(layer);

        if (layer.isComposited())
            layer.backing()->clearBackingSharingLayers({ UpdateBackingSharingFlags::DuringCompositingUpdate });

        return;
    }

    if (!layer.isComposited())
        return;

    if (!stackingContextAncestor)
        return;

    bool canBeBackingProvider = !layer.hasCompositingDescendant();
    if (canBeBackingProvider) {
        if (!sharingState.backingSharingStackingContext()) {
            computeExtent(overlapMap, layer, layerExtent);
            sharingState.startBackingSharingSequence(layer, layerExtent.bounds, *stackingContextAncestor);
            LOG_WITH_STREAM(Compositing, stream << TextStream::Repeat(depth * 2, ' ') << " - started sharing sequence with provider candidate " << &layer);
            return;
        }

        computeExtent(overlapMap, layer, layerExtent);
        if (sharingState.isAdditionalProviderCandidate(layer, layerExtent.bounds, stackingContextAncestor)) {
            sharingState.addBackingSharingCandidate(layer, layerExtent.bounds, *stackingContextAncestor, backingSharingSnapshot);
            LOG_WITH_STREAM(Compositing, stream << TextStream::Repeat(depth * 2, ' ') << " - added additional provider candidate " << &layer);
            return;
        }
    }

    layer.backing()->clearBackingSharingLayers({ UpdateBackingSharingFlags::DuringCompositingUpdate });
    LOG_WITH_STREAM(Compositing, stream << TextStream::Repeat(depth * 2, ' ') << " - is composited; maybe ending existing backing sequence with candidates " << sharingState.backingProviderCandidates() << " stacking context " << sharingState.backingSharingStackingContext());

    // A layer that composites resets backing-sharing, since subsequent layers need to composite to overlap it. If a descendant didn't already end the sharing sequence that was current when processing of this layer started, end it now.
    if (backingSharingSnapshot && backingSharingSnapshot->sequenceIdentifier == sharingState.sequenceIdentifier())
        sharingState.endBackingSharingSequence(layer);
}


// Finds the set of overflow:scroll layers whose overflow controls hosting layer needs to be reparented,
// to ensure that the scrollbars show on top of positioned content inside the scroller.
void RenderLayerCompositor::adjustOverflowScrollbarContainerLayers(RenderLayer& stackingContextLayer, const Vector<RenderLayer*>& overflowScrollLayers, const Vector<RenderLayer*>& layersClippedByScrollers, Vector<Ref<GraphicsLayer>>& layerChildren)
{
    if (layersClippedByScrollers.isEmpty())
        return;

    HashMap<CheckedPtr<RenderLayer>, CheckedPtr<RenderLayer>> overflowScrollToLastContainedLayerMap;

    for (auto* clippedLayer : layersClippedByScrollers) {
        auto* clippingStack = clippedLayer->backing()->ancestorClippingStack();

        for (const auto& stackEntry : clippingStack->stack()) {
            if (!stackEntry.clipData.isOverflowScroll)
                continue;

            if (auto* layer = stackEntry.clipData.clippingLayer.get())
                overflowScrollToLastContainedLayerMap.set(layer, clippedLayer);
        }
    }

    for (auto* overflowScrollingLayer : overflowScrollLayers) {
        auto it = overflowScrollToLastContainedLayerMap.find(overflowScrollingLayer);
        if (it == overflowScrollToLastContainedLayerMap.end())
            continue;
    
        CheckedPtr lastContainedDescendant = it->value;
        if (!lastContainedDescendant || !lastContainedDescendant->isComposited())
            continue;

        auto* lastContainedDescendantBacking = lastContainedDescendant->backing();
        auto* overflowBacking = overflowScrollingLayer->backing();
        if (!overflowBacking)
            continue;
        
        RefPtr overflowContainerLayer = overflowBacking->overflowControlsContainer();
        if (!overflowContainerLayer)
            continue;

        overflowContainerLayer->removeFromParent();

        if (overflowBacking->hasAncestorClippingLayers())
            overflowBacking->ensureOverflowControlsHostLayerAncestorClippingStack(&stackingContextLayer);

        if (auto* overflowControlsAncestorClippingStack = overflowBacking->overflowControlsHostLayerAncestorClippingStack()) {
            RefPtr { overflowControlsAncestorClippingStack->lastLayer() }->setChildren({ Ref { *overflowContainerLayer } });
            overflowContainerLayer = overflowControlsAncestorClippingStack->firstLayer();
        }

        RefPtr lastDescendantGraphicsLayer = lastContainedDescendantBacking->childForSuperlayers();
        RefPtr overflowScrollerGraphicsLayer = overflowBacking->childForSuperlayers();
        
        std::optional<size_t> lastDescendantLayerIndex;
        std::optional<size_t> scrollerLayerIndex;
        for (size_t i = 0; i < layerChildren.size(); ++i) {
            const RefPtr graphicsLayer = layerChildren[i].ptr();
            if (graphicsLayer == lastDescendantGraphicsLayer)
                lastDescendantLayerIndex = i;
            else if (graphicsLayer == overflowScrollerGraphicsLayer)
                scrollerLayerIndex = i;
        }

        if (lastDescendantLayerIndex && scrollerLayerIndex) {
            auto insertionIndex = std::max(lastDescendantLayerIndex.value() + 1, scrollerLayerIndex.value() + 1);
            LOG_WITH_STREAM(Compositing, stream << "Moving overflow controls layer for " << *overflowScrollingLayer << " to appear after " << *lastContainedDescendant);
            layerChildren.insert(insertionIndex, *overflowContainerLayer);
        }

        overflowBacking->adjustOverflowControlsPositionRelativeToAncestor(stackingContextLayer);
    }
}

void RenderLayerCompositor::appendDocumentOverlayLayers(Vector<Ref<GraphicsLayer>>& childList)
{
    if (!isRootFrameCompositor() || !m_compositing)
        return;

    if (!page().pageOverlayController().hasDocumentOverlays())
        return;

    Ref<GraphicsLayer> overlayHost = page().pageOverlayController().layerWithDocumentOverlays();
    childList.append(WTFMove(overlayHost));
}

bool RenderLayerCompositor::needsCompositingForContentOrOverlays() const
{
    return m_contentLayersCount + page().pageOverlayController().overlayCount();
}

void RenderLayerCompositor::layerBecameComposited(const RenderLayer& layer)
{
    if (&layer != m_renderView.layer())
        ++m_contentLayersCount;
}

void RenderLayerCompositor::layerBecameNonComposited(const RenderLayer& layer)
{
    // Inform the inspector that the given RenderLayer was destroyed.
    // FIXME: "destroyed" is a misnomer.
    InspectorInstrumentation::renderLayerDestroyed(protectedPage().ptr(), layer);

    if (&layer != m_renderView.layer()) {
        ASSERT(m_contentLayersCount > 0);
        --m_contentLayersCount;
    }
}

#if !LOG_DISABLED
void RenderLayerCompositor::logLayerInfo(const RenderLayer& layer, ASCIILiteral phase, int depth)
{
    if (!compositingLogEnabled())
        return;

    auto* backing = layer.backing();
    RequiresCompositingData queryData;
    if (requiresCompositingLayer(layer, queryData) || layer.isRenderViewLayer()) {
        ++m_obligateCompositedLayerCount;
        m_obligatoryBackingStoreBytes += backing->backingStoreMemoryEstimate();
    } else {
        ++m_secondaryCompositedLayerCount;
        m_secondaryBackingStoreBytes += backing->backingStoreMemoryEstimate();
    }

    LayoutRect absoluteBounds = backing->compositedBounds();
    absoluteBounds.move(layer.offsetFromAncestor(m_renderView.layer()));
    
    StringBuilder logString;
    logString.append(pad(' ', 12 + depth * 2, hex(reinterpret_cast<uintptr_t>(&layer), Lowercase)), " id "_s, backing->graphicsLayer()->primaryLayerID() ? backing->graphicsLayer()->primaryLayerID()->object().toUInt64() : 0, " ("_s, absoluteBounds.x().toFloat(), ',', absoluteBounds.y().toFloat(), '-', absoluteBounds.maxX().toFloat(), ',', absoluteBounds.maxY().toFloat(), ") "_s, FormattedNumber::fixedWidth(backing->backingStoreMemoryEstimate() / 1024, 2), "KB"_s);

    if (!layer.renderer().style().hasAutoUsedZIndex())
        logString.append(" z-index: "_s, layer.renderer().style().usedZIndex());

    logString.append(" ("_s, logOneReasonForCompositing(layer), ") "_s);

    if (backing->graphicsLayer()->contentsOpaque() || backing->paintsIntoCompositedAncestor() || backing->foregroundLayer() || backing->backgroundLayer()) {
        logString.append('[');

        auto prefix = ""_s;
        if (backing->graphicsLayer()->contentsOpaque()) {
            logString.append("opaque"_s);
            prefix = ", "_s;
        }

        if (backing->paintsIntoCompositedAncestor()) {
            logString.append(prefix, "paints into ancestor"_s);
            prefix = ", "_s;
        }

        if (backing->foregroundLayer() || backing->backgroundLayer()) {
            if (backing->foregroundLayer() && backing->backgroundLayer()) {
                logString.append(prefix, "+foreground+background"_s);
                prefix = ", "_s;
            } else if (backing->foregroundLayer()) {
                logString.append(prefix, "+foreground"_s);
                prefix = ", "_s;
            } else {
                logString.append(prefix, "+background"_s);
                prefix = ", "_s;
            }
        }

        logString.append("] "_s);
    }

    logString.append(layer.name(), " - "_s, phase);

    LOG(Compositing, "%s", logString.toString().utf8().data());
}
#endif

static bool clippingChanged(const RenderStyle& oldStyle, const RenderStyle& newStyle)
{
    return oldStyle.overflowX() != newStyle.overflowX()
        || oldStyle.overflowY() != newStyle.overflowY()
        || oldStyle.clip() != newStyle.clip();
}

static bool styleAffectsLayerGeometry(const RenderStyle& style)
{
    return style.hasClip() || style.hasClipPath() || style.hasBorderRadius();
}

static bool recompositeChangeRequiresGeometryUpdate(const RenderStyle& oldStyle, const RenderStyle& newStyle)
{
    return oldStyle.transform() != newStyle.transform()
        || oldStyle.translate() != newStyle.translate()
        || oldStyle.scale() != newStyle.scale()
        || oldStyle.rotate() != newStyle.rotate()
        || oldStyle.transformBox() != newStyle.transformBox()
        || oldStyle.transformOriginX() != newStyle.transformOriginX()
        || oldStyle.transformOriginY() != newStyle.transformOriginY()
        || oldStyle.transformOriginZ() != newStyle.transformOriginZ()
        || oldStyle.usedTransformStyle3D() != newStyle.usedTransformStyle3D()
        || oldStyle.perspective() != newStyle.perspective()
        || oldStyle.perspectiveOrigin() != newStyle.perspectiveOrigin()
        || oldStyle.backfaceVisibility() != newStyle.backfaceVisibility()
        || oldStyle.offsetPath() != newStyle.offsetPath()
        || oldStyle.offsetAnchor() != newStyle.offsetAnchor()
        || oldStyle.offsetPosition() != newStyle.offsetPosition()
        || oldStyle.offsetDistance() != newStyle.offsetDistance()
        || oldStyle.offsetRotate() != newStyle.offsetRotate()
        || oldStyle.clipPath() != newStyle.clipPath()
        || oldStyle.overscrollBehaviorX() != newStyle.overscrollBehaviorX()
        || oldStyle.overscrollBehaviorY() != newStyle.overscrollBehaviorY();
}

static bool recompositeChangeRequiresChildrenGeometryUpdate(const RenderStyle& oldStyle, const RenderStyle& newStyle)
{
    return oldStyle.hasPerspective() != newStyle.hasPerspective()
        || oldStyle.usedTransformStyle3D() != newStyle.usedTransformStyle3D();
}

void RenderLayerCompositor::layerGainedCompositedScrollableOverflow(RenderLayer& layer)
{
    RequiresCompositingData queryData;
    queryData.layoutUpToDate = LayoutUpToDate::No;

    updateExplicitBacking(layer, queryData, BackingRequired::Yes);

    auto* backing = layer.backing();
    if (!backing)
        return;

    backing->updateConfigurationAfterStyleChange();
}

void RenderLayerCompositor::layerStyleChanged(StyleDifference diff, RenderLayer& layer, const RenderStyle* oldStyle)
{
    if (diff == StyleDifference::Equal)
        return;

    // Create or destroy backing here so that code that runs during layout can reliably use isComposited() (though this
    // is only true for layers composited for direct reasons).
    // Also, it allows us to avoid a tree walk in updateCompositingLayers() when no layer changed its compositing state.
    RequiresCompositingData queryData;
    queryData.layoutUpToDate = LayoutUpToDate::No;

    updateExplicitBacking(layer, queryData);
    layer.setIntrinsicallyComposited(queryData.intrinsic);

    if (queryData.reevaluateAfterLayout)
        layer.setNeedsPostLayoutCompositingUpdate();

    const auto& newStyle = layer.renderer().style();

    if (hasContentCompositingLayers()) {
        if (diff >= StyleDifference::LayoutOutOfFlowMovementOnly) {
            layer.setNeedsPostLayoutCompositingUpdate();
            layer.setNeedsCompositingGeometryUpdate();
        }

        if (diff >= StyleDifference::Layout) {
            // FIXME: only set flags here if we know we have a composited descendant, but we might not know at this point.
            if (oldStyle && clippingChanged(*oldStyle, newStyle)) {
                if (layer.isStackingContext()) {
                    layer.setNeedsPostLayoutCompositingUpdate(); // Layer needs to become composited if it has composited descendants.
                    layer.setNeedsCompositingConfigurationUpdate(); // If already composited, layer needs to create/destroy clipping layer.
                    layer.setChildrenNeedCompositingGeometryUpdate(); // Clipping layers on this layer affect descendant layer geometry.
                } else {
                    // Descendant (in containing block order) compositing layers need to re-evaluate their clipping,
                    // but they might be siblings in z-order so go up to our stacking context.
                    if (auto* stackingContext = layer.stackingContext())
                        stackingContext->setDescendantsNeedUpdateBackingAndHierarchyTraversal();
                }
            }

            // This ensures that the viewport anchor layer will be updated when updating compositing layers upon style change
            auto styleChangeAffectsAnchorLayer = [](const RenderStyle* oldStyle, const RenderStyle& newStyle) {
                if (!oldStyle)
                    return false;

                return oldStyle->hasViewportConstrainedPosition() != newStyle.hasViewportConstrainedPosition();
            };

            if (styleChangeAffectsAnchorLayer(oldStyle, newStyle))
                layer.setNeedsCompositingConfigurationUpdate();

            // These properties trigger compositing if some descendant is composited.
            if (oldStyle && styleChangeMayAffectIndirectCompositingReasons(*oldStyle, newStyle))
                layer.setNeedsPostLayoutCompositingUpdate();

            layer.setNeedsCompositingGeometryUpdate();
        }
    }

    if (diff >= StyleDifference::Repaint && oldStyle) {
        // This ensures that we update border-radius clips on layers that are descendants in containing-block order but not paint order. This is necessary even when
        // the current layer is not composited.
        bool changeAffectsClippingOfNonPaintOrderDescendants = !layer.isStackingContext() && layer.renderer().hasNonVisibleOverflow() && oldStyle->border() != newStyle.border();
        if (changeAffectsClippingOfNonPaintOrderDescendants) {
            if (auto* parent = layer.paintOrderParent())
                parent->setChildrenNeedCompositingGeometryUpdate();
        }
    }

    auto* backing = layer.backing();
    if (!backing)
        return;

#if HAVE(CORE_ANIMATION_SEPARATED_LAYERS)
    auto styleChangeAffectsSeparatedProperties = [](const RenderStyle* oldStyle, const RenderStyle& newStyle) {
        if (!oldStyle)
            return newStyle.usedTransformStyle3D() == TransformStyle3D::Separated;

        return oldStyle->usedTransformStyle3D() != newStyle.usedTransformStyle3D()
            && (oldStyle->usedTransformStyle3D() == TransformStyle3D::Separated
                || newStyle.usedTransformStyle3D() == TransformStyle3D::Separated);
    };

    // We need a full compositing configuration update since this also impacts the clipping strategy.
    if (styleChangeAffectsSeparatedProperties(oldStyle, newStyle))
        layer.setNeedsCompositingConfigurationUpdate();
#endif

    backing->updateConfigurationAfterStyleChange();

    if (diff >= StyleDifference::Repaint) {
        // Visibility change may affect geometry of the enclosing composited layer.
        if (oldStyle && oldStyle->usedVisibility() != newStyle.usedVisibility())
            layer.setNeedsCompositingGeometryUpdate();
        
        // We'll get a diff of Repaint when things like clip-path change; these might affect layer or inner-layer geometry.
        if (layer.isComposited() && oldStyle) {
            if (styleAffectsLayerGeometry(*oldStyle) || styleAffectsLayerGeometry(newStyle))
                layer.setNeedsCompositingGeometryUpdate();
        }

        // image rendering mode can determine whether we use device pixel ratio for the backing store.
        if (oldStyle && oldStyle->imageRendering() != newStyle.imageRendering())
            layer.setNeedsCompositingConfigurationUpdate();
    }

    if (diff >= StyleDifference::RecompositeLayer) {
        if (layer.isComposited()) {
            bool hitTestingStateChanged = oldStyle && (oldStyle->usedPointerEvents() != newStyle.usedPointerEvents());
            if (is<RenderWidget>(layer.renderer()) || hitTestingStateChanged) {
                // For RenderWidgets this is necessary to get iframe layers hooked up in response to scheduleInvalidateStyleAndLayerComposition().
                layer.setNeedsCompositingConfigurationUpdate();
            }
            // If we're changing to/from 0 opacity, then we need to reconfigure the layer since we try to
            // skip backing store allocation for opacity:0.
            if (oldStyle && oldStyle->opacity() != newStyle.opacity() && (oldStyle->opacity().isTransparent() || newStyle.opacity().isTransparent()))
                layer.setNeedsCompositingConfigurationUpdate();
        }
        if (oldStyle && recompositeChangeRequiresGeometryUpdate(*oldStyle, newStyle)) {
            // FIXME: transform changes really need to trigger layout. See RenderElement::adjustStyleDifference().
            layer.setNeedsPostLayoutCompositingUpdate();
            layer.setNeedsCompositingGeometryUpdate();
        }
        if (oldStyle && recompositeChangeRequiresChildrenGeometryUpdate(*oldStyle, newStyle))
            layer.setChildrenNeedCompositingGeometryUpdate();
    }
}

void RenderLayerCompositor::establishesTopLayerWillChangeForLayer(RenderLayer& layer)
{
    clearBackingProviderSequencesInStackingContextOfLayer(layer);
}

// This is a recursive walk similar to RenderLayer::collectLayers().
static void clearBackingSharingWithinStackingContext(RenderLayer& stackingContextRoot, RenderLayer& curLayer)
{
    if (curLayer.establishesTopLayer())
        return;

    if (&curLayer != &stackingContextRoot && curLayer.isStackingContext())
        return;

    for (auto* child = curLayer.firstChild(); child; child = child->nextSibling()) {
        if (child->isComposited())
            child->backing()->clearBackingSharingLayers({ });

        if (!curLayer.isReflectionLayer(*child))
            clearBackingSharingWithinStackingContext(stackingContextRoot, *child);
    }
}

void RenderLayerCompositor::clearBackingProviderSequencesInStackingContextOfLayer(RenderLayer& layer)
{
    // We can't rely on z-order lists to be up-to-date here. For fullscreen, we may already have done a style update which dirties them.
    if (auto* stackingContextLayer = layer.stackingContext())
        clearBackingSharingWithinStackingContext(*stackingContextLayer, *stackingContextLayer);
}

// FIXME: remove and never ask questions about reflection layers.
static RenderLayerModelObject& rendererForCompositingTests(const RenderLayer& layer)
{
    auto* renderer = &layer.renderer();

    // The compositing state of a reflection should match that of its reflected layer.
    if (layer.isReflection())
        renderer = downcast<RenderLayerModelObject>(renderer->parent()); // The RenderReplica's parent is the object being reflected.

    return *renderer;
}

void RenderLayerCompositor::updateRootContentLayerClipping()
{
    RefPtr { m_rootContentsLayer }->setMasksToBounds(!m_renderView.settings().backgroundShouldExtendBeyondPage());
}

bool RenderLayerCompositor::updateExplicitBacking(RenderLayer& layer, RequiresCompositingData& queryData, BackingRequired backingRequired)
{
    if (backingRequired == BackingRequired::Unknown)
        backingRequired = needsToBeComposited(layer, queryData) ? BackingRequired::Yes : BackingRequired::No;
    else {
        // Need to fetch viewportConstrainedNotCompositedReason, but without doing all the work that needsToBeComposited does.
        requiresCompositingForPosition(rendererForCompositingTests(layer), layer, queryData);
    }

    bool hadBacking = layer.isComposited();
    if (backingRequired == BackingRequired::Yes) {
        // If we need to repaint, do so before making backing and disconnecting from the backing provider layer.
        if (!layer.backing())
            repaintOnCompositingChange(layer, layer.repaintContainer());
    }

    updateBacking(layer, queryData, nullptr, backingRequired);

    if (hadBacking != layer.isComposited())
        layer.compositingStatusChanged(queryData.layoutUpToDate);

    if (backingRequired == BackingRequired::No && hadBacking)
        repaintOnCompositingChange(layer, layer.repaintContainer());

    return hadBacking != layer.isComposited();
}


bool RenderLayerCompositor::updateBacking(RenderLayer& layer, RequiresCompositingData& queryData, BackingSharingState* backingSharingState, BackingRequired backingRequired)
{
    bool layerChanged = false;
    bool repaintRequired = false;
    if (backingRequired == BackingRequired::Unknown)
        backingRequired = needsToBeComposited(layer, queryData) ? BackingRequired::Yes : BackingRequired::No;
    else {
        // Need to fetch viewportConstrainedNotCompositedReason, but without doing all the work that needsToBeComposited does.
        requiresCompositingForPosition(rendererForCompositingTests(layer), layer, queryData);
    }

    OptionSet<UpdateBackingSharingFlags> updateBackingSharingFlags;
    if (backingSharingState)
        updateBackingSharingFlags.add(UpdateBackingSharingFlags::DuringCompositingUpdate);

    if (backingRequired == BackingRequired::Yes) {
        // If we need to repaint, do so before making backing and disconnecting from the backing provider layer.
        if (!layer.backing())
            repaintRequired = true;

        layer.disconnectFromBackingProviderLayer(updateBackingSharingFlags);

        enableCompositingMode();
        
        if (!layer.backing()) {
            layer.ensureBacking();

            if (layer.isRenderViewLayer() && useCoordinatedScrollingForLayer(layer)) {
                Ref frameView = m_renderView.frameView();
                if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
                    scrollingCoordinator->frameViewRootLayerDidChange(frameView);
#if HAVE(RUBBER_BANDING)
                updateLayerForHeader(frameView->headerHeight());
                updateLayerForFooter(frameView->footerHeight());
#endif
                updateRootContentLayerClipping();

                if (auto* tiledBacking = layer.backing()->tiledBacking())
                    tiledBacking->setObscuredContentInsets(frameView->obscuredContentInsets());
            }

            layer.setNeedsCompositingGeometryUpdate();
            layer.setNeedsCompositingConfigurationUpdate();
            layer.setNeedsCompositingPaintOrderChildrenUpdate();

            layerChanged = true;
        }
    } else {
        if (layer.backing()) {
            // If we're removing backing on a reflection, clear the source GraphicsLayer's pointer to
            // its replica GraphicsLayer. In practice this should never happen because reflectee and reflection 
            // are both either composited, or not composited.
            if (layer.isReflection()) {
                auto* sourceLayer = downcast<RenderLayerModelObject>(*layer.renderer().parent()).layer();
                if (auto* backing = sourceLayer->backing()) {
                    ASSERT(backing->graphicsLayer()->replicaLayer() == layer.backing()->graphicsLayer());
                    RefPtr { backing->graphicsLayer() }->setReplicatedByLayer(nullptr);
                }
            }

            layer.clearBacking(updateBackingSharingFlags);
            layerChanged = true;

            // If we need to repaint, do so now that we've removed the backing.
            repaintRequired = true;
        }
    }

#if ENABLE(VIDEO)
    if (layerChanged) {
        if (CheckedPtr renderVideo = dynamicDowncast<RenderVideo>(layer.renderer())) {
            // If it's a video, give the media player a chance to hook up to the layer.
            renderVideo->acceleratedRenderingStateChanged();
        }
    }
#endif

    if (layerChanged) {
        if (RefPtr renderWidget = dynamicDowncast<RenderWidget>(layer.renderer())) {
            auto* innerCompositor = frameContentsCompositor(*renderWidget);
            if (innerCompositor && innerCompositor->usesCompositing())
                innerCompositor->updateRootLayerAttachment();
        }
    }
    
    if (layerChanged)
        layer.clearClipRectsIncludingDescendants(PaintingClipRects);

    // If a fixed position layer gained/lost a backing or the reason not compositing it changed,
    // the scrolling coordinator needs to recalculate whether it can do fast scrolling.
    if (layer.renderer().isFixedPositioned()) {
        if (layer.viewportConstrainedNotCompositedReason() != queryData.nonCompositedForPositionReason  && !queryData.reevaluateAfterLayout) {
            layer.setViewportConstrainedNotCompositedReason(queryData.nonCompositedForPositionReason);
            layerChanged = true;
        }
        if (layerChanged) {
            if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
                scrollingCoordinator->frameViewFixedObjectsDidChange(m_renderView.frameView());
        }
    } else
        layer.setViewportConstrainedNotCompositedReason(RenderLayer::NoNotCompositedReason);
    
    if (layer.backing())
        layer.backing()->updateDebugIndicators(m_showDebugBorders, m_showRepaintCounter);

    if (layerChanged) {
        layer.setChildrenNeedCompositingGeometryUpdate();
        layer.setNeedsCompositingLayerConnection();
        layer.setSubsequentLayersNeedCompositingRequirementsTraversal();
        // Ancestor layers that composited for indirect reasons (things listed in styleChangeMayAffectIndirectCompositingReasons()) need to get updated.
        // This could be optimized by only setting this flag on layers with the relevant styles.
        layer.setNeedsPostLayoutCompositingUpdateOnAncestors();
    }

    return repaintRequired;
}

bool RenderLayerCompositor::updateReflectionCompositingState(RenderLayer& layer, const RenderLayer* compositingAncestor, RequiresCompositingData& queryData)
{
    bool layerChanged = updateExplicitBacking(layer, queryData);

    // See if we need content or clipping layers. Methods called here should assume
    // that the compositing state of descendant layers has not been updated yet.
    if (layer.backing() && layer.backing()->updateConfiguration(compositingAncestor))
        layerChanged = true;

    return layerChanged;
}

void RenderLayerCompositor::repaintOnCompositingChange(RenderLayer& layer, RenderLayerModelObject* repaintContainer)
{
    // If the renderer is not attached yet, no need to repaint.
    if (&layer.renderer() != &m_renderView && !layer.renderer().parent())
        return;

    layer.repaintIncludingNonCompositingDescendants(repaintContainer);
    if (repaintContainer == &m_renderView) {
        // The contents of this layer may be moving between the window
        // and a GraphicsLayer, so we need to make sure the window system
        // synchronizes those changes on the screen.
        m_renderView.frameView().setNeedsOneShotDrawingSynchronization();
    }
}

// This method assumes that layout is up-to-date, unlike repaintOnCompositingChange().
void RenderLayerCompositor::repaintInCompositedAncestor(RenderLayer& layer, const LayoutRect& rect)
{
    auto* compositedAncestor = layer.enclosingCompositingLayerForRepaint(ExcludeSelf).layer;
    if (!compositedAncestor)
        return;

    ASSERT(compositedAncestor->backing());
    LayoutRect repaintRect = rect;
    repaintRect.move(layer.offsetFromAncestor(compositedAncestor));
    compositedAncestor->setBackingNeedsRepaintInRect(repaintRect);

    // The contents of this layer may be moving from a GraphicsLayer to the window,
    // so we need to make sure the window system synchronizes those changes on the screen.
    if (compositedAncestor->isRenderViewLayer())
        m_renderView.frameView().setNeedsOneShotDrawingSynchronization();
}

void RenderLayerCompositor::layerWillBeRemoved(RenderLayer& parent, RenderLayer& child)
{
    if (parent.renderer().renderTreeBeingDestroyed())
        return;

    if (child.isComposited())
        repaintInCompositedAncestor(child, child.backing()->compositedBounds()); // FIXME: do via dirty bits?
    else if (child.paintsIntoProvidedBacking()) {
        auto* backingProviderLayer = child.backingProviderLayer();
        // FIXME: Optimize this repaint.
        backingProviderLayer->setBackingNeedsRepaint();
        backingProviderLayer->backing()->removeBackingSharingLayer(child, { });
    } else
        return;

    child.clearRepaintContainer();
    child.setNeedsCompositingLayerConnection();
}

RenderLayer* RenderLayerCompositor::enclosingNonStackingClippingLayer(const RenderLayer& layer) const
{
    for (auto* parent = layer.parent(); parent; parent = parent->parent()) {
        if (parent->isStackingContext())
            return nullptr;
        if (parent->renderer().hasClipOrNonVisibleOverflow())
            return parent;
    }
    return nullptr;
}

void RenderLayerCompositor::computeExtent(const LayerOverlapMap& overlapMap, const RenderLayer& layer, OverlapExtent& extent) const
{
    if (extent.extentComputed)
        return;

    auto markExtentAsComputed = WTF::makeScopeExit([&]() {
        extent.extentComputed = true;
    });

    RenderLayerModelObject& renderer = layer.renderer();
    if (renderer.isStickilyPositioned()) {
        // Use rectangle that represents union of all possible sticky element positions,
        // because it could be moved around without re-computing overlap.
        auto const& box = downcast<RenderBoxModelObject>(renderer);
        StickyPositionViewportConstraints constraints;
        auto constrainingRectForStickyPosition = box.constrainingRectForStickyPosition();
        box.computeStickyPositionConstraints(constraints, constrainingRectForStickyPosition);
        extent.bounds = LayoutRect(constraints.computeStickyExtent());
        return;
    }

    LayoutRect layerBounds;
    if (extent.hasTransformAnimation)
        extent.animationCausesExtentUncertainty = !layer.getOverlapBoundsIncludingChildrenAccountingForTransformAnimations(layerBounds);
    else
        layerBounds = layer.overlapBounds();
    
    // In the animating transform case, we avoid double-accounting for the transform because
    // we told pushMappingsToAncestor() to ignore transforms earlier.
    extent.bounds = enclosingLayoutRect(overlapMap.geometryMap().absoluteRect(layerBounds));

    // Empty rects never intersect, but we need them to for the purposes of overlap testing.
    if (extent.bounds.isEmpty())
        extent.bounds.setSize(LayoutSize(1, 1));

    if (renderer.isFixedPositioned() && renderer.container() == &m_renderView) {
        // Because fixed elements get moved around without re-computing overlap, we have to compute an overlap
        // rect that covers all the locations that the fixed element could move to.
        extent.bounds = m_renderView.frameView().fixedScrollableAreaBoundsInflatedForScrolling(extent.bounds);
    }
}

enum class AncestorTraversal { Continue, Stop };

// This is a simplified version of containing block walking that only handles absolute and fixed position.
template <typename Function>
static AncestorTraversal traverseAncestorLayers(const RenderLayer& layer, Function&& function)
{
    auto positioningBehavior = layer.renderer().style().position();
    RenderLayer* nextPaintOrderParent = layer.paintOrderParent();
    
    for (const auto* ancestorLayer = layer.parent(); ancestorLayer; ancestorLayer = ancestorLayer->parent()) {
        bool inContainingBlockChain = true;

        switch (positioningBehavior) {
        case PositionType::Static:
        case PositionType::Relative:
        case PositionType::Sticky:
            break;
        case PositionType::Absolute:
            inContainingBlockChain = ancestorLayer->renderer().canContainAbsolutelyPositionedObjects();
            break;
        case PositionType::Fixed:
            inContainingBlockChain = ancestorLayer->renderer().canContainFixedPositionObjects();
            break;
        }

        if (function(*ancestorLayer, inContainingBlockChain, ancestorLayer == nextPaintOrderParent) == AncestorTraversal::Stop)
            return AncestorTraversal::Stop;

        if (inContainingBlockChain)
            positioningBehavior = ancestorLayer->renderer().style().position();
        
        if (ancestorLayer == nextPaintOrderParent)
            nextPaintOrderParent = ancestorLayer->paintOrderParent();
    }
    
    return AncestorTraversal::Continue;
}

void RenderLayerCompositor::computeClippingScopes(const RenderLayer& layer, OverlapExtent& extent) const
{
    if (extent.clippingScopesComputed)
        return;

    // FIXME: constrain the scopes (by composited stacking context ancestor I think).
    auto populateEnclosingClippingScopes = [](const RenderLayer& layer, const RenderLayer& rootLayer, LayerOverlapMap::LayerAndBoundsVector& clippingScopes) {

        auto createsClippingScope = [](const RenderLayer& layer) {
            return layer.hasCompositedScrollableOverflow();
        };

        clippingScopes.append({ const_cast<RenderLayer&>(rootLayer), { } });

        if (!layer.hasCompositedScrollingAncestor())
            return;

        traverseAncestorLayers(layer, [&](const RenderLayer& ancestorLayer, bool inContainingBlockChain, bool) {
            if (inContainingBlockChain && createsClippingScope(ancestorLayer)) {
                LayoutRect clipRect;
                if (CheckedPtr box = dynamicDowncast<RenderBox>(ancestorLayer.renderer())) {
                    // FIXME: This is expensive. Broken with transforms.
                    LayoutPoint offsetFromRoot = ancestorLayer.convertToLayerCoords(&rootLayer, { });
                    clipRect = box->overflowClipRect(offsetFromRoot);
                }

                LayerOverlapMap::LayerAndBounds layerAndBounds { const_cast<RenderLayer&>(ancestorLayer), clipRect };
                clippingScopes.insert(1, layerAndBounds); // Order is roots to leaves.
            }
            return AncestorTraversal::Continue;
        });
    };

    populateEnclosingClippingScopes(layer, rootRenderLayer(), extent.clippingScopes);
    extent.clippingScopesComputed = true;
}

LayoutRect RenderLayerCompositor::computeClippedOverlapBounds(LayerOverlapMap& overlapMap, const RenderLayer& layer, OverlapExtent& extent) const
{
    computeExtent(overlapMap, layer, extent);
    computeClippingScopes(layer, extent);

    LayoutRect clipRect;
    if (layer.hasCompositedScrollingAncestor()) {
        // Compute a clip up to the composited scrolling ancestor, then convert it to absolute coordinates.
        auto& scrollingScope = extent.clippingScopes.last();
        auto& scopeLayer = scrollingScope.layer;
        clipRect = layer.backgroundClipRect(RenderLayer::ClipRectsContext(&scopeLayer, PaintingClipRects, { RenderLayer::ClipRectsOption::Temporary })).rect();
        if (!clipRect.isInfinite())
            clipRect.setLocation(scopeLayer.convertToLayerCoords(&rootRenderLayer(), clipRect.location()));
    } else
        clipRect = layer.backgroundClipRect(RenderLayer::ClipRectsContext(&rootRenderLayer(), AbsoluteClipRects)).rect(); // FIXME: Incorrect for CSS regions.

    auto clippedBounds = extent.bounds;
    if (!clipRect.isInfinite()) {
        // With delegated page scaling, pageScaleFactor() is not applied by RenderView, so we should not scale here.
        if (!page().delegatesScaling())
            clipRect.scale(pageScaleFactor());

        clippedBounds.intersect(clipRect);
    }

    return clippedBounds;
}

void RenderLayerCompositor::addToOverlapMap(LayerOverlapMap& overlapMap, const RenderLayer& layer, OverlapExtent& extent) const
{
    if (layer.isRenderViewLayer())
        return;

    auto clippedBounds = computeClippedOverlapBounds(overlapMap, layer, extent);

    computeClippingScopes(layer, extent);
    overlapMap.add(layer, clippedBounds, extent.clippingScopes);
}

void RenderLayerCompositor::addDescendantsToOverlapMapRecursive(LayerOverlapMap& overlapMap, const RenderLayer& layer, const RenderLayer* ancestorLayer) const
{
    if (!canBeComposited(layer))
        return;

    // A null ancestorLayer is an indication that 'layer' has already been pushed.
    if (ancestorLayer) {
        overlapMap.geometryMap().pushMappingsToAncestor(&layer, ancestorLayer);
    
        OverlapExtent layerExtent;
        addToOverlapMap(overlapMap, layer, layerExtent);
    }

#if ASSERT_ENABLED
    LayerListMutationDetector mutationChecker(const_cast<RenderLayer&>(layer));
#endif

    for (auto* renderLayer : layer.negativeZOrderLayers())
        addDescendantsToOverlapMapRecursive(overlapMap, *renderLayer, &layer);

    for (auto* renderLayer : layer.normalFlowLayers())
        addDescendantsToOverlapMapRecursive(overlapMap, *renderLayer, &layer);

    for (auto* renderLayer : layer.positiveZOrderLayers())
        addDescendantsToOverlapMapRecursive(overlapMap, *renderLayer, &layer);
    
    if (ancestorLayer)
        overlapMap.geometryMap().popMappingsToAncestor(ancestorLayer);
}

void RenderLayerCompositor::updateOverlapMap(LayerOverlapMap& overlapMap, const RenderLayer& layer, OverlapExtent& layerExtent, bool didPushContainer, bool addLayerToOverlap, bool addDescendantsToOverlap) const
{
    if (addLayerToOverlap)
        addToOverlapMap(overlapMap, layer, layerExtent);

    if (addDescendantsToOverlap) {
        // If this is the first non-root layer to composite, we need to add all the descendants we already traversed to the overlap map.
        addDescendantsToOverlapMapRecursive(overlapMap, layer);
        LOG_WITH_STREAM(CompositingOverlap, stream << "layer " << &layer << " composited post descendant traversal, added recursive " << overlapMap);
    }

    if (didPushContainer) {
        overlapMap.popCompositingContainer(layer);
        LOG_WITH_STREAM(CompositingOverlap, stream << "layer " << &layer << " is composited or shared, popped container " << overlapMap);
    }
}

bool RenderLayerCompositor::layerOverlaps(const LayerOverlapMap& overlapMap, const RenderLayer& layer, OverlapExtent& extent) const
{
    computeExtent(overlapMap, layer, extent);
    computeClippingScopes(layer, extent);

    return overlapMap.overlapsLayers(layer, extent.bounds, extent.clippingScopes);
}

#if ENABLE(VIDEO)
bool RenderLayerCompositor::canAccelerateVideoRendering(RenderVideo& video) const
{
    if (!m_hasAcceleratedCompositing)
        return false;

    return video.supportsAcceleratedRendering();
}
#endif

void RenderLayerCompositor::frameViewDidChangeLocation(FloatPoint contentsOffset)
{
    if (m_overflowControlsHostLayer)
        m_overflowControlsHostLayer->setPosition(contentsOffset);
}

void RenderLayerCompositor::frameViewDidChangeSize()
{
    if (auto* layer = m_renderView.layer())
        layer->setNeedsCompositingGeometryUpdate();

    if (m_scrolledContentsLayer) {
        updateScrollLayerClipping();
        frameViewDidScroll();
        updateOverflowControlsLayers();

#if HAVE(RUBBER_BANDING)
        updateSizeAndPositionForOverhangAreaLayer();
#endif
    }
}

void RenderLayerCompositor::widgetDidChangeSize(RenderWidget& widget)
{
    if (!widget.hasLayer())
        return;

    auto& layer = *widget.layer();

    LOG_WITH_STREAM(Compositing, stream << "RenderLayerCompositor " << this << " widgetDidChangeSize (layer " << &layer << ")");

    // Widget size affects answer to requiresCompositingForFrame() so we need to trigger
    // a compositing update.
    layer.setNeedsPostLayoutCompositingUpdate();
    scheduleCompositingLayerUpdate();

    if (layer.isComposited())
        layer.backing()->updateAfterWidgetResize();
}

bool RenderLayerCompositor::hasCoordinatedScrolling() const
{
    RefPtr scrollingCoordinator = this->scrollingCoordinator();
    return scrollingCoordinator && scrollingCoordinator->coordinatesScrollingForFrameView(m_renderView.frameView());
}

void RenderLayerCompositor::updateScrollLayerPosition()
{
    ASSERT(!hasCoordinatedScrolling());
    ASSERT(m_scrolledContentsLayer);

    Ref frameView = m_renderView.frameView();
    IntPoint scrollPosition = frameView->scrollPosition();

    // We use scroll position here because the root content layer is offset to account for scrollOrigin (see LocalFrameView::positionForRootContentLayer).
    m_scrolledContentsLayer->setPosition(FloatPoint(-scrollPosition.x(), -scrollPosition.y()));

    if (RefPtr fixedBackgroundLayer = fixedRootBackgroundLayer())
        fixedBackgroundLayer->setPosition(frameView->scrollPositionForFixedPosition());
}

void RenderLayerCompositor::updateScrollLayerClipping()
{
    RefPtr layerForClipping = this->layerForClipping();
    if (!layerForClipping)
        return;

    auto layerSize = m_renderView.frameView().sizeForVisibleContent();
    layerForClipping->setSize(layerSize);
    layerForClipping->setPosition(positionForClipLayer());

#if ENABLE(SCROLLING_THREAD)
    if (layerForClipping == m_clipLayer) {
        EventRegion eventRegion;
        auto eventRegionContext = eventRegion.makeContext();
        eventRegionContext.unite(FloatRoundedRect(FloatRect({ }, layerSize)), m_renderView, RenderStyle::defaultStyleSingleton());
#if ENABLE(INTERACTION_REGIONS_IN_EVENT_REGION)
        eventRegionContext.copyInteractionRegionsToEventRegion(m_renderView.settings().interactionRegionMinimumCornerRadius());
#endif
        RefPtr { m_clipLayer }->setEventRegion(WTFMove(eventRegion));
    }
#endif
}

FloatPoint RenderLayerCompositor::positionForClipLayer() const
{
    Ref frameView = m_renderView.frameView();

    auto clipLayerPosition = LocalFrameView::positionForInsetClipLayer(frameView->scrollPosition(), frameView->obscuredContentInsets());
    return FloatPoint(frameView->insetForLeftScrollbarSpace() + clipLayerPosition.x(), clipLayerPosition.y());
}

void RenderLayerCompositor::frameViewDidScroll()
{
    if (!m_scrolledContentsLayer)
        return;

    // If there's a scrolling coordinator that manages scrolling for this frame view,
    // it will also manage updating the scroll layer position.
    if (hasCoordinatedScrolling()) {
        // We have to schedule a flush in order for the main TiledBacking to update its tile coverage.
        scheduleRenderingUpdate();
        return;
    }

    updateScrollLayerPosition();
}

void RenderLayerCompositor::frameViewDidAddOrRemoveScrollbars()
{
    updateOverflowControlsLayers();
}

void RenderLayerCompositor::frameViewDidLayout()
{
    if (auto* renderViewBacking = m_renderView.layer()->backing())
        renderViewBacking->adjustTiledBackingCoverage();
}

void RenderLayerCompositor::rootLayerConfigurationChanged()
{
    auto* renderViewBacking = m_renderView.layer()->backing();
    if (renderViewBacking && renderViewBacking->isFrameLayerWithTiledBacking()) {
        m_renderView.layer()->setNeedsCompositingConfigurationUpdate();
        scheduleCompositingLayerUpdate();
    }
}

void RenderLayerCompositor::updateCompositingForLayerTreeAsTextDump()
{
    Ref frameView = m_renderView.frameView();

    frameView->updateLayoutAndStyleIfNeededRecursive(LayoutOptions::UpdateCompositingLayers);

    updateEventRegions();

    for (RefPtr child = frameView->frame().tree().firstRenderedChild(); child; child = child->tree().traverseNextRendered()) {
        RefPtr localChild = dynamicDowncast<LocalFrame>(child);
        if (!localChild)
            continue;
        if (auto* renderer = localChild->contentRenderer())
            renderer->compositor().updateEventRegions();
    }

    updateCompositingLayers(CompositingUpdateType::AfterLayout);

    if (!m_rootContentsLayer)
        return;

    flushPendingLayerChanges(true);
    // We need to trigger an update because the flushPendingLayerChanges() will have pushed changes to platform layers,
    // which may cause painting to happen in the current runloop.
    protectedPage()->triggerRenderingUpdateForTesting();
}

String RenderLayerCompositor::layerTreeAsText(OptionSet<LayerTreeAsTextOptions> options, uint32_t baseIndent)
{
    LOG_WITH_STREAM(Compositing, stream << "RenderLayerCompositor " << this << " layerTreeAsText");

    updateCompositingForLayerTreeAsTextDump();

    // Exclude any implicitly created layers that wrap the root contents layer, unless the caller explicitly requested the true root to be included.
    RefPtr dumpRootLayer = (options & LayerTreeAsTextOptions::IncludeRootLayers) ? rootGraphicsLayer() : m_rootContentsLayer;

    if (!dumpRootLayer)
        return String();

    // We skip dumping the scroll and clip layers to keep layerTreeAsText output
    // similar between platforms.
    String layerTreeText = dumpRootLayer->layerTreeAsText(options, baseIndent);

    // Dump an empty layer tree only if the only composited layer is the main frame's tiled backing,
    // so that tests expecting us to drop out of accelerated compositing when there are no layers succeed.
    if (!hasContentCompositingLayers() && documentUsesTiledBacking() && !(options & LayerTreeAsTextOptions::IncludeTileCaches) && !(options & LayerTreeAsTextOptions::IncludeRootLayerProperties))
        layerTreeText = emptyString();

    // The true root layer is not included in the dump, so if we want to report
    // its repaint rects, they must be included here.
    if (options & LayerTreeAsTextOptions::IncludeRepaintRects)
        return makeString(m_renderView.frameView().trackedRepaintRectsAsText(), layerTreeText);

    return layerTreeText;
}

std::optional<String> RenderLayerCompositor::platformLayerTreeAsText(Element& element, OptionSet<PlatformLayerTreeAsTextFlags> flags)
{
    LOG_WITH_STREAM(Compositing, stream << "RenderLayerCompositor " << this << " platformLayerTreeAsText");

    updateCompositingForLayerTreeAsTextDump();
    if (!element.renderer() || !element.renderer()->hasLayer())
        return std::nullopt;

    auto& layerModelObject = downcast<RenderLayerModelObject>(*element.renderer());
    if (!layerModelObject.layer()->isComposited())
        return std::nullopt;

    auto* backing = layerModelObject.layer()->backing();
    return backing->graphicsLayer()->platformLayerTreeAsText(flags);
}

static RenderView* frameContentsRenderView(RenderWidget& renderer)
{
    if (RefPtr contentDocument = renderer.protectedFrameOwnerElement()->contentDocument())
        return contentDocument->renderView();

    return nullptr;
}

RenderLayerCompositor* RenderLayerCompositor::frameContentsCompositor(RenderWidget& renderer)
{
    if (auto* view = frameContentsRenderView(renderer))
        return &view->compositor();

    return nullptr;
}

auto RenderLayerCompositor::attachWidgetContentLayersIfNecessary(RenderWidget& renderer) -> WidgetLayerAttachment
{
    auto* layer = renderer.layer();
    if (!layer->isComposited())
        return { false, false };

    auto* backing = layer->backing();
    RefPtr hostingLayer = backing->parentForSublayers();

    bool isVisible = renderer.style().usedVisibility() == Visibility::Visible;

    auto addContentsLayerChildIfNecessary = [&](GraphicsLayer& contentsLayer, bool isVisible) -> bool {
        if (isVisible && hostingLayer->children().size() == 1 && hostingLayer->children()[0].ptr() == &contentsLayer)
            return false;

        if (!isVisible && hostingLayer->children().isEmpty())
            return false;

        hostingLayer->removeAllChildren();
        if (isVisible)
            hostingLayer->addChild(contentsLayer);
        return true;
    };

    WidgetLayerAttachment result;
    if (isCompositedPlugin(renderer)) {
        if (RefPtr contentsLayer = backing->layerForContents()) {
            result.widgetLayersAttachedAsChildren = isVisible;
            result.layerHierarchyChanged = addContentsLayerChildIfNecessary(*contentsLayer, isVisible);
            if (!isLayerForPluginWithScrollCoordinatedContents(*layer))
                return result;

            RefPtr scrollingCoordinator = this->scrollingCoordinator();
            if (!scrollingCoordinator)
                return result;

            auto pluginHostingNodeID = backing->scrollingNodeIDForRole(ScrollCoordinationRole::PluginHosting);
            if (!pluginHostingNodeID)
                return result;

            CheckedPtr renderEmbeddedObject = dynamicDowncast<RenderEmbeddedObject>(renderer);
            renderEmbeddedObject->willAttachScrollingNode();

            if (auto pluginScrollingNodeID = renderEmbeddedObject->scrollingNodeID()) {
                if (isVisible) {
                    scrollingCoordinator->insertNode(m_renderView.frameView().frame().rootFrame().frameID(), ScrollingNodeType::PluginScrolling, *pluginScrollingNodeID, *pluginHostingNodeID, 0);
                    renderEmbeddedObject->didAttachScrollingNode();
                } else
                    scrollingCoordinator->unparentNode(*pluginScrollingNodeID);
            }
            return result;
        }
    }

    auto* innerCompositor = frameContentsCompositor(renderer);
    if (!innerCompositor || !innerCompositor->usesCompositing() || innerCompositor->rootLayerAttachment() != RootLayerAttachedViaEnclosingFrame)
        return result;

    result.widgetLayersAttachedAsChildren = isVisible;
    if (RefPtr iframeRootLayer = innerCompositor->rootGraphicsLayer())
        result.layerHierarchyChanged = addContentsLayerChildIfNecessary(*iframeRootLayer, isVisible);

    if (auto frameHostingNodeID = backing->scrollingNodeIDForRole(ScrollCoordinationRole::FrameHosting)) {
        RefPtr scrollingCoordinator = this->scrollingCoordinator();
        if (!scrollingCoordinator)
            return result;

        auto* contentsRenderView = frameContentsRenderView(renderer);
        if (auto frameRootScrollingNodeID = contentsRenderView->frameView().scrollingNodeID()) {
            if (isVisible)
                scrollingCoordinator->insertNode(m_renderView.frameView().frame().rootFrame().frameID(), ScrollingNodeType::Subframe, *frameRootScrollingNodeID, *frameHostingNodeID, 0);
            else
                scrollingCoordinator->unparentNode(*frameRootScrollingNodeID);
        }
    }

    return result;
}

void RenderLayerCompositor::repaintCompositedLayers()
{
    recursiveRepaintLayer(rootRenderLayer());
}

void RenderLayerCompositor::recursiveRepaintLayer(RenderLayer& layer)
{
    layer.updateLayerListsIfNeeded();

    // FIXME: This method does not work correctly with transforms.
    if (layer.isComposited() && !layer.backing()->paintsIntoCompositedAncestor())
        layer.setBackingNeedsRepaint();

#if ASSERT_ENABLED
    LayerListMutationDetector mutationChecker(layer);
#endif

    if (layer.hasCompositingDescendant()) {
        for (auto* renderLayer : layer.negativeZOrderLayers())
            recursiveRepaintLayer(*renderLayer);

        for (auto* renderLayer : layer.positiveZOrderLayers())
            recursiveRepaintLayer(*renderLayer);
    }

    for (auto* renderLayer : layer.normalFlowLayers())
        recursiveRepaintLayer(*renderLayer);
}

bool RenderLayerCompositor::layerRepaintTargetsBackingSharingLayer(RenderLayer& layer, BackingSharingState& sharingState) const
{
    if (sharingState.backingProviderCandidates().isEmpty())
        return false;

    for (const auto* currLayer = &layer; currLayer; currLayer = currLayer->paintOrderParent()) {
        if (compositedWithOwnBackingStore(*currLayer))
            return false;
        
        if (currLayer->paintsIntoProvidedBacking())
            return false;

        if (sharingState.backingProviderForLayer(*currLayer))
            return true;
    }

    return false;
}

RenderLayer& RenderLayerCompositor::rootRenderLayer() const
{
    return *m_renderView.layer();
}

GraphicsLayer* RenderLayerCompositor::rootGraphicsLayer() const
{
    if (m_overflowControlsHostLayer)
        return m_overflowControlsHostLayer.get();
    return m_rootContentsLayer.get();
}

void RenderLayerCompositor::setIsInWindow(bool isInWindow)
{
    LOG(Compositing, "RenderLayerCompositor %p setIsInWindow %d", this, isInWindow);

    if (!usesCompositing())
        return;

    if (RefPtr rootLayer = rootGraphicsLayer()) {
        GraphicsLayer::traverse(*rootLayer, [isInWindow](GraphicsLayer& layer) {
            layer.setIsInWindow(isInWindow);
        });
    }

    if (isInWindow) {
        if (m_rootLayerAttachment != RootLayerUnattached)
            return;

        RootLayerAttachment attachment = isRootFrameCompositor() ? RootLayerAttachedViaChromeClient : RootLayerAttachedViaEnclosingFrame;
        attachRootLayer(attachment);
#if PLATFORM(IOS_FAMILY)
        if (m_legacyScrollingLayerCoordinator) {
            m_legacyScrollingLayerCoordinator->registerAllViewportConstrainedLayers(*this);
            m_legacyScrollingLayerCoordinator->registerAllScrollingLayers();
        }
#endif
    } else {
        if (m_rootLayerAttachment == RootLayerUnattached)
            return;

        detachRootLayer();
#if PLATFORM(IOS_FAMILY)
        if (m_legacyScrollingLayerCoordinator) {
            m_legacyScrollingLayerCoordinator->unregisterAllViewportConstrainedLayers();
            m_legacyScrollingLayerCoordinator->unregisterAllScrollingLayers();
        }
#endif
    }
}

void RenderLayerCompositor::invalidateEventRegionForAllFrames()
{
    for (RefPtr frame = &page().mainFrame(); frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(frame);
        if (!localFrame)
            continue;
        if (auto* view = localFrame->contentRenderer())
            view->compositor().invalidateEventRegionForAllLayers();
    }
}

void RenderLayerCompositor::invalidateEventRegionForAllLayers()
{
    applyToCompositedLayerIncludingDescendants(*m_renderView.layer(), [](auto& layer) {
        layer.invalidateEventRegion(RenderLayer::EventRegionInvalidationReason::SettingDidChange);
    });
}

void RenderLayerCompositor::clearBackingForAllLayers()
{
    applyToCompositedLayerIncludingDescendants(*m_renderView.layer(), [](auto& layer) { layer.clearBacking({ }); });
}

void RenderLayerCompositor::updateRootLayerPosition()
{
    if (RefPtr rootContentsLayer = m_rootContentsLayer) {
        Ref frameView = m_renderView.frameView();
        rootContentsLayer->setSize(frameView->contentsSize());
        rootContentsLayer->setPosition(frameView->positionForRootContentLayer());
        rootContentsLayer->setAnchorPoint(FloatPoint3D());
    }

    updateScrollLayerClipping();

#if HAVE(RUBBER_BANDING)
    if (m_contentShadowLayer && m_rootContentsLayer) {
        m_contentShadowLayer->setPosition(m_rootContentsLayer->position());
        RefPtr { m_contentShadowLayer }->setSize(m_rootContentsLayer->size());
    }

    updateLayerForTopOverhangColorExtension(m_layerForTopOverhangColorExtension);
    updateSizeAndPositionForTopOverhangColorExtensionLayer();
    updateLayerForTopOverhangImage(m_layerForTopOverhangImage);
    updateLayerForBottomOverhangArea(m_layerForBottomOverhangArea);
    updateLayerForHeader(m_layerForHeader);
    updateLayerForFooter(m_layerForFooter);
#endif // HAVE(RUBBER_BANDING)
}

bool RenderLayerCompositor::has3DContent() const
{
    return layerHas3DContent(rootRenderLayer());
}

bool RenderLayerCompositor::needsToBeComposited(const RenderLayer& layer, RequiresCompositingData& queryData) const
{
    if (!canBeComposited(layer))
        return false;

    return requiresCompositingLayer(layer, queryData) || layer.mustCompositeForIndirectReasons() || (usesCompositing() && layer.isRenderViewLayer());
}

// Note: this specifies whether the RL needs a compositing layer for intrinsic reasons.
// Use needsToBeComposited() to determine if a RL actually needs a compositing layer.
// FIXME: is clipsCompositingDescendants() an intrinsic reason?
bool RenderLayerCompositor::requiresCompositingLayer(const RenderLayer& layer, RequiresCompositingData& queryData) const
{
    auto& renderer = rendererForCompositingTests(layer);

    if (!renderer.layer()) {
        ASSERT_NOT_REACHED();
        return false;
    }

    // The root layer always has a compositing layer, but it may not have backing.
    if (requiresCompositingForTransform(renderer)
        || requiresCompositingForAnimation(renderer)
        || requiresCompositingForPosition(renderer, *renderer.layer(), queryData)
        || requiresCompositingForCanvas(renderer)
        || requiresCompositingForFilters(renderer)
        || requiresCompositingForWillChange(renderer)
        || requiresCompositingForBackfaceVisibility(renderer)
        || requiresCompositingForViewTransition(renderer)
        || requiresCompositingForVideo(renderer)
        || requiresCompositingForModel(renderer)
        || requiresCompositingForFrame(renderer, queryData)
        || requiresCompositingForPlugin(renderer, queryData)
        || requiresCompositingForOverflowScrolling(*renderer.layer(), queryData)
        || requiresCompositingForAnchorPositioning(*renderer.layer())) {
        queryData.intrinsic = true;
        return true;
    }
    return false;
}

bool RenderLayerCompositor::canBeComposited(const RenderLayer& layer) const
{
    if (m_hasAcceleratedCompositing && layer.isSelfPaintingLayer()) {
        if (layer.renderer().isSkippedContent())
            return false;

        if (!layer.isInsideFragmentedFlow())
            return true;

        // CSS Regions flow threads do not need to be composited as we use composited RenderFragmentContainers
        // to render the background of the RenderFragmentedFlow.
        if (layer.isRenderFragmentedFlow())
            return false;

        return true;
    }
    return false;
}

#if ENABLE(FULLSCREEN_API)
enum class FullScreenDescendant { Yes, No, NotApplicable };
static FullScreenDescendant isDescendantOfFullScreenLayer(const RenderLayer& layer)
{
    RefPtr documentFullscreen = layer.renderer().document().fullscreenIfExists();
    if (!documentFullscreen)
        return FullScreenDescendant::NotApplicable;

    RefPtr fullScreenElement = documentFullscreen->fullscreenElement();
    if (!fullScreenElement)
        return FullScreenDescendant::NotApplicable;

    auto* fullScreenRenderer = dynamicDowncast<RenderLayerModelObject>(fullScreenElement->renderer());
    if (!fullScreenRenderer)
        return FullScreenDescendant::NotApplicable;

    auto* fullScreenLayer = fullScreenRenderer->layer();
    if (!fullScreenLayer)
        return FullScreenDescendant::NotApplicable;

    auto backdropRenderer = fullScreenRenderer->backdropRenderer();
    if (backdropRenderer && backdropRenderer.get() == &layer.renderer())
        return FullScreenDescendant::Yes;

    return layer.isDescendantOf(*fullScreenLayer) ? FullScreenDescendant::Yes : FullScreenDescendant::No;
}
#endif

bool RenderLayerCompositor::requiresOwnBackingStore(const RenderLayer& layer, const RenderLayer* compositingAncestorLayer, const LayoutRect& layerCompositedBoundsInAncestor, const LayoutRect& ancestorCompositedBounds) const
{
    auto& renderer = layer.renderer();

    if (compositingAncestorLayer
        && !(compositingAncestorLayer->backing()->graphicsLayer()->drawsContent()
            || compositingAncestorLayer->backing()->paintsIntoWindow()
            || compositingAncestorLayer->backing()->paintsIntoCompositedAncestor()))
        return true;

    RequiresCompositingData queryData;
    if (layer.isRenderViewLayer()
        || layer.transform() // note: excludes perspective and transformStyle3D.
        || requiresCompositingForAnimation(renderer)
        || requiresCompositingForPosition(renderer, layer, queryData)
        || requiresCompositingForCanvas(renderer)
        || requiresCompositingForFilters(renderer)
        || requiresCompositingForWillChange(renderer)
        || requiresCompositingForBackfaceVisibility(renderer)
        || requiresCompositingForViewTransition(renderer)
        || requiresCompositingForVideo(renderer)
        || requiresCompositingForModel(renderer)
        || requiresCompositingForFrame(renderer, queryData)
        || requiresCompositingForPlugin(renderer, queryData)
        || requiresCompositingForOverflowScrolling(layer, queryData)
        || requiresCompositingForAnchorPositioning(layer)
        || needsContentsCompositingLayer(layer)
        || renderer.isTransparent()
        || renderer.hasMask()
        || renderer.hasReflection()
        || renderer.hasFilter()
#if HAVE(CORE_MATERIAL)
        || renderer.hasAppleVisualEffect()
#endif
        || renderer.hasBackdropFilter())
        return true;

    if (layer.isComposited() && layer.backing()->hasBackingSharingLayers())
        return true;

    // FIXME: We really need to keep track of the ancestor layer that has its own backing store.
    if (!ancestorCompositedBounds.contains(layerCompositedBoundsInAncestor))
        return true;

    if (layer.mustCompositeForIndirectReasons()) {
        IndirectCompositingReason reason = layer.indirectCompositingReason();
        return reason == IndirectCompositingReason::Overlap
            || reason == IndirectCompositingReason::OverflowScrollPositioning
            || reason == IndirectCompositingReason::Stacking
            || reason == IndirectCompositingReason::BackgroundLayer
            || reason == IndirectCompositingReason::GraphicalEffect
            || reason == IndirectCompositingReason::Preserve3D; // preserve-3d has to create backing store to ensure that 3d-transformed elements intersect.
    }

    return false;
}

OptionSet<CompositingReason> RenderLayerCompositor::reasonsForCompositing(const RenderLayer& layer) const
{
    OptionSet<CompositingReason> reasons;

    if (!layer.isComposited())
        return reasons;

    RequiresCompositingData queryData;

    auto& renderer = rendererForCompositingTests(layer);

    if (requiresCompositingForTransform(renderer))
        reasons.add(CompositingReason::Transform3D);

    if (requiresCompositingForVideo(renderer))
        reasons.add(CompositingReason::Video);
    else if (requiresCompositingForCanvas(renderer))
        reasons.add(CompositingReason::Canvas);
    else if (requiresCompositingForModel(renderer))
        reasons.add(CompositingReason::Model);
    else if (requiresCompositingForPlugin(renderer, queryData))
        reasons.add(CompositingReason::Plugin);
    else if (requiresCompositingForFrame(renderer, queryData))
        reasons.add(CompositingReason::IFrame);

    if ((canRender3DTransforms() && renderer.style().backfaceVisibility() == BackfaceVisibility::Hidden))
        reasons.add(CompositingReason::BackfaceVisibilityHidden);

    if (requiresCompositingForAnimation(renderer))
        reasons.add(CompositingReason::Animation);

    if (requiresCompositingForFilters(renderer))
        reasons.add(CompositingReason::Filters);

    if (requiresCompositingForWillChange(renderer))
        reasons.add(CompositingReason::WillChange);

    if (requiresCompositingForPosition(renderer, *renderer.layer(), queryData))
        reasons.add(renderer.isFixedPositioned() ? CompositingReason::PositionFixed : CompositingReason::PositionSticky);

    if (requiresCompositingForOverflowScrolling(*renderer.layer(), queryData))
        reasons.add(CompositingReason::OverflowScrolling);

    if (requiresCompositingForAnchorPositioning(*renderer.layer()))
        reasons.add(CompositingReason::AnchorPositioning);

    switch (renderer.layer()->indirectCompositingReason()) {
    case IndirectCompositingReason::None:
        break;
    case IndirectCompositingReason::Clipping:
        reasons.add(CompositingReason::ClipsCompositingDescendants);
        break;
    case IndirectCompositingReason::Stacking:
        reasons.add(CompositingReason::Stacking);
        break;
    case IndirectCompositingReason::OverflowScrollPositioning:
        reasons.add(CompositingReason::OverflowScrollPositioning);
        break;
    case IndirectCompositingReason::Overlap:
        reasons.add(CompositingReason::Overlap);
        break;
    case IndirectCompositingReason::BackgroundLayer:
        reasons.add(CompositingReason::NegativeZIndexChildren);
        break;
    case IndirectCompositingReason::GraphicalEffect:
        if (renderer.isTransformed())
            reasons.add(CompositingReason::TransformWithCompositedDescendants);

        if (renderer.isTransparent())
            reasons.add(CompositingReason::OpacityWithCompositedDescendants);

        if (renderer.hasMask())
            reasons.add(CompositingReason::MaskWithCompositedDescendants);

        if (renderer.hasReflection())
            reasons.add(CompositingReason::ReflectionWithCompositedDescendants);

        if (renderer.hasFilter() || renderer.hasBackdropFilter())
            reasons.add(CompositingReason::FilterWithCompositedDescendants);

#if HAVE(CORE_MATERIAL)
        if (renderer.hasAppleVisualEffect())
            reasons.add(CompositingReason::FilterWithCompositedDescendants);
#endif

        if (layer.isBackdropRoot())
            reasons.add(CompositingReason::BackdropRoot);

        if (layer.isolatesCompositedBlending())
            reasons.add(CompositingReason::IsolatesCompositedBlendingDescendants);

        if (layer.hasBlendMode())
            reasons.add(CompositingReason::BlendingWithCompositedDescendants);

        if (renderer.hasClipPath())
            reasons.add(CompositingReason::ClipsCompositingDescendants);
        break;
    case IndirectCompositingReason::Perspective:
        reasons.add(CompositingReason::Perspective);
        break;
    case IndirectCompositingReason::Preserve3D:
        reasons.add(CompositingReason::Preserve3D);
        break;
    }

    if (usesCompositing() && renderer.layer()->isRenderViewLayer())
        reasons.add(CompositingReason::Root);

    return reasons;
}

static ASCIILiteral compositingReasonToString(CompositingReason reason)
{
    switch (reason) {
    case CompositingReason::Transform3D: return "3D transform"_s;
    case CompositingReason::Video: return "video"_s;
    case CompositingReason::Canvas: return "canvas"_s;
    case CompositingReason::Plugin: return "plugin"_s;
    case CompositingReason::IFrame: return "iframe"_s;
    case CompositingReason::BackfaceVisibilityHidden: return "backface-visibility: hidden"_s;
    case CompositingReason::ClipsCompositingDescendants: return "clips compositing descendants"_s;
    case CompositingReason::Animation: return "animation"_s;
    case CompositingReason::Filters: return "filters"_s;
    case CompositingReason::PositionFixed: return "position: fixed"_s;
    case CompositingReason::PositionSticky: return "position: sticky"_s;
    case CompositingReason::OverflowScrolling: return "async overflow scrolling"_s;
    case CompositingReason::Stacking: return "stacking"_s;
    case CompositingReason::Overlap: return "overlap"_s;
    case CompositingReason::OverflowScrollPositioning: return "overflow scroll positioning"_s;
    case CompositingReason::NegativeZIndexChildren: return "negative z-index children"_s;
    case CompositingReason::TransformWithCompositedDescendants: return "transform with composited descendants"_s;
    case CompositingReason::OpacityWithCompositedDescendants: return "opacity with composited descendants"_s;
    case CompositingReason::MaskWithCompositedDescendants: return "mask with composited descendants"_s;
    case CompositingReason::ReflectionWithCompositedDescendants: return "reflection with composited descendants"_s;
    case CompositingReason::FilterWithCompositedDescendants: return "filter with composited descendants"_s;
    case CompositingReason::BlendingWithCompositedDescendants: return "blending with composited descendants"_s;
    case CompositingReason::IsolatesCompositedBlendingDescendants: return "isolates composited blending descendants"_s;
    case CompositingReason::Perspective: return "perspective"_s;
    case CompositingReason::Preserve3D: return "preserve-3d"_s;
    case CompositingReason::WillChange: return "will-change"_s;
    case CompositingReason::Root: return "root"_s;
    case CompositingReason::Model: return "model"_s;
    case CompositingReason::BackdropRoot: return "backdrop root"_s;
    case CompositingReason::AnchorPositioning: return "anchor positioning"_s;
    }
    return ""_s;
}

#if !LOG_DISABLED
ASCIILiteral RenderLayerCompositor::logOneReasonForCompositing(const RenderLayer& layer)
{
    for (auto reason : reasonsForCompositing(layer))
        return compositingReasonToString(reason);
    return ""_s;
}
#endif


static bool canUseDescendantClippingLayer(const RenderLayer& layer)
{
    if (layer.isolatesCompositedBlending())
        return false;

    // We can only use the "descendant clipping layer" strategy when the clip rect is entirely within
    // the border box, because of interactions with border-radius clipping and compositing.
    if (auto* renderer = layer.renderBox(); renderer && renderer->hasClip()) {
        auto borderBoxRect = renderer->borderBoxRect();
        auto clipRect = renderer->clipRect({ });
        
        bool clipRectInsideBorderRect = intersection(borderBoxRect, clipRect) == clipRect;
        return clipRectInsideBorderRect;
    }

    return true;
}

// Return true if the given layer has some ancestor in the RenderLayer hierarchy that clips,
// up to the enclosing compositing ancestor. This is required because compositing layers are parented
// according to the z-order hierarchy, yet clipping goes down the renderer hierarchy.
// Thus, a RenderLayer can be clipped by a RenderLayer that is an ancestor in the renderer hierarchy,
// but a sibling in the z-order hierarchy.
// FIXME: can we do this without a tree walk?
bool RenderLayerCompositor::clippedByAncestor(RenderLayer& layer, const RenderLayer* compositingAncestor) const
{
    ASSERT(layer.isComposited());
    if (!compositingAncestor)
        return false;

    if (layer.renderer().capturedInViewTransition())
        return false;

    // If the compositingAncestor clips, that will be taken care of by clipsCompositingDescendants(),
    // so we only care about clipping between its first child that is our ancestor (the computeClipRoot),
    // and layer. The exception is when the compositingAncestor isolates composited blending children,
    // in this case it is not allowed to clipsCompositingDescendants() and each of its children
    // will be clippedByAncestor()s, including the compositingAncestor.
    auto* computeClipRoot = compositingAncestor;
    if (canUseDescendantClippingLayer(*compositingAncestor)) {
        computeClipRoot = nullptr;
        auto* parent = &layer;
        while (parent) {
            auto* next = parent->parent();
            if (next == compositingAncestor) {
                computeClipRoot = parent;
                break;
            }
            parent = next;
        }

        if (!computeClipRoot || computeClipRoot == &layer)
            return false;
    }

    auto backgroundClipRect = layer.backgroundClipRect(RenderLayer::ClipRectsContext(computeClipRoot, PaintingClipRects, RenderLayer::clipRectTemporaryOptions));
    return !backgroundClipRect.isInfinite(); // FIXME: Incorrect for CSS regions.
}

bool RenderLayerCompositor::updateAncestorClippingStack(const RenderLayer& layer, const RenderLayer* compositingAncestor) const
{
    ASSERT(layer.isComposited());

    auto clippingStack = computeAncestorClippingStack(layer, compositingAncestor);
    return layer.backing()->updateAncestorClippingStack(WTFMove(clippingStack));
}

Vector<CompositedClipData> RenderLayerCompositor::computeAncestorClippingStack(const RenderLayer& layer, const RenderLayer* compositingAncestor) const
{
    // On first pass in WK1, the root may not have become composited yet.
    if (!compositingAncestor)
        return { };

    // We'll start by building a child-to-ancestors stack.
    Vector<CompositedClipData> newStack;

    // Walk up the containing block chain to composited ancestor, prepending an entry to the clip stack for:
    // * each composited scrolling layer
    // * each set of RenderLayers which contribute a clip.
    bool haveNonScrollableClippingIntermediateLayer = false;
    const RenderLayer* currentClippedLayer = &layer;
    
    auto pushNonScrollableClip = [&](const RenderLayer& clippedLayer, const RenderLayer& clippingRoot, ShouldRespectOverflowClip respectClip = IgnoreOverflowClip) {
        // Use IgnoreOverflowClip to ignore overflow contributed by clippingRoot (which may be a scroller).
        OptionSet<RenderLayer::ClipRectsOption> options = RenderLayer::ClipRectsOption::Temporary;
        if (respectClip == RespectOverflowClip)
            options.add(RenderLayer::ClipRectsOption::RespectOverflowClip);

        auto backgroundClip = clippedLayer.backgroundClipRect(RenderLayer::ClipRectsContext(&clippingRoot, PaintingClipRects, options));
        ASSERT(!backgroundClip.affectedByRadius());
        auto clipRect = backgroundClip.rect();
        if (clipRect.isInfinite())
            return;

        auto infiniteRect = LayoutRect::infiniteRect();
        auto renderableInfiniteRect = [] {
            // Return a infinite-like rect whose values are such that, when converted to float pixel values, they can reasonably represent device pixels.
            return LayoutRect(LayoutUnit::nearlyMin() / 32, LayoutUnit::nearlyMin() / 32, LayoutUnit::nearlyMax() / 16, LayoutUnit::nearlyMax() / 16);
        }();

        if (clipRect.width() == infiniteRect.width()) {
            clipRect.setX(renderableInfiniteRect.x());
            clipRect.setWidth(renderableInfiniteRect.width());
        }

        if (clipRect.height() == infiniteRect.height()) {
            clipRect.setY(renderableInfiniteRect.y());
            clipRect.setHeight(renderableInfiniteRect.height());
        }

        auto offset = layer.convertToLayerCoords(&clippingRoot, { }, RenderLayer::AdjustForColumns);
        clipRect.moveBy(-offset);

        CompositedClipData clipData { const_cast<RenderLayer*>(&clippedLayer), LayoutRoundedRect { clipRect }, false };
        newStack.insert(0, WTFMove(clipData));
    };

    // Surprisingly, the deprecated CSS "clip" property on abspos ancestors of fixedpos elements clips them <https://github.com/w3c/csswg-drafts/issues/8336>.
    bool checkAbsoluteAncestorForClip = layer.renderer().isFixedPositioned();

    traverseAncestorLayers(layer, [&](const RenderLayer& ancestorLayer, bool isContainingBlockChain, bool /*isPaintOrderAncestor*/) {
        if (&ancestorLayer == compositingAncestor) {
            bool canUseDescendantClip = canUseDescendantClippingLayer(ancestorLayer);
            if (haveNonScrollableClippingIntermediateLayer)
                pushNonScrollableClip(*currentClippedLayer, ancestorLayer, !canUseDescendantClip ? RespectOverflowClip : IgnoreOverflowClip);
            else if (!canUseDescendantClip && newStack.isEmpty())
                pushNonScrollableClip(*currentClippedLayer, ancestorLayer, RespectOverflowClip);

            return AncestorTraversal::Stop;
        }

        auto ancestorLayerMayClip = [&]() {
            if (checkAbsoluteAncestorForClip && ancestorLayer.renderer().hasClip())
                return true;

            return isContainingBlockChain && ancestorLayer.renderer().hasClipOrNonVisibleOverflow();
        };

        if (ancestorLayerMayClip()) {
            auto* box = ancestorLayer.renderBox();
            if (!box)
                return AncestorTraversal::Continue;

            if (ancestorLayer.hasCompositedScrollableOverflow()) {
                if (haveNonScrollableClippingIntermediateLayer) {
                    pushNonScrollableClip(*currentClippedLayer, ancestorLayer);
                    haveNonScrollableClippingIntermediateLayer = false;
                }

                auto clipRoundedRect = parentRelativeScrollableRect(ancestorLayer, &ancestorLayer);
                auto offset = layer.convertToLayerCoords(&ancestorLayer, { }, RenderLayer::AdjustForColumns);
                clipRoundedRect.moveBy(-offset);

                CompositedClipData clipData { const_cast<RenderLayer*>(&ancestorLayer), clipRoundedRect, true };
                newStack.insert(0, WTFMove(clipData));
                currentClippedLayer = &ancestorLayer;
            } else if (box->hasNonVisibleOverflow() && box->style().hasBorderRadius()) {
                if (haveNonScrollableClippingIntermediateLayer) {
                    pushNonScrollableClip(*currentClippedLayer, ancestorLayer);
                    haveNonScrollableClippingIntermediateLayer = false;
                }

                auto borderShape = BorderShape::shapeForBorderRect(box->style(), box->borderBoxRect());
                auto clipRoundedRect = borderShape.deprecatedInnerRoundedRect();

                auto offset = layer.convertToLayerCoords(&ancestorLayer, { }, RenderLayer::AdjustForColumns);
                auto rect = clipRoundedRect.rect();
                rect.moveBy(-offset);
                clipRoundedRect.setRect(rect);

                CompositedClipData clipData { const_cast<RenderLayer*>(&ancestorLayer), clipRoundedRect, false };
                newStack.insert(0, WTFMove(clipData));
                currentClippedLayer = &ancestorLayer;
            } else
                haveNonScrollableClippingIntermediateLayer = true;
        }

        return AncestorTraversal::Continue;
    });
    
    return newStack;
}

// Note that this returns the ScrollingNodeID of the scroller this layer is embedded in, not the layer's own ScrollingNodeID if it has one.
std::optional<ScrollingNodeID> RenderLayerCompositor::asyncScrollableContainerNodeID(const RenderObject& renderer)
{
    auto* enclosingLayer = renderer.enclosingLayer();
    if (!enclosingLayer)
        return std::nullopt;
    
    auto layerScrollingNodeID = [](const RenderLayer& layer) -> std::optional<ScrollingNodeID> {
        if (layer.isComposited())
            return layer.backing()->scrollingNodeIDForRole(ScrollCoordinationRole::Scrolling);
        return std::nullopt;
    };

    // If the renderer is inside the layer, we care about the layer's scrollability. Otherwise, we let traverseAncestorLayers look at ancestors.
    if (!renderer.hasLayer()) {
        if (auto scrollingNodeID = layerScrollingNodeID(*enclosingLayer))
            return scrollingNodeID;
    }

    std::optional<ScrollingNodeID> containerScrollingNodeID;
    traverseAncestorLayers(*enclosingLayer, [&](const RenderLayer& ancestorLayer, bool isContainingBlockChain, bool /*isPaintOrderAncestor*/) {
        if (isContainingBlockChain && ancestorLayer.hasCompositedScrollableOverflow()) {
            containerScrollingNodeID = layerScrollingNodeID(ancestorLayer);
            return AncestorTraversal::Stop;
        }
        return AncestorTraversal::Continue;
    });

    return containerScrollingNodeID;
}

bool RenderLayerCompositor::hasCompositedWidgetContents(const RenderObject& renderer)
{
    auto* renderWidget = dynamicDowncast<RenderWidget>(renderer);
    if (!renderWidget)
        return false;

    return renderWidget->requiresAcceleratedCompositing();
}

bool RenderLayerCompositor::isCompositedPlugin(const RenderObject& renderer)
{
    auto* renderEmbeddedObject = dynamicDowncast<RenderEmbeddedObject>(renderer);
    if (!renderEmbeddedObject)
        return false;

    return renderEmbeddedObject->requiresAcceleratedCompositing();
}

#if HAVE(CORE_ANIMATION_SEPARATED_LAYERS)
bool RenderLayerCompositor::isSeparated(const RenderObject& renderer)
{
    return renderer.style().usedTransformStyle3D() == TransformStyle3D::Separated;
}
#endif

// Return true if the given layer is a stacking context and has compositing child
// layers that it needs to clip. In this case we insert a clipping GraphicsLayer
// into the hierarchy between this layer and its children in the z-order hierarchy.
bool RenderLayerCompositor::clipsCompositingDescendants(const RenderLayer& layer)
{
    // View transition new always has composited descendants in the graphics layer
    // tree due to hosting (but not in the RenderLayer tree).
    if (layer.renderer().style().pseudoElementType() == PseudoId::ViewTransitionNew && layer.renderer().hasClipOrNonVisibleOverflow())
        return true;

    if (!(layer.hasCompositingDescendant() && layer.renderer().hasClipOrNonVisibleOverflow()))
        return false;

    if (layer.hasCompositedNonContainedDescendants())
        return false;

    return canUseDescendantClippingLayer(layer);
}

bool RenderLayerCompositor::requiresCompositingForAnimation(RenderLayerModelObject& renderer) const
{
    if (!(m_compositingTriggers & ChromeClient::AnimationTrigger))
        return false;

    if (auto styleable = Styleable::fromRenderer(renderer)) {
        if (styleable->hasRunningAcceleratedAnimations())
            return true;
        if (auto* effectsStack = styleable->keyframeEffectStack()) {
            return (effectsStack->isCurrentlyAffectingProperty(CSSPropertyOpacity)
                && (usesCompositing() || (m_compositingTriggers & ChromeClient::AnimatedOpacityTrigger)))
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyFilter)
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyBackdropFilter)
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyWebkitBackdropFilter)
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyTranslate)
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyScale)
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyRotate)
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyTransform)
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyOffsetAnchor)
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyOffsetDistance)
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyOffsetPath)
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyOffsetPosition)
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyOffsetRotate);
        }
    }

    return false;
}

static bool styleHas3DTransformOperation(const RenderStyle& style)
{
    return style.transform().has3DOperation()
        || style.translate().is3DOperation()
        || style.scale().is3DOperation()
        || style.rotate().is3DOperation();
}

static bool styleTransformOperationsAreRepresentableIn2D(const RenderStyle& style)
{
    return style.transform().isRepresentableIn2D()
        && style.translate().isRepresentableIn2D()
        && style.scale().isRepresentableIn2D()
        && style.rotate().isRepresentableIn2D();
}

bool RenderLayerCompositor::requiresCompositingForTransform(RenderLayerModelObject& renderer) const
{
    if (!(m_compositingTriggers & ChromeClient::ThreeDTransformTrigger))
        return false;

    // Note that we ask the renderer if it has a transform, because the style may have transforms,
    // but the renderer may be an inline that doesn't suppport them.
    if (!renderer.isTransformed())
        return false;

    auto compositingPolicy = m_compositingPolicy;
#if !USE(COMPOSITING_FOR_SMALL_CANVASES)
    if (RefPtr canvas = dynamicDowncast<HTMLCanvasElement>(renderer.element())) {
        auto canvasArea = canvas->size().area<RecordOverflow>();
        if (!canvasArea.hasOverflowed() && canvasArea < canvasAreaThresholdRequiringCompositing)
            compositingPolicy = CompositingPolicy::Conservative;
    }
#endif
    
    switch (compositingPolicy) {
    case CompositingPolicy::Normal:
        return styleHas3DTransformOperation(renderer.style());
    case CompositingPolicy::Conservative:
        // Continue to allow pages to avoid the very slow software filter path.
        if (styleHas3DTransformOperation(renderer.style()) && renderer.hasFilter())
            return true;
        return styleTransformOperationsAreRepresentableIn2D(renderer.style()) ? false : true;
    }
    return false;
}

bool RenderLayerCompositor::requiresCompositingForBackfaceVisibility(RenderLayerModelObject& renderer) const
{
    if (!(m_compositingTriggers & ChromeClient::ThreeDTransformTrigger))
        return false;

    if (renderer.style().backfaceVisibility() != BackfaceVisibility::Hidden)
        return false;

    if (renderer.layer()->has3DTransformedAncestor())
        return true;
    
    // FIXME: workaround for webkit.org/b/132801
    auto* stackingContext = renderer.layer()->stackingContext();
    if (stackingContext && stackingContext->renderer().style().preserves3D())
        return true;

    return false;
}

bool RenderLayerCompositor::requiresCompositingForViewTransition(RenderLayerModelObject& renderer) const
{
    return renderer.effectiveCapturedInViewTransition() || renderer.isRenderViewTransitionCapture() || renderer.isViewTransitionContainingBlock() || (renderer.isRenderView() && renderer.protectedDocument()->activeViewTransition());
}

bool RenderLayerCompositor::requiresCompositingForVideo(RenderLayerModelObject& renderer) const
{
    if (!(m_compositingTriggers & ChromeClient::VideoTrigger))
        return false;

#if ENABLE(VIDEO)
    CheckedPtr video = dynamicDowncast<RenderVideo>(renderer);
    if (!video)
        return false;

    if ((video->requiresImmediateCompositing() || video->shouldDisplayVideo()) && canAccelerateVideoRendering(*video))
        return true;
#else
    UNUSED_PARAM(renderer);
#endif
    return false;
}

bool RenderLayerCompositor::requiresCompositingForCanvas(RenderLayerModelObject& renderer) const
{
    if (!(m_compositingTriggers & ChromeClient::CanvasTrigger))
        return false;

    if (!renderer.isRenderHTMLCanvas())
        return false;

    bool isCanvasLargeEnoughToForceCompositing = true;
#if !USE(COMPOSITING_FOR_SMALL_CANVASES)
    RefPtr canvas = downcast<HTMLCanvasElement>(renderer.element());
    auto canvasArea = canvas->size().area<RecordOverflow>();
    isCanvasLargeEnoughToForceCompositing = !canvasArea.hasOverflowed() && canvasArea >= canvasAreaThresholdRequiringCompositing;
#endif

    CanvasCompositingStrategy compositingStrategy = canvasCompositingStrategy(renderer);
    if (compositingStrategy == CanvasAsLayerContents)
        return true;

    if (m_compositingPolicy == CompositingPolicy::Normal)
        return compositingStrategy == CanvasPaintedToLayer && isCanvasLargeEnoughToForceCompositing;

    return false;
}

bool RenderLayerCompositor::requiresCompositingForFilters(RenderLayerModelObject& renderer) const
{
    if (renderer.hasBackdropFilter())
        return true;

#if HAVE(CORE_MATERIAL)
    if (renderer.hasAppleVisualEffect())
        return true;
#endif

    if (!(m_compositingTriggers & ChromeClient::FilterTrigger))
        return false;

    return renderer.hasFilter();
}

bool RenderLayerCompositor::requiresCompositingForWillChange(RenderLayerModelObject& renderer) const
{
    if (!renderer.style().willChange() || !renderer.style().willChange()->canTriggerCompositing())
        return false;

#if ENABLE(FULLSCREEN_API)
    // FIXME: does this require layout?
    if (renderer.layer() && isDescendantOfFullScreenLayer(*renderer.layer()) == FullScreenDescendant::No)
        return false;
#endif

#if !PLATFORM(MAC)
    // Ugly workaround for rdar://71881767. Undo when webkit.org/b/222092 and webkit.org/b/222132 are fixed.
    if (m_compositingPolicy == CompositingPolicy::Conservative)
        return false;
#endif

    if (is<RenderBox>(renderer))
        return true;

    return renderer.style().willChange()->canTriggerCompositingOnInline();
}

bool RenderLayerCompositor::requiresCompositingForModel(RenderLayerModelObject& renderer) const
{
#if ENABLE(MODEL_ELEMENT)
    if (is<RenderModel>(renderer))
        return true;
#else
    UNUSED_PARAM(renderer);
#endif

    return false;
}

bool RenderLayerCompositor::requiresCompositingForPlugin(RenderLayerModelObject& renderer, RequiresCompositingData& queryData) const
{
    if (!(m_compositingTriggers & ChromeClient::PluginTrigger))
        return false;

    if (!isCompositedPlugin(renderer))
        return false;

    auto& pluginRenderer = downcast<RenderWidget>(renderer);
    if (pluginRenderer.style().usedVisibility() != Visibility::Visible)
        return false;

    // If we can't reliably know the size of the plugin yet, don't change compositing state.
    if (queryData.layoutUpToDate == LayoutUpToDate::No) {
        queryData.reevaluateAfterLayout = true;
        return pluginRenderer.isComposited();
    }

    // Don't go into compositing mode if height or width are zero, or size is 1x1.
    IntRect contentBox = snappedIntRect(pluginRenderer.contentBoxRect());
    return (contentBox.height() * contentBox.width() > 1);
}
    
bool RenderLayerCompositor::requiresCompositingForFrame(RenderLayerModelObject& renderer, RequiresCompositingData& queryData) const
{
    RefPtr frameRenderer = dynamicDowncast<RenderWidget>(renderer);
    if (!frameRenderer)
        return false;

    if (frameRenderer->style().usedVisibility() != Visibility::Visible)
        return false;

    if (!frameRenderer->requiresAcceleratedCompositing())
        return false;

    if (queryData.layoutUpToDate == LayoutUpToDate::No) {
        queryData.reevaluateAfterLayout = true;
        return frameRenderer->isComposited();
    }

    // Don't go into compositing mode if height or width are zero.
    return !snappedIntRect(frameRenderer->contentBoxRect()).isEmpty();
}

bool RenderLayerCompositor::requiresCompositingForScrollableFrame(RequiresCompositingData& queryData) const
{
    if (isRootFrameCompositor())
        return false;

#if PLATFORM(COCOA) || USE(COORDINATED_GRAPHICS)
    if (!m_renderView.settings().asyncFrameScrollingEnabled())
        return false;
#endif

    if (!(m_compositingTriggers & ChromeClient::ScrollableNonMainFrameTrigger))
        return false;

    if (queryData.layoutUpToDate == LayoutUpToDate::No) {
        queryData.reevaluateAfterLayout = true;
        return m_renderView.isComposited();
    }

    return m_renderView.frameView().isScrollable();
}

bool RenderLayerCompositor::requiresCompositingForPosition(RenderLayerModelObject& renderer, const RenderLayer& layer, RequiresCompositingData& queryData) const
{
    // position:fixed elements that create their own stacking context (e.g. have an explicit z-index,
    // opacity, transform) can get their own composited layer. A stacking context is required otherwise
    // z-index and clipping will be broken.
    if (!renderer.isPositioned())
        return false;

#if ENABLE(FULLSCREEN_API)
    if (isDescendantOfFullScreenLayer(layer) == FullScreenDescendant::No)
        return false;
#endif

    auto position = renderer.style().position();
    bool isFixed = renderer.isFixedPositioned();
    if (isFixed && !layer.isStackingContext())
        return false;
    
    bool isSticky = renderer.isInFlowPositioned() && position == PositionType::Sticky;
    if (!isFixed && !isSticky)
        return false;

    // FIXME: acceleratedCompositingForFixedPositionEnabled should probably be renamed acceleratedCompositingForViewportConstrainedPositionEnabled().
    if (!m_renderView.settings().acceleratedCompositingForFixedPositionEnabled())
        return false;

    if (isSticky)
        return isAsyncScrollableStickyLayer(layer);

    if (queryData.layoutUpToDate == LayoutUpToDate::No) {
        queryData.reevaluateAfterLayout = true;
        return layer.isComposited();
    }

    auto container = renderer.container();
    ASSERT(container);

    // Don't promote fixed position elements that are descendants of a non-view container, e.g. transformed elements.
    // They will stay fixed wrt the container rather than the enclosing frame.
    if (container != &m_renderView) {
        queryData.nonCompositedForPositionReason = RenderLayer::NotCompositedForNonViewContainer;
        return false;
    }

    bool paintsContent = layer.isVisuallyNonEmpty() || layer.hasVisibleDescendant();
    if (!paintsContent) {
        queryData.nonCompositedForPositionReason = RenderLayer::NotCompositedForNoVisibleContent;
        return false;
    }

    bool intersectsViewport = fixedLayerIntersectsViewport(layer);
    if (!intersectsViewport) {
        queryData.nonCompositedForPositionReason = RenderLayer::NotCompositedForBoundsOutOfView;
        LOG_WITH_STREAM(Compositing, stream << "Layer " << &layer << " is outside the viewport");
        return false;
    }

    return true;
}

bool RenderLayerCompositor::requiresCompositingForOverflowScrolling(const RenderLayer& layer, RequiresCompositingData& queryData) const
{
    if (!layer.canUseCompositedScrolling())
        return false;

    if (queryData.layoutUpToDate == LayoutUpToDate::No) {
        queryData.reevaluateAfterLayout = true;
        return layer.isComposited();
    }

    const_cast<RenderLayer&>(layer).computeHasCompositedScrollableOverflow(LayoutUpToDate::Yes);
    return layer.hasCompositedScrollableOverflow();
}

bool RenderLayerCompositor::requiresCompositingForAnchorPositioning(const RenderLayer& layer) const
{
    return !!layer.snapshottedScrollOffsetForAnchorPositioning();
}

IndirectCompositingReason RenderLayerCompositor::computeIndirectCompositingReason(const RenderLayer& layer, bool hasCompositedDescendants, bool has3DTransformedDescendants, bool paintsIntoProvidedBacking) const
{
    // When a layer has composited descendants, some effects, like 2d transforms, filters, masks etc must be implemented
    // via compositing so that they also apply to those composited descendants.
    auto& renderer = layer.renderer();
    if (hasCompositedDescendants && (layer.isolatesCompositedBlending() || layer.isBackdropRoot() || layer.transform() || renderer.createsGroup() || renderer.hasReflection()))
        return IndirectCompositingReason::GraphicalEffect;

    // A layer with preserve-3d or perspective only needs to be composited if there are descendant layers that
    // will be affected by the preserve-3d or perspective.
    if (has3DTransformedDescendants) {
        if (renderer.style().preserves3D())
            return IndirectCompositingReason::Preserve3D;
    
        if (renderer.style().hasPerspective())
            return IndirectCompositingReason::Perspective;
    }

    // If this layer scrolls independently from the layer that it would paint into, it needs to get composited.
    if (!paintsIntoProvidedBacking && layer.hasCompositedScrollingAncestor()) {
        auto* paintDestination = layer.paintOrderParent();
        if (paintDestination && layerScrollBehahaviorRelativeToCompositedAncestor(layer, *paintDestination) != ScrollPositioningBehavior::None)
            return IndirectCompositingReason::OverflowScrollPositioning;
    }

    // Check for clipping last; if compositing just for clipping, the layer doesn't need its own backing store.
    if (hasCompositedDescendants && clipsCompositingDescendants(layer))
        return IndirectCompositingReason::Clipping;

    return IndirectCompositingReason::None;
}

bool RenderLayerCompositor::styleChangeMayAffectIndirectCompositingReasons(const RenderStyle& oldStyle, const RenderStyle& newStyle)
{
    if (RenderElement::createsGroupForStyle(newStyle) != RenderElement::createsGroupForStyle(oldStyle))
        return true;
    if (newStyle.isolation() != oldStyle.isolation())
        return true;
    if (newStyle.hasTransform() != oldStyle.hasTransform())
        return true;
    if (newStyle.boxReflect() != oldStyle.boxReflect())
        return true;
    if (newStyle.usedTransformStyle3D() != oldStyle.usedTransformStyle3D())
        return true;
    if (newStyle.hasPerspective() != oldStyle.hasPerspective())
        return true;

    return false;
}

bool RenderLayerCompositor::isAsyncScrollableStickyLayer(const RenderLayer& layer, const RenderLayer** enclosingAcceleratedOverflowLayer) const
{
    ASSERT(layer.renderer().isStickilyPositioned());

    auto* enclosingOverflowLayer = layer.enclosingOverflowClipLayer(ExcludeSelf);

    if (enclosingOverflowLayer && enclosingOverflowLayer->hasCompositedScrollableOverflow()) {
        if (enclosingAcceleratedOverflowLayer)
            *enclosingAcceleratedOverflowLayer = enclosingOverflowLayer;
        return true;
    }

    // If the layer is inside normal overflow, it's not async-scrollable.
    if (enclosingOverflowLayer)
        return false;

    // No overflow ancestor, so see if the frame supports async scrolling.
    if (hasCoordinatedScrolling())
        return true;

#if PLATFORM(IOS_FAMILY)
    // iOS WK1 has fixed/sticky support in the main frame via WebFixedPositionContent.
    return isMainFrameCompositor();
#else
    return false;
#endif
}

ViewportConstrainedSublayers RenderLayerCompositor::viewportConstrainedSublayers(const RenderLayer& layer, const RenderLayer* compositingAncestor) const
{
    using enum ViewportConstrainedSublayers;

    auto sublayersForViewportConstrainedLayer = [&] {
        if (!m_renderView.settings().contentInsetBackgroundFillEnabled())
            return Anchor;

        if (!isMainFrameCompositor())
            return Anchor;

        if (compositingAncestor != m_renderView.layer())
            return Anchor;

#if ENABLE(FULLSCREEN_API)
        if (RefPtr fullscreen = m_renderView.document().fullscreenIfExists(); fullscreen && fullscreen->isFullscreen())
            return Anchor;
#endif

        return ClippingAndAnchor;
    };

    if (layer.renderer().isStickilyPositioned()) {
        const RenderLayer* overflowLayer = nullptr;
        if (!isAsyncScrollableStickyLayer(layer, &overflowLayer))
            return None;

        if (overflowLayer)
            return Anchor;

        return sublayersForViewportConstrainedLayer();
    }

    if (!(layer.renderer().isFixedPositioned() && layer.behavesAsFixed()))
        return None;

    for (auto* ancestor = layer.parent(); ancestor; ancestor = ancestor->parent()) {
        if (ancestor->hasCompositedScrollableOverflow())
            return sublayersForViewportConstrainedLayer();

        if (ancestor->isStackingContext() && ancestor->isComposited() && ancestor->renderer().isFixedPositioned())
            return None;
    }

    return sublayersForViewportConstrainedLayer();
}

bool RenderLayerCompositor::fixedLayerIntersectsViewport(const RenderLayer& layer) const
{
    ASSERT(layer.renderer().isFixedPositioned());

    // Fixed position elements that are invisible in the current view don't get their own layer.
    // FIXME: We shouldn't have to check useFixedLayout() here; one of the viewport rects needs to give the correct answer.
    LayoutRect viewBounds;
    Ref frameView = m_renderView.frameView();
    if (frameView->useFixedLayout())
        viewBounds = m_renderView.unscaledDocumentRect();
    else
        viewBounds = frameView->rectForFixedPositionLayout();

    LayoutRect layerBounds = layer.calculateLayerBounds(&layer, LayoutSize(), { RenderLayer::UseLocalClipRectIfPossible, RenderLayer::IncludeFilterOutsets, RenderLayer::UseFragmentBoxesExcludingCompositing,
        RenderLayer::ExcludeHiddenDescendants, RenderLayer::DontConstrainForMask, RenderLayer::IncludeCompositedDescendants });
    // Map to m_renderView to ignore page scale.
    FloatRect absoluteBounds = layer.renderer().localToContainerQuad(FloatRect(layerBounds), &m_renderView).boundingBox();
    return viewBounds.intersects(enclosingIntRect(absoluteBounds));
}

bool RenderLayerCompositor::useCoordinatedScrollingForLayer(const RenderLayer& layer) const
{
    if (layer.isRenderViewLayer() && hasCoordinatedScrolling())
        return true;

    if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
        return scrollingCoordinator->coordinatesScrollingForOverflowLayer(layer);

    return false;
}

ScrollPositioningBehavior RenderLayerCompositor::layerScrollBehahaviorRelativeToCompositedAncestor(const RenderLayer& layer, const RenderLayer& compositedAncestor)
{
    if (!layer.hasCompositedScrollingAncestor())
        return ScrollPositioningBehavior::None;

    auto needsMovesNode = [&] {
        bool result = false;
        traverseAncestorLayers(layer, [&](const RenderLayer& ancestorLayer, bool isContainingBlockChain, bool /* isPaintOrderAncestor */) {
            if (&ancestorLayer == &compositedAncestor)
                return AncestorTraversal::Stop;

            if (isContainingBlockChain && ancestorLayer.hasCompositedScrollableOverflow()) {
                result = true;
                return AncestorTraversal::Stop;
            }

            return AncestorTraversal::Continue;
        });

        return result;
    };

    if (needsMovesNode())
        return ScrollPositioningBehavior::Moves;

    if (layer.boxScrollingScope() != compositedAncestor.contentsScrollingScope())
        return ScrollPositioningBehavior::Stationary;

    return ScrollPositioningBehavior::None;
}

static void collectStationaryLayerRelatedOverflowNodes(const RenderLayer& layer, const RenderLayer&, Vector<ScrollingNodeID>& scrollingNodes)
{
    ASSERT(layer.isComposited());

    auto appendOverflowLayerNodeID = [&scrollingNodes] (const RenderLayer& overflowLayer) {
        ASSERT(overflowLayer.isComposited());
        if (overflowLayer.isComposited()) {
            if (auto scrollingNodeID = overflowLayer.backing()->scrollingNodeIDForRole(ScrollCoordinationRole::Scrolling)) {
                scrollingNodes.append(*scrollingNodeID);
                return;
            }
        }
        LOG(Scrolling, "Layer %p isn't composited or doesn't have scrolling node ID yet", &overflowLayer);
    };

    // Collect all the composited scrollers which affect the position of this layer relative to its compositing ancestor (which might be inside the scroller or the scroller itself).
    bool seenPaintOrderAncestor = false;
    traverseAncestorLayers(layer, [&](const RenderLayer& ancestorLayer, bool isContainingBlockChain, bool isPaintOrderAncestor) {
        seenPaintOrderAncestor |= isPaintOrderAncestor;
        if (isContainingBlockChain && isPaintOrderAncestor)
            return AncestorTraversal::Stop;

        if (seenPaintOrderAncestor && !isContainingBlockChain && ancestorLayer.hasCompositedScrollableOverflow())
            appendOverflowLayerNodeID(ancestorLayer);

        return AncestorTraversal::Continue;
    });
}

ScrollPositioningBehavior RenderLayerCompositor::computeCoordinatedPositioningForLayer(const RenderLayer& layer, const RenderLayer* compositedAncestor) const
{
    if (layer.isRenderViewLayer())
        return ScrollPositioningBehavior::None;

    if (layer.renderer().isFixedPositioned() && layer.behavesAsFixed())
        return ScrollPositioningBehavior::None;
    
    if (!layer.hasCompositedScrollingAncestor())
        return ScrollPositioningBehavior::None;

    RefPtr scrollingCoordinator = this->scrollingCoordinator();
    if (!scrollingCoordinator)
        return ScrollPositioningBehavior::None;

    if (!compositedAncestor) {
        ASSERT_NOT_REACHED();
        return ScrollPositioningBehavior::None;
    }

    return layerScrollBehahaviorRelativeToCompositedAncestor(layer, *compositedAncestor);
}

static Vector<ScrollingNodeID> collectRelatedCoordinatedScrollingNodes(const RenderLayer& layer, ScrollPositioningBehavior positioningBehavior)
{
    Vector<ScrollingNodeID> overflowNodeIDs;

    switch (positioningBehavior) {
    case ScrollPositioningBehavior::Stationary: {
        auto* compositedAncestor = layer.ancestorCompositingLayer();
        if (!compositedAncestor)
            return overflowNodeIDs;
        collectStationaryLayerRelatedOverflowNodes(layer, *compositedAncestor, overflowNodeIDs);
        break;
    }
    case ScrollPositioningBehavior::Moves:
    case ScrollPositioningBehavior::None:
        ASSERT_NOT_REACHED();
        break;
    }

    return overflowNodeIDs;
}

bool RenderLayerCompositor::isLayerForIFrameWithScrollCoordinatedContents(const RenderLayer& layer) const
{
    auto* renderWidget = dynamicDowncast<RenderWidget>(layer.renderer());
    if (!renderWidget)
        return false;

    RefPtr frame = renderWidget->frameOwnerElement().contentFrame();
    if (frame && is<RemoteFrame>(frame))
        return renderWidget->hasLayer() && renderWidget->layer()->isComposited();

    RefPtr contentDocument = renderWidget->protectedFrameOwnerElement()->contentDocument();
    if (!contentDocument)
        return false;

    auto* view = contentDocument->renderView();
    if (!view)
        return false;

    if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
        return scrollingCoordinator->coordinatesScrollingForFrameView(view->frameView());

    return false;
}

bool RenderLayerCompositor::isLayerForPluginWithScrollCoordinatedContents(const RenderLayer& layer) const
{
    CheckedPtr renderEmbeddedObject = dynamicDowncast<RenderEmbeddedObject>(layer.renderer());
    if (!renderEmbeddedObject)
        return false;

    return renderEmbeddedObject->usesAsyncScrolling();
}

bool RenderLayerCompositor::isRunningTransformAnimation(RenderLayerModelObject& renderer) const
{
    if (!(m_compositingTriggers & ChromeClient::AnimationTrigger))
        return false;

    if (auto styleable = Styleable::fromRenderer(renderer)) {
        if (auto* effectsStack = styleable->keyframeEffectStack())
            return effectsStack->isCurrentlyAffectingProperty(CSSPropertyTransform)
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyRotate)
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyScale)
                || effectsStack->isCurrentlyAffectingProperty(CSSPropertyTranslate);
    }

    return false;
}

// If an element has composited negative z-index children, those children render in front of the
// layer background, so we need an extra 'contents' layer for the foreground of the layer object.
bool RenderLayerCompositor::needsContentsCompositingLayer(const RenderLayer& layer) const
{
    for (auto* layer : layer.negativeZOrderLayers()) {
        if (layer->isComposited() || layer->hasCompositingDescendant())
            return true;
    }

    return false;
}

bool RenderLayerCompositor::requiresScrollLayer(RootLayerAttachment attachment) const
{
    Ref frameView = m_renderView.frameView();

    // This applies when the application UI handles scrolling, in which case RenderLayerCompositor doesn't need to manage it.
    if (frameView->delegatedScrollingMode() == DelegatedScrollingMode::DelegatedToNativeScrollView && isMainFrameCompositor())
        return false;

    // We need to handle our own scrolling if we're:
    return !m_renderView.frameView().platformWidget() // viewless (i.e. non-Mac, or Mac in WebKit2)
        || attachment == RootLayerAttachedViaEnclosingFrame; // a composited frame on Mac
}

void paintScrollbar(Scrollbar* scrollbar, GraphicsContext& context, const IntRect& clip, const Color& backgroundColor)
{
    if (!scrollbar)
        return;

    context.save();
    const IntRect& scrollbarRect = scrollbar->frameRect();
    context.translate(-scrollbarRect.location());
    IntRect transformedClip = clip;
    transformedClip.moveBy(scrollbarRect.location());
#if HAVE(RUBBER_BANDING)
    UNUSED_PARAM(backgroundColor);
#else
    if (!scrollbar->isOverlayScrollbar() && backgroundColor.isVisible())
        context.fillRect(transformedClip, backgroundColor);
#endif
    scrollbar->paint(context, transformedClip);
    context.restore();
}

void RenderLayerCompositor::paintContents(const GraphicsLayer* graphicsLayer, GraphicsContext& context, const FloatRect& clip, OptionSet<GraphicsLayerPaintBehavior>)
{
#if PLATFORM(MAC)
    LocalDefaultSystemAppearance localAppearance(m_renderView.useDarkAppearance());
#endif

    IntRect pixelSnappedRectForIntegralPositionedItems = snappedIntRect(LayoutRect(clip));
    if (graphicsLayer == layerForHorizontalScrollbar())
        paintScrollbar(RefPtr { m_renderView.frameView().horizontalScrollbar() }.get(), context, pixelSnappedRectForIntegralPositionedItems, m_viewBackgroundColor);
    else if (graphicsLayer == layerForVerticalScrollbar())
        paintScrollbar(RefPtr { m_renderView.frameView().verticalScrollbar() }.get(), context, pixelSnappedRectForIntegralPositionedItems, m_viewBackgroundColor);
    else if (graphicsLayer == layerForScrollCorner()) {
        Ref frameView = m_renderView.frameView();
        const IntRect& scrollCorner = frameView->scrollCornerRect();
        context.save();
        context.translate(-scrollCorner.location());
        IntRect transformedClip = pixelSnappedRectForIntegralPositionedItems;
        transformedClip.moveBy(scrollCorner.location());
        frameView->paintScrollCorner(context, transformedClip);
        context.restore();
    }
}

bool RenderLayerCompositor::supportsFixedRootBackgroundCompositing() const
{
    auto* renderViewBacking = m_renderView.layer()->backing();
    return renderViewBacking && renderViewBacking->isFrameLayerWithTiledBacking();
}

bool RenderLayerCompositor::needsFixedRootBackgroundLayer(const RenderLayer& layer) const
{
    if (!layer.isRenderViewLayer())
        return false;

    if (m_renderView.settings().fixedBackgroundsPaintRelativeToDocument())
        return false;

    return supportsFixedRootBackgroundCompositing() && m_renderView.rootBackgroundIsEntirelyFixed();
}

GraphicsLayer* RenderLayerCompositor::fixedRootBackgroundLayer() const
{
    // Get the fixed root background from the RenderView layer's backing.
    auto* viewLayer = m_renderView.layer();
    if (!viewLayer)
        return nullptr;

    if (viewLayer->isComposited() && viewLayer->backing()->backgroundLayerPaintsFixedRootBackground())
        return viewLayer->backing()->backgroundLayer();

    return nullptr;
}

void RenderLayerCompositor::resetTrackedRepaintRects()
{
    if (RefPtr rootLayer = rootGraphicsLayer()) {
        GraphicsLayer::traverse(*rootLayer, [](GraphicsLayer& layer) {
            layer.resetTrackedRepaints();
        });
    }
}

float RenderLayerCompositor::deviceScaleFactor() const
{
    return page().deviceScaleFactor();
}

float RenderLayerCompositor::pageScaleFactor() const
{
    return page().pageScaleFactor();
}

float RenderLayerCompositor::zoomedOutPageScaleFactor() const
{
    return page().zoomedOutPageScaleFactor();
}

FloatSize RenderLayerCompositor::enclosingFrameViewVisibleSize() const
{
    const Ref frameView = m_renderView.frameView();
#if PLATFORM(IOS_FAMILY)
    return frameView->exposedContentRect().size();
#endif
    if (m_scrolledContentsLayer)
        return frameView->sizeForVisibleContent(scrollbarInclusionForVisibleRect());
    return frameView->visibleContentRect().size();
}

float RenderLayerCompositor::contentsScaleMultiplierForNewTiles(const GraphicsLayer*) const
{
#if PLATFORM(IOS_FAMILY)
    RefPtr<LegacyTileCache> tileCache;
    RefPtr localMainFrame = page().localMainFrame();
    if (auto* frameView = localMainFrame ? localMainFrame->view() : nullptr)
        tileCache = frameView->legacyTileCache();

    if (!tileCache)
        return 1;

    return tileCache->tileControllerShouldUseLowScaleTiles() ? 0.125 : 1;
#else
    return 1;
#endif
}

bool RenderLayerCompositor::documentUsesTiledBacking() const
{
    auto* layer = m_renderView.layer();
    if (!layer)
        return false;

    auto* backing = layer->backing();
    if (!backing)
        return false;

    return backing->isFrameLayerWithTiledBacking();
}

bool RenderLayerCompositor::isRootFrameCompositor() const
{
    return m_renderView.frameView().frame().isRootFrame();
}

bool RenderLayerCompositor::isMainFrameCompositor() const
{
    return m_renderView.frameView().frame().isMainFrame();
}

bool RenderLayerCompositor::shouldCompositeOverflowControls() const
{
    Ref frameView = m_renderView.frameView();

    if (!frameView->managesScrollbars())
        return false;

    if (documentUsesTiledBacking())
        return true;

    if (m_overflowControlsHostLayer && isRootFrameCompositor())
        return true;

#if !USE(COORDINATED_GRAPHICS)
    if (!frameView->hasOverlayScrollbars())
        return false;
#endif

    return true;
}

bool RenderLayerCompositor::requiresHorizontalScrollbarLayer() const
{
    return shouldCompositeOverflowControls() && m_renderView.frameView().horizontalScrollbar();
}

bool RenderLayerCompositor::requiresVerticalScrollbarLayer() const
{
    return shouldCompositeOverflowControls() && m_renderView.frameView().verticalScrollbar();
}

bool RenderLayerCompositor::requiresScrollCornerLayer() const
{
    return shouldCompositeOverflowControls() && m_renderView.frameView().isScrollCornerVisible();
}

#if HAVE(RUBBER_BANDING)
bool RenderLayerCompositor::requiresOverhangAreasLayer() const
{
    if (!isMainFrameCompositor())
        return false;

    // We do want a layer if we're using tiled drawing and can scroll.
    Ref frameView = m_renderView.frameView();
    if (documentUsesTiledBacking() && frameView->hasOpaqueBackground() && !frameView->prohibitsScrolling())
        return true;

    return false;
}

bool RenderLayerCompositor::requiresContentShadowLayer() const
{
    if (!isMainFrameCompositor())
        return false;

#if PLATFORM(COCOA)
    if (viewHasTransparentBackground())
        return false;

    // If the background is going to extend, then it doesn't make sense to have a shadow layer.
    if (m_renderView.settings().backgroundShouldExtendBeyondPage())
        return false;

    // On Mac, we want a content shadow layer if we're using tiled drawing and can scroll.
    if (documentUsesTiledBacking() && !m_renderView.frameView().prohibitsScrolling())
        return true;
#endif

    return false;
}

GraphicsLayer* RenderLayerCompositor::updateLayerForTopOverhangImage(bool wantsLayer)
{
    if (!isMainFrameCompositor())
        return nullptr;

    if (!wantsLayer) {
        GraphicsLayer::unparentAndClear(m_layerForTopOverhangImage);
        return nullptr;
    }

    if (!m_layerForTopOverhangImage) {
        m_layerForTopOverhangImage = GraphicsLayer::create(graphicsLayerFactory(), *this);
        m_layerForTopOverhangImage->setName(MAKE_STATIC_STRING_IMPL("top overhang (image)"));
        RefPtr { m_scrolledContentsLayer }->addChildBelow(*m_layerForTopOverhangImage, m_rootContentsLayer.get());
    }

    return m_layerForTopOverhangImage.get();
}

GraphicsLayer* RenderLayerCompositor::updateLayerForTopOverhangColorExtension(bool wantsLayer)
{
    if (!isMainFrameCompositor())
        return nullptr;

    if (!wantsLayer) {
        GraphicsLayer::unparentAndClear(m_layerForTopOverhangColorExtension);
        return nullptr;
    }

    if (!m_layerForTopOverhangColorExtension) {
        m_layerForTopOverhangColorExtension = GraphicsLayer::create(graphicsLayerFactory(), *this);
        m_layerForTopOverhangColorExtension->setName(MAKE_STATIC_STRING_IMPL("top overhang (color extension)"));
        m_layerForTopOverhangColorExtension->setDrawsContent(false);
        RefPtr { m_scrolledContentsLayer }->addChildBelow(*m_layerForTopOverhangColorExtension, m_layerForTopOverhangImage.get() ?: m_rootContentsLayer.get());
    }

    return m_layerForTopOverhangColorExtension.get();
}

GraphicsLayer* RenderLayerCompositor::updateLayerForBottomOverhangArea(bool wantsLayer)
{
    if (!isMainFrameCompositor())
        return nullptr;

    if (!wantsLayer) {
        GraphicsLayer::unparentAndClear(m_layerForBottomOverhangArea);
        return nullptr;
    }

    if (!m_layerForBottomOverhangArea) {
        m_layerForBottomOverhangArea = GraphicsLayer::create(graphicsLayerFactory(), *this);
        m_layerForBottomOverhangArea->setName(MAKE_STATIC_STRING_IMPL("bottom overhang"));
        RefPtr { m_scrolledContentsLayer }->addChildBelow(*m_layerForBottomOverhangArea, m_rootContentsLayer.get());
    }

    Ref frameView = m_renderView.frameView();
    m_layerForBottomOverhangArea->setPosition(FloatPoint(0, m_rootContentsLayer->size().height() + frameView->headerHeight()
        + frameView->footerHeight() + frameView->obscuredContentInsets().top()));
    return m_layerForBottomOverhangArea.get();
}

GraphicsLayer* RenderLayerCompositor::updateLayerForHeader(bool wantsLayer)
{
    if (!isMainFrameCompositor())
        return nullptr;

    if (!wantsLayer) {
        if (m_layerForHeader) {
            GraphicsLayer::unparentAndClear(m_layerForHeader);

            // The ScrollingTree knows about the header layer, and the position of the root layer is affected
            // by the header layer, so if we remove the header, we need to tell the scrolling tree.
            if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
                scrollingCoordinator->frameViewRootLayerDidChange(m_renderView.frameView());
        }
        return nullptr;
    }

    if (!m_layerForHeader) {
        m_layerForHeader = GraphicsLayer::create(graphicsLayerFactory(), *this);
        m_layerForHeader->setName(MAKE_STATIC_STRING_IMPL("header"));
        RefPtr { m_scrolledContentsLayer }->addChildAbove(*m_layerForHeader, m_rootContentsLayer.get());
    }

    Ref frameView = m_renderView.frameView();
    m_layerForHeader->setPosition(FloatPoint(0,
        LocalFrameView::yPositionForHeaderLayer(frameView->scrollPosition(), frameView->obscuredContentInsets().top())));
    m_layerForHeader->setAnchorPoint(FloatPoint3D());
    RefPtr { m_layerForHeader }->setSize(FloatSize(frameView->visibleWidth(), frameView->headerHeight()));

    if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
        scrollingCoordinator->frameViewRootLayerDidChange(frameView);

    page().chrome().client().didAddHeaderLayer(*m_layerForHeader);

    return m_layerForHeader.get();
}

GraphicsLayer* RenderLayerCompositor::updateLayerForFooter(bool wantsLayer)
{
    if (!isMainFrameCompositor())
        return nullptr;

    Ref frameView = m_renderView.frameView();

    if (!wantsLayer) {
        if (m_layerForFooter) {
            GraphicsLayer::unparentAndClear(m_layerForFooter);

            // The ScrollingTree knows about the footer layer, and the total scrollable size is affected
            // by the footer layer, so if we remove the footer, we need to tell the scrolling tree.
            if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
                scrollingCoordinator->frameViewRootLayerDidChange(frameView);
        }
        return nullptr;
    }

    if (!m_layerForFooter) {
        m_layerForFooter = GraphicsLayer::create(graphicsLayerFactory(), *this);
        m_layerForFooter->setName(MAKE_STATIC_STRING_IMPL("footer"));
        RefPtr { m_scrolledContentsLayer }->addChildAbove(*m_layerForFooter, m_rootContentsLayer.get());
    }

    float totalContentHeight = m_rootContentsLayer->size().height() + frameView->headerHeight() + frameView->footerHeight();
    m_layerForFooter->setPosition(FloatPoint(0, LocalFrameView::yPositionForFooterLayer(frameView->scrollPosition(),
        frameView->obscuredContentInsets().top(), totalContentHeight, frameView->footerHeight())));
    m_layerForFooter->setAnchorPoint(FloatPoint3D());
    RefPtr { m_layerForFooter }->setSize(FloatSize(frameView->visibleWidth(), frameView->footerHeight()));

    if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
        scrollingCoordinator->frameViewRootLayerDidChange(frameView);

    page().chrome().client().didAddFooterLayer(*m_layerForFooter);

    return m_layerForFooter.get();
}

void RenderLayerCompositor::updateLayerForOverhangAreasBackgroundColor()
{
    if (!m_layerForOverhangAreas)
        return;

    Color backgroundColor;

    if (m_renderView.settings().backgroundShouldExtendBeyondPage()) {
        backgroundColor = ([&] {
            if (auto underPageBackgroundColorOverride = protectedPage()->underPageBackgroundColorOverride(); underPageBackgroundColorOverride.isValid())
                return underPageBackgroundColorOverride;

            return m_rootExtendedBackgroundColor;
        })();
        RefPtr { m_layerForOverhangAreas }->setBackgroundColor(backgroundColor);
    }
}

#endif // HAVE(RUBBER_BANDING)

bool RenderLayerCompositor::viewNeedsToInvalidateEventRegionOfEnclosingCompositingLayerForRepaint() const
{
    // Event regions are only updated on compositing layers. Non-composited layers must
    // delegate to their enclosing compositing layer for repaint to update the event region
    // for elements inside them.
    return !m_renderView.isComposited();
}

bool RenderLayerCompositor::viewHasTransparentBackground(Color* backgroundColor) const
{
    Ref frameView = m_renderView.frameView();
    if (frameView->isTransparent()) {
        if (backgroundColor)
            *backgroundColor = Color(); // Return an invalid color.
        return true;
    }

    Color documentBackgroundColor = frameView->documentBackgroundColor();
    if (!documentBackgroundColor.isValid())
        documentBackgroundColor = frameView->baseBackgroundColor();

    ASSERT(documentBackgroundColor.isValid());

    if (backgroundColor)
        *backgroundColor = documentBackgroundColor;

    return !documentBackgroundColor.isOpaque();
}

// We can't rely on getting layerStyleChanged() for a style change that affects the root background, because the style change may
// be on the body which has no RenderLayer.
void RenderLayerCompositor::rootOrBodyStyleChanged(RenderElement& renderer, const RenderStyle* oldStyle)
{
    if (!usesCompositing())
        return;

    Color oldBackgroundColor;
    if (oldStyle)
        oldBackgroundColor = oldStyle->visitedDependentColorWithColorFilter(CSSPropertyBackgroundColor);

    if (oldBackgroundColor != renderer.style().visitedDependentColorWithColorFilter(CSSPropertyBackgroundColor))
        rootBackgroundColorOrTransparencyChanged();

    bool hadFixedBackground = oldStyle && oldStyle->hasEntirelyFixedBackground();
    if (hadFixedBackground != renderer.style().hasEntirelyFixedBackground())
        rootLayerConfigurationChanged();
    
    if (oldStyle && (oldStyle->overscrollBehaviorX() != renderer.style().overscrollBehaviorX() || oldStyle->overscrollBehaviorY() != renderer.style().overscrollBehaviorY())) {
        if (auto* layer = m_renderView.layer())
            layer->setNeedsCompositingGeometryUpdate();
    }
}

void RenderLayerCompositor::setRootElementCapturedInViewTransition(bool captured)
{
    if (m_rootElementCapturedInViewTransition == captured)
        return;
    m_rootElementCapturedInViewTransition = captured;
    updateRootContentsLayerBackgroundColor();
}

void RenderLayerCompositor::updateRootContentsLayerBackgroundColor()
{
    if (!m_rootContentsLayer)
        return;

    RefPtr rootContentsLayer = m_rootContentsLayer;
    if (m_rootElementCapturedInViewTransition)
        rootContentsLayer->setBackgroundColor(m_viewBackgroundColor);
    else
        rootContentsLayer->setBackgroundColor(Color());
}

void RenderLayerCompositor::rootBackgroundColorOrTransparencyChanged()
{
    if (!usesCompositing())
        return;

    Color backgroundColor;
    bool isTransparent = viewHasTransparentBackground(&backgroundColor);
    
    Color extendedBackgroundColor = m_renderView.settings().backgroundShouldExtendBeyondPage() ? backgroundColor : Color();
    
    bool transparencyChanged = m_viewBackgroundIsTransparent != isTransparent;
    bool backgroundColorChanged = m_viewBackgroundColor != backgroundColor;
    bool extendedBackgroundColorChanged = m_rootExtendedBackgroundColor != extendedBackgroundColor;

    if (!transparencyChanged && !backgroundColorChanged && !extendedBackgroundColorChanged)
        return;

    LOG(Compositing, "RenderLayerCompositor %p rootBackgroundColorOrTransparencyChanged. isTransparent=%d", this, isTransparent);

    m_viewBackgroundIsTransparent = isTransparent;
    m_viewBackgroundColor = backgroundColor;
    m_rootExtendedBackgroundColor = extendedBackgroundColor;
    
    if (extendedBackgroundColorChanged) {
        page().chrome().client().pageExtendedBackgroundColorDidChange();
        
#if HAVE(RUBBER_BANDING)
        updateLayerForOverhangAreasBackgroundColor();
#endif
        updateRootContentsLayerBackgroundColor();
    }
    
    rootLayerConfigurationChanged();
}

#if HAVE(RUBBER_BANDING)
void RenderLayerCompositor::updateSizeAndPositionForOverhangAreaLayer()
{
    RefPtr layer = m_layerForOverhangAreas;
    if (!layer)
        return;

    Ref frameView = m_renderView.frameView();
    auto obscuredContentInsets = frameView->obscuredContentInsets();
    IntSize overhangAreaSize = frameView->frameRect().size();
    overhangAreaSize.contract(obscuredContentInsets.left(), obscuredContentInsets.top());
    overhangAreaSize.clampNegativeToZero();
    layer->setSize(overhangAreaSize);
    layer->setPosition({ obscuredContentInsets.left(), obscuredContentInsets.top() });
}

void RenderLayerCompositor::updateSizeAndPositionForTopOverhangColorExtensionLayer()
{
    RefPtr layer = m_layerForTopOverhangColorExtension;
    if (!layer)
        return;

    Ref frameView = m_renderView.frameView();
    IntSize layerSize { frameView->contentsSize().width(), frameView->visibleSize().height() };
    layer->setSize(layerSize);

    auto rootLayerPosition = frameView->positionForRootContentLayer();
    layer->setPosition({ rootLayerPosition.x(), rootLayerPosition.y() - layerSize.height() });
}
#endif // HAVE(RUBBER_BANDING)

void RenderLayerCompositor::updateOverflowControlsLayers()
{
#if HAVE(RUBBER_BANDING)
    if (requiresOverhangAreasLayer()) {
        if (!m_layerForOverhangAreas) {
            m_layerForOverhangAreas = GraphicsLayer::create(graphicsLayerFactory(), *this);
            m_layerForOverhangAreas->setName(MAKE_STATIC_STRING_IMPL("overhang areas"));
            RefPtr { m_layerForOverhangAreas }->setDrawsContent(false);

            updateSizeAndPositionForOverhangAreaLayer();
            m_layerForOverhangAreas->setAnchorPoint(FloatPoint3D());
            updateLayerForOverhangAreasBackgroundColor();

            // We want the overhang areas layer to be positioned below the frame contents,
            // so insert it below the clip layer.
            RefPtr { m_overflowControlsHostLayer }->addChildBelow(*m_layerForOverhangAreas, RefPtr { layerForClipping() }.get());
        }
    } else
        GraphicsLayer::unparentAndClear(m_layerForOverhangAreas);

    if (requiresContentShadowLayer()) {
        if (!m_contentShadowLayer) {
            m_contentShadowLayer = GraphicsLayer::create(graphicsLayerFactory(), *this);
            m_contentShadowLayer->setName(MAKE_STATIC_STRING_IMPL("content shadow"));
            RefPtr { m_contentShadowLayer }->setSize(m_rootContentsLayer->size());
            m_contentShadowLayer->setPosition(m_rootContentsLayer->position());
            m_contentShadowLayer->setAnchorPoint(FloatPoint3D());
            m_contentShadowLayer->setCustomAppearance(GraphicsLayer::CustomAppearance::ScrollingShadow);

            RefPtr { m_scrolledContentsLayer }->addChildBelow(*m_contentShadowLayer, m_rootContentsLayer.get());
        }
    } else
        GraphicsLayer::unparentAndClear(m_contentShadowLayer);
#endif

    if (requiresHorizontalScrollbarLayer()) {
        if (!m_layerForHorizontalScrollbar) {
            m_layerForHorizontalScrollbar = GraphicsLayer::create(graphicsLayerFactory(), *this);
            m_layerForHorizontalScrollbar->setAllowsBackingStoreDetaching(false);
            m_layerForHorizontalScrollbar->setAllowsTiling(false);
            m_layerForHorizontalScrollbar->setShowDebugBorder(m_showDebugBorders);
            m_layerForHorizontalScrollbar->setName(MAKE_STATIC_STRING_IMPL("horizontal scrollbar container"));
#if USE(CA)
            m_layerForHorizontalScrollbar->setAcceleratesDrawing(acceleratedDrawingEnabled());
#endif
            RefPtr { m_overflowControlsHostLayer }->addChild(*m_layerForHorizontalScrollbar);

            if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
                scrollingCoordinator->scrollableAreaScrollbarLayerDidChange(m_renderView.frameView(), ScrollbarOrientation::Horizontal);
        }
    } else if (m_layerForHorizontalScrollbar) {
        GraphicsLayer::unparentAndClear(m_layerForHorizontalScrollbar);

        if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
            scrollingCoordinator->scrollableAreaScrollbarLayerDidChange(m_renderView.frameView(), ScrollbarOrientation::Horizontal);
    }

    if (requiresVerticalScrollbarLayer()) {
        if (!m_layerForVerticalScrollbar) {
            m_layerForVerticalScrollbar = GraphicsLayer::create(graphicsLayerFactory(), *this);
            m_layerForVerticalScrollbar->setAllowsBackingStoreDetaching(false);
            m_layerForVerticalScrollbar->setAllowsTiling(false);
            m_layerForVerticalScrollbar->setShowDebugBorder(m_showDebugBorders);
            m_layerForVerticalScrollbar->setName(MAKE_STATIC_STRING_IMPL("vertical scrollbar container"));
#if USE(CA)
            m_layerForVerticalScrollbar->setAcceleratesDrawing(acceleratedDrawingEnabled());
#endif
            RefPtr { m_overflowControlsHostLayer }->addChild(*m_layerForVerticalScrollbar);

            if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
                scrollingCoordinator->scrollableAreaScrollbarLayerDidChange(m_renderView.frameView(), ScrollbarOrientation::Vertical);
        }
    } else if (m_layerForVerticalScrollbar) {
        GraphicsLayer::unparentAndClear(m_layerForVerticalScrollbar);

        if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
            scrollingCoordinator->scrollableAreaScrollbarLayerDidChange(m_renderView.frameView(), ScrollbarOrientation::Vertical);
    }

    if (requiresScrollCornerLayer()) {
        if (!m_layerForScrollCorner) {
            m_layerForScrollCorner = GraphicsLayer::create(graphicsLayerFactory(), *this);
            m_layerForScrollCorner->setAllowsBackingStoreDetaching(false);
            m_layerForScrollCorner->setShowDebugBorder(m_showDebugBorders);
            m_layerForScrollCorner->setName(MAKE_STATIC_STRING_IMPL("scroll corner"));
#if USE(CA)
            m_layerForScrollCorner->setAcceleratesDrawing(acceleratedDrawingEnabled());
#endif
            RefPtr { m_overflowControlsHostLayer }->addChild(*m_layerForScrollCorner);
        }
    } else
        GraphicsLayer::unparentAndClear(m_layerForScrollCorner);

    m_renderView.frameView().positionScrollbarLayers();
}

void RenderLayerCompositor::ensureRootLayer()
{
    RootLayerAttachment expectedAttachment = isRootFrameCompositor() ? RootLayerAttachedViaChromeClient : RootLayerAttachedViaEnclosingFrame;
    if (expectedAttachment == m_rootLayerAttachment)
         return;

    if (!m_rootContentsLayer) {
        m_rootContentsLayer = GraphicsLayer::create(graphicsLayerFactory(), *this);
        m_rootContentsLayer->setName(MAKE_STATIC_STRING_IMPL("content root"));
        IntRect overflowRect = snappedIntRect(m_renderView.layoutOverflowRect());
        RefPtr { m_rootContentsLayer }->setSize(FloatSize(overflowRect.maxX(), overflowRect.maxY()));
        m_rootContentsLayer->setPosition(FloatPoint());

#if PLATFORM(IOS_FAMILY)
        // Page scale is applied above this on iOS, so we'll just say that our root layer applies it.
        if (m_renderView.frameView().frame().isRootFrame())
            m_rootContentsLayer->setAppliesPageScale();
#endif

        // Need to clip to prevent transformed content showing outside this frame
        updateRootContentLayerClipping();
        updateRootContentsLayerBackgroundColor();
    }

    if (requiresScrollLayer(expectedAttachment)) {
        if (!m_overflowControlsHostLayer) {
            ASSERT(!m_scrolledContentsLayer);
            ASSERT(!m_clipLayer);

            // Create a layer to host the clipping layer and the overflow controls layers.
            m_overflowControlsHostLayer = GraphicsLayer::create(graphicsLayerFactory(), *this);
            m_overflowControlsHostLayer->setName(MAKE_STATIC_STRING_IMPL("overflow controls host"));

            m_scrolledContentsLayer = GraphicsLayer::create(graphicsLayerFactory(), *this, GraphicsLayer::Type::ScrolledContents);
            m_scrolledContentsLayer->setName(MAKE_STATIC_STRING_IMPL("frame scrolled contents"));
            m_scrolledContentsLayer->setAnchorPoint({ });

#if PLATFORM(IOS_FAMILY)
            if (m_renderView.settings().asyncFrameScrollingEnabled()) {
                m_scrollContainerLayer = GraphicsLayer::create(graphicsLayerFactory(), *this, GraphicsLayer::Type::ScrollContainer);

                m_scrollContainerLayer->setName(MAKE_STATIC_STRING_IMPL("scroll container"));
                m_scrollContainerLayer->setMasksToBounds(true);
                m_scrollContainerLayer->setAnchorPoint({ });

                m_scrollContainerLayer->addChild(*m_scrolledContentsLayer);
                m_overflowControlsHostLayer->addChild(*m_scrollContainerLayer);
            }
#endif
            // FIXME: m_scrollContainerLayer and m_clipLayer have similar roles here, but m_clipLayer has some special positioning to
            // account for clipping and top content inset (see LocalFrameView::positionForInsetClipLayer()).
            if (!m_scrollContainerLayer) {
                m_clipLayer = GraphicsLayer::create(graphicsLayerFactory(), *this);
                RefPtr clipLayer = m_clipLayer;
                clipLayer->setName(MAKE_STATIC_STRING_IMPL("frame clipping"));
                clipLayer->setMasksToBounds(true);
                clipLayer->setAnchorPoint({ });

                clipLayer->addChild(*m_scrolledContentsLayer);
                RefPtr { m_overflowControlsHostLayer }->addChild(*m_clipLayer);
            }

            RefPtr { m_scrolledContentsLayer }->addChild(*m_rootContentsLayer);

            updateScrollLayerClipping();
            updateOverflowControlsLayers();

            if (hasCoordinatedScrolling())
                scheduleRenderingUpdate();
            else
                updateScrollLayerPosition();
        }
    } else {
        if (m_overflowControlsHostLayer) {
            GraphicsLayer::unparentAndClear(m_overflowControlsHostLayer);
            GraphicsLayer::unparentAndClear(m_clipLayer);
            GraphicsLayer::unparentAndClear(m_scrollContainerLayer);
            GraphicsLayer::unparentAndClear(m_scrolledContentsLayer);
        }
    }

    // Check to see if we have to change the attachment
    if (m_rootLayerAttachment != RootLayerUnattached)
        detachRootLayer();

    attachRootLayer(expectedAttachment);
}

void RenderLayerCompositor::destroyRootLayer()
{
    if (!m_rootContentsLayer)
        return;

    detachRootLayer();

#if HAVE(RUBBER_BANDING)
    GraphicsLayer::unparentAndClear(m_layerForOverhangAreas);
#endif

    Ref frameView = m_renderView.frameView();

    if (m_layerForHorizontalScrollbar) {
        GraphicsLayer::unparentAndClear(m_layerForHorizontalScrollbar);
        if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
            scrollingCoordinator->scrollableAreaScrollbarLayerDidChange(frameView, ScrollbarOrientation::Horizontal);
        if (RefPtr horizontalScrollbar = frameView->horizontalScrollbar())
            frameView->invalidateScrollbar(*horizontalScrollbar, IntRect(IntPoint(0, 0), horizontalScrollbar->frameRect().size()));
    }

    if (m_layerForVerticalScrollbar) {
        GraphicsLayer::unparentAndClear(m_layerForVerticalScrollbar);
        if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
            scrollingCoordinator->scrollableAreaScrollbarLayerDidChange(frameView, ScrollbarOrientation::Vertical);
        if (RefPtr verticalScrollbar = frameView->verticalScrollbar())
            frameView->invalidateScrollbar(*verticalScrollbar, IntRect(IntPoint(0, 0), verticalScrollbar->frameRect().size()));
    }

    if (m_layerForScrollCorner) {
        GraphicsLayer::unparentAndClear(m_layerForScrollCorner);
        frameView->invalidateScrollCorner(frameView->scrollCornerRect());
    }

    if (m_overflowControlsHostLayer) {
        GraphicsLayer::unparentAndClear(m_overflowControlsHostLayer);
        GraphicsLayer::unparentAndClear(m_clipLayer);
        GraphicsLayer::unparentAndClear(m_scrollContainerLayer);
        GraphicsLayer::unparentAndClear(m_scrolledContentsLayer);
    }
    ASSERT(!m_scrolledContentsLayer);
    GraphicsLayer::unparentAndClear(m_rootContentsLayer);
}

void RenderLayerCompositor::attachRootLayer(RootLayerAttachment attachment)
{
    if (!m_rootContentsLayer)
        return;

    LOG(Compositing, "RenderLayerCompositor %p attachRootLayer %d", this, attachment);

    switch (attachment) {
        case RootLayerUnattached:
            ASSERT_NOT_REACHED();
            break;
        case RootLayerAttachedViaChromeClient: {
            page().chrome().client().attachRootGraphicsLayer(m_renderView.frameView().protectedFrame(), RefPtr { rootGraphicsLayer() }.get());
            break;
        }
        case RootLayerAttachedViaEnclosingFrame: {
            // The layer will get hooked up via RenderLayerBacking::updateConfiguration()
            // for the frame's renderer in the parent document.
            if (RefPtr ownerElement = m_renderView.protectedDocument()->ownerElement())
                ownerElement->scheduleInvalidateStyleAndLayerComposition();
            break;
        }
    }

    m_rootLayerAttachment = attachment;
    rootLayerAttachmentChanged();
    
    if (m_shouldFlushOnReattach) {
        scheduleRenderingUpdate();
        m_shouldFlushOnReattach = false;
    }
}

void RenderLayerCompositor::detachRootLayer()
{
    if (!m_rootContentsLayer || m_rootLayerAttachment == RootLayerUnattached)
        return;

    if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
        scrollingCoordinator->frameViewWillBeDetached(m_renderView.frameView());

    switch (m_rootLayerAttachment) {
    case RootLayerAttachedViaEnclosingFrame: {
        // The layer will get unhooked up via RenderLayerBacking::updateConfiguration()
        // for the frame's renderer in the parent document.
        if (RefPtr layer = m_overflowControlsHostLayer)
            layer->removeFromParent();
        else
            RefPtr { m_rootContentsLayer }->removeFromParent();

        if (RefPtr ownerElement = m_renderView.protectedDocument()->ownerElement())
            ownerElement->scheduleInvalidateStyleAndLayerComposition();

        if (auto frameRootScrollingNodeID = m_renderView.frameView().scrollingNodeID()) {
            if (RefPtr scrollingCoordinator = this->scrollingCoordinator()) {
                scrollingCoordinator->frameViewWillBeDetached(m_renderView.frameView());
                scrollingCoordinator->unparentNode(*frameRootScrollingNodeID);
            }
        }
        break;
    }
    case RootLayerAttachedViaChromeClient: {
        if (RefPtr scrollingCoordinator = this->scrollingCoordinator())
            scrollingCoordinator->frameViewWillBeDetached(m_renderView.frameView());
        page().chrome().client().attachRootGraphicsLayer(m_renderView.frameView().protectedFrame(), nullptr);
    }
    break;
    case RootLayerUnattached:
        break;
    }

    m_rootLayerAttachment = RootLayerUnattached;
    rootLayerAttachmentChanged();
}

void RenderLayerCompositor::updateRootLayerAttachment()
{
    ensureRootLayer();
}

void RenderLayerCompositor::rootLayerAttachmentChanged()
{
    // The document-relative page overlay layer (which is pinned to the main frame's layer tree)
    // is moved between different RenderLayerCompositors' layer trees, and needs to be
    // reattached whenever we swap in a new RenderLayerCompositor.
    if (m_rootLayerAttachment == RootLayerUnattached)
        return;

    // The attachment can affect whether the RenderView layer's paintsIntoWindow() behavior,
    // so call updateDrawsContent() to update that.
    auto* layer = m_renderView.layer();
    if (auto* backing = layer ? layer->backing() : nullptr)
        backing->updateDrawsContent();

    if (!m_renderView.frameView().frame().isMainFrame())
        return;

    Ref<GraphicsLayer> overlayHost = page().pageOverlayController().layerWithDocumentOverlays();
    RefPtr { m_rootContentsLayer }->addChild(WTFMove(overlayHost));
}

void RenderLayerCompositor::notifyIFramesOfCompositingChange()
{
    // Compositing affects the answer to RenderIFrame::requiresAcceleratedCompositing(), so
    // we need to schedule a style recalc in our parent document.
    if (RefPtr ownerElement = m_renderView.protectedDocument()->ownerElement())
        ownerElement->scheduleInvalidateStyleAndLayerComposition();
}

bool RenderLayerCompositor::layerHas3DContent(const RenderLayer& layer) const
{
    const RenderStyle& style = layer.renderer().style();

    if (style.preserves3D() || style.hasPerspective() || styleHas3DTransformOperation(style))
        return true;

    const_cast<RenderLayer&>(layer).updateLayerListsIfNeeded();

#if ASSERT_ENABLED
    LayerListMutationDetector mutationChecker(const_cast<RenderLayer&>(layer));
#endif

    for (auto* renderLayer : layer.negativeZOrderLayers()) {
        if (layerHas3DContent(*renderLayer))
            return true;
    }

    for (auto* renderLayer : layer.positiveZOrderLayers()) {
        if (layerHas3DContent(*renderLayer))
            return true;
    }

    for (auto* renderLayer : layer.normalFlowLayers()) {
        if (layerHas3DContent(*renderLayer))
            return true;
    }

    return false;
}

void RenderLayerCompositor::deviceOrPageScaleFactorChanged()
{
    // Page scale will only be applied at to the RenderView and sublayers, but the device scale factor
    // needs to be applied at the level of rootGraphicsLayer().
    if (RefPtr rootLayer = rootGraphicsLayer())
        rootLayer->noteDeviceOrPageScaleFactorChangedIncludingDescendants();
}

void RenderLayerCompositor::removeFromScrollCoordinatedLayers(RenderLayer& layer)
{
#if PLATFORM(IOS_FAMILY)
    if (m_legacyScrollingLayerCoordinator)
        m_legacyScrollingLayerCoordinator->removeLayer(layer);
#endif

    detachScrollCoordinatedLayer(layer, allScrollCoordinationRoles());
}

FixedPositionViewportConstraints RenderLayerCompositor::computeFixedViewportConstraints(RenderLayer& layer) const
{
    ASSERT(layer.isComposited());

    RefPtr scrollingNodeLayer = layer.backing()->viewportClippingOrAnchorLayer();
    if (!scrollingNodeLayer) {
        ASSERT_NOT_REACHED();
        return { };
    }

    FixedPositionViewportConstraints constraints;
    constraints.setLayerPositionAtLastLayout(scrollingNodeLayer->position());
    constraints.setViewportRectAtLastLayout(m_renderView.frameView().rectForFixedPositionLayout());
    constraints.setAlignmentOffset(scrollingNodeLayer->pixelAlignmentOffset());

    const RenderStyle& style = layer.renderer().style();
    if (!style.left().isAuto())
        constraints.addAnchorEdge(ViewportConstraints::AnchorEdgeLeft);

    if (!style.right().isAuto())
        constraints.addAnchorEdge(ViewportConstraints::AnchorEdgeRight);

    if (!style.top().isAuto())
        constraints.addAnchorEdge(ViewportConstraints::AnchorEdgeTop);

    if (!style.bottom().isAuto())
        constraints.addAnchorEdge(ViewportConstraints::AnchorEdgeBottom);

    // If left and right are auto, use left.
    if (style.left().isAuto() && style.right().isAuto())
        constraints.addAnchorEdge(ViewportConstraints::AnchorEdgeLeft);

    // If top and bottom are auto, use top.
    if (style.top().isAuto() && style.bottom().isAuto())
        constraints.addAnchorEdge(ViewportConstraints::AnchorEdgeTop);
        
    return constraints;
}

StickyPositionViewportConstraints RenderLayerCompositor::computeStickyViewportConstraints(RenderLayer& layer) const
{
    ASSERT(layer.isComposited());

    auto& renderer = downcast<RenderBoxModelObject>(layer.renderer());

    RefPtr scrollingNodeLayer = layer.backing()->viewportClippingOrAnchorLayer();
    if (!scrollingNodeLayer) {
        ASSERT_NOT_REACHED();
        return { };
    }

    RefPtr anchorLayer = layer.backing()->viewportAnchorLayer();
    if (!anchorLayer) {
        ASSERT_NOT_REACHED();
        return { };
    }

    StickyPositionViewportConstraints constraints;
    renderer.computeStickyPositionConstraints(constraints, renderer.constrainingRectForStickyPosition());

    constraints.setViewportRectAtLastLayout(m_renderView.frameView().rectForFixedPositionLayout());
    constraints.setLayerPositionAtLastLayout(scrollingNodeLayer->position());
    if (scrollingNodeLayer != anchorLayer)
        constraints.setAnchorLayerOffsetAtLastLayout(toFloatSize(anchorLayer->position()));
    constraints.setStickyOffsetAtLastLayout(renderer.stickyPositionOffset());
    constraints.setAlignmentOffset(scrollingNodeLayer->pixelAlignmentOffset());

    return constraints;
}

static inline ScrollCoordinationRole scrollCoordinationRoleForNodeType(ScrollingNodeType nodeType)
{
    switch (nodeType) {
    case ScrollingNodeType::MainFrame:
    case ScrollingNodeType::Subframe:
    case ScrollingNodeType::Overflow:
    case ScrollingNodeType::PluginScrolling:
        return ScrollCoordinationRole::Scrolling;
    case ScrollingNodeType::OverflowProxy:
        return ScrollCoordinationRole::ScrollingProxy;
    case ScrollingNodeType::FrameHosting:
        return ScrollCoordinationRole::FrameHosting;
    case ScrollingNodeType::PluginHosting:
        return ScrollCoordinationRole::PluginHosting;
    case ScrollingNodeType::Fixed:
    case ScrollingNodeType::Sticky:
        return ScrollCoordinationRole::ViewportConstrained;
    case ScrollingNodeType::Positioned:
        return ScrollCoordinationRole::Positioning;
    }
    ASSERT_NOT_REACHED();
    return ScrollCoordinationRole::Scrolling;
}

std::optional<ScrollingNodeID> RenderLayerCompositor::attachScrollingNode(RenderLayer& layer, ScrollingNodeType nodeType, ScrollingTreeState& treeState)
{
    RefPtr scrollingCoordinator = this->scrollingCoordinator();
    if (!scrollingCoordinator)
        return std::nullopt;

    auto* backing = layer.backing();
    // Crash logs suggest that backing can be null here, but we don't know how: rdar://problem/18545452.
    ASSERT(backing);
    if (!backing)
        return std::nullopt;


    ASSERT(treeState.hasParent || nodeType == ScrollingNodeType::Subframe);
    ASSERT_IMPLIES(nodeType == ScrollingNodeType::MainFrame, !treeState.parentNodeID);

    ScrollCoordinationRole role = scrollCoordinationRoleForNodeType(nodeType);
    auto nodeID = backing->scrollingNodeIDForRole(role);
    
    nodeID = registerScrollingNodeID(*scrollingCoordinator, nodeID, nodeType, treeState);

    LOG_WITH_STREAM(ScrollingTree, stream << "RenderLayerCompositor " << this << " attachScrollingNode " << nodeID << " (layer " << backing->graphicsLayer()->primaryLayerID() << ") type " << nodeType << " parent " << treeState.parentNodeID);

    if (!nodeID)
        return std::nullopt;
    
    backing->setScrollingNodeIDForRole(*nodeID, role);

#if ENABLE(SCROLLING_THREAD)
    if (nodeType == ScrollingNodeType::Subframe)
        RefPtr { m_clipLayer }->setScrollingNodeID(*nodeID);
#endif

    m_scrollingNodeToLayerMap.add(*nodeID, layer);

    return nodeID;
}

std::optional<ScrollingNodeID> RenderLayerCompositor::registerScrollingNodeID(ScrollingCoordinator& scrollingCoordinator, std::optional<ScrollingNodeID> nodeID, ScrollingNodeType nodeType, struct ScrollingTreeState& treeState)
{
    if (!nodeID)
        nodeID = scrollingCoordinator.uniqueScrollingNodeID();

    if (nodeType == ScrollingNodeType::Subframe && !treeState.hasParent)
        nodeID = scrollingCoordinator.createNode(m_renderView.frameView().frame().rootFrame().frameID(), nodeType, *nodeID);
    else {
        auto newNodeID = scrollingCoordinator.insertNode(m_renderView.frameView().frame().rootFrame().frameID(), nodeType, *nodeID, treeState.parentNodeID, treeState.nextChildIndex);
        if (newNodeID != nodeID) {
            // We'll get a new nodeID if the type changed (and not if the node is new).
            scrollingCoordinator.unparentChildrenAndDestroyNode(*nodeID);
            m_scrollingNodeToLayerMap.remove(*nodeID);
        }
        nodeID = newNodeID;
    }

    ASSERT(nodeID);
    if (!nodeID)
        return std::nullopt;
    
    ++treeState.nextChildIndex;
    return nodeID;
}

void RenderLayerCompositor::detachScrollCoordinatedLayerWithRole(RenderLayer& layer, ScrollingCoordinator& scrollingCoordinator, ScrollCoordinationRole role)
{
    auto unregisterNode = [&](ScrollingNodeID nodeID) {
        auto childNodes = scrollingCoordinator.childrenOfNode(nodeID);
        for (auto childNodeID : childNodes) {
            if (auto weakLayer = m_scrollingNodeToLayerMap.get(childNodeID))
                weakLayer->setNeedsScrollingTreeUpdate();
        }

        m_scrollingNodeToLayerMap.remove(nodeID);
    };

    if (role == ScrollCoordinationRole::ScrollingProxy) {
        ASSERT(layer.isComposited());
        auto* clippingStack = layer.backing()->ancestorClippingStack();
        if (!clippingStack)
            return;
        
        auto& stack = clippingStack->stack();
        for (auto& entry : stack) {
            if (entry.overflowScrollProxyNodeID)
                unregisterNode(*entry.overflowScrollProxyNodeID);
        }
        return;
    }

    if (auto nodeID = layer.backing()->scrollingNodeIDForRole(role))
        unregisterNode(*nodeID);
}

void RenderLayerCompositor::detachScrollCoordinatedLayer(RenderLayer& layer, OptionSet<ScrollCoordinationRole> roles)
{
    auto* backing = layer.backing();
    if (!backing)
        return;

    RefPtr scrollingCoordinator = this->scrollingCoordinator();
    if (!scrollingCoordinator)
        return;

    if (roles.contains(ScrollCoordinationRole::Scrolling))
        detachScrollCoordinatedLayerWithRole(layer, *scrollingCoordinator, ScrollCoordinationRole::Scrolling);

    if (roles.contains(ScrollCoordinationRole::ScrollingProxy))
        detachScrollCoordinatedLayerWithRole(layer, *scrollingCoordinator, ScrollCoordinationRole::ScrollingProxy);

    if (roles.contains(ScrollCoordinationRole::FrameHosting))
        detachScrollCoordinatedLayerWithRole(layer, *scrollingCoordinator, ScrollCoordinationRole::FrameHosting);

    if (roles.contains(ScrollCoordinationRole::PluginHosting))
        detachScrollCoordinatedLayerWithRole(layer, *scrollingCoordinator, ScrollCoordinationRole::PluginHosting);

    if (roles.contains(ScrollCoordinationRole::ViewportConstrained))
        detachScrollCoordinatedLayerWithRole(layer, *scrollingCoordinator, ScrollCoordinationRole::ViewportConstrained);

    if (roles.contains(ScrollCoordinationRole::Positioning))
        detachScrollCoordinatedLayerWithRole(layer, *scrollingCoordinator, ScrollCoordinationRole::Positioning);

    backing->detachFromScrollingCoordinator(roles);
}

OptionSet<ScrollCoordinationRole> RenderLayerCompositor::coordinatedScrollingRolesForLayer(const RenderLayer& layer, const RenderLayer* compositingAncestor) const
{
    OptionSet<ScrollCoordinationRole> coordinationRoles;
    if (viewportConstrainedSublayers(layer, compositingAncestor) != ViewportConstrainedSublayers::None)
        coordinationRoles.add(ScrollCoordinationRole::ViewportConstrained);

    if (useCoordinatedScrollingForLayer(layer))
        coordinationRoles.add(ScrollCoordinationRole::Scrolling);

    auto coordinatedPositioning = computeCoordinatedPositioningForLayer(layer, compositingAncestor);
    switch (coordinatedPositioning) {
    case ScrollPositioningBehavior::Moves:
        coordinationRoles.add(ScrollCoordinationRole::ScrollingProxy);
        break;
    case ScrollPositioningBehavior::Stationary:
        coordinationRoles.add(ScrollCoordinationRole::Positioning);
        break;
    case ScrollPositioningBehavior::None:
        break;
    }

    if (isLayerForIFrameWithScrollCoordinatedContents(layer))
        coordinationRoles.add(ScrollCoordinationRole::FrameHosting);

    if (isLayerForPluginWithScrollCoordinatedContents(layer))
        coordinationRoles.add(ScrollCoordinationRole::PluginHosting);

    return coordinationRoles;
}

std::optional<ScrollingNodeID> RenderLayerCompositor::updateScrollCoordinationForLayer(RenderLayer& layer, const RenderLayer* compositingAncestor, ScrollingTreeState& treeState, OptionSet<ScrollingNodeChangeFlags> changes)
{
    auto roles = coordinatedScrollingRolesForLayer(layer, compositingAncestor);

#if PLATFORM(IOS_FAMILY)
    if (m_legacyScrollingLayerCoordinator) {
        if (roles.contains(ScrollCoordinationRole::ViewportConstrained))
            m_legacyScrollingLayerCoordinator->addViewportConstrainedLayer(layer);
        else
            m_legacyScrollingLayerCoordinator->removeViewportConstrainedLayer(layer);
    }
#endif

    if (!hasCoordinatedScrolling()) {
        // If this frame isn't coordinated, it cannot contain any scrolling tree nodes.
        return std::nullopt;
    }

    auto newNodeID = treeState.parentNodeID;

    ScrollingTreeState childTreeState;
    ScrollingTreeState* currentTreeState = &treeState;

    // If there's a positioning node, it's the parent scrolling node for fixed/sticky/scrolling/frame hosting.
    if (roles.contains(ScrollCoordinationRole::Positioning)) {
        newNodeID = updateScrollingNodeForPositioningRole(layer, compositingAncestor, *currentTreeState, changes);
        childTreeState.parentNodeID = newNodeID;
        childTreeState.hasParent = true;
        currentTreeState = &childTreeState;
    } else
        detachScrollCoordinatedLayer(layer, ScrollCoordinationRole::Positioning);

    // If there's a scrolling proxy node, it's the parent scrolling node for fixed/sticky/scrolling/frame hosting.
    if (roles.contains(ScrollCoordinationRole::ScrollingProxy)) {
        newNodeID = updateScrollingNodeForScrollingProxyRole(layer, *currentTreeState, changes);
        childTreeState.parentNodeID = newNodeID;
        childTreeState.hasParent = true;
        currentTreeState = &childTreeState;
    } else
        detachScrollCoordinatedLayer(layer, ScrollCoordinationRole::ScrollingProxy);

    // If is fixed or sticky, it's the parent scrolling node for scrolling/frame hosting.
    if (roles.contains(ScrollCoordinationRole::ViewportConstrained)) {
        newNodeID = updateScrollingNodeForViewportConstrainedRole(layer, *currentTreeState, changes);
        // ViewportConstrained nodes are the parent of same-layer scrolling nodes, so adjust the ScrollingTreeState.
        childTreeState.parentNodeID = newNodeID;
        childTreeState.hasParent = true;
        currentTreeState = &childTreeState;
    } else
        detachScrollCoordinatedLayer(layer, ScrollCoordinationRole::ViewportConstrained);

    if (roles.contains(ScrollCoordinationRole::Scrolling))
        newNodeID = updateScrollingNodeForScrollingRole(layer, *currentTreeState, changes);
    else
        detachScrollCoordinatedLayer(layer, ScrollCoordinationRole::Scrolling);

    if (roles.contains(ScrollCoordinationRole::FrameHosting))
        newNodeID = updateScrollingNodeForFrameHostingRole(layer, *currentTreeState, changes);
    else
        detachScrollCoordinatedLayer(layer, ScrollCoordinationRole::FrameHosting);

    if (roles.contains(ScrollCoordinationRole::PluginHosting))
        newNodeID = updateScrollingNodeForPluginHostingRole(layer, *currentTreeState, changes);
    else
        detachScrollCoordinatedLayer(layer, ScrollCoordinationRole::PluginHosting);

    return newNodeID;
}

std::optional<ScrollingNodeID> RenderLayerCompositor::updateScrollingNodeForViewportConstrainedRole(RenderLayer& layer, ScrollingTreeState& treeState, OptionSet<ScrollingNodeChangeFlags> changes)
{
    RefPtr scrollingCoordinator = this->scrollingCoordinator();

    auto nodeType = ScrollingNodeType::Fixed;
    if (layer.renderer().style().position() == PositionType::Sticky)
        nodeType = ScrollingNodeType::Sticky;
    else
        ASSERT(layer.renderer().isFixedPositioned());

    auto newNodeID = attachScrollingNode(layer, nodeType, treeState);
    if (!newNodeID) {
        ASSERT_NOT_REACHED();
        return treeState.parentNodeID;
    }

    LOG_WITH_STREAM(Compositing, stream << "Registering ViewportConstrained " << nodeType << " node " << newNodeID << " (layer " << layer.backing()->graphicsLayer()->primaryLayerID() << ") as child of " << treeState.parentNodeID);

    if (changes & ScrollingNodeChangeFlags::Layer) {
        scrollingCoordinator->setNodeLayers(*newNodeID, {
            .layer = layer.backing()->viewportClippingOrAnchorLayer(),
            .viewportAnchorLayer = layer.backing()->viewportAnchorLayer(),
        });
    }

    if (changes & ScrollingNodeChangeFlags::LayerGeometry) {
        switch (nodeType) {
        case ScrollingNodeType::Fixed:
            scrollingCoordinator->setViewportConstraintedNodeConstraints(*newNodeID, computeFixedViewportConstraints(layer));
            break;
        case ScrollingNodeType::Sticky:
            scrollingCoordinator->setViewportConstraintedNodeConstraints(*newNodeID, computeStickyViewportConstraints(layer));
            break;
        default:
            break;
        }
    }

    return newNodeID;
}

LayoutRoundedRect RenderLayerCompositor::parentRelativeScrollableRect(const RenderLayer& layer, const RenderLayer* ancestorLayer) const
{
    // FIXME: ancestorLayer needs to be always non-null, so should become a reference.
    if (!ancestorLayer) {
        if (!layer.scrollableArea())
            return LayoutRoundedRect { LayoutRect { } };
        return LayoutRoundedRect { LayoutRect({ }, LayoutSize(CheckedPtr { layer.scrollableArea() }->visibleSize())) };
    }

    LayoutRoundedRect scrollableRect(LayoutRect { });
    {
        CheckedPtr box = dynamicDowncast<RenderBox>(layer.renderer());
        if (!box)
            return LayoutRoundedRect { LayoutRect { } };

        scrollableRect = LayoutRoundedRect { box->paddingBoxRect() };
        if (box->style().hasBorderRadius()) {
            auto borderShape = BorderShape::shapeForBorderRect(box->style(), box->borderBoxRect());
            scrollableRect = borderShape.deprecatedInnerRoundedRect();
        }
    }

    auto offset = layer.convertToLayerCoords(ancestorLayer, scrollableRect.rect().location()); // FIXME: broken for columns.
    auto rect = scrollableRect.rect();
    rect.setLocation(offset);
    scrollableRect.setRect(rect);
    return scrollableRect;
}

void RenderLayerCompositor::updateScrollingNodeLayers(ScrollingNodeID nodeID, RenderLayer& layer, ScrollingCoordinator& scrollingCoordinator)
{
    // Plugins handle their own scrolling node layers.
    if (isLayerForPluginWithScrollCoordinatedContents(layer))
        return;

    if (layer.isRenderViewLayer()) {
        Ref frameView = m_renderView.frameView();
        scrollingCoordinator.setNodeLayers(nodeID, { nullptr,
            scrollContainerLayer(), scrolledContentsLayer(),
            fixedRootBackgroundLayer(), clipLayer(), rootContentsLayer(),
            frameView->layerForHorizontalScrollbar(), frameView->layerForVerticalScrollbar() });
    } else {
        CheckedPtr scrollableArea = layer.scrollableArea();
        ASSERT(scrollableArea);

        auto& backing = *layer.backing();
        scrollingCoordinator.setNodeLayers(nodeID, { backing.graphicsLayer(),
            backing.scrollContainerLayer(), backing.scrolledContentsLayer(),
            nullptr, nullptr, nullptr,
            scrollableArea->layerForHorizontalScrollbar(), scrollableArea->layerForVerticalScrollbar() });
    }
}

std::optional<ScrollingNodeID> RenderLayerCompositor::updateScrollingNodeForScrollingRole(RenderLayer& layer, ScrollingTreeState& treeState, OptionSet<ScrollingNodeChangeFlags> changes)
{
    RefPtr scrollingCoordinator = this->scrollingCoordinator();

    std::optional<ScrollingNodeID> newNodeID;

    if (layer.isRenderViewLayer()) {
        Ref frameView = m_renderView.frameView();
        ASSERT_UNUSED(frameView, scrollingCoordinator->coordinatesScrollingForFrameView(frameView));

        newNodeID = attachScrollingNode(*m_renderView.layer(), m_renderView.frame().isMainFrame() ? ScrollingNodeType::MainFrame : ScrollingNodeType::Subframe, treeState);

        if (!newNodeID) {
            ASSERT_NOT_REACHED();
            return treeState.parentNodeID;
        }

        if (changes & ScrollingNodeChangeFlags::Layer)
            updateScrollingNodeLayers(*newNodeID, layer, *scrollingCoordinator);

        if (changes & ScrollingNodeChangeFlags::LayerGeometry) {
            scrollingCoordinator->setScrollingNodeScrollableAreaGeometry(*newNodeID, frameView);
            scrollingCoordinator->setFrameScrollingNodeState(*newNodeID, frameView);
        }
        page().chrome().client().ensureScrollbarsController(protectedPage(), frameView, true);

    } else {
        newNodeID = attachScrollingNode(layer, ScrollingNodeType::Overflow, treeState);
        if (!newNodeID) {
            ASSERT_NOT_REACHED();
            return treeState.parentNodeID;
        }

        // Plugins handle their own scrolling node layers and geometry.
        if (isLayerForPluginWithScrollCoordinatedContents(layer))
            return newNodeID;

        if (changes & ScrollingNodeChangeFlags::Layer)
            updateScrollingNodeLayers(*newNodeID, layer, *scrollingCoordinator);

        if (changes & ScrollingNodeChangeFlags::LayerGeometry && treeState.hasParent) {
            if (CheckedPtr scrollableArea = layer.scrollableArea())
                scrollingCoordinator->setScrollingNodeScrollableAreaGeometry(*newNodeID, *scrollableArea);
        }
        if (CheckedPtr scrollableArea = layer.scrollableArea())
            page().chrome().client().ensureScrollbarsController(protectedPage(), *scrollableArea, true);
    }

    return newNodeID;
}

bool RenderLayerCompositor::setupScrollProxyRelatedOverflowScrollingNode(ScrollingCoordinator& scrollingCoordinator, ScrollingNodeID scrollingProxyNodeID, RenderLayer& overflowScrollingLayer)
{
    auto* backing = overflowScrollingLayer.backing();
    if (!backing)
        return false;

    auto overflowScrollNodeID = backing->scrollingNodeIDForRole(ScrollCoordinationRole::Scrolling);
    if (!overflowScrollNodeID)
        return false;

    scrollingCoordinator.setRelatedOverflowScrollingNodes(scrollingProxyNodeID, { *overflowScrollNodeID });
    return true;
}

std::optional<ScrollingNodeID> RenderLayerCompositor::updateScrollingNodeForScrollingProxyRole(RenderLayer& layer, ScrollingTreeState& treeState, OptionSet<ScrollingNodeChangeFlags> changes)
{
    RefPtr scrollingCoordinator = this->scrollingCoordinator();
    auto* clippingStack = layer.backing()->ancestorClippingStack();
    if (!clippingStack)
        return treeState.parentNodeID;

    std::optional<ScrollingNodeID> nodeID;
    for (auto& entry : clippingStack->stack()) {
        if (!entry.clipData.isOverflowScroll)
            continue;

        nodeID = registerScrollingNodeID(*scrollingCoordinator, entry.overflowScrollProxyNodeID, ScrollingNodeType::OverflowProxy, treeState);
        if (!nodeID) {
            ASSERT_NOT_REACHED();
            return treeState.parentNodeID;
        }
        entry.overflowScrollProxyNodeID = *nodeID;
#if ENABLE(SCROLLING_THREAD)
        if (RefPtr scrollingLayer = entry.scrollingLayer)
            scrollingLayer->setScrollingNodeID(*nodeID);
#endif

        if (changes & ScrollingNodeChangeFlags::Layer)
            scrollingCoordinator->setNodeLayers(*entry.overflowScrollProxyNodeID, { entry.scrollingLayer.get() });

        if (changes & ScrollingNodeChangeFlags::LayerGeometry) {
            ASSERT(entry.clipData.clippingLayer);
            ASSERT(entry.clipData.clippingLayer->isComposited());

            if (!setupScrollProxyRelatedOverflowScrollingNode(*scrollingCoordinator, *entry.overflowScrollProxyNodeID, *entry.clipData.clippingLayer))
                m_layersWithUnresolvedRelations.add(layer);
        }
    }
    
    // FIXME: also m_overflowControlsHostLayerAncestorClippingStack

    if (!nodeID)
        return treeState.parentNodeID;

    return nodeID;
}

std::optional<ScrollingNodeID> RenderLayerCompositor::updateScrollingNodeForFrameHostingRole(RenderLayer& layer, ScrollingTreeState& treeState, OptionSet<ScrollingNodeChangeFlags> changes)
{
    RefPtr scrollingCoordinator = this->scrollingCoordinator();

    auto newNodeID = attachScrollingNode(layer, ScrollingNodeType::FrameHosting, treeState);
    if (!newNodeID) {
        ASSERT_NOT_REACHED();
        return treeState.parentNodeID;
    }

    if (changes & ScrollingNodeChangeFlags::Layer)
        scrollingCoordinator->setNodeLayers(*newNodeID, { layer.backing()->graphicsLayer() });

    if (auto* renderWidget = dynamicDowncast<RenderWidget>(layer.renderer())) {
        if (auto* frame = renderWidget->frameOwnerElement().contentFrame()) {
            if (is<RemoteFrame>(frame))
                scrollingCoordinator->setLayerHostingContextIdentifierForFrameHostingNode(*newNodeID, dynamicDowncast<RemoteFrame>(frame)->layerHostingContextIdentifier());
        }
    }
    return newNodeID;
}

std::optional<ScrollingNodeID> RenderLayerCompositor::updateScrollingNodeForPluginHostingRole(RenderLayer& layer, ScrollingTreeState& treeState, OptionSet<ScrollingNodeChangeFlags> changes)
{
    UNUSED_PARAM(changes);

    auto newNodeID = attachScrollingNode(layer, ScrollingNodeType::PluginHosting, treeState);
    if (!newNodeID) {
        ASSERT_NOT_REACHED();
        return treeState.parentNodeID;
    }

    return newNodeID;
}

std::optional<ScrollingNodeID> RenderLayerCompositor::updateScrollingNodeForPositioningRole(RenderLayer& layer, const RenderLayer* compositingAncestor, ScrollingTreeState& treeState, OptionSet<ScrollingNodeChangeFlags> changes)
{
    RefPtr scrollingCoordinator = this->scrollingCoordinator();

    auto newNodeID = attachScrollingNode(layer, ScrollingNodeType::Positioned, treeState);
    if (!newNodeID) {
        ASSERT_NOT_REACHED();
        return treeState.parentNodeID;
    }

    if (changes & ScrollingNodeChangeFlags::Layer) {
        auto& backing = *layer.backing();
        scrollingCoordinator->setNodeLayers(*newNodeID, { backing.graphicsLayer() });
    }

    if (changes & ScrollingNodeChangeFlags::LayerGeometry && treeState.hasParent) {
        // Would be nice to avoid calling computeCoordinatedPositioningForLayer() again.
        auto positioningBehavior = computeCoordinatedPositioningForLayer(layer, compositingAncestor);
        auto relatedNodeIDs = collectRelatedCoordinatedScrollingNodes(layer, positioningBehavior);
        scrollingCoordinator->setRelatedOverflowScrollingNodes(*newNodeID, WTFMove(relatedNodeIDs));

        RefPtr graphicsLayer = layer.backing()->graphicsLayer();
        AbsolutePositionConstraints constraints;
        constraints.setAlignmentOffset(graphicsLayer->pixelAlignmentOffset());
        constraints.setLayerPositionAtLastLayout(graphicsLayer->position());
        scrollingCoordinator->setPositionedNodeConstraints(*newNodeID, constraints);
    }

    return newNodeID;
}

void RenderLayerCompositor::resolveScrollingTreeRelationships()
{
    if (m_layersWithUnresolvedRelations.isEmptyIgnoringNullReferences())
        return;

    RefPtr scrollingCoordinator = this->scrollingCoordinator();

    for (auto& layer : m_layersWithUnresolvedRelations) {
        LOG_WITH_STREAM(ScrollingTree, stream << "RenderLayerCompositor::resolveScrollingTreeRelationships - resolving relationship for layer " << &layer);

        if (!layer.isComposited())
            continue;

        if (auto* clippingStack = layer.backing()->ancestorClippingStack()) {
            for (auto& entry : clippingStack->stack()) {
                if (!entry.clipData.isOverflowScroll)
                    continue;

                bool succeeded = setupScrollProxyRelatedOverflowScrollingNode(*scrollingCoordinator, *entry.overflowScrollProxyNodeID, *entry.clipData.clippingLayer);
                ASSERT_UNUSED(succeeded, succeeded);
            }
        }
    }

    m_layersWithUnresolvedRelations.clear();
}

void RenderLayerCompositor::updateSynchronousScrollingNodes()
{
    if (!hasCoordinatedScrolling())
        return;

    if (m_renderView.settings().fixedBackgroundsPaintRelativeToDocument())
        return;

    RefPtr scrollingCoordinator = this->scrollingCoordinator();
    ASSERT(scrollingCoordinator);

    auto rootScrollingNodeID = m_renderView.frameView().scrollingNodeID();
    HashSet<ScrollingNodeID> nodesToClear;
    nodesToClear.reserveInitialCapacity(m_scrollingNodeToLayerMap.size());
    for (auto key : m_scrollingNodeToLayerMap.keys())
        nodesToClear.add(key);
    
    auto clearSynchronousReasonsOnNonRootNodes = [&] {
        for (auto nodeID : nodesToClear) {
            if (nodeID == rootScrollingNodeID)
                continue;

            // Harmless to call setSynchronousScrollingReasons on a non-scrolling node.
            scrollingCoordinator->setSynchronousScrollingReasons(nodeID, { });
        }
    };
    
    auto setHasSlowRepaintObjectsSynchronousScrollingReasonOnRootNode = [&](bool hasSlowRepaintObjects) {
        if (!rootScrollingNodeID)
            return;
        // ScrollingCoordinator manages all bits other than HasSlowRepaintObjects, so maintain their current value.
        auto reasons = scrollingCoordinator->synchronousScrollingReasons(*rootScrollingNodeID);
        reasons.set({ SynchronousScrollingReason::HasSlowRepaintObjects }, hasSlowRepaintObjects);
        scrollingCoordinator->setSynchronousScrollingReasons(*rootScrollingNodeID, reasons);
    };

    auto slowRepaintObjects = m_renderView.frameView().slowRepaintObjects();
    if (!slowRepaintObjects) {
        setHasSlowRepaintObjectsSynchronousScrollingReasonOnRootNode(false);
        clearSynchronousReasonsOnNonRootNodes();
        return;
    }

    auto relevantScrollingScope = [](auto& renderer, const RenderLayer& layer) {
        if (&layer.renderer() == &renderer)
            return layer.boxScrollingScope();
        return layer.contentsScrollingScope();
    };

    bool rootHasSlowRepaintObjects = false;
    for (auto& renderer : *slowRepaintObjects) {
        auto layer = renderer.enclosingLayer();
        if (!layer)
            continue;

        auto scrollingScope = relevantScrollingScope(renderer, *layer);
        if (!scrollingScope)
            continue;

        if (auto enclosingScrollingNodeID = asyncScrollableContainerNodeID(renderer)) {
            LOG_WITH_STREAM(Scrolling, stream << "RenderLayerCompositor::updateSynchronousScrollingNodes - node " << enclosingScrollingNodeID << " slow-scrolling because of fixed backgrounds");
            ASSERT(enclosingScrollingNodeID != rootScrollingNodeID);

            scrollingCoordinator->setSynchronousScrollingReasons(*enclosingScrollingNodeID, { SynchronousScrollingReason::HasSlowRepaintObjects });
            nodesToClear.remove(*enclosingScrollingNodeID);

            // Although the root scrolling layer does not have a slow repaint object in it directly,
            // we need to set some synchronous scrolling reason on it so that
            // ScrollingCoordinator::shouldUpdateScrollLayerPositionSynchronously returns an
            // appropriate value. (Scrolling itself would be correct without this, since the
            // scrolling tree propagates DescendantScrollersHaveSynchronousScrolling bits up the
            // tree, but shouldUpdateScrollLayerPositionSynchronously looks at the scrolling state
            // tree instead.)
            rootHasSlowRepaintObjects = true;
        } else if (!layer->behavesAsFixed()) {
            LOG_WITH_STREAM(Scrolling, stream << "RenderLayerCompositor::updateSynchronousScrollingNodes - root node slow-scrolling because of fixed backgrounds");
            rootHasSlowRepaintObjects = true;
        }
    }

    setHasSlowRepaintObjectsSynchronousScrollingReasonOnRootNode(rootHasSlowRepaintObjects);
    clearSynchronousReasonsOnNonRootNodes();
}

ScrollableArea* RenderLayerCompositor::scrollableAreaForScrollingNodeID(std::optional<ScrollingNodeID> nodeID) const
{
    if (!nodeID)
        return nullptr;

    if (*nodeID == m_renderView.frameView().scrollingNodeID())
        return &m_renderView.frameView();

    if (auto weakLayer = m_scrollingNodeToLayerMap.get(*nodeID))
        return weakLayer->scrollableArea();

    return nullptr;
}

void RenderLayerCompositor::willRemoveScrollingLayerWithBacking(RenderLayer& layer, RenderLayerBacking& backing)
{
    if (scrollingCoordinator())
        return;

#if PLATFORM(IOS_FAMILY)
    ASSERT(m_renderView.document().backForwardCacheState() == Document::NotInBackForwardCache);
    if (m_legacyScrollingLayerCoordinator)
        m_legacyScrollingLayerCoordinator->removeScrollingLayer(layer, backing);
#else
    UNUSED_PARAM(layer);
    UNUSED_PARAM(backing);
#endif
}

// FIXME: This should really be called from the updateBackingAndHierarchy.
void RenderLayerCompositor::didAddScrollingLayer(RenderLayer& layer)
{
    if (scrollingCoordinator())
        return;

#if PLATFORM(IOS_FAMILY)
    ASSERT(m_renderView.document().backForwardCacheState() == Document::NotInBackForwardCache);
    if (m_legacyScrollingLayerCoordinator)
        m_legacyScrollingLayerCoordinator->addScrollingLayer(layer);
#else
    UNUSED_PARAM(layer);
#endif
}

ScrollingCoordinator* RenderLayerCompositor::scrollingCoordinator() const
{
    RefPtr frame = m_renderView.document().frame();
    if (!frame)
        return nullptr;

    RefPtr page = frame->page();
    if (!page)
        return nullptr;

    return page->scrollingCoordinator();
}

GraphicsLayerFactory* RenderLayerCompositor::graphicsLayerFactory() const
{
    return page().chrome().client().graphicsLayerFactory();
}

void RenderLayerCompositor::updateScrollSnapPropertiesWithFrameView(const LocalFrameView& frameView) const
{
    if (RefPtr coordinator = scrollingCoordinator())
        coordinator->updateScrollSnapPropertiesWithFrameView(frameView);
}

Page& RenderLayerCompositor::page() const
{
    return m_renderView.page();
}

Ref<Page> RenderLayerCompositor::protectedPage() const
{
    return page();
}

TextStream& operator<<(TextStream& ts, CompositingUpdateType updateType)
{
    switch (updateType) {
    case CompositingUpdateType::AfterStyleChange: ts << "after style change"_s; break;
    case CompositingUpdateType::AfterLayout: ts << "after layout"_s; break;
    case CompositingUpdateType::OnScroll: ts << "on scroll"_s; break;
    case CompositingUpdateType::OnCompositedScroll: ts << "on composited scroll"_s; break;
    }
    return ts;
}

TextStream& operator<<(TextStream& ts, CompositingPolicy compositingPolicy)
{
    switch (compositingPolicy) {
    case CompositingPolicy::Normal: ts << "normal"_s; break;
    case CompositingPolicy::Conservative: ts << "conservative"_s; break;
    }
    return ts;
}

TextStream& operator<<(TextStream& ts, CompositingReason compositingReason)
{
    return ts << compositingReasonToString(compositingReason);
}

TextStream& operator<<(TextStream& ts, const RenderLayerCompositor::BackingSharingState::Provider& provider)
{
    ts << "provider "_s << provider.providerLayer.get() << ", sharing layers "_s;

    bool outputComma = false;
    for (auto& layer : provider.sharingLayers) {
        if (outputComma)
            ts << ", "_s;

        ts << &layer;
        outputComma = true;
    }

    return ts;
}

#if PLATFORM(IOS_FAMILY)
typedef HashMap<PlatformLayer*, std::unique_ptr<ViewportConstraints>> LayerMap;
typedef HashMap<PlatformLayer*, PlatformLayer*> StickyContainerMap;

void LegacyWebKitScrollingLayerCoordinator::registerAllViewportConstrainedLayers(RenderLayerCompositor& compositor)
{
    if (!m_coordinateViewportConstrainedLayers)
        return;

    LayerMap layerMap;
    StickyContainerMap stickyContainerMap;

    for (auto& layer : m_viewportConstrainedLayers) {
        ASSERT(layer.isComposited());

        std::unique_ptr<ViewportConstraints> constraints;
        if (layer.renderer().isStickilyPositioned()) {
            constraints = makeUnique<StickyPositionViewportConstraints>(compositor.computeStickyViewportConstraints(layer));
            const RenderLayer* enclosingTouchScrollableLayer = nullptr;
            if (compositor.isAsyncScrollableStickyLayer(layer, &enclosingTouchScrollableLayer) && enclosingTouchScrollableLayer) {
                ASSERT(enclosingTouchScrollableLayer->isComposited());
                // what
                stickyContainerMap.add(layer.backing()->graphicsLayer()->platformLayer(), enclosingTouchScrollableLayer->backing()->scrollContainerLayer()->platformLayer());
            }
        } else if (layer.renderer().isFixedPositioned())
            constraints = makeUnique<FixedPositionViewportConstraints>(compositor.computeFixedViewportConstraints(layer));
        else
            continue;

        layerMap.add(layer.backing()->graphicsLayer()->platformLayer(), WTFMove(constraints));
    }
    
    m_chromeClient.updateViewportConstrainedLayers(layerMap, stickyContainerMap);
}

void LegacyWebKitScrollingLayerCoordinator::unregisterAllViewportConstrainedLayers()
{
    if (!m_coordinateViewportConstrainedLayers)
        return;

    LayerMap layerMap;
    m_chromeClient.updateViewportConstrainedLayers(layerMap, { });
}

void LegacyWebKitScrollingLayerCoordinator::updateScrollingLayer(RenderLayer& layer)
{
    auto* backing = layer.backing();
    ASSERT(backing);

    auto* scrollableArea = layer.scrollableArea();
    ASSERT(scrollableArea);

    bool allowHorizontalScrollbar = scrollableArea->horizontalNativeScrollbarVisibility() != NativeScrollbarVisibility::HiddenByStyle;
    bool allowVerticalScrollbar = scrollableArea->verticalNativeScrollbarVisibility() != NativeScrollbarVisibility::HiddenByStyle;

    m_chromeClient.addOrUpdateScrollingLayer(layer.renderer().element(), backing->scrollContainerLayer()->platformLayer(), backing->scrolledContentsLayer()->platformLayer(),
        scrollableArea->reachableTotalContentsSize(), allowHorizontalScrollbar, allowVerticalScrollbar);
}

void LegacyWebKitScrollingLayerCoordinator::registerAllScrollingLayers()
{
    for (auto& layer : m_scrollingLayers)
        updateScrollingLayer(layer);
}

void LegacyWebKitScrollingLayerCoordinator::unregisterAllScrollingLayers()
{
    for (auto& layer : m_scrollingLayers) {
        auto* backing = layer.backing();
        ASSERT(backing);
        m_chromeClient.removeScrollingLayer(layer.renderer().element(), backing->scrollContainerLayer()->platformLayer(), backing->scrolledContentsLayer()->platformLayer());
    }
}

void LegacyWebKitScrollingLayerCoordinator::addScrollingLayer(RenderLayer& layer)
{
    m_scrollingLayers.add(layer);
}

void LegacyWebKitScrollingLayerCoordinator::removeScrollingLayer(RenderLayer& layer, RenderLayerBacking& backing)
{
    if (m_scrollingLayers.remove(layer)) {
        auto* scrollContainerLayer = backing.scrollContainerLayer()->platformLayer();
        auto* scrolledContentsLayer = backing.scrolledContentsLayer()->platformLayer();
        m_chromeClient.removeScrollingLayer(layer.renderer().element(), scrollContainerLayer, scrolledContentsLayer);
    }
}

void LegacyWebKitScrollingLayerCoordinator::removeLayer(RenderLayer& layer)
{
    removeScrollingLayer(layer, *layer.backing());

    // We'll put the new set of layers to the client via registerAllViewportConstrainedLayers() at flush time.
    m_viewportConstrainedLayers.remove(layer);
}

void LegacyWebKitScrollingLayerCoordinator::addViewportConstrainedLayer(RenderLayer& layer)
{
    m_viewportConstrainedLayers.add(layer);
}

void LegacyWebKitScrollingLayerCoordinator::removeViewportConstrainedLayer(RenderLayer& layer)
{
    m_viewportConstrainedLayers.remove(layer);
}

#endif

} // namespace WebCore

#if ENABLE(TREE_DEBUGGING)
void showGraphicsLayerTreeForCompositor(WebCore::RenderLayerCompositor& compositor)
{
    showGraphicsLayerTree(compositor.rootGraphicsLayer());
}
#endif
