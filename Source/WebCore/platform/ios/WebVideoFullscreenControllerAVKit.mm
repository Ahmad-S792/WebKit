/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
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
#import "WebVideoFullscreenControllerAVKit.h"

#if PLATFORM(IOS_FAMILY)

#import "HTMLVideoElement.h"
#import "LocalFrameView.h"
#import "Logging.h"
#import "MediaSelectionOption.h"
#import "PlaybackSessionInterfaceAVKitLegacy.h"
#import "PlaybackSessionModelMediaElement.h"
#import "RenderObjectInlines.h"
#import "RenderVideo.h"
#import "TimeRanges.h"
#import "VideoPresentationInterfaceAVKitLegacy.h"
#import "VideoPresentationModelVideoElement.h"
#import "WebCoreThreadRun.h"
#import <QuartzCore/CoreAnimation.h>
#import <UIKit/UIView.h>
#import <pal/spi/cocoa/QuartzCoreSPI.h>
#import <wtf/CheckedRef.h>
#import <wtf/CrossThreadCopier.h>
#import <wtf/TZoneMallocInlines.h>
#import <wtf/WorkQueue.h>

#import <pal/ios/UIKitSoftLink.h>

using namespace WebCore;

#if !(ENABLE(VIDEO_PRESENTATION_MODE) && HAVE(AVKIT))

@implementation WebVideoFullscreenController
- (void)setVideoElement:(NakedPtr<WebCore::HTMLVideoElement>)videoElement
{
    UNUSED_PARAM(videoElement);
}

- (NakedPtr<WebCore::HTMLVideoElement>)videoElement
{
    return nullptr;
}

- (void)enterFullscreen:(UIView *)view mode:(WebCore::HTMLMediaElementEnums::VideoFullscreenMode)mode
{
    UNUSED_PARAM(view);
    UNUSED_PARAM(mode);
}

- (void)requestHideAndExitFullscreen
{
}

- (void)exitFullscreen
{
}
@end

#else

static IntRect elementRectInWindow(HTMLVideoElement* videoElement)
{
    if (!videoElement)
        return { };
    auto* renderer = videoElement->renderer();
    auto* view = videoElement->document().view();
    if (!renderer || !view)
        return { };
    return view->convertToContainingWindow(renderer->absoluteBoundingBoxRect());
}

class VideoFullscreenControllerContext;

@interface WebVideoFullscreenController (delegate)
-(void)didFinishFullscreen:(VideoFullscreenControllerContext*)context;
@end

class VideoFullscreenControllerContext final
    : public VideoPresentationModel
    , private VideoPresentationModelClient
    , private PlaybackSessionModel
    , private PlaybackSessionModelClient
    , public CanMakeCheckedPtr<VideoFullscreenControllerContext> {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(VideoFullscreenControllerContext);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(VideoFullscreenControllerContext);
public:
    static Ref<VideoFullscreenControllerContext> create()
    {
        return adoptRef(*new VideoFullscreenControllerContext);
    }

    ~VideoFullscreenControllerContext();

    void setController(WebVideoFullscreenController* controller) { m_controller = controller; }
    void setUpFullscreen(HTMLVideoElement&, UIView *, HTMLMediaElementEnums::VideoFullscreenMode);
    void exitFullscreen() final;
    void requestHideAndExitFullscreen();
    void invalidate();

private:
    VideoFullscreenControllerContext() { }

    // CheckedPtr interface
    uint32_t checkedPtrCount() const final { return CanMakeCheckedPtr::checkedPtrCount(); }
    uint32_t checkedPtrCountWithoutThreadCheck() const final { return CanMakeCheckedPtr::checkedPtrCountWithoutThreadCheck(); }
    void incrementCheckedPtrCount() const final { CanMakeCheckedPtr::incrementCheckedPtrCount(); }
    void decrementCheckedPtrCount() const final { CanMakeCheckedPtr::decrementCheckedPtrCount(); }

    // VideoPresentationModelClient
    void hasVideoChanged(bool) override;
    void videoDimensionsChanged(const FloatSize&) override;

    // PlaybackSessionModel
    void addClient(PlaybackSessionModelClient&) override;
    void removeClient(PlaybackSessionModelClient&) override;
    void play() override;
    void pause() override;
    void togglePlayState() override;
    void beginScrubbing() override;
    void endScrubbing() override;
    void seekToTime(double, double, double) override;
    void fastSeek(double time) override;
    void beginScanningForward() override;
    void beginScanningBackward() override;
    void endScanning() override;
    void setDefaultPlaybackRate(double) override;
    void setPlaybackRate(double) override;
    void selectAudioMediaOption(uint64_t) override;
    void selectLegibleMediaOption(uint64_t) override;
    double duration() const override;
    double playbackStartedTime() const override { return 0; }
    double currentTime() const override;
    double bufferedTime() const override;
    OptionSet<PlaybackState> playbackState() const override;
    bool isPlaying() const override;
    bool isStalled() const override;
    bool isScrubbing() const override { return false; }
    double defaultPlaybackRate() const override;
    double playbackRate() const override;
    PlatformTimeRanges seekableRanges() const override;
    double seekableTimeRangesLastModifiedTime() const override;
    double liveUpdateInterval() const override;
    bool canPlayFastReverse() const override;
    Vector<MediaSelectionOption> audioMediaSelectionOptions() const override;
    uint64_t audioMediaSelectedIndex() const override;
    Vector<MediaSelectionOption> legibleMediaSelectionOptions() const override;
    uint64_t legibleMediaSelectedIndex() const override;
    bool externalPlaybackEnabled() const override;
    ExternalPlaybackTargetType externalPlaybackTargetType() const override;
    String externalPlaybackLocalizedDeviceName() const override;
    bool wirelessVideoPlaybackDisabled() const override;
    void togglePictureInPicture() override { }
    void enterInWindowFullscreen() override { }
    void exitInWindowFullscreen() override { }
    void enterFullscreen() override { }
    void setPlayerIdentifierForVideoElement() final { }
    void toggleMuted() override;
    void setMuted(bool) final;
    void setVolume(double) final;
    void setPlayingOnSecondScreen(bool) final;
    void setVideoReceiverEndpoint(const VideoReceiverEndpoint&) final { }
    AudioSessionSoundStageSize soundStageSize() const final { return AudioSessionSoundStageSize::Automatic; }
    void setSoundStageSize(AudioSessionSoundStageSize) final { }

    // PlaybackSessionModelClient
    void durationChanged(double) override;
    void currentTimeChanged(double currentTime, double anchorTime) override;
    void bufferedTimeChanged(double) override;
    void rateChanged(OptionSet<PlaybackSessionModel::PlaybackState>, double playbackRate, double defaultPlaybackRate) override;
    void seekableRangesChanged(const PlatformTimeRanges&, double lastModifiedTime, double liveUpdateInterval) override;
    void canPlayFastReverseChanged(bool) override;
    void audioMediaSelectionOptionsChanged(const Vector<MediaSelectionOption>& options, uint64_t selectedIndex) override;
    void legibleMediaSelectionOptionsChanged(const Vector<MediaSelectionOption>& options, uint64_t selectedIndex) override;
    void externalPlaybackChanged(bool enabled, PlaybackSessionModel::ExternalPlaybackTargetType, const String& localizedDeviceName) override;
    void wirelessVideoPlaybackDisabledChanged(bool) override;
    void mutedChanged(bool) override;
    void volumeChanged(double) override;

    // VideoPresentationModel
    void addClient(VideoPresentationModelClient&) override;
    void removeClient(VideoPresentationModelClient&) override;
    void requestFullscreenMode(HTMLMediaElementEnums::VideoFullscreenMode, bool finishedWithMedia = false) override;
    void setVideoLayerFrame(FloatRect) override;
    void setVideoLayerGravity(MediaPlayerEnums::VideoGravity) override;
    void setVideoFullscreenFrame(FloatRect) override { }
    void fullscreenModeChanged(HTMLMediaElementEnums::VideoFullscreenMode) override;
    bool hasVideo() const override;
    bool isChildOfElementFullscreen() const override;
    FloatSize videoDimensions() const override;
    bool isMuted() const override;
    double volume() const override;
    bool isPictureInPictureSupported() const override;
    bool isPictureInPictureActive() const override;
    void willEnterPictureInPicture() final;
    void didEnterPictureInPicture() final;
    void failedToEnterPictureInPicture() final;
    void willExitPictureInPicture() final;
    void didExitPictureInPicture() final;

    void requestUpdateInlineRect() final;
    void requestVideoContentLayer() final;
    void returnVideoContentLayer() final;
    void returnVideoView() final { }
    void didSetupFullscreen() final;
    void willExitFullscreen() final;
    void didExitFullscreen() final;
    void didCleanupFullscreen() final;
    void fullscreenMayReturnToInline() final;

    HashSet<CheckedPtr<PlaybackSessionModelClient>> m_playbackClients;
    HashSet<CheckedPtr<VideoPresentationModelClient>> m_presentationClients;
    RefPtr<VideoPresentationInterfaceIOS> m_interface;
    RefPtr<VideoPresentationModelVideoElement> m_presentationModel;
    RefPtr<PlaybackSessionModelMediaElement> m_playbackModel;
    RefPtr<HTMLVideoElement> m_videoElement;
    RetainPtr<UIView> m_videoFullscreenView;
    RetainPtr<WebVideoFullscreenController> m_controller;
};

VideoFullscreenControllerContext::~VideoFullscreenControllerContext()
{
    auto notifyClientsModelWasDestroyed = [this] {
        while (!m_playbackClients.isEmpty())
            (*m_playbackClients.begin())->modelDestroyed();
    };
    if (isUIThread()) {
        WebThreadLock();
        notifyClientsModelWasDestroyed();
        m_playbackModel = nullptr;
        m_presentationModel = nullptr;
    } else
        WorkQueue::mainSingleton().dispatchSync(WTFMove(notifyClientsModelWasDestroyed));
}

#pragma mark VideoPresentationModel

void VideoFullscreenControllerContext::requestUpdateInlineRect()
{
#if PLATFORM(IOS_FAMILY)
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this] () mutable {
        IntRect clientRect = elementRectInWindow(m_videoElement.get());
        RunLoop::mainSingleton().dispatch([protectedThis = WTFMove(protectedThis), this, clientRect] {
            m_interface->setInlineRect(clientRect, clientRect != IntRect(0, 0, 0, 0));
        });
    });
#else
    ASSERT_NOT_REACHED();
#endif
}

void VideoFullscreenControllerContext::requestVideoContentLayer()
{
#if PLATFORM(IOS_FAMILY)
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this, videoFullscreenLayer = retainPtr([m_videoFullscreenView layer])] () mutable {
        [videoFullscreenLayer setBackgroundColor:cachedCGColor(WebCore::Color::transparentBlack).get()];
        m_presentationModel->setVideoFullscreenLayer(videoFullscreenLayer.get(), [protectedThis = WTFMove(protectedThis), this] () mutable {
            RunLoop::mainSingleton().dispatch([protectedThis = WTFMove(protectedThis), this] {
                if (!m_interface)
                    return;

                m_interface->setHasVideoContentLayer(true);
            });
        });
    });
#else
    ASSERT_NOT_REACHED();
#endif
}

void VideoFullscreenControllerContext::returnVideoContentLayer()
{
#if PLATFORM(IOS_FAMILY)
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this, videoFullscreenLayer = retainPtr([m_videoFullscreenView layer])] () mutable {
        [videoFullscreenLayer setBackgroundColor:cachedCGColor(WebCore::Color::transparentBlack).get()];
        m_presentationModel->setVideoFullscreenLayer(nil, [protectedThis = WTFMove(protectedThis), this] () mutable {
            RunLoop::mainSingleton().dispatch([protectedThis = WTFMove(protectedThis), this] {
                if (!m_interface)
                    return;

                m_interface->setHasVideoContentLayer(false);
            });
        });
    });
#else
    ASSERT_NOT_REACHED();
#endif
}

void VideoFullscreenControllerContext::didSetupFullscreen()
{
    ASSERT(isUIThread());
#if PLATFORM(IOS_FAMILY)
    RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, this] {
        m_interface->enterFullscreen();
    });
#else
    WebThreadRun([protectedThis = Ref { *this }, this, videoFullscreenLayer = retainPtr([m_videoFullscreenView layer])] () mutable {
        [videoFullscreenLayer setBackgroundColor:cachedCGColor(WebCore::Color::transparentBlack)];
        m_presentationModel->setVideoFullscreenLayer(videoFullscreenLayer.get(), [protectedThis = WTFMove(protectedThis), this] () mutable {
            RunLoop::mainSingleton().dispatch([protectedThis = WTFMove(protectedThis), this] {
                m_interface->enterFullscreen();
            });
        });
    });
#endif
}

void VideoFullscreenControllerContext::willExitFullscreen()
{
#if PLATFORM(WATCHOS)
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this] () mutable {
        m_presentationModel->willExitFullscreen();
    });
#endif
}

void VideoFullscreenControllerContext::didExitFullscreen()
{
    ASSERT(isUIThread());
#if PLATFORM(IOS_FAMILY)
    RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, this] {
        m_interface->cleanupFullscreen();
    });
#else
    WebThreadRun([protectedThis = Ref { *this }, this] () mutable {
        m_presentationModel->setVideoFullscreenLayer(nil, [protectedThis = WTFMove(protectedThis), this] () mutable {
            RunLoop::mainSingleton().dispatch([protectedThis = WTFMove(protectedThis), this] {
                m_interface->cleanupFullscreen();
            });
        });
    });
#endif
}

void VideoFullscreenControllerContext::didCleanupFullscreen()
{
    ASSERT(isUIThread());
    m_interface->setVideoPresentationModel(nullptr);
    m_interface = nullptr;
    m_videoFullscreenView = nil;

    WebThreadRun([protectedThis = Ref { *this }, this] {
        m_presentationModel->setVideoFullscreenLayer(nil);
        m_presentationModel->setVideoElement(nullptr);
        m_playbackModel->setMediaElement(nullptr);
        m_playbackModel->removeClient(*this);
        m_presentationModel->removeClient(*this);
        m_presentationModel = nullptr;
        m_videoElement = nullptr;

        [m_controller didFinishFullscreen:this];
    });
}

void VideoFullscreenControllerContext::fullscreenMayReturnToInline()
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this] () mutable {
        IntRect clientRect = elementRectInWindow(m_videoElement.get());
        RunLoop::mainSingleton().dispatch([protectedThis = WTFMove(protectedThis), this, clientRect] {
            m_interface->preparedToReturnToInline(true, clientRect);
        });
    });
}

#pragma mark PlaybackSessionModelClient

void VideoFullscreenControllerContext::durationChanged(double duration)
{
    if (WebThreadIsCurrent()) {
        RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, duration] {
            protectedThis->durationChanged(duration);
        });
        return;
    }

    for (auto& client : m_playbackClients)
        client->durationChanged(duration);
}

void VideoFullscreenControllerContext::currentTimeChanged(double currentTime, double anchorTime)
{
    if (WebThreadIsCurrent()) {
        RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, currentTime, anchorTime] {
            protectedThis->currentTimeChanged(currentTime, anchorTime);
        });
        return;
    }

    for (auto& client : m_playbackClients)
        client->currentTimeChanged(currentTime, anchorTime);
}

void VideoFullscreenControllerContext::bufferedTimeChanged(double bufferedTime)
{
    if (WebThreadIsCurrent()) {
        RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, bufferedTime] {
            protectedThis->bufferedTimeChanged(bufferedTime);
        });
        return;
    }

    for (auto& client : m_playbackClients)
        client->bufferedTimeChanged(bufferedTime);
}

void VideoFullscreenControllerContext::rateChanged(OptionSet<PlaybackSessionModel::PlaybackState> playbackState, double playbackRate, double defaultPlaybackRate)
{
    if (WebThreadIsCurrent()) {
        RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, playbackState, playbackRate, defaultPlaybackRate] {
            protectedThis->rateChanged(playbackState, playbackRate, defaultPlaybackRate);
        });
        return;
    }

    for (auto& client : m_playbackClients)
        client->rateChanged(playbackState, playbackRate, defaultPlaybackRate);
}

void VideoFullscreenControllerContext::hasVideoChanged(bool hasVideo)
{
    if (WebThreadIsCurrent()) {
        RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, hasVideo] {
            protectedThis->hasVideoChanged(hasVideo);
        });
        return;
    }

    for (auto& client : m_presentationClients)
        client->hasVideoChanged(hasVideo);
}

void VideoFullscreenControllerContext::videoDimensionsChanged(const FloatSize& videoDimensions)
{
    if (WebThreadIsCurrent()) {
        RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, videoDimensions = videoDimensions] {
            protectedThis->videoDimensionsChanged(videoDimensions);
        });
        return;
    }

    for (auto& client : m_presentationClients)
        client->videoDimensionsChanged(videoDimensions);
}

void VideoFullscreenControllerContext::seekableRangesChanged(const PlatformTimeRanges& timeRanges, double lastModifiedTime, double liveUpdateInterval)
{
    if (WebThreadIsCurrent()) {
        RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, timeRanges, lastModifiedTime, liveUpdateInterval] {
            protectedThis->seekableRangesChanged(timeRanges, lastModifiedTime, liveUpdateInterval);
        });
        return;
    }

    for (auto &client : m_playbackClients)
        client->seekableRangesChanged(timeRanges, lastModifiedTime, liveUpdateInterval);
}

void VideoFullscreenControllerContext::canPlayFastReverseChanged(bool canPlayFastReverse)
{
    if (WebThreadIsCurrent()) {
        RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, canPlayFastReverse] {
            protectedThis->canPlayFastReverseChanged(canPlayFastReverse);
        });
        return;
    }

    for (auto &client : m_playbackClients)
        client->canPlayFastReverseChanged(canPlayFastReverse);
}

void VideoFullscreenControllerContext::audioMediaSelectionOptionsChanged(const Vector<MediaSelectionOption>& options, uint64_t selectedIndex)
{
    if (WebThreadIsCurrent()) {
        RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, options = crossThreadCopy(options), selectedIndex] {
            protectedThis->audioMediaSelectionOptionsChanged(options, selectedIndex);
        });
        return;
    }

    for (auto& client : m_playbackClients)
        client->audioMediaSelectionOptionsChanged(options, selectedIndex);
}

void VideoFullscreenControllerContext::legibleMediaSelectionOptionsChanged(const Vector<MediaSelectionOption>& options, uint64_t selectedIndex)
{
    if (WebThreadIsCurrent()) {
        RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, options = crossThreadCopy(options), selectedIndex] {
            protectedThis->legibleMediaSelectionOptionsChanged(options, selectedIndex);
        });
        return;
    }

    for (auto& client : m_playbackClients)
        client->legibleMediaSelectionOptionsChanged(options, selectedIndex);
}

void VideoFullscreenControllerContext::externalPlaybackChanged(bool enabled, PlaybackSessionModel::ExternalPlaybackTargetType type, const String& localizedDeviceName)
{
    if (WebThreadIsCurrent()) {
        callOnMainThread([protectedThis = Ref { *this }, this, enabled, type, localizedDeviceName = localizedDeviceName.isolatedCopy()] {
            for (auto& client : m_playbackClients)
                client->externalPlaybackChanged(enabled, type, localizedDeviceName);
        });
        return;
    }

    for (auto& client : m_playbackClients)
        client->externalPlaybackChanged(enabled, type, localizedDeviceName);
}

void VideoFullscreenControllerContext::wirelessVideoPlaybackDisabledChanged(bool disabled)
{
    if (WebThreadIsCurrent()) {
        RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, disabled] {
            protectedThis->wirelessVideoPlaybackDisabledChanged(disabled);
        });
        return;
    }

    for (auto& client : m_playbackClients)
        client->wirelessVideoPlaybackDisabledChanged(disabled);
}

void VideoFullscreenControllerContext::mutedChanged(bool muted)
{
    if (WebThreadIsCurrent()) {
        RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, muted] {
            protectedThis->mutedChanged(muted);
        });
        return;
    }

    for (auto& client : m_playbackClients)
        client->mutedChanged(muted);
}

void VideoFullscreenControllerContext::volumeChanged(double volume)
{
    if (WebThreadIsCurrent()) {
        RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, volume] {
            protectedThis->volumeChanged(volume);
        });
        return;
    }

    for (auto& client : m_playbackClients)
        client->volumeChanged(volume);
}
#pragma mark VideoPresentationModel

void VideoFullscreenControllerContext::addClient(VideoPresentationModelClient& client)
{
    ASSERT(isUIThread());
    ASSERT(!m_presentationClients.contains(&client));
    m_presentationClients.add(&client);
}

void VideoFullscreenControllerContext::removeClient(VideoPresentationModelClient& client)
{
    ASSERT(isUIThread());
    ASSERT(m_presentationClients.contains(&client));
    m_presentationClients.remove(&client);
}

void VideoFullscreenControllerContext::requestFullscreenMode(HTMLMediaElementEnums::VideoFullscreenMode mode, bool finishedWithMedia)
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this, mode, finishedWithMedia] {
        if (m_presentationModel)
            m_presentationModel->requestFullscreenMode(mode, finishedWithMedia);
    });
}

void VideoFullscreenControllerContext::setVideoLayerFrame(FloatRect frame)
{
    ASSERT(isUIThread());
    RetainPtr<CALayer> videoFullscreenLayer = [m_videoFullscreenView layer];
    [videoFullscreenLayer setSublayerTransform:[videoFullscreenLayer transform]];

    dispatchAsyncOnMainThreadWithWebThreadLockIfNeeded([protectedThis = Ref { *this }, this, frame, videoFullscreenLayer = WTFMove(videoFullscreenLayer)] {
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        [CATransaction setAnimationDuration:0];

        [videoFullscreenLayer setSublayerTransform:CATransform3DIdentity];

        if (m_presentationModel)
            m_presentationModel->setVideoLayerFrame(frame);
        [CATransaction commit];
    });
}

void VideoFullscreenControllerContext::setVideoLayerGravity(MediaPlayerEnums::VideoGravity videoGravity)
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this, videoGravity] {
        if (m_presentationModel)
            m_presentationModel->setVideoLayerGravity(videoGravity);
    });
}

void VideoFullscreenControllerContext::fullscreenModeChanged(HTMLMediaElementEnums::VideoFullscreenMode mode)
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this, mode] {
        if (m_presentationModel)
            m_presentationModel->fullscreenModeChanged(mode);
    });
}

bool VideoFullscreenControllerContext::hasVideo() const
{
    ASSERT(isUIThread());
    return m_presentationModel ? m_presentationModel->hasVideo() : false;
}

bool VideoFullscreenControllerContext::isChildOfElementFullscreen() const
{
    ASSERT(isUIThread());
    return m_presentationModel ? m_presentationModel->isChildOfElementFullscreen() : false;
}

bool VideoFullscreenControllerContext::isMuted() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->isMuted() : false;
}

double VideoFullscreenControllerContext::volume() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->volume() : 0;
}

bool VideoFullscreenControllerContext::isPictureInPictureActive() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->isPictureInPictureActive() : false;
}

bool VideoFullscreenControllerContext::isPictureInPictureSupported() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->isPictureInPictureSupported() : false;
}

void VideoFullscreenControllerContext::willEnterPictureInPicture()
{
    ASSERT(isUIThread());
    for (auto& client : m_presentationClients)
        client->willEnterPictureInPicture();
}

void VideoFullscreenControllerContext::didEnterPictureInPicture()
{
    ASSERT(isUIThread());
    for (auto& client : m_presentationClients)
        client->didEnterPictureInPicture();
}

void VideoFullscreenControllerContext::failedToEnterPictureInPicture()
{
    ASSERT(isUIThread());
    for (auto& client : m_presentationClients)
        client->failedToEnterPictureInPicture();
}

void VideoFullscreenControllerContext::willExitPictureInPicture()
{
    ASSERT(isUIThread());
    for (auto& client : m_presentationClients)
        client->willExitPictureInPicture();
}

void VideoFullscreenControllerContext::didExitPictureInPicture()
{
    ASSERT(isUIThread());
    for (auto& client : m_presentationClients)
        client->didExitPictureInPicture();
}

FloatSize VideoFullscreenControllerContext::videoDimensions() const
{
    ASSERT(isUIThread());
    return m_presentationModel ? m_presentationModel->videoDimensions() : FloatSize();
}

#pragma mark - PlaybackSessionModel

void VideoFullscreenControllerContext::addClient(PlaybackSessionModelClient& client)
{
    ASSERT(!m_playbackClients.contains(&client));
    m_playbackClients.add(&client);
}

void VideoFullscreenControllerContext::removeClient(PlaybackSessionModelClient& client)
{
    ASSERT(m_playbackClients.contains(&client));
    m_playbackClients.remove(&client);
}

void VideoFullscreenControllerContext::play()
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this] {
        if (m_playbackModel)
            m_playbackModel->play();
    });
}

void VideoFullscreenControllerContext::pause()
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this] {
        if (m_playbackModel)
            m_playbackModel->pause();
    });
}

void VideoFullscreenControllerContext::togglePlayState()
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this] {
        if (m_playbackModel)
            m_playbackModel->togglePlayState();
    });
}

void VideoFullscreenControllerContext::toggleMuted()
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this] {
        if (m_playbackModel)
            m_playbackModel->toggleMuted();
    });
}

void VideoFullscreenControllerContext::setMuted(bool muted)
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this, muted] {
        if (m_playbackModel)
            m_playbackModel->setMuted(muted);
    });
}

void VideoFullscreenControllerContext::setVolume(double volume)
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this, volume] {
        if (m_playbackModel)
            m_playbackModel->setVolume(volume);
    });
}

void VideoFullscreenControllerContext::setPlayingOnSecondScreen(bool value)
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this, value] {
        if (m_playbackModel)
            m_playbackModel->setPlayingOnSecondScreen(value);
    });
}

void VideoFullscreenControllerContext::beginScrubbing()
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this] {
        if (m_playbackModel)
            m_playbackModel->beginScrubbing();
    });
}

void VideoFullscreenControllerContext::endScrubbing()
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this] {
        if (m_playbackModel)
            m_playbackModel->endScrubbing();
    });
}

void VideoFullscreenControllerContext::seekToTime(double time, double toleranceBefore, double toleranceAfter)
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this, time, toleranceBefore, toleranceAfter] {
        if (m_playbackModel)
            m_playbackModel->seekToTime(time, toleranceBefore, toleranceAfter);
    });
}

void VideoFullscreenControllerContext::fastSeek(double time)
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this, time] {
        if (m_playbackModel)
            m_playbackModel->fastSeek(time);
    });
}

void VideoFullscreenControllerContext::beginScanningForward()
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this] {
        if (m_playbackModel)
            m_playbackModel->beginScanningForward();
    });
}

void VideoFullscreenControllerContext::beginScanningBackward()
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this] {
        if (m_playbackModel)
            m_playbackModel->beginScanningBackward();
    });
}

void VideoFullscreenControllerContext::endScanning()
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this] {
        if (m_playbackModel)
            m_playbackModel->endScanning();
    });
}

void VideoFullscreenControllerContext::setDefaultPlaybackRate(double defaultPlaybackRate)
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this, defaultPlaybackRate] {
        if (m_playbackModel)
            m_playbackModel->setDefaultPlaybackRate(defaultPlaybackRate);
    });
}

void VideoFullscreenControllerContext::setPlaybackRate(double playbackRate)
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this, playbackRate] {
        if (m_playbackModel)
            m_playbackModel->setPlaybackRate(playbackRate);
    });
}

void VideoFullscreenControllerContext::selectAudioMediaOption(uint64_t index)
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this, index] {
        if (m_playbackModel)
            m_playbackModel->selectAudioMediaOption(index);
    });
}

void VideoFullscreenControllerContext::selectLegibleMediaOption(uint64_t index)
{
    ASSERT(isUIThread());
    WebThreadRun([protectedThis = Ref { *this }, this, index] {
        if (m_playbackModel)
            m_playbackModel->selectLegibleMediaOption(index);
    });
}

double VideoFullscreenControllerContext::duration() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->duration() : 0;
}

double VideoFullscreenControllerContext::currentTime() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->currentTime() : 0;
}

double VideoFullscreenControllerContext::bufferedTime() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->bufferedTime() : 0;
}

auto VideoFullscreenControllerContext::playbackState() const -> OptionSet<PlaybackState>
{
    ASSERT(isUIThread());
    if (m_playbackModel)
        return m_playbackModel->playbackState();
    return { };
}

bool VideoFullscreenControllerContext::isPlaying() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->isPlaying() : false;
}

bool VideoFullscreenControllerContext::isStalled() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->isStalled() : false;
}

double VideoFullscreenControllerContext::defaultPlaybackRate() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->defaultPlaybackRate() : 0;
}

double VideoFullscreenControllerContext::playbackRate() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->playbackRate() : 0;
}

PlatformTimeRanges VideoFullscreenControllerContext::seekableRanges() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->seekableRanges() : PlatformTimeRanges::emptyRanges();
}

double VideoFullscreenControllerContext::seekableTimeRangesLastModifiedTime() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->seekableTimeRangesLastModifiedTime() : 0;
}

double VideoFullscreenControllerContext::liveUpdateInterval() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->liveUpdateInterval() : 0;
}

bool VideoFullscreenControllerContext::canPlayFastReverse() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->canPlayFastReverse() : false;
}

Vector<MediaSelectionOption> VideoFullscreenControllerContext::audioMediaSelectionOptions() const
{
    ASSERT(isUIThread());
    if (m_playbackModel)
        return m_playbackModel->audioMediaSelectionOptions();
    return { };
}

uint64_t VideoFullscreenControllerContext::audioMediaSelectedIndex() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->audioMediaSelectedIndex() : -1;
}

Vector<MediaSelectionOption> VideoFullscreenControllerContext::legibleMediaSelectionOptions() const
{
    ASSERT(isUIThread());
    if (m_playbackModel)
        return m_playbackModel->legibleMediaSelectionOptions();
    return { };
}

uint64_t VideoFullscreenControllerContext::legibleMediaSelectedIndex() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->legibleMediaSelectedIndex() : -1;
}

bool VideoFullscreenControllerContext::externalPlaybackEnabled() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->externalPlaybackEnabled() : false;
}

PlaybackSessionModel::ExternalPlaybackTargetType VideoFullscreenControllerContext::externalPlaybackTargetType() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->externalPlaybackTargetType() : ExternalPlaybackTargetType::TargetTypeNone;
}

String VideoFullscreenControllerContext::externalPlaybackLocalizedDeviceName() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->externalPlaybackLocalizedDeviceName() : String();
}

bool VideoFullscreenControllerContext::wirelessVideoPlaybackDisabled() const
{
    ASSERT(isUIThread());
    return m_playbackModel ? m_playbackModel->wirelessVideoPlaybackDisabled() : true;
}

#pragma mark Other

void VideoFullscreenControllerContext::setUpFullscreen(HTMLVideoElement& videoElement, UIView *view, HTMLMediaElementEnums::VideoFullscreenMode mode)
{
    ASSERT(isMainThread());
    RetainPtr<UIView> viewRef = view;
    m_videoElement = &videoElement;
    m_playbackModel = PlaybackSessionModelMediaElement::create();
    m_playbackModel->addClient(*this);
    m_playbackModel->setMediaElement(m_videoElement.get());

    m_presentationModel = VideoPresentationModelVideoElement::create();
    m_presentationModel->addClient(*this);
    m_presentationModel->setVideoElement(m_videoElement.get());

    bool allowsPictureInPicture = m_videoElement->webkitSupportsPresentationMode(HTMLVideoElement::VideoPresentationMode::PictureInPicture);

    IntRect videoElementClientRect = elementRectInWindow(m_videoElement.get());
    FloatRect videoLayerFrame = FloatRect(FloatPoint(), videoElementClientRect.size());
    m_presentationModel->setVideoLayerFrame(videoLayerFrame);

    FloatSize videoDimensions = { (float)videoElement.videoWidth(), (float)videoElement.videoHeight() };

    RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, this, videoElementClientRect, videoDimensions, viewRef, mode, allowsPictureInPicture] {
        ASSERT(isUIThread());
        WebThreadLock();
        Ref<PlaybackSessionInterfaceIOS> sessionInterface = PlaybackSessionInterfaceAVKitLegacy::create(*this);
        m_interface = VideoPresentationInterfaceAVKitLegacy::create(sessionInterface.get());
        m_interface->setVideoPresentationModel(this);

        m_videoFullscreenView = adoptNS([PAL::allocUIViewInstance() init]);

        m_interface->setupFullscreen(videoElementClientRect, videoDimensions, viewRef.get(), mode, allowsPictureInPicture, false, false);
    });
}

void VideoFullscreenControllerContext::exitFullscreen()
{
    ASSERT(WebThreadIsCurrent() || isMainThread());
    IntRect clientRect = elementRectInWindow(m_videoElement.get());
    RunLoop::mainSingleton().dispatch([protectedThis = Ref { *this }, this, clientRect] {
        ASSERT(isUIThread());
        m_interface->exitFullscreen(clientRect);
    });
}

void VideoFullscreenControllerContext::requestHideAndExitFullscreen()
{
    ASSERT(isUIThread());
    m_interface->requestHideAndExitFullscreen();
}

@implementation WebVideoFullscreenController {
    RefPtr<VideoFullscreenControllerContext> _context;
    RefPtr<HTMLVideoElement> _videoElement;
}

- (instancetype)init
{
    if (!(self = [super init]))
        return nil;

    return self;
}

- (void)setVideoElement:(NakedPtr<HTMLVideoElement>)videoElement
{
    _videoElement = videoElement;
}

- (NakedPtr<HTMLVideoElement>)videoElement
{
    return _videoElement.get();
}

- (void)enterFullscreen:(UIView *)view mode:(HTMLMediaElementEnums::VideoFullscreenMode)mode
{
    ASSERT(isMainThread());
    _context = VideoFullscreenControllerContext::create();
    _context->setController(self);
    _context->setUpFullscreen(*_videoElement.get(), view, mode);
}

- (void)exitFullscreen
{
    ASSERT(WebThreadIsCurrent() || isMainThread());
    _context->exitFullscreen();
}

- (void)requestHideAndExitFullscreen
{
    ASSERT(isUIThread());
    if (_context)
        _context->requestHideAndExitFullscreen();
}

- (void)didFinishFullscreen:(VideoFullscreenControllerContext*)context
{
    ASSERT(WebThreadIsCurrent());
    ASSERT_UNUSED(context, context == _context);
    auto strongSelf = retainPtr(self); // retain self before breaking a retain cycle.
    _context->setController(nil);
    _context = nullptr;
    _videoElement = nullptr;
}

@end

#endif // !HAVE(AVKIT)

#endif // PLATFORM(IOS_FAMILY)
