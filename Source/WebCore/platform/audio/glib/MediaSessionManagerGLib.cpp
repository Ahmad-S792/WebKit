/*
 *  Copyright (C) 2021 Igalia S.L
 *  Copyright (C) 2024 Alexander M (webkit@sata.lol)
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "MediaSessionManagerGLib.h"

#if USE(GLIB) && ENABLE(MEDIA_SESSION)

#include "AudioSession.h"
#include "HTMLMediaElement.h"
#include "MediaPlayer.h"
#include "MediaSessionGLib.h"
#include "MediaStrategy.h"
#include "NowPlayingInfo.h"
#include "PlatformMediaSession.h"
#include "PlatformStrategies.h"
#include <gio/gio.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/glib/GUniquePtr.h>

// https://specifications.freedesktop.org/mpris-spec/latest/
static const char s_mprisInterface[] =
    "<node>"
        "<interface name=\"org.mpris.MediaPlayer2\">"
            "<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"true\"/>"
            "<method name=\"Raise\"/>"
            "<method name=\"Quit\"/>"
            "<property name=\"CanQuit\" type=\"b\" access=\"read\"/>"
            "<property name=\"CanRaise\" type=\"b\" access=\"read\"/>"
            "<property name=\"HasTrackList\" type=\"b\" access=\"read\"/>"
            "<property name=\"Identity\" type=\"s\" access=\"read\"/>"
            "<property name=\"DesktopEntry\" type=\"s\" access=\"read\"/>"
            "<property name=\"SupportedUriSchemes\" type=\"as\" access=\"read\"/>"
            "<property name=\"SupportedMimeTypes\" type=\"as\" access=\"read\"/>"
        "</interface>"
        "<interface name=\"org.mpris.MediaPlayer2.Player\">"
            "<method name=\"Next\"/>"
            "<method name=\"Previous\"/>"
            "<method name=\"Pause\"/>"
            "<method name=\"PlayPause\"/>"
            "<method name=\"Stop\"/>"
            "<method name=\"Play\"/>"
            "<method name=\"Seek\">"
                "<arg direction=\"in\" type=\"x\" name=\"Offset\"/>"
            "</method>"
            "<method name=\"SetPosition\">"
                "<arg direction=\"in\" type=\"o\" name=\"TrackId\"/>"
                "<arg direction=\"in\" type=\"x\" name=\"Position\"/>"
            "</method>"
            "<method name=\"OpenUri\">"
                "<arg direction=\"in\" type=\"s\" name=\"Uri\"/>"
            "</method>"
            "<property name=\"PlaybackStatus\" type=\"s\" access=\"read\">"
                "<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"true\"/>"
            "</property>"
            "<property name=\"Rate\" type=\"d\" access=\"readwrite\">"
                "<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"true\"/>"
            "</property>"
            "<property name=\"Metadata\" type=\"a{sv}\" access=\"read\">"
                "<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"true\"/>"
            "</property>"
            "<property name=\"Volume\" type=\"d\" access=\"readwrite\">"
                "<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"true\"/>"
            "</property>"
            "<property name=\"Position\" type=\"x\" access=\"read\">"
                "<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"false\"/>"
            "</property>"
            "<property name=\"MinimumRate\" type=\"d\" access=\"read\">"
                "<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"true\"/>"
            "</property>"
            "<property name=\"MaximumRate\" type=\"d\" access=\"read\">"
                "<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"true\"/>"
            "</property>"
            "<property name=\"CanGoNext\" type=\"b\" access=\"read\">"
                "<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"true\"/>"
            "</property>"
            "<property name=\"CanGoPrevious\" type=\"b\" access=\"read\">"
                "<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"true\"/>"
            "</property>"
            "<property name=\"CanPlay\" type=\"b\" access=\"read\">"
                "<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"true\"/>"
            "</property>"
            "<property name=\"CanPause\" type=\"b\" access=\"read\">"
                "<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"true\"/>"
            "</property>"
            "<property name=\"CanSeek\" type=\"b\" access=\"read\">"
                "<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"true\"/>"
            "</property>"
            "<property name=\"CanControl\" type=\"b\" access=\"read\">"
                "<annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"false\"/>"
            "</property>"
            "<signal name=\"Seeked\">"
                "<arg name=\"Position\" type=\"x\"/>"
            "</signal>"
        "</interface>"
    "</node>";

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(MediaSessionManagerGLib);

RefPtr<PlatformMediaSessionManager> PlatformMediaSessionManager::create(std::optional<PageIdentifier>)
{
    GUniqueOutPtr<GError> error;
    auto mprisInterface = adoptGRef(g_dbus_node_info_new_for_xml(s_mprisInterface, &error.outPtr()));
    if (!mprisInterface) {
        g_warning("Failed at parsing XML Interface definition: %s", error->message);
        return nullptr;
    }
    return adoptRef(new MediaSessionManagerGLib(WTFMove(mprisInterface)));
}

MediaSessionManagerGLib::MediaSessionManagerGLib(GRefPtr<GDBusNodeInfo>&& mprisInterface)
    : m_mprisInterface(WTFMove(mprisInterface))
    , m_nowPlayingManager(platformStrategies()->mediaStrategy().createNowPlayingManager())
{
}

MediaSessionManagerGLib::~MediaSessionManagerGLib() = default;

void MediaSessionManagerGLib::beginInterruption(PlatformMediaSession::InterruptionType type)
{
    if (type == PlatformMediaSession::InterruptionType::SystemInterruption) {
        forEachSession([] (auto& session) {
            session.setHasPlayedAudiblySinceLastInterruption(false);
        });
    }

    PlatformMediaSessionManager::beginInterruption(type);
}

void MediaSessionManagerGLib::scheduleSessionStatusUpdate()
{
    callOnMainThread([this] () mutable {
        m_nowPlayingManager->setSupportsSeeking(computeSupportsSeeking());
        updateNowPlayingInfo();

        forEachSession([] (auto& session) {
            session.updateMediaUsageIfChanged();
        });
    });
}

bool MediaSessionManagerGLib::sessionWillBeginPlayback(PlatformMediaSessionInterface& session)
{
    if (!PlatformMediaSessionManager::sessionWillBeginPlayback(session))
        return false;

    scheduleSessionStatusUpdate();
    return true;
}

void MediaSessionManagerGLib::sessionDidEndRemoteScrubbing(PlatformMediaSessionInterface&)
{
    scheduleSessionStatusUpdate();
}

void MediaSessionManagerGLib::addSession(PlatformMediaSessionInterface& platformSession)
{
    auto identifier = platformSession.mediaSessionIdentifier();
    auto session = MediaSessionGLib::create(*this, identifier);
    if (!session)
        return;

    m_sessions.add(identifier, WTFMove(session));
    m_nowPlayingManager->addClient(*this);

    PlatformMediaSessionManager::addSession(platformSession);
}

void MediaSessionManagerGLib::removeSession(PlatformMediaSessionInterface& session)
{
    PlatformMediaSessionManager::removeSession(session);

    m_sessions.remove(session.mediaSessionIdentifier());
    if (hasNoSession())
        m_nowPlayingManager->removeClient(*this);

    scheduleSessionStatusUpdate();
}

void MediaSessionManagerGLib::setCurrentSession(PlatformMediaSessionInterface& session)
{
    PlatformMediaSessionManager::setCurrentSession(session);

    setPrimarySessionIfNeeded(session);
    m_nowPlayingManager->updateSupportedCommands();
}

void MediaSessionManagerGLib::sessionWillEndPlayback(PlatformMediaSessionInterface& session, DelayCallingUpdateNowPlaying delayCallingUpdateNowPlaying)
{
    PlatformMediaSessionManager::sessionWillEndPlayback(session, delayCallingUpdateNowPlaying);

    callOnMainThread([weakSession = WeakPtr { session }] {
        if (weakSession)
            weakSession->updateMediaUsageIfChanged();
    });

    if (delayCallingUpdateNowPlaying == DelayCallingUpdateNowPlaying::No)
        updateNowPlayingInfo();
    else {
        callOnMainThread([this] {
            updateNowPlayingInfo();
        });
    }
}

void MediaSessionManagerGLib::sessionStateChanged(PlatformMediaSessionInterface& platformSession)
{
    PlatformMediaSessionManager::sessionStateChanged(platformSession);

    auto session = m_sessions.get(platformSession.mediaSessionIdentifier());
    if (!session)
        return;

    session->playbackStatusChanged(platformSession);
}

void MediaSessionManagerGLib::clientCharacteristicsChanged(PlatformMediaSessionInterface& platformSession, bool)
{
    ALWAYS_LOG(LOGIDENTIFIER, platformSession.logIdentifier());
    if (m_isSeeking) {
        m_isSeeking = false;
        auto session = m_sessions.get(platformSession.mediaSessionIdentifier());
        session->emitPositionChanged(platformSession.nowPlayingInfo()->currentTime);
    }
    scheduleSessionStatusUpdate();
}

void MediaSessionManagerGLib::sessionCanProduceAudioChanged()
{
    ALWAYS_LOG(LOGIDENTIFIER);
    PlatformMediaSessionManager::sessionCanProduceAudioChanged();
    scheduleSessionStatusUpdate();
}

void MediaSessionManagerGLib::addSupportedCommand(PlatformMediaSession::RemoteControlCommandType command)
{
    m_nowPlayingManager->addSupportedCommand(command);
}

void MediaSessionManagerGLib::removeSupportedCommand(PlatformMediaSession::RemoteControlCommandType command)
{
    m_nowPlayingManager->removeSupportedCommand(command);
}

RemoteCommandListener::RemoteCommandsSet MediaSessionManagerGLib::supportedCommands() const
{
    return m_nowPlayingManager->supportedCommands();
}

void MediaSessionManagerGLib::setPrimarySessionIfNeeded(PlatformMediaSessionInterface& platformSession)
{
    if (PlatformMediaSessionManager::currentSession().get() != &platformSession)
        return;

    auto session = m_sessions.get(platformSession.mediaSessionIdentifier());
    ASSERT(session);
    if (!session)
        return;

    session->setMprisRegistrationEligibility(MediaSessionGLib::MprisRegistrationEligiblilty::Eligible);
    unregisterAllOtherSessions(platformSession);
}

void MediaSessionManagerGLib::unregisterAllOtherSessions(PlatformMediaSessionInterface& platformSession)
{
    ALWAYS_LOG(LOGIDENTIFIER, platformSession.logIdentifier());
    for (auto& [sessionId, session] : m_sessions) {
        if (sessionId != platformSession.mediaSessionIdentifier())
            session->unregisterMprisSession();
    }
}

WeakPtr<PlatformMediaSession> MediaSessionManagerGLib::nowPlayingEligibleSession()
{
    return bestEligibleSessionForRemoteControls([](auto& session) {
        return session.isNowPlayingEligible();
    }, PlatformMediaSession::PlaybackControlsPurpose::NowPlaying);
}

void MediaSessionManagerGLib::updateNowPlayingInfo()
{
    auto platformSession = nowPlayingEligibleSession();
    if (!platformSession) {
        if (m_registeredAsNowPlayingApplication) {
            ALWAYS_LOG(LOGIDENTIFIER, "clearing now playing info");
            m_nowPlayingManager->clearNowPlayingInfo();
        }

        m_registeredAsNowPlayingApplication = false;
        m_nowPlayingActive = false;
        m_lastUpdatedNowPlayingTitle = emptyString();
        m_lastUpdatedNowPlayingDuration = NAN;
        m_lastUpdatedNowPlayingElapsedTime = NAN;
        m_lastUpdatedNowPlayingInfoUniqueIdentifier = std::nullopt;
        return;
    }

    auto session = m_sessions.get(platformSession->mediaSessionIdentifier());
    if (!session)
        return;

    auto nowPlayingInfo = platformSession->nowPlayingInfo();
    if (!nowPlayingInfo)
        return;

    m_haveEverRegisteredAsNowPlayingApplication = true;

    if (m_nowPlayingManager->setNowPlayingInfo(*nowPlayingInfo))
        ALWAYS_LOG(LOGIDENTIFIER, "title = \"", nowPlayingInfo->metadata.title, "\", isPlaying = ", nowPlayingInfo->isPlaying, ", duration = ", nowPlayingInfo->duration, ", now = ", nowPlayingInfo->currentTime, ", id = ", (nowPlayingInfo->uniqueIdentifier ? nowPlayingInfo->uniqueIdentifier->toUInt64() : 0), ", registered = ", m_registeredAsNowPlayingApplication, ", src = \"", nowPlayingInfo->metadata.artwork ? nowPlayingInfo->metadata.artwork->src : String(), "\"");

    if (!m_registeredAsNowPlayingApplication) {
        m_registeredAsNowPlayingApplication = true;
        providePresentingApplicationPIDIfNecessary();
    }

    if (!nowPlayingInfo->metadata.title.isEmpty())
        m_lastUpdatedNowPlayingTitle = nowPlayingInfo->metadata.title;

    double duration = nowPlayingInfo->duration;
    if (std::isfinite(duration) && duration != MediaPlayer::invalidTime())
        m_lastUpdatedNowPlayingDuration = duration;

    m_lastUpdatedNowPlayingInfoUniqueIdentifier = nowPlayingInfo->uniqueIdentifier;

    double currentTime = nowPlayingInfo->currentTime;
    if (std::isfinite(currentTime) && currentTime != MediaPlayer::invalidTime() && nowPlayingInfo->supportsSeeking)
        m_lastUpdatedNowPlayingElapsedTime = currentTime;

    m_nowPlayingActive = nowPlayingInfo->allowsNowPlayingControlsVisibility;

    session->updateNowPlaying(*nowPlayingInfo);
}

void MediaSessionManagerGLib::dispatch(PlatformMediaSession::RemoteControlCommandType platformCommand, PlatformMediaSession::RemoteCommandArgument argument)
{
    m_isSeeking = platformCommand == PlatformMediaSession::RemoteControlCommandType::SeekToPlaybackPositionCommand;
    m_nowPlayingManager->didReceiveRemoteControlCommand(platformCommand, argument);
}

} // namespace WebCore

#endif // USE(GLIB) && ENABLE(MEDIA_SESSION)
