/*
 * Copyright (C) 2013 Cable Television Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "TrackPrivateBaseGStreamer.h"

#include "GStreamerCommon.h"
#include "TrackPrivateBase.h"
#include <gst/tag/tag.h>
#include <wtf/glib/GUniquePtr.h>
#include <wtf/text/CString.h>
#include <wtf/text/StringCommon.h>
#include <wtf/text/StringToIntegerConversion.h>

GST_DEBUG_CATEGORY_EXTERN(webkit_media_player_debug);
#define GST_CAT_DEFAULT webkit_media_player_debug

namespace WebCore {

static GRefPtr<GstPad> findBestUpstreamPad(GRefPtr<GstPad> pad)
{
    if (!pad)
        return nullptr;

    GRefPtr<GstPad> sinkPad = pad;
    GRefPtr<GstPad> peerSrcPad;

    peerSrcPad = adoptGRef(gst_pad_get_peer(sinkPad.get()));
    // Some tag events with language tags don't reach the webkittextcombiner pads on time.
    // It's better to listen for them in the earlier upstream ghost pads.
    if (GST_IS_GHOST_PAD(peerSrcPad.get()))
        sinkPad = adoptGRef(gst_ghost_pad_get_target(GST_GHOST_PAD(peerSrcPad.get())));
    return sinkPad;
}

TrackPrivateBaseGStreamer::TrackPrivateBaseGStreamer(TrackType type, TrackPrivateBase* owner, unsigned index, GRefPtr<GstPad>&& pad, bool shouldHandleStreamStartEvent)
    : m_notifier(MainThreadNotifier<MainThreadNotification>::create())
    , m_index(index)
    , m_type(type)
    , m_owner(owner)
    , m_shouldHandleStreamStartEvent(shouldHandleStreamStartEvent)
{
    setPad(WTFMove(pad));
    ASSERT(m_pad);

    // We can't call notifyTrackOfTagsChanged() directly, because we need tagsChanged() to setup m_tags.
    tagsChanged();
}

TrackPrivateBaseGStreamer::TrackPrivateBaseGStreamer(TrackType type, TrackPrivateBase* owner, unsigned index, GRefPtr<GstPad>&& pad, TrackID trackId)
    : m_notifier(MainThreadNotifier<MainThreadNotification>::create())
    , m_index(index)
    , m_id(trackId)
    , m_type(type)
    , m_owner(owner)
    , m_shouldUsePadStreamId(false)
    , m_shouldHandleStreamStartEvent(false)
{
    setPad(WTFMove(pad));
    ASSERT(m_pad);

    // We can't call notifyTrackOfTagsChanged() directly, because we need tagsChanged() to setup m_tags.
    tagsChanged();
}

TrackPrivateBaseGStreamer::TrackPrivateBaseGStreamer(TrackType type, TrackPrivateBase* owner, unsigned index, GstStream* stream)
    : m_notifier(MainThreadNotifier<MainThreadNotification>::create())
    , m_index(index)
    , m_gstStreamId(AtomString(unsafeSpan8(gst_stream_get_stream_id(stream))))
    , m_id(parseStreamId(m_gstStreamId).value_or(index))
    , m_stream(stream)
    , m_type(type)
    , m_owner(owner)
{
    ASSERT(m_stream);

    g_signal_connect_swapped(m_stream.get(), "notify::tags", G_CALLBACK(+[](TrackPrivateBaseGStreamer* track) {
        track->tagsChanged();
    }), this);

    // We can't call notifyTrackOfTagsChanged() directly, because we need tagsChanged() to setup m_tags.
    tagsChanged();
}

void TrackPrivateBaseGStreamer::setPad(GRefPtr<GstPad>&& pad)
{
    ASSERT(isMainThread()); // because this code writes to AtomString members.

    if (m_bestUpstreamPad && m_eventProbe)
        gst_pad_remove_probe(m_bestUpstreamPad.get(), m_eventProbe);

    m_pad = WTFMove(pad);
    m_bestUpstreamPad = findBestUpstreamPad(m_pad);
    m_gstStreamId = AtomString(unsafeSpan8(gst_pad_get_stream_id(m_pad.get())));

    if (m_shouldUsePadStreamId)
        m_id = parseStreamId(m_gstStreamId).value_or(m_index);

    if (!m_bestUpstreamPad)
        return;

    m_eventProbe = gst_pad_add_probe(m_bestUpstreamPad.get(), GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, reinterpret_cast<GstPadProbeCallback>(+[](GstPad*, GstPadProbeInfo* info, TrackPrivateBaseGStreamer* track) -> GstPadProbeReturn {
        auto* event = gst_pad_probe_info_get_event(info);
        switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_TAG:
            track->tagsChanged();
            break;
        case GST_EVENT_STREAM_START:
            if (track->m_shouldHandleStreamStartEvent)
                track->streamChanged();
            break;
        case GST_EVENT_CAPS: {
            track->m_taskQueue.enqueueTask([track, event = GRefPtr<GstEvent>(event)]() {
                GstCaps* caps;
                gst_event_parse_caps(event.get(), &caps);
                if (!caps)
                    return;
                track->capsChanged(track->m_id, GRefPtr<GstCaps>(caps));
            });
            break;
        }
        default:
            break;
        }
        return GST_PAD_PROBE_OK;
    }), this, nullptr);
}

TrackPrivateBaseGStreamer::~TrackPrivateBaseGStreamer()
{
    disconnect();
    m_notifier->invalidate();
}

GstObject* TrackPrivateBaseGStreamer::objectForLogging() const
{
    if (m_stream)
        return GST_OBJECT_CAST(m_stream.get());

    ASSERT(m_pad);
    return GST_OBJECT_CAST(m_pad.get());
}

void TrackPrivateBaseGStreamer::disconnect()
{
    if (m_stream)
        g_signal_handlers_disconnect_matched(m_stream.get(), G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, this);

    m_tags.clear();

    m_notifier->cancelPendingNotifications();

    if (m_bestUpstreamPad && m_eventProbe) {
        gst_pad_remove_probe(m_bestUpstreamPad.get(), m_eventProbe);
        m_eventProbe = 0;
        m_bestUpstreamPad.clear();
    }

    if (m_pad) {
        g_signal_handlers_disconnect_matched(m_pad.get(), G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, this);
        m_pad.clear();
    }
}

void TrackPrivateBaseGStreamer::tagsChanged()
{
    // May be called by any thread, including the streaming thread.
    GRefPtr<GstTagList> tags;
    if (m_bestUpstreamPad) {
        GRefPtr<GstEvent> tagEvent;
        guint i = 0;
        // Prefer the tag event having a language tag, if available.
        do {
            tagEvent = adoptGRef(gst_pad_get_sticky_event(m_bestUpstreamPad.get(), GST_EVENT_TAG, i));
            if (tagEvent) {
                GstTagList* tagsFromEvent = nullptr;
                gst_event_parse_tag(tagEvent.get(), &tagsFromEvent);
                tags = adoptGRef(gst_tag_list_copy(tagsFromEvent));
                String language;
                if (getTag(tags.get(), GST_TAG_LANGUAGE_CODE, language))
                    break;
            }
            i++;
        } while (tagEvent);
    } else if (m_stream)
        tags = adoptGRef(gst_stream_get_tags(m_stream.get()));

    if (!tags)
        tags = adoptGRef(gst_tag_list_new_empty());

    GST_DEBUG("Inspecting track %" PRIu64 " with tags: %" GST_PTR_FORMAT, m_id, tags.get());
    {
        Locker locker { m_tagMutex };
        m_tags.swap(tags);
    }

    m_notifier->notify(MainThreadNotification::TagsChanged, [this] {
        notifyTrackOfTagsChanged();
    });
}

bool TrackPrivateBaseGStreamer::getLanguageCode(GstTagList* tags, AtomString& value)
{
    String language;
    if (getTag(tags, GST_TAG_LANGUAGE_CODE, language)) {
        AtomString convertedLanguage = AtomString(unsafeSpan8(gst_tag_get_language_code_iso_639_1(language.utf8().data())));
        GST_DEBUG("Converted track %" PRIu64 "'s language code to %s.", m_id, convertedLanguage.string().utf8().data());
        if (convertedLanguage != value) {
            value = WTFMove(convertedLanguage);
            return true;
        }
    }
    return false;
}

template<class StringType>
bool TrackPrivateBaseGStreamer::getTag(GstTagList* tags, const gchar* tagName, StringType& value)
{
    GUniqueOutPtr<gchar> tagValue;
    if (gst_tag_list_get_string(tags, tagName, &tagValue.outPtr())) {
        GST_DEBUG("Track %" PRIu64 " got %s %s.", m_id, tagName, tagValue.get());
        value = StringType { String::fromLatin1(tagValue.get()) };
        return true;
    }
    return false;
}

void TrackPrivateBaseGStreamer::notifyTrackOfTagsChanged()
{
    ASSERT(isMainThread()); // because this code writes to AtomString members.
    GRefPtr<GstTagList> tags;
    {
        Locker locker { m_tagMutex };
        tags.swap(m_tags);
    }

    if (!tags)
        return;

    tagsChanged(GRefPtr<GstTagList>(tags));

    if (getTag(tags.get(), GST_TAG_TITLE, m_label)) {
        m_owner->notifyMainThreadClient([&](auto& client) {
            client.labelChanged(m_label);
        });
    }

    AtomString language;
    if (!getLanguageCode(tags.get(), language))
        return;

    if (language == m_language)
        return;

    m_language = language;
    m_owner->notifyMainThreadClient([&](auto& client) {
        client.languageChanged(m_language);
    });
}

void TrackPrivateBaseGStreamer::notifyTrackOfStreamChanged()
{
    if (!m_pad)
        return;

    auto gstStreamId = AtomString(unsafeSpan8(gst_pad_get_stream_id(m_pad.get())));
    auto streamId = parseStreamId(gstStreamId);
    if (!streamId)
        return;

    ASSERT(isMainThread()); // because this code writes to AtomString members.
    m_gstStreamId = gstStreamId;
    m_id = streamId.value();
    GST_INFO("Track %" PRIu64 " got stream start. GStreamer stream-id: %s", m_id, m_gstStreamId.string().utf8().data());
}

void TrackPrivateBaseGStreamer::streamChanged()
{
    m_notifier->notify(MainThreadNotification::StreamChanged, [this] {
        notifyTrackOfStreamChanged();
    });
}

void TrackPrivateBaseGStreamer::installUpdateConfigurationHandlers()
{
    if (m_pad) {
        g_signal_connect_swapped(m_pad.get(), "notify::caps", G_CALLBACK(+[](TrackPrivateBaseGStreamer* track) {
            if (!track->m_pad)
                return;
            auto caps = adoptGRef(gst_pad_get_current_caps(track->m_pad.get()));
            // We will receive a synchronous notification for caps being unset during pipeline teardown.
            if (!caps)
                return;

            track->m_taskQueue.enqueueTask([track, caps = WTFMove(caps)]() mutable {
                track->capsChanged(getStreamIdFromPad(track->m_pad.get()).value_or(track->m_index), WTFMove(caps));
            });
        }), this);
        g_signal_connect_swapped(m_pad.get(), "notify::tags", G_CALLBACK(+[](TrackPrivateBaseGStreamer* track) {
            track->m_taskQueue.enqueueTask([track]() {
                if (!track->m_pad)
                    return;
                track->updateConfigurationFromTags(getAllTags(track->m_pad));
            });
        }), this);
    } else if (m_stream) {
        g_signal_connect_swapped(m_stream.get(), "notify::caps", G_CALLBACK(+[](TrackPrivateBaseGStreamer* track) {
            track->m_taskQueue.enqueueTask([track]() {
                auto caps = adoptGRef(gst_stream_get_caps(track->m_stream.get()));
                track->capsChanged(getStreamIdFromStream(track->m_stream.get()).value_or(track->m_index), WTFMove(caps));
            });
        }), this);

        // This signal can be triggered from the main thread
        // (CanvasCaptureMediaStreamTrack::Source::captureCanvas() triggering the mediastreamsrc
        // InternalSource::videoFrameAvailable() which can update the stream tags.)
        g_signal_connect_swapped(m_stream.get(), "notify::tags", G_CALLBACK(+[](TrackPrivateBaseGStreamer* track) {
            if (isMainThread()) {
                auto tags = adoptGRef(gst_stream_get_tags(track->m_stream.get()));
                track->updateConfigurationFromTags(WTFMove(tags));
                return;
            }
            track->m_taskQueue.enqueueTask([track]() {
                auto tags = adoptGRef(gst_stream_get_tags(track->m_stream.get()));
                track->updateConfigurationFromTags(WTFMove(tags));
            });
        }), this);
    }
}

GRefPtr<GstTagList> TrackPrivateBaseGStreamer::getAllTags(const GRefPtr<GstPad>& pad)
{
    auto allTags = adoptGRef(gst_tag_list_new_empty());
    GstTagList* taglist = nullptr;
    for (guint i = 0;; i++) {
        GRefPtr<GstEvent> tagsEvent = adoptGRef(gst_pad_get_sticky_event(pad.get(), GST_EVENT_TAG, i));
        if (!tagsEvent)
            break;
        gst_event_parse_tag(tagsEvent.get(), &taglist);
        allTags = adoptGRef(gst_tag_list_merge(allTags.get(), taglist, GST_TAG_MERGE_APPEND));
    }
    return allTags;
}

bool TrackPrivateBaseGStreamer::updateTrackIDFromTags(const GRefPtr<GstTagList>& tags)
{
    ASSERT(isMainThread()); // because this code writes to AtomString members.
    GUniqueOutPtr<char> trackIDString;
    if (!gst_tag_list_get_string(tags.get(), "container-specific-track-id", &trackIDString.outPtr()))
        return false;

    auto trackID = WTF::parseInteger<TrackID>(StringView { unsafeSpan(trackIDString.get()) });
    if (trackID && *trackID != m_trackID.value_or(0)) {
        m_trackID = *trackID;
        ASSERT(m_trackID);
        return true;
    }
    return false;
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(VIDEO) && USE(GSTREAMER)
