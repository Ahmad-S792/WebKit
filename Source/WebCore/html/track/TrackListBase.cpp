/*
 * Copyright (C) 2011, 2012 Apple Inc. All rights reserved.
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

#if ENABLE(VIDEO)

#include "TrackListBase.h"

#include "EventNames.h"
#include "ScriptExecutionContext.h"
#include "TrackEvent.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(TrackListBase);

TrackListBase::TrackListBase(ScriptExecutionContext* context, Type type)
    : ActiveDOMObject(context)
    , m_type(type)
{
}

TrackListBase::~TrackListBase() = default;

void TrackListBase::didMoveToNewDocument(Document& newDocument)
{
    ActiveDOMObject::didMoveToNewDocument(newDocument);
    for (RefPtr track : m_inbandTracks)
        track->didMoveToNewDocument(newDocument);
}

WebCoreOpaqueRoot TrackListBase::opaqueRoot()
{
    if (auto* rootObserver = m_opaqueRootObserver.get())
        return (*rootObserver)();
    return WebCoreOpaqueRoot { this };
}

unsigned TrackListBase::length() const
{
    return m_inbandTracks.size();
}

RefPtr<TrackBase> TrackListBase::find(TrackID trackID) const
{
    size_t index = m_inbandTracks.findIf([&](auto& value) {
        return value->trackId() == trackID;
    });
    if (index == notFound)
        return nullptr;
    return m_inbandTracks[index];
}

void TrackListBase::remove(TrackID trackID, bool scheduleEvent)
{
    if (RefPtr track = find(trackID))
        remove(*track, scheduleEvent);
}

void TrackListBase::remove(TrackBase& track, bool scheduleEvent)
{
    size_t index = m_inbandTracks.find(&track);
    if (index == notFound)
        return;

    if (track.trackList() == this)
        track.clearTrackList();

    Ref<TrackBase> trackRef = *m_inbandTracks[index];

    m_inbandTracks.removeAt(index);

    if (scheduleEvent)
        scheduleRemoveTrackEvent(WTFMove(trackRef));
}

bool TrackListBase::contains(TrackBase& track) const
{
    return m_inbandTracks.find(&track) != notFound;
}

bool TrackListBase::contains(TrackID trackID) const
{
    return find(trackID);
}

void TrackListBase::scheduleTrackEvent(const AtomString& eventName, Ref<TrackBase>&& track)
{
    queueTaskToDispatchEvent(*this, TaskSource::MediaElement, TrackEvent::create(eventName, Event::CanBubble::No, Event::IsCancelable::No, WTFMove(track)));
}

void TrackListBase::scheduleAddTrackEvent(Ref<TrackBase>&& track)
{
    // 4.8.10.5 Loading the media resource
    // ...
    // Fire a trusted event with the name addtrack, that does not bubble and is
    // not cancelable, and that uses the TrackEvent interface, with the track
    // attribute initialized to the new AudioTrack object, at this
    // AudioTrackList object.
    // ...
    // Fire a trusted event with the name addtrack, that does not bubble and is
    // not cancelable, and that uses the TrackEvent interface, with the track
    // attribute initialized to the new VideoTrack object, at this
    // VideoTrackList object.

    // 4.8.10.12.3 Sourcing out-of-band text tracks
    // 4.8.10.12.4 Text track API
    // ... then queue a task to fire an event with the name addtrack, that does not
    // bubble and is not cancelable, and that uses the TrackEvent interface, with
    // the track attribute initialized to the text track's TextTrack object, at
    // the media element's textTracks attribute's TextTrackList object.
    scheduleTrackEvent(eventNames().addtrackEvent, WTFMove(track));
}

void TrackListBase::scheduleRemoveTrackEvent(Ref<TrackBase>&& track)
{
    // 4.8.10.6 Offsets into the media resource
    // If at any time the user agent learns that an audio or video track has
    // ended and all media data relating to that track corresponds to parts of
    // the media timeline that are before the earliest possible position, the
    // user agent may queue a task to remove the track from the audioTracks
    // attribute's AudioTrackList object or the videoTracks attribute's
    // VideoTrackList object as appropriate and then fire a trusted event
    // with the name removetrack, that does not bubble and is not cancelable,
    // and that uses the TrackEvent interface, with the track attribute
    // initialized to the AudioTrack or VideoTrack object representing the
    // track, at the media element's aforementioned AudioTrackList or
    // VideoTrackList object.

    // 4.8.10.12.3 Sourcing out-of-band text tracks
    // When a track element's parent element changes and the old parent was a
    // media element, then the user agent must remove the track element's
    // corresponding text track from the media element's list of text tracks,
    // and then queue a task to fire a trusted event with the name removetrack,
    // that does not bubble and is not cancelable, and that uses the TrackEvent
    // interface, with the track attribute initialized to the text track's
    // TextTrack object, at the media element's textTracks attribute's
    // TextTrackList object.
    scheduleTrackEvent(eventNames().removetrackEvent, WTFMove(track));
}

void TrackListBase::scheduleChangeEvent()
{
    // 4.8.10.6 Offsets into the media resource
    // Whenever an audio track in an AudioTrackList is enabled or disabled, the
    // user agent must queue a task to fire a simple event named change at the
    // AudioTrackList object.
    // ...
    // Whenever a track in a VideoTrackList that was previously not selected is
    // selected, the user agent must queue a task to fire a simple event named
    // change at the VideoTrackList object.
    m_isChangeEventScheduled = true;
    queueTaskKeepingObjectAlive(*this, TaskSource::MediaElement, [](auto& trackList) {
        trackList.m_isChangeEventScheduled = false;
        trackList.dispatchEvent(Event::create(eventNames().changeEvent, Event::CanBubble::No, Event::IsCancelable::No));
    });
}

bool TrackListBase::isAnyTrackEnabled() const
{
    for (auto& track : m_inbandTracks) {
        if (track->enabled())
            return true;
    }
    return false;
}

} // namespace WebCore

#endif
