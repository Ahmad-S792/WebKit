/*
 * Copyright (C) 2012 Apple Inc. All rights reserved.
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

#include "config.h"
#include "ScrollingTreeScrollingNode.h"

#if ENABLE(ASYNC_SCROLLING)

#include "Logging.h"
#if ENABLE(SCROLLING_THREAD)
#include "ScrollingStateFrameScrollingNode.h"
#endif
#include "ScrollingEffectsController.h"
#include "ScrollingStateScrollingNode.h"
#include "ScrollingStateTree.h"
#include "ScrollingTree.h"
#include "ScrollingTreeScrollingNodeDelegate.h"
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/TextStream.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(ScrollingTreeScrollingNode);

ScrollingTreeScrollingNode::ScrollingTreeScrollingNode(ScrollingTree& scrollingTree, ScrollingNodeType nodeType, ScrollingNodeID nodeID)
    : ScrollingTreeNode(scrollingTree, nodeType, nodeID)
{
}

ScrollingTreeScrollingNode::~ScrollingTreeScrollingNode() = default;

bool ScrollingTreeScrollingNode::commitStateBeforeChildren(const ScrollingStateNode& stateNode)
{
    auto* state = dynamicDowncast<ScrollingStateScrollingNode>(stateNode);
    if (!state)
        return false;

    if (state->hasChangedProperty(ScrollingStateNode::Property::ScrollableAreaSize))
        m_scrollableAreaSize = state->scrollableAreaSize();

    if (state->hasChangedProperty(ScrollingStateNode::Property::TotalContentsSize)) {
        if (scrollingTree()->isRubberBandInProgressForNode(scrollingNodeID()))
            m_totalContentsSizeForRubberBand = m_totalContentsSize;
        else
            m_totalContentsSizeForRubberBand = state->totalContentsSize();

        m_totalContentsSize = state->totalContentsSize();
    }

    if (state->hasChangedProperty(ScrollingStateNode::Property::ReachableContentsSize))
        m_reachableContentsSize = state->reachableContentsSize();

    if (state->hasChangedProperty(ScrollingStateNode::Property::ScrollPosition)) {
        m_lastCommittedScrollPosition = state->scrollPosition();
        if (m_isFirstCommit && !state->hasScrollPositionRequest())
            m_currentScrollPosition = m_lastCommittedScrollPosition;
    }

    if (state->hasChangedProperty(ScrollingStateNode::Property::ScrollOrigin))
        m_scrollOrigin = state->scrollOrigin();

    if (state->hasChangedProperty(ScrollingStateNode::Property::SnapOffsetsInfo))
        m_snapOffsetsInfo = state->snapOffsetsInfo();

    if (state->hasChangedProperty(ScrollingStateNode::Property::CurrentHorizontalSnapOffsetIndex))
        m_currentHorizontalSnapPointIndex = state->currentHorizontalSnapPointIndex();

    if (state->hasChangedProperty(ScrollingStateNode::Property::CurrentVerticalSnapOffsetIndex))
        m_currentVerticalSnapPointIndex = state->currentVerticalSnapPointIndex();

    if (state->hasChangedProperty(ScrollingStateNode::Property::ScrollableAreaParams))
        m_scrollableAreaParameters = state->scrollableAreaParameters();

#if ENABLE(SCROLLING_THREAD)
    if (state->hasChangedProperty(ScrollingStateNode::Property::ReasonsForSynchronousScrolling))
        m_synchronousScrollingReasons = state->synchronousScrollingReasons();
#endif

    if (state->hasChangedProperty(ScrollingStateNode::Property::ScrollContainerLayer))
        m_scrollContainerLayer = state->scrollContainerLayer();

    if (state->hasChangedProperty(ScrollingStateNode::Property::ScrolledContentsLayer))
        m_scrolledContentsLayer = state->scrolledContentsLayer();

    return true;
}

bool ScrollingTreeScrollingNode::commitStateAfterChildren(const ScrollingStateNode& stateNode)
{
    auto* scrollingStateNode = dynamicDowncast<ScrollingStateScrollingNode>(stateNode);
    if (!scrollingStateNode)
        return false;

    if (scrollingStateNode->hasChangedProperty(ScrollingStateNode::Property::RequestedScrollPosition))
        handleScrollPositionRequest(scrollingStateNode->requestedScrollData());

    if (scrollingStateNode->hasChangedProperty(ScrollingStateNode::Property::KeyboardScrollData))
        requestKeyboardScroll(scrollingStateNode->keyboardScrollData());

    // This synthetic bit is added back in ScrollingTree::propagateSynchronousScrollingReasons().
#if ENABLE(SCROLLING_THREAD)
    m_synchronousScrollingReasons.remove(SynchronousScrollingReason::DescendantScrollersHaveSynchronousScrolling);
#endif
    m_isFirstCommit = false;
    return true;
}

void ScrollingTreeScrollingNode::didCompleteCommitForNode()
{
    m_scrolledSinceLastCommit = false;
}

bool ScrollingTreeScrollingNode::isLatchedNode() const
{
    return scrollingTree()->latchedNodeID() == scrollingNodeID();
}

bool ScrollingTreeScrollingNode::shouldRubberBandOnSide(BoxSide side, RectEdges<bool> pinnedEdges) const
{
    if (!pinnedEdges[side])
        return false;

    auto mainFrameRubberBandingBehavior = scrollingTree()->clientAllowsMainFrameRubberBandingOnSide(side);

    if (isRootNode() && mainFrameRubberBandingBehavior == RubberBandingBehavior::Never)
        return false;

    switch (side) {
    case BoxSide::Top:
    case BoxSide::Bottom:
        if (!overscrollBehaviorAllowsVerticalRubberBand())
            return false;

        // The root allows rubberbanding if it doesn't have enough content, but only if a scrollbar is allowed.
        if (isRootNode() && canHaveVerticalScrollbar()) {
            if (!allowsVerticalScrolling() && mainFrameRubberBandingBehavior == RubberBandingBehavior::BasedOnSize)
                return false;

            return true;
        }

        if (!allowsVerticalScrolling())
            return false;

        return verticalOverscrollBehaviorPreventsPropagation();

    case BoxSide::Left:
    case BoxSide::Right:
        if (!overscrollBehaviorAllowsHorizontalRubberBand())
            return false;

        // The root allows rubberbanding if it doesn't have enough content, but only if a scrollbar is allowed.
        if (isRootNode() && canHaveHorizontalScrollbar()) {
            if (!allowsHorizontalScrolling() && mainFrameRubberBandingBehavior == RubberBandingBehavior::BasedOnSize)
                return false;

            return true;
        }

        if (!allowsHorizontalScrolling())
            return false;

        return horizontalOverscrollBehaviorPreventsPropagation();
    }
    return true;
}

bool ScrollingTreeScrollingNode::shouldRubberBand(const PlatformWheelEvent& wheelEvent, EventTargeting eventTargeting) const
{
    if (isLatchedNode())
        return true;

    if (eventTargeting == EventTargeting::NodeOnly)
        return true;

    if (wheelEvent.isNonGestureEvent())
        return false;

    auto pinnedEdges = edgePinnedState();

    auto horizontalSide = ScrollableArea::targetSideForScrollDelta(-wheelEvent.delta(), ScrollEventAxis::Horizontal);
    if (horizontalSide && shouldRubberBandOnSide(*horizontalSide, pinnedEdges))
        return true;

    auto verticalSide = ScrollableArea::targetSideForScrollDelta(-wheelEvent.delta(), ScrollEventAxis::Vertical);
    if (verticalSide && shouldRubberBandOnSide(*verticalSide, pinnedEdges))
        return true;

    return false;
}

bool ScrollingTreeScrollingNode::canHandleWheelEvent(const PlatformWheelEvent& wheelEvent, EventTargeting eventTargeting) const
{
    if (!canHaveScrollbars())
        return false;

    // MayBegin and End are used to update scrollbars; if this node is scrollable, it can handle it.
    if (wheelEvent.phase() == PlatformWheelEventPhase::MayBegin || wheelEvent.phase() == PlatformWheelEventPhase::Ended || wheelEvent.momentumPhase() == PlatformWheelEventPhase::Ended)
        return true;

    if (shouldRubberBand(wheelEvent, eventTargeting))
        return true;

    return eventCanScrollContents(wheelEvent);
}

WheelEventHandlingResult ScrollingTreeScrollingNode::handleWheelEvent(const PlatformWheelEvent&, EventTargeting)
{
    return WheelEventHandlingResult::unhandled();
}

FloatPoint ScrollingTreeScrollingNode::clampScrollPosition(const FloatPoint& scrollPosition) const
{
    return scrollPosition.constrainedBetween(minimumScrollPosition(), maximumScrollPosition());
}

FloatPoint ScrollingTreeScrollingNode::minimumScrollPosition() const
{
    auto minimumScrollOffset = FloatPoint { };
    return ScrollableArea::scrollPositionFromOffset(minimumScrollOffset, toFloatSize(scrollOrigin()));
}

FloatPoint ScrollingTreeScrollingNode::maximumScrollPosition() const
{
    FloatPoint contentSizePoint(totalContentsSize());
    auto maximumScrollOffset = FloatPoint(contentSizePoint - scrollableAreaSize()).expandedTo(FloatPoint());
    return ScrollableArea::scrollPositionFromOffset(maximumScrollOffset, toFloatSize(scrollOrigin()));
}

bool ScrollingTreeScrollingNode::eventCanScrollContents(const PlatformWheelEvent& wheelEvent) const
{
#if PLATFORM(WPE) || PLATFORM(GTK)
    // In case of GTK and WPE the end of momentum scroll events always have a delta of 0.
    if (wheelEvent.isEndOfNonMomentumScroll())
        return true;
#endif

    if (wheelEvent.delta().isZero())
        return false;

    auto wheelDelta = wheelEvent.delta();

    if (!m_scrollableAreaParameters.allowsHorizontalScrolling)
        wheelDelta.setWidth(0);

    if (!m_scrollableAreaParameters.allowsVerticalScrolling)
        wheelDelta.setHeight(0);

    auto oldScrollPosition = currentScrollPosition();
    auto newScrollPosition = (oldScrollPosition - wheelDelta).constrainedBetween(minimumScrollPosition(), maximumScrollPosition());
    return newScrollPosition != oldScrollPosition;
}

RectEdges<bool> ScrollingTreeScrollingNode::edgePinnedState() const
{
    auto scrollPosition = currentScrollPosition();
    auto minScrollPosition = minimumScrollPosition();
    auto maxScrollPosition = maximumScrollPosition();

    bool horizontallyUnscrollable = !allowsHorizontalScrolling();
    bool verticallyUnscrollable = !allowsVerticalScrolling();

    // Top, right, bottom, left.
    return {
        verticallyUnscrollable || scrollPosition.y() <= minScrollPosition.y(),
        horizontallyUnscrollable || scrollPosition.x() >= maxScrollPosition.x(),
        verticallyUnscrollable || scrollPosition.y() >= maxScrollPosition.y(),
        horizontallyUnscrollable || scrollPosition.x() <= minScrollPosition.x()
    };
}

bool ScrollingTreeScrollingNode::isUserScrollInProgress() const
{
    return scrollingTree()->isUserScrollInProgressForNode(scrollingNodeID());
}

void ScrollingTreeScrollingNode::setUserScrollInProgress(bool isUserScrolling)
{
    scrollingTree()->setUserScrollInProgressForNode(scrollingNodeID(), isUserScrolling);
}

bool ScrollingTreeScrollingNode::isScrollSnapInProgress() const
{
    return scrollingTree()->isScrollSnapInProgressForNode(scrollingNodeID());
}

void ScrollingTreeScrollingNode::setScrollSnapInProgress(bool isSnapping)
{
    scrollingTree()->setNodeScrollSnapInProgress(scrollingNodeID(), isSnapping);
}

void ScrollingTreeScrollingNode::willStartAnimatedScroll()
{
    scrollingTree()->scrollingTreeNodeWillStartAnimatedScroll(*this);
}

void ScrollingTreeScrollingNode::didStopAnimatedScroll()
{
    if (!isScrollSnapInProgress()) {
        LOG_WITH_STREAM(Scrolling, stream << "ScrollingTreeScrollingNode " << scrollingNodeID() << " didStopAnimatedScroll");
        scrollingTree()->scrollingTreeNodeDidStopAnimatedScroll(*this);
    }
}

void ScrollingTreeScrollingNode::willStartWheelEventScroll()
{
    scrollingTree()->scrollingTreeNodeWillStartWheelEventScroll(*this);
}

void ScrollingTreeScrollingNode::didStopWheelEventScroll()
{
    if (!isScrollSnapInProgress())
        scrollingTree()->scrollingTreeNodeDidStopWheelEventScroll(*this);
}

bool ScrollingTreeScrollingNode::startAnimatedScrollToPosition(FloatPoint destinationPosition)
{
    return m_delegate ? m_delegate->startAnimatedScrollToPosition(destinationPosition) : false;
}

void ScrollingTreeScrollingNode::stopAnimatedScroll()
{
    if (m_delegate)
        m_delegate->stopAnimatedScroll();
}

void ScrollingTreeScrollingNode::didStopProgrammaticScroll()
{
    if (!isScrollSnapInProgress())
        scrollingTree()->scrollingTreeNodeDidStopProgrammaticScroll(*this);
}

void ScrollingTreeScrollingNode::serviceScrollAnimation(MonotonicTime currentTime)
{
    if (m_delegate)
        m_delegate->serviceScrollAnimation(currentTime);
}

void ScrollingTreeScrollingNode::setScrollAnimationInProgress(bool animationInProgress)
{
    scrollingTree()->setScrollAnimationInProgressForNode(scrollingNodeID(), animationInProgress);
}

void ScrollingTreeScrollingNode::handleKeyboardScrollRequest(const RequestedKeyboardScrollData& scrollData)
{
    if (m_delegate)
        m_delegate->handleKeyboardScrollRequest(scrollData);
}

void ScrollingTreeScrollingNode::requestKeyboardScroll(const RequestedKeyboardScrollData& scrollData)
{
    scrollingTree()->scrollingTreeNodeRequestsKeyboardScroll(scrollingNodeID(), scrollData);
}

void ScrollingTreeScrollingNode::handleScrollPositionRequest(const RequestedScrollData& requestedScrollData)
{
    stopAnimatedScroll();

    if (requestedScrollData.requestType == ScrollRequestType::CancelAnimatedScroll) {
        ASSERT(!requestedScrollData.requestedDataBeforeAnimatedScroll);
        LOG_WITH_STREAM(Scrolling, stream << "ScrollingTreeScrollingNode " << scrollingNodeID() << " handleScrollPositionRequest() - cancel animated scroll");
        scrollingTree()->removePendingScrollAnimationForNode(scrollingNodeID());
        return;
    }

    if (scrollingTree()->scrollingTreeNodeRequestsScroll(scrollingNodeID(), requestedScrollData))
        return;

    LOG_WITH_STREAM(Scrolling, stream << "ScrollingTreeScrollingNode " << scrollingNodeID() << " handleScrollPositionRequest() with data " << requestedScrollData);

    if (requestedScrollData.requestedDataBeforeAnimatedScroll) {
        auto& [requestType, positionOrDeltaBeforeAnimatedScroll, scrollType, clamping] = *requestedScrollData.requestedDataBeforeAnimatedScroll;

        switch (requestType) {
        case ScrollRequestType::PositionUpdate:
        case ScrollRequestType::DeltaUpdate: {
            auto intermediatePosition = RequestedScrollData::computeDestinationPosition(currentScrollPosition(), requestType, positionOrDeltaBeforeAnimatedScroll);
            scrollTo(intermediatePosition, scrollType, clamping);
            break;
        }
        case ScrollRequestType::CancelAnimatedScroll:
            stopAnimatedScroll();
            break;
        }
    }

    auto destinationPosition = requestedScrollData.destinationPosition(currentScrollPosition());
    if (requestedScrollData.animated == ScrollIsAnimated::Yes) {
        startAnimatedScrollToPosition(destinationPosition);
        return;
    }

    scrollTo(destinationPosition, requestedScrollData.scrollType, requestedScrollData.clamping);
    didStopProgrammaticScroll();
}

FloatPoint ScrollingTreeScrollingNode::adjustedScrollPosition(const FloatPoint& scrollPosition, ScrollClamping clamping) const
{
    auto adjustedPosition = m_delegate ? m_delegate->adjustedScrollPosition(scrollPosition) : scrollPosition;
    if (clamping == ScrollClamping::Clamped)
        return clampScrollPosition(adjustedPosition);

    return adjustedPosition;
}

void ScrollingTreeScrollingNode::scrollBy(const FloatSize& delta, ScrollClamping clamp)
{
    scrollTo(currentScrollPosition() + delta, ScrollType::User, clamp);
}

void ScrollingTreeScrollingNode::scrollTo(const FloatPoint& position, ScrollType scrollType, ScrollClamping clamp)
{
    if (position == m_currentScrollPosition)
        return;

    scrollingTree()->setIsHandlingProgrammaticScroll(scrollType == ScrollType::Programmatic);

    if (scrollType == ScrollType::Programmatic)
        willDoProgrammaticScroll(position);

    m_currentScrollPosition = adjustedScrollPosition(position, clamp);
    
    LOG_WITH_STREAM(Scrolling, stream << "ScrollingTreeScrollingNode " << scrollingNodeID() << " scrollTo " << position << " adjusted to "
        << m_currentScrollPosition << " (" << scrollType << ", " << clamp << ") (delta from last committed position " << (m_lastCommittedScrollPosition - m_currentScrollPosition) << ")"
        << " rubberbanding " << scrollingTree()->isRubberBandInProgressForNode(scrollingNodeID()));

    updateViewportForCurrentScrollPosition();
    currentScrollPositionChanged(scrollType);

    scrollingTree()->setIsHandlingProgrammaticScroll(false);
}

void ScrollingTreeScrollingNode::currentScrollPositionChanged(ScrollType, ScrollingLayerPositionAction action)
{
    m_scrolledSinceLastCommit = true;
    scrollingTree()->scrollingTreeNodeDidScroll(*this, action);
}

bool ScrollingTreeScrollingNode::scrollPositionAndLayoutViewportMatch(const FloatPoint& position, std::optional<FloatRect>)
{
    return position == m_currentScrollPosition;
}

void ScrollingTreeScrollingNode::applyLayerPositions()
{
    repositionScrollingLayers();
    repositionRelatedLayers();
}

void ScrollingTreeScrollingNode::wasScrolledByDelegatedScrolling(const FloatPoint& position, std::optional<FloatRect> overrideLayoutViewport, ScrollingLayerPositionAction scrollingLayerPositionAction)
{
    bool scrollPositionChanged = !scrollPositionAndLayoutViewportMatch(position, overrideLayoutViewport);
    if (!scrollPositionChanged && scrollingLayerPositionAction != ScrollingLayerPositionAction::Set)
        return;

    m_currentScrollPosition = adjustedScrollPosition(position, ScrollClamping::Unclamped);
    updateViewportForCurrentScrollPosition(overrideLayoutViewport);

    repositionRelatedLayers();

    scrollingTree()->notifyRelatedNodesAfterScrollPositionChange(*this);
    scrollingTree()->scrollingTreeNodeDidScroll(*this, scrollingLayerPositionAction);
    scrollingTree()->setNeedsApplyLayerPositionsAfterCommit();
}

void ScrollingTreeScrollingNode::dumpProperties(TextStream& ts, OptionSet<ScrollingStateTreeAsTextBehavior> behavior) const
{
    ScrollingTreeNode::dumpProperties(ts, behavior);
    ts.dumpProperty("scrollable area size"_s, m_scrollableAreaSize);
    ts.dumpProperty("total content size"_s, m_totalContentsSize);
    if (m_totalContentsSizeForRubberBand != m_totalContentsSize)
        ts.dumpProperty("total content size for rubber band"_s, m_totalContentsSizeForRubberBand);
    if (m_reachableContentsSize != m_totalContentsSize)
        ts.dumpProperty("reachable content size"_s, m_reachableContentsSize);
    ts.dumpProperty("last committed scroll position"_s, m_lastCommittedScrollPosition);

    if (!m_currentScrollPosition.isZero())
        ts.dumpProperty("scroll position"_s, m_currentScrollPosition);

    if (!m_scrollOrigin.isZero())
        ts.dumpProperty("scroll origin"_s, m_scrollOrigin);

    if (m_snapOffsetsInfo.horizontalSnapOffsets.size())
        ts.dumpProperty("horizontal snap offsets"_s, m_snapOffsetsInfo.horizontalSnapOffsets);

    if (m_snapOffsetsInfo.verticalSnapOffsets.size())
        ts.dumpProperty("vertical snap offsets"_s, m_snapOffsetsInfo.verticalSnapOffsets);

    if (m_currentHorizontalSnapPointIndex)
        ts.dumpProperty("current horizontal snap point index"_s, m_currentHorizontalSnapPointIndex);

    if (m_currentVerticalSnapPointIndex)
        ts.dumpProperty("current vertical snap point index"_s, m_currentVerticalSnapPointIndex);

    ts.dumpProperty("scrollable area parameters"_s, m_scrollableAreaParameters);

#if ENABLE(SCROLLING_THREAD)
    if (!m_synchronousScrollingReasons.isEmpty())
        ts.dumpProperty("synchronous scrolling reasons"_s, ScrollingCoordinator::synchronousScrollingReasonsAsText(m_synchronousScrollingReasons));
#endif
}

const FloatScrollSnapOffsetsInfo& ScrollingTreeScrollingNode::snapOffsetsInfo() const
{
    return m_snapOffsetsInfo;
}

std::optional<unsigned> ScrollingTreeScrollingNode::currentHorizontalSnapPointIndex() const
{
    return m_currentHorizontalSnapPointIndex;
}

std::optional<unsigned> ScrollingTreeScrollingNode::currentVerticalSnapPointIndex() const
{
    return m_currentVerticalSnapPointIndex;
}

void ScrollingTreeScrollingNode::setCurrentHorizontalSnapPointIndex(std::optional<unsigned> index)
{
    m_currentHorizontalSnapPointIndex = index;
}

void ScrollingTreeScrollingNode::setCurrentVerticalSnapPointIndex(std::optional<unsigned> index)
{
    m_currentVerticalSnapPointIndex = index;
}

PlatformWheelEvent ScrollingTreeScrollingNode::eventForPropagation(const PlatformWheelEvent& wheelEvent) const
{
    auto filteredDelta = wheelEvent.delta();
#if PLATFORM(MAC)
    auto biasedDelta = ScrollingEffectsController::wheelDeltaBiasingTowardsVertical(wheelEvent.delta());
#else
    auto biasedDelta = wheelEvent.delta();
#endif
    if (horizontalOverscrollBehaviorPreventsPropagation() || verticalOverscrollBehaviorPreventsPropagation()) {
        if(horizontalOverscrollBehaviorPreventsPropagation() || (verticalOverscrollBehaviorPreventsPropagation() && !biasedDelta.width()))
           filteredDelta.setWidth(0);
        if(verticalOverscrollBehaviorPreventsPropagation() || (horizontalOverscrollBehaviorPreventsPropagation() && !biasedDelta.height()))
           filteredDelta.setHeight(0);
        return wheelEvent.copyWithDeltaAndVelocity(filteredDelta, wheelEvent.scrollingVelocity());
    }
    return wheelEvent;
}

ScrollPropagationInfo ScrollingTreeScrollingNode::computeScrollPropagation(const FloatSize& delta) const
{
    ScrollPropagationInfo propagation;
    if (!horizontalOverscrollBehaviorPreventsPropagation() && !verticalOverscrollBehaviorPreventsPropagation())
        return propagation;
    
    // History swipe case
    if (horizontalOverscrollBehaviorPreventsPropagation() && !delta.height() && delta.width()) {
        propagation.shouldBlockScrollPropagation = true;
        propagation.isHandled = false;
        return propagation;
    }

    if ((horizontalOverscrollBehaviorPreventsPropagation() && verticalOverscrollBehaviorPreventsPropagation())
        || (horizontalOverscrollBehaviorPreventsPropagation() && !delta.height())
        || (verticalOverscrollBehaviorPreventsPropagation() && !delta.width())) {
        propagation.shouldBlockScrollPropagation = true;
        propagation.isHandled = true;
    }
    return propagation;
}

void ScrollingTreeScrollingNode::scrollbarVisibilityDidChange(ScrollbarOrientation orientation, bool isVisible)
{
    scrollingTree()->scrollingTreeNodeScrollbarVisibilityDidChange(scrollingNodeID(), orientation, isVisible);
}

void ScrollingTreeScrollingNode::scrollbarMinimumThumbLengthDidChange(ScrollbarOrientation orientation, int minimumThumbLength)
{
    scrollingTree()->scrollingTreeNodeScrollbarMinimumThumbLengthDidChange(scrollingNodeID(), orientation, minimumThumbLength);
}

} // namespace WebCore

#endif // ENABLE(ASYNC_SCROLLING)
