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

#pragma once

#if ENABLE(UI_SIDE_COMPOSITING)

#include "MessageReceiver.h"
#include "RemoteScrollingCoordinator.h"
#include "RemoteScrollingTree.h"
#include "RemoteScrollingUIState.h"
#include <WebCore/PlatformLayer.h>
#include <WebCore/PlatformLayerIdentifier.h>
#include <WebCore/ScrollSnapOffsetsInfo.h>
#include <WebCore/WheelEventTestMonitor.h>
#include <wtf/Noncopyable.h>
#include <wtf/RefPtr.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakPtr.h>

OBJC_CLASS UIScrollView;

namespace WebCore {
class FloatPoint;
class PlatformWheelEvent;
}

namespace WebKit {

class NativeWebWheelEvent;
class RemoteLayerTreeHost;
class RemoteLayerTreeNode;
class RemoteScrollingCoordinatorTransaction;
class RemoteScrollingTree;
class WebPageProxy;
class WebWheelEvent;

class RemoteScrollingCoordinatorProxy : public CanMakeWeakPtr<RemoteScrollingCoordinatorProxy>, public CanMakeCheckedPtr<RemoteScrollingCoordinatorProxy> {
    WTF_MAKE_TZONE_ALLOCATED(RemoteScrollingCoordinatorProxy);
    WTF_MAKE_NONCOPYABLE(RemoteScrollingCoordinatorProxy);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(RemoteScrollingCoordinatorProxy);
public:
    virtual ~RemoteScrollingCoordinatorProxy();
    
    constexpr bool isRemoteScrollingCoordinatorProxyIOS() const
    {
#if PLATFORM(IOS_FAMILY)
        return true;
#else
        return false;
#endif
    }

    constexpr bool isRemoteScrollingCoordinatorProxyMac() const
    {
#if PLATFORM(MAC)
        return true;
#else
        return false;
#endif
    }

    // Inform the web process that the scroll position changed (called from the scrolling tree)
    virtual bool scrollingTreeNodeRequestsScroll(WebCore::ScrollingNodeID, const WebCore::RequestedScrollData&);
    virtual bool scrollingTreeNodeRequestsKeyboardScroll(WebCore::ScrollingNodeID, const WebCore::RequestedKeyboardScrollData&);

    void scrollingThreadAddedPendingUpdate();

    WebCore::TrackingType eventTrackingTypeForPoint(WebCore::EventTrackingRegions::EventType, WebCore::IntPoint) const;

    // Called externally when native views move around.
    void viewportChangedViaDelegatedScrolling(const WebCore::FloatPoint& scrollPosition, const WebCore::FloatRect& layoutViewport, double scale);

    virtual void applyScrollingTreeLayerPositionsAfterCommit();

    void currentSnapPointIndicesDidChange(WebCore::ScrollingNodeID, std::optional<unsigned> horizontal, std::optional<unsigned> vertical);

    virtual void cacheWheelEventScrollingAccelerationCurve(const NativeWebWheelEvent&) { }
    virtual void handleWheelEvent(const WebWheelEvent&, WebCore::RectEdges<WebCore::RubberBandingBehavior> rubberBandableEdges);
    void continueWheelEventHandling(const WebWheelEvent&, WebCore::WheelEventHandlingResult);
    virtual void wheelEventHandlingCompleted(const WebCore::PlatformWheelEvent&, std::optional<WebCore::ScrollingNodeID>, std::optional<WebCore::WheelScrollGestureState>, bool /* wasHandled */) { }

    virtual WebCore::PlatformWheelEvent filteredWheelEvent(const WebCore::PlatformWheelEvent& wheelEvent) { return wheelEvent; }

    std::optional<WebCore::ScrollingNodeID> rootScrollingNodeID() const;

    const RemoteLayerTreeHost* layerTreeHost() const;
    WebPageProxy& webPageProxy() const;
    Ref<WebPageProxy> protectedWebPageProxy() const;

    void stickyScrollingTreeNodeBeganSticking(WebCore::ScrollingNodeID);

    std::optional<WebCore::RequestedScrollData> commitScrollingTreeState(IPC::Connection&, const RemoteScrollingCoordinatorTransaction&, std::optional<WebCore::LayerHostingContextIdentifier> = std::nullopt);

    bool hasFixedOrSticky() const;
    bool hasScrollableMainFrame() const;
    bool hasScrollableOrZoomedMainFrame() const;

    WebCore::ScrollbarWidth mainFrameScrollbarWidth() const;

    WebCore::OverscrollBehavior mainFrameHorizontalOverscrollBehavior() const;
    WebCore::OverscrollBehavior mainFrameVerticalOverscrollBehavior() const;

    virtual void scrollingTreeNodeWillStartPanGesture(WebCore::ScrollingNodeID) { }
    virtual void scrollingTreeNodeWillStartScroll(WebCore::ScrollingNodeID) { }
    virtual void scrollingTreeNodeDidEndScroll(WebCore::ScrollingNodeID) { }
    virtual void clearNodesWithUserScrollInProgress() { }
    virtual void hasNodeWithAnimatedScrollChanged(bool) { }
    virtual void setRootNodeIsInUserScroll(bool) { }
    virtual void setRubberBandingInProgressForNode(WebCore::ScrollingNodeID, bool isRubberBanding) { }

    virtual void scrollingTreeNodeDidBeginScrollSnapping(WebCore::ScrollingNodeID) { }
    virtual void scrollingTreeNodeDidEndScrollSnapping(WebCore::ScrollingNodeID) { }
    
    virtual void willCommitLayerAndScrollingTrees() { }
    virtual void didCommitLayerAndScrollingTrees() { }

#if ENABLE(THREADED_ANIMATION_RESOLUTION)
    virtual void animationsWereAddedToNode(RemoteLayerTreeNode&) { }
    virtual void animationsWereRemovedFromNode(RemoteLayerTreeNode&) { }
#endif

    String scrollingTreeAsText() const;

    void resetStateAfterProcessExited();

    virtual void displayDidRefresh(WebCore::PlatformDisplayID);
    void reportExposedUnfilledArea(MonotonicTime, unsigned unfilledArea);
    void reportSynchronousScrollingReasonsChanged(MonotonicTime, OptionSet<WebCore::SynchronousScrollingReason>);
    void reportFilledVisibleFreshTile(MonotonicTime, unsigned);
    bool scrollingPerformanceTestingEnabled() const;
    
    void receivedWheelEventWithPhases(WebCore::PlatformWheelEventPhase phase, WebCore::PlatformWheelEventPhase momentumPhase);
    void deferWheelEventTestCompletionForReason(std::optional<WebCore::ScrollingNodeID>, WebCore::WheelEventTestMonitor::DeferReason);
    void removeWheelEventTestCompletionDeferralForReason(std::optional<WebCore::ScrollingNodeID>, WebCore::WheelEventTestMonitor::DeferReason);

    virtual void windowScreenWillChange() { }
    virtual void windowScreenDidChange(WebCore::PlatformDisplayID, std::optional<WebCore::FramesPerSecond>) { }

    WebCore::FloatBoxExtent obscuredContentInsets() const;
    WebCore::FloatPoint currentMainFrameScrollPosition() const;
    WebCore::FloatRect computeVisibleContentRect();
    WebCore::IntPoint scrollOrigin() const;
    int headerHeight() const;
    int footerHeight() const;
    float mainFrameScaleFactor() const;
    WebCore::FloatSize totalContentsSize() const;
    
    void viewWillStartLiveResize();
    void viewWillEndLiveResize();
    void viewSizeDidChange();
    String scrollbarStateForScrollingNodeID(std::optional<WebCore::ScrollingNodeID>, bool isVertical);
    bool overlayScrollbarsEnabled();

    void sendScrollingTreeNodeUpdate();
    
    void scrollingTreeNodeScrollbarVisibilityDidChange(WebCore::ScrollingNodeID, WebCore::ScrollbarOrientation, bool);
    void scrollingTreeNodeScrollbarMinimumThumbLengthDidChange(WebCore::ScrollingNodeID, WebCore::ScrollbarOrientation, int);
    void receivedLastScrollingTreeNodeUpdateReply();
    bool isMonitoringWheelEvents();

protected:
    explicit RemoteScrollingCoordinatorProxy(WebPageProxy&);

    RemoteScrollingTree& scrollingTree() const { return m_scrollingTree.get(); }

    virtual void connectStateNodeLayers(WebCore::ScrollingStateTree&, const RemoteLayerTreeHost&) = 0;
    virtual void establishLayerTreeScrollingRelations(const RemoteLayerTreeHost&) = 0;

    virtual void didReceiveWheelEvent(bool /* wasHandled */) { }

    void sendUIStateChangedIfNecessary();

private:
    WeakRef<WebPageProxy> m_webPageProxy;
    const Ref<RemoteScrollingTree> m_scrollingTree;

protected:
    std::optional<WebCore::RequestedScrollData> m_requestedScroll;
    RemoteScrollingUIState m_uiState;
    std::optional<unsigned> m_currentHorizontalSnapPointIndex;
    std::optional<unsigned> m_currentVerticalSnapPointIndex;
    bool m_waitingForDidScrollReply { false };
    HashSet<WebCore::PlatformLayerIdentifier> m_layersWithScrollingRelations;
};

} // namespace WebKit

#define SPECIALIZE_TYPE_TRAITS_REMOTE_SCROLLING_COORDINATOR_PROXY(ToValueTypeName, predicate) \
SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::ToValueTypeName) \
    static bool isType(const WebKit::RemoteScrollingCoordinatorProxy& scrollingCoordinatorProxy) { return scrollingCoordinatorProxy.predicate; } \
SPECIALIZE_TYPE_TRAITS_END()

#endif // ENABLE(UI_SIDE_COMPOSITING)
