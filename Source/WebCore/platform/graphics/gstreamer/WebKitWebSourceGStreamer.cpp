/*
 *  Copyright (C) 2009, 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *  Copyright (C) 2013 Collabora Ltd.
 *  Copyright (C) 2019, 2020 Igalia S.L.
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
#include "WebKitWebSourceGStreamer.h"

#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "GStreamerCommon.h"
#include "GUniquePtrGStreamer.h"
#include "HTTPHeaderNames.h"
#include "MediaPlayerPrivateGStreamer.h"
#include "PlatformMediaResourceLoader.h"
#include "PolicyChecker.h"
#include "ResourceError.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "SecurityOrigin.h"
#include <cstdint>
#include <wtf/Condition.h>
#include <wtf/DataMutex.h>
#include <wtf/PrintStream.h>
#include <wtf/RunLoop.h>
#include <wtf/Scope.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/glib/GThreadSafeWeakPtr.h>
#include <wtf/glib/WTFGType.h>
#include <wtf/text/CString.h>
#include <wtf/text/StringToIntegerConversion.h>

using namespace WebCore;

// Never pause download of media resources smaller than 2MiB.
#define SMALL_MEDIA_RESOURCE_MAX_SIZE 2 * 1024 * 1024

// Keep at most 2% of the full, non-small, media resource buffered. When this
// threshold is reached, the download task is paused.
#define HIGH_QUEUE_FACTOR_THRESHOLD 0.02

// Keep at least 20% of maximum queue size buffered. When this threshold is
// reached, the download task resumes.
#define LOW_QUEUE_FACTOR_THRESHOLD 0.2

struct WebKitWebSrcPrivate {

    ThreadSafeWeakPtr<WebCore::MediaPlayerPrivateGStreamer> player;

    // Constants initialized during construction:
    unsigned minimumBlocksize;

    // Configuration of the element (properties set by the user of WebKitWebSrc):
    // They can only change when state < PAUSED.
    CString originalURI;
    bool keepAlive { false };
    GUniquePtr<GstStructure> extraHeaders;
    bool compress { false };
    GUniquePtr<gchar> httpMethod;

    struct StreamingMembers {
#ifndef NDEBUG
        ~StreamingMembers()
        {
            // By the time we're destroying WebKitWebSrcPrivate unLock() should have been called and therefore resource
            // should have already been cleared.
            ASSERT(!resource);
        }
#endif

        // Properties initially empty, but set once the first HTTP response arrives:
        bool wasResponseReceived { false };
        CString redirectedURI;
        bool didPassAccessControlCheck { false };
        std::optional<uint64_t> size;
        bool isSeekable { false };
        GRefPtr<GstCaps> pendingCaps;
        GRefPtr<GstMessage> pendingHttpHeadersMessage; // Set from MT, sent from create().
        GRefPtr<GstEvent> pendingHttpHeadersEvent; // Set from MT, sent from create().

        // Properties updated with every downloaded data block:
        WallTime downloadStartTime { WallTime::nan() };
        uint64_t totalDownloadedBytes { 0 };
        bool doesHaveEOS { false }; // Set both when we reach stopPosition and on errors (including on responseReceived).
        bool isDownloadSuspended { false }; // Set to true from the network handler when the high water level is reached.

        // Obtained by means of GstContext queries before making the first HTTP request, unless it
        // was explicitely set using webKitWebSrcSetResourceLoader() by the playbin source-setup
        // signal handler in MediaPlayerPrivateGStreamer.
        RefPtr<WebCore::PlatformMediaResourceLoader> loader;

        // MediaPlayer referrer cached value. The corresponding method has to be called from the
        // main thread, so the value needs to be cached before use in non-main thread.
        String referrer;

        // Properties used for GStreamer data-flow in create().
        bool isFlushing { false };
        Condition responseCondition; // Must be signaled after any updates on HTTP requests, and when flushing.
        GRefPtr<GstAdapter> adapter;
        bool isDurationSet { false };
        uint64_t readPosition { 0 };

        // Properties only set during seek.
        // basesrc ensures they can't change during a create() call by taking the STREAMING_LOCK.
        // (An initial seek is also guaranteed by basesrc.)
        unsigned requestNumber { 1 };
        uint64_t requestedPosition { 0 };
        uint64_t stopPosition { UINT64_MAX };

        bool isRequestPending { true };
        HashSet<RefPtr<WebCore::SecurityOrigin>> origins;

        RefPtr<PlatformMediaResource> resource;
    };
    DataMutex<StreamingMembers> dataMutex;
};

class CachedResourceStreamingClient final : public PlatformMediaResourceClient {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(CachedResourceStreamingClient);
    WTF_MAKE_NONCOPYABLE(CachedResourceStreamingClient);
public:
    CachedResourceStreamingClient(WebKitWebSrc*, ResourceRequest&&, unsigned requestNumber);
    virtual ~CachedResourceStreamingClient();

private:
    void checkUpdateBlocksize(unsigned bytesRead);
    void recalculateLengthAndSeekableIfNeeded(DataMutexLocker<WebKitWebSrcPrivate::StreamingMembers>&);

    // PlatformMediaResourceClient virtual methods.
    void responseReceived(PlatformMediaResource&, const ResourceResponse&, CompletionHandler<void(ShouldContinuePolicyCheck)>&&) override;
    void redirectReceived(PlatformMediaResource&, ResourceRequest&&, const ResourceResponse&, CompletionHandler<void(ResourceRequest&&)>&&) override;
    void dataReceived(PlatformMediaResource&, const SharedBuffer&) override;
    void accessControlCheckFailed(PlatformMediaResource&, const ResourceError&) override;
    void loadFailed(PlatformMediaResource&, const ResourceError&) override;
    void loadFinished(PlatformMediaResource&, const NetworkLoadMetrics&) override;

    static constexpr int s_growBlocksizeLimit { 1 };
    static constexpr int s_growBlocksizeCount { 2 };
    static constexpr int s_growBlocksizeFactor { 2 };
    static constexpr float s_reduceBlocksizeLimit { 0.5 };
    static constexpr int s_reduceBlocksizeCount { 2 };
    static constexpr float s_reduceBlocksizeFactor { 0.5 };
    int m_reduceBlocksizeCount { 0 };
    int m_increaseBlocksizeCount { 0 };
    unsigned m_requestNumber;

    GThreadSafeWeakPtr<WebKitWebSrc> m_src;
    ResourceRequest m_request;
};

enum {
    WEBKIT_WEBSRC_PROP_0,
    WEBKIT_WEBSRC_PROP_LOCATION,
    WEBKIT_WEBSRC_PROP_RESOLVED_LOCATION,
    WEBKIT_WEBSRC_PROP_KEEP_ALIVE,
    WEBKIT_WEBSRC_PROP_EXTRA_HEADERS,
    WEBKIT_WEBSRC_PROP_COMPRESS,
    WEBKIT_WEBSRC_PROP_METHOD
};

static GstStaticPadTemplate webSrcTemplate = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC(webkit_web_src_debug);
#define GST_CAT_DEFAULT webkit_web_src_debug

static void webKitWebSrcUriHandlerInit(gpointer gIface, gpointer ifaceData);

static void webKitWebSrcConstructed(GObject*);
static void webKitWebSrcSetProperty(GObject*, guint propertyID, const GValue*, GParamSpec*);
static void webKitWebSrcGetProperty(GObject*, guint propertyID, GValue*, GParamSpec*);
static GstFlowReturn webKitWebSrcCreate(GstPushSrc*, GstBuffer**);
static void webKitWebSrcMakeRequest(WebKitWebSrc*, DataMutexLocker<WebKitWebSrcPrivate::StreamingMembers>&);
static gboolean webKitWebSrcStart(GstBaseSrc*);
static gboolean webKitWebSrcStop(GstBaseSrc*);
static gboolean webKitWebSrcGetSize(GstBaseSrc*, guint64* size);
static gboolean webKitWebSrcIsSeekable(GstBaseSrc*);
static gboolean webKitWebSrcDoSeek(GstBaseSrc*, GstSegment*);
static gboolean webKitWebSrcQuery(GstBaseSrc*, GstQuery*);
static gboolean webKitWebSrcEvent(GstBaseSrc*, GstEvent*);
static gboolean webKitWebSrcUnLock(GstBaseSrc*);
static gboolean webKitWebSrcUnLockStop(GstBaseSrc*);
static void webKitWebSrcSetContext(GstElement*, GstContext*);
static void restartLoaderIfNeeded(WebKitWebSrc*, DataMutexLocker<WebKitWebSrcPrivate::StreamingMembers>&);
static void stopLoaderIfNeeded(WebKitWebSrc*, DataMutexLocker<WebKitWebSrcPrivate::StreamingMembers>&);

WEBKIT_DEFINE_TYPE_WITH_CODE(WebKitWebSrc, webkit_web_src, GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER, webKitWebSrcUriHandlerInit);
    GST_DEBUG_CATEGORY_INIT(webkit_web_src_debug, "webkitwebsrc", 0, "websrc element");
)

static void webkit_web_src_class_init(WebKitWebSrcClass* klass)
{
    GObjectClass* oklass = G_OBJECT_CLASS(klass);

    oklass->constructed = webKitWebSrcConstructed;
    oklass->set_property = webKitWebSrcSetProperty;
    oklass->get_property = webKitWebSrcGetProperty;

    GstElementClass* eklass = GST_ELEMENT_CLASS(klass);
    gst_element_class_add_static_pad_template(eklass, &webSrcTemplate);

    gst_element_class_set_metadata(eklass, "WebKit Web source element", "Source/Network", "Handles HTTP/HTTPS uris",
        "Philippe Normand <philn@igalia.com>");

    /* Allows setting the uri using the 'location' property, which is used
     * for example by gst_element_make_from_uri() */
    g_object_class_install_property(oklass, WEBKIT_WEBSRC_PROP_LOCATION,
        g_param_spec_string("location", nullptr, nullptr,
            nullptr, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(oklass, WEBKIT_WEBSRC_PROP_RESOLVED_LOCATION,
        g_param_spec_string("resolved-location", nullptr, nullptr,
            nullptr, static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(oklass, WEBKIT_WEBSRC_PROP_KEEP_ALIVE,
        g_param_spec_boolean("keep-alive", nullptr, nullptr,
            FALSE, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(oklass, WEBKIT_WEBSRC_PROP_EXTRA_HEADERS,
        g_param_spec_boxed("extra-headers", nullptr, nullptr,
            GST_TYPE_STRUCTURE, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(oklass, WEBKIT_WEBSRC_PROP_COMPRESS,
        g_param_spec_boolean("compress", nullptr, nullptr,
            FALSE, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(oklass, WEBKIT_WEBSRC_PROP_METHOD,
        g_param_spec_string("method", nullptr, nullptr,
            nullptr, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    eklass->set_context = GST_DEBUG_FUNCPTR(webKitWebSrcSetContext);

    GstBaseSrcClass* baseSrcClass = GST_BASE_SRC_CLASS(klass);
    baseSrcClass->start = GST_DEBUG_FUNCPTR(webKitWebSrcStart);
    baseSrcClass->stop = GST_DEBUG_FUNCPTR(webKitWebSrcStop);
    baseSrcClass->unlock = GST_DEBUG_FUNCPTR(webKitWebSrcUnLock);
    baseSrcClass->unlock_stop = GST_DEBUG_FUNCPTR(webKitWebSrcUnLockStop);
    baseSrcClass->get_size = GST_DEBUG_FUNCPTR(webKitWebSrcGetSize);
    baseSrcClass->is_seekable = GST_DEBUG_FUNCPTR(webKitWebSrcIsSeekable);
    baseSrcClass->do_seek = GST_DEBUG_FUNCPTR(webKitWebSrcDoSeek);
    baseSrcClass->query = GST_DEBUG_FUNCPTR(webKitWebSrcQuery);
    baseSrcClass->event = GST_DEBUG_FUNCPTR(webKitWebSrcEvent);

    GstPushSrcClass* pushSrcClass = GST_PUSH_SRC_CLASS(klass);
    pushSrcClass->create = GST_DEBUG_FUNCPTR(webKitWebSrcCreate);
}

enum class ResetType {
    Soft,
    Hard
};

static void webkitWebSrcReset([[maybe_unused]] WebKitWebSrc* src, DataMutexLocker<WebKitWebSrcPrivate::StreamingMembers>& members, ResetType resetType)
{
    GST_DEBUG_OBJECT(src, "Resetting internal state");
    gst_adapter_clear(members->adapter.get());
    members->isRequestPending = true;

    // Reset request state. Any previous request has been cancelled at this point.
    members->wasResponseReceived = false;
    members->doesHaveEOS = false;
    members->downloadStartTime = WallTime::nan();
    members->totalDownloadedBytes = 0; // Resetted for each request, used to estimate download speed.
    members->pendingHttpHeadersMessage = nullptr;
    members->pendingHttpHeadersEvent = nullptr;

    // After a flush, we have to emit a segment again.
    members->isDurationSet = false;

    // Hard reset is done during initialization and state transitions.
    // Soft reset is done during flushes. In these, we preserve the seek target.
    if (resetType == ResetType::Hard) {
        members->didPassAccessControlCheck = false;
        members->redirectedURI = CString();
        members->isSeekable = false;
        members->size = { };
        members->requestedPosition = 0;
        members->stopPosition = UINT64_MAX;
        members->readPosition = members->requestedPosition;
    }
}

static void webKitWebSrcConstructed(GObject* object)
{
    G_OBJECT_CLASS(webkit_web_src_parent_class)->constructed(object);

    WebKitWebSrc* src = WEBKIT_WEB_SRC(object);
    WebKitWebSrcPrivate* priv = src->priv;

    priv->minimumBlocksize = gst_base_src_get_blocksize(GST_BASE_SRC_CAST(src));

    DataMutexLocker members { priv->dataMutex };
    members->adapter = adoptGRef(gst_adapter_new());
    webkitWebSrcReset(src, members, ResetType::Hard);

    gst_base_src_set_automatic_eos(GST_BASE_SRC_CAST(src), FALSE);
}

static void webKitWebSrcSetProperty(GObject* object, guint propID, const GValue* value, GParamSpec* pspec)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(object);

    switch (propID) {
    case WEBKIT_WEBSRC_PROP_LOCATION:
        gst_uri_handler_set_uri(reinterpret_cast<GstURIHandler*>(src), g_value_get_string(value), nullptr);
        break;
    case WEBKIT_WEBSRC_PROP_KEEP_ALIVE:
        src->priv->keepAlive = g_value_get_boolean(value);
        break;
    case WEBKIT_WEBSRC_PROP_EXTRA_HEADERS: {
        const GstStructure* s = gst_value_get_structure(value);
        src->priv->extraHeaders.reset(s ? gst_structure_copy(s) : nullptr);
        break;
    }
    case WEBKIT_WEBSRC_PROP_COMPRESS:
        src->priv->compress = g_value_get_boolean(value);
        break;
    case WEBKIT_WEBSRC_PROP_METHOD:
        src->priv->httpMethod.reset(g_value_dup_string(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propID, pspec);
        break;
    }
}

static void webKitWebSrcGetProperty(GObject* object, guint propID, GValue* value, GParamSpec* pspec)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(object);
    WebKitWebSrcPrivate* priv = src->priv;

    switch (propID) {
    case WEBKIT_WEBSRC_PROP_LOCATION:
        g_value_set_string(value, priv->originalURI.data());
        break;
    case WEBKIT_WEBSRC_PROP_RESOLVED_LOCATION: {
        DataMutexLocker members { priv->dataMutex };
        g_value_set_string(value, members->redirectedURI.isNull() ? priv->originalURI.data() : members->redirectedURI.data());
        break;
    }
    case WEBKIT_WEBSRC_PROP_KEEP_ALIVE:
        g_value_set_boolean(value, priv->keepAlive);
        break;
    case WEBKIT_WEBSRC_PROP_EXTRA_HEADERS:
        gst_value_set_structure(value, priv->extraHeaders.get());
        break;
    case WEBKIT_WEBSRC_PROP_COMPRESS:
        g_value_set_boolean(value, priv->compress);
        break;
    case WEBKIT_WEBSRC_PROP_METHOD:
        g_value_set_string(value, priv->httpMethod.get());
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propID, pspec);
        break;
    }
}

static void webKitWebSrcSetContext(GstElement* element, GstContext* context)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(element);
    WebKitWebSrcPrivate* priv = src->priv;

    GST_DEBUG_OBJECT(src, "context type: %s", gst_context_get_context_type(context));
    if (gst_context_has_context_type(context, WEBKIT_WEB_SRC_RESOURCE_LOADER_CONTEXT_TYPE_NAME)) {
        const GValue* value = gst_structure_get_value(gst_context_get_structure(context), "loader");
        DataMutexLocker members { priv->dataMutex };
        members->loader = reinterpret_cast<WebCore::PlatformMediaResourceLoader*>(g_value_get_pointer(value));
    }
    GST_ELEMENT_CLASS(webkit_web_src_parent_class)->set_context(element, context);
}

static void restartLoaderIfNeeded(WebKitWebSrc* src, DataMutexLocker<WebKitWebSrcPrivate::StreamingMembers>& members)
{
    if (!members->isDownloadSuspended) {
        GST_TRACE_OBJECT(src, "download already active");
        return;
    }

    GST_TRACE_OBJECT(src, "is download suspended %s, does have EOS %s, does have size %s, is seekable %s, size %" G_GUINT64_FORMAT
        " (min %u)", boolForPrinting(members->isDownloadSuspended), boolForPrinting(members->doesHaveEOS), boolForPrinting(members->size.has_value())
        , boolForPrinting(members->isSeekable), members->size.value_or(-1), SMALL_MEDIA_RESOURCE_MAX_SIZE);
    if (members->doesHaveEOS || !members->size || !members->isSeekable || *members->size <= SMALL_MEDIA_RESOURCE_MAX_SIZE) {
        GST_TRACE_OBJECT(src, "download cannot be stopped/restarted");
        return;
    }
    GST_TRACE_OBJECT(src, "read position %" G_GUINT64_FORMAT ", state %s", members->readPosition, gst_element_state_get_name(GST_STATE(src)));
    if (!members->readPosition || members->readPosition == *members->size || GST_STATE(src) < GST_STATE_PAUSED) {
        GST_TRACE_OBJECT(src, "can't restart download");
        return;
    }

    size_t queueSize = gst_adapter_available(members->adapter.get());
    GST_TRACE_OBJECT(src, "queue size %zu (min %1.0f)", queueSize, *members->size * HIGH_QUEUE_FACTOR_THRESHOLD * LOW_QUEUE_FACTOR_THRESHOLD);

    if (queueSize >= *members->size * HIGH_QUEUE_FACTOR_THRESHOLD * LOW_QUEUE_FACTOR_THRESHOLD) {
        GST_TRACE_OBJECT(src, "queue size above low watermark, not restarting download");
        return;
    }

    GST_DEBUG_OBJECT(src, "restarting download");
    members->isDownloadSuspended = false;
    members->requestNumber++;
    members->requestedPosition = members->readPosition;
    webKitWebSrcMakeRequest(src, members);
}


static void stopLoaderIfNeeded([[maybe_unused]] WebKitWebSrc* src, DataMutexLocker<WebKitWebSrcPrivate::StreamingMembers>& members)
{
    ASSERT(isMainThread());

    if (members->isDownloadSuspended) {
        GST_TRACE_OBJECT(src, "download already suspended");
        return;
    }

    GST_TRACE_OBJECT(src, "is download suspended %s, does have size %s, is seekable %s, size %" G_GUINT64_FORMAT " (min %u)"
        , boolForPrinting(members->isDownloadSuspended), boolForPrinting(members->size.has_value()), boolForPrinting(members->isSeekable), members->size.value_or(-1)
        , SMALL_MEDIA_RESOURCE_MAX_SIZE);
    if (!members->size)
        return;

    if (!members->isSeekable || members->size <= SMALL_MEDIA_RESOURCE_MAX_SIZE) {
        GST_TRACE_OBJECT(src, "download cannot be stopped/restarted");
        return;
    }

    size_t queueSize = gst_adapter_available(members->adapter.get());
    GST_TRACE_OBJECT(src, "queue size %zu (max %1.0f)", queueSize, *members->size * HIGH_QUEUE_FACTOR_THRESHOLD);
    if (queueSize <= *members->size * HIGH_QUEUE_FACTOR_THRESHOLD) {
        GST_TRACE_OBJECT(src, "queue size under high watermark, not stopping download");
        return;
    }

    if (members->readPosition == *members->size) {
        GST_TRACE_OBJECT(src, "just downloaded the last chunk in the file, loadFinished() is about to be called");
        return;
    }

    GST_DEBUG_OBJECT(src, "R%u: stopping download", members->requestNumber);
    members->isDownloadSuspended = true;
    members->resource->shutdown();
}

static GstFlowReturn webKitWebSrcCreate(GstPushSrc* pushSrc, GstBuffer** buffer)
{
    ASSERT(!isMainThread());
    GstBaseSrc* baseSrc = GST_BASE_SRC_CAST(pushSrc);
    WebKitWebSrc* src = WEBKIT_WEB_SRC(baseSrc);
    WebKitWebSrcPrivate* priv = src->priv;
    DataMutexLocker members { priv->dataMutex };

    // We need members->loader to make requests. There are two mechanisms for this.
    //
    // 1) webKitWebSrcSetResourceLoader() is called by MediaPlayerPrivateGStreamer by means of hooking playbin's
    //    "source-setup" event. This doesn't work for additional WebKitWebSrc elements created by adaptivedemux.
    //
    // 2) A GstContext query made here.
    if (!members->loader) {
        members.runUnlocked([src, baseSrc]() {
            GRefPtr<GstQuery> query = adoptGRef(gst_query_new_context(WEBKIT_WEB_SRC_RESOURCE_LOADER_CONTEXT_TYPE_NAME));
            if (gst_pad_peer_query(GST_BASE_SRC_PAD(baseSrc), query.get())) {
                GstContext* context;

                gst_query_parse_context(query.get(), &context);
                gst_element_set_context(GST_ELEMENT_CAST(src), context);
            } else
                gst_element_post_message(GST_ELEMENT_CAST(src), gst_message_new_need_context(GST_OBJECT_CAST(src), WEBKIT_WEB_SRC_RESOURCE_LOADER_CONTEXT_TYPE_NAME));
        });
        if (members->isFlushing)
            return GST_FLOW_FLUSHING;
    }
    if (!members->loader) {
        GST_ERROR_OBJECT(src, "Couldn't obtain WebKitWebSrcPlayerContext, which is necessary to make network requests");
        return GST_FLOW_ERROR;
    }

    GST_TRACE_OBJECT(src, "readPosition = %" G_GUINT64_FORMAT " requestedPosition = %" G_GUINT64_FORMAT, members->readPosition, members->requestedPosition);

    if (members->isRequestPending) {
        members->isRequestPending = false;
        webKitWebSrcMakeRequest(src, members);
    }

    // Wait for the response headers.
    members->responseCondition.wait(members.mutex(), [&] () {
        return members->wasResponseReceived || members->isFlushing;
    });

    if (members->isFlushing)
        return GST_FLOW_FLUSHING;

    if (members->pendingCaps) {
        GST_DEBUG_OBJECT(src, "Setting caps: %" GST_PTR_FORMAT, members->pendingCaps.get());
        members.runUnlocked([baseSrc, caps = members->pendingCaps.leakRef()]() {
            gst_base_src_set_caps(baseSrc, caps);
        });
        if (members->isFlushing)
            return GST_FLOW_FLUSHING;
    }

    if (members->size && !members->isDurationSet) {
        GST_DEBUG_OBJECT(src, "Setting duration to %" G_GUINT64_FORMAT, *members->size);
        baseSrc->segment.duration = *members->size;
        members->isDurationSet = true;
        gst_element_post_message(GST_ELEMENT_CAST(src), gst_message_new_duration_changed(GST_OBJECT_CAST(src)));
    }

    if (members->pendingHttpHeadersMessage)
        gst_element_post_message(GST_ELEMENT(src), members->pendingHttpHeadersMessage.leakRef());
    if (members->pendingHttpHeadersEvent)
        gst_pad_push_event(GST_BASE_SRC_PAD(baseSrc), members->pendingHttpHeadersEvent.leakRef());

    restartLoaderIfNeeded(src, members);

    // We don't use the GstAdapter methods marked as fast anymore because sometimes it was slower. The reason why this was slower is that we can be
    // waiting for more "fast" (that could be retrieved with the _fast API) buffers to be available even in cases where the queue is not empty. These
    // other buffers can be retrieved with the "non _fast" API.
    GST_TRACE_OBJECT(src, "doesHaveEOS: %s, isDownloadSuspended: %s", boolForPrinting(members->doesHaveEOS), boolForPrinting(members->isDownloadSuspended));

    unsigned size = gst_base_src_get_blocksize(baseSrc);
    size_t queueSize = gst_adapter_available(members->adapter.get());
    GST_TRACE_OBJECT(src, "available bytes %" G_GSIZE_FORMAT ", block size %u", queueSize, size);
    if (!queueSize) {
        GST_TRACE_OBJECT(src, "let's wait for data or EOS");
        members->responseCondition.wait(members.mutex(), [&] {
            return members->isFlushing || gst_adapter_available(members->adapter.get()) || members->doesHaveEOS;
        });
        if (members->isFlushing)
            return GST_FLOW_FLUSHING;

        queueSize = gst_adapter_available(members->adapter.get());
        GST_TRACE_OBJECT(src, "available %" G_GSIZE_FORMAT, queueSize);
    }

    if (queueSize) {
        if (queueSize < size) {
            GST_TRACE_OBJECT(src, "Did not get the %u blocksize bytes, let's push the %" G_GSIZE_FORMAT " bytes we got", size, queueSize);
            size = queueSize;
        } else
            GST_TRACE_OBJECT(src, "Taking %u bytes from adapter", size);

        *buffer = gst_adapter_take_buffer(members->adapter.get(), size);
        RELEASE_ASSERT(*buffer);

        GST_BUFFER_OFFSET(*buffer) = baseSrc->segment.position;
        GST_BUFFER_OFFSET_END(*buffer) = GST_BUFFER_OFFSET(*buffer) + size;
        GST_TRACE_OBJECT(src, "Buffer bounds set to %" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT, GST_BUFFER_OFFSET(*buffer), GST_BUFFER_OFFSET_END(*buffer));
        GST_TRACE_OBJECT(src, "buffer size: %u, total content size: %" G_GUINT64_FORMAT, size, members->size.value_or(-1));

        restartLoaderIfNeeded(src, members);
        return GST_FLOW_OK;
    }

    // If the queue is empty reached this point, the only other option is that we are in EOS.
    ASSERT(members->doesHaveEOS);
    GST_DEBUG_OBJECT(src, "Reached the end of the response, signalling EOS");

    gst_element_post_message(GST_ELEMENT_CAST(src), gst_message_new_element(GST_OBJECT_CAST(src),
        gst_structure_new_empty("webkit-web-src-has-eos")));

    return GST_FLOW_EOS;
}

static bool webKitWebSrcSetExtraHeader(StringView fieldId, const GValue* value, ResourceRequest& request)
{
    GUniquePtr<gchar> fieldContent;

    if (G_VALUE_HOLDS_STRING(value))
        fieldContent.reset(g_value_dup_string(value));
    else {
        GValue dest = G_VALUE_INIT;

        g_value_init(&dest, G_TYPE_STRING);
        if (g_value_transform(value, &dest))
            fieldContent.reset(g_value_dup_string(&dest));
    }

    auto field = fieldId.toStringWithoutCopying();
    GST_DEBUG("Appending extra header: \"%s: %s\"", field.ascii().data(), fieldContent.get());
    request.setHTTPHeaderField(field, String::fromLatin1(fieldContent.get()));
    return true;
}

static gboolean webKitWebSrcStart(GstBaseSrc* baseSrc)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(baseSrc);

    if (src->priv->originalURI.isNull()) {
        GST_ERROR_OBJECT(src, "No URI provided");
        return FALSE;
    }
    return TRUE;
}

static void webKitWebSrcMakeRequest(WebKitWebSrc* src, DataMutexLocker<WebKitWebSrcPrivate::StreamingMembers>& members)
{
    WebKitWebSrcPrivate* priv = src->priv;

    ASSERT(!priv->originalURI.isNull());
    ASSERT(members->requestedPosition != members->stopPosition);

    GST_DEBUG_OBJECT(src, "Posting task to request R%u %s requestedPosition=%" G_GUINT64_FORMAT " stopPosition=%" G_GUINT64_FORMAT, members->requestNumber, priv->originalURI.data(), members->requestedPosition, members->stopPosition);
    URL url { String::fromLatin1(priv->originalURI.data()) };

    ResourceRequest request(WTFMove(url));
    request.setAllowCookies(true);
    request.setHTTPReferrer(members->referrer);

    if (priv->httpMethod.get())
        request.setHTTPMethod(String::fromLatin1(priv->httpMethod.get()));

#if USE(SOUP)
    // By default, HTTP Accept-Encoding is disabled here as we don't
    // want the received response to be encoded in any way as we need
    // to rely on the proper size of the returned data on
    // didReceiveResponse.
    // If Accept-Encoding is used, the server may send the data in encoded format and
    // request.expectedContentLength() will have the "wrong" size (the size of the
    // compressed data), even though the data received in didReceiveData is uncompressed.
    // This is however useful to enable for adaptive streaming
    // scenarios, when the demuxer needs to download playlists.
    if (!priv->compress)
        request.setAcceptEncoding(false);
#endif

    if (members->requestedPosition || members->stopPosition != UINT64_MAX) {
        GUniquePtr<char> formatedRange;
        if (members->stopPosition != UINT64_MAX)
            formatedRange.reset(g_strdup_printf("bytes=%" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT, members->requestedPosition, members->stopPosition > 0 ? members->stopPosition - 1 : 0));
        else
            formatedRange.reset(g_strdup_printf("bytes=%" G_GUINT64_FORMAT "-", members->requestedPosition));
        GST_DEBUG_OBJECT(src, "Range request: %s", formatedRange.get());
        request.setHTTPHeaderField(HTTPHeaderName::Range, String::fromLatin1(formatedRange.get()));
    }
    ASSERT(members->readPosition == members->requestedPosition);

    GST_DEBUG_OBJECT(src, "Persistent connection support %s", priv->keepAlive ? "enabled" : "disabled");
    if (!priv->keepAlive)
        request.setHTTPHeaderField(HTTPHeaderName::Connection, "close"_s);

    if (priv->extraHeaders) {
        gstStructureForeach(priv->extraHeaders.get(), [&](auto id, auto value) -> bool {
            auto fieldId = gstIdToString(id);
            if (G_VALUE_TYPE(value) == GST_TYPE_ARRAY) {
                unsigned size = gst_value_array_get_size(value);

                for (unsigned i = 0; i < size; i++) {
                    if (!webKitWebSrcSetExtraHeader(fieldId, gst_value_array_get_value(value, i), request))
                        return FALSE;
                }
                return TRUE;
            }

            if (G_VALUE_TYPE(value) == GST_TYPE_LIST) {
                unsigned size = gst_value_list_get_size(value);

                for (unsigned i = 0; i < size; i++) {
                    if (!webKitWebSrcSetExtraHeader(fieldId, gst_value_list_get_value(value, i), request))
                        return FALSE;
                }
                return TRUE;
            }

            return webKitWebSrcSetExtraHeader(fieldId, value, request);
        });
    }

    // We always request Icecast/Shoutcast metadata, just in case ...
    request.setHTTPHeaderField(HTTPHeaderName::IcyMetadata, "1"_s);

    ASSERT(!isMainThread());
    RunLoop::mainSingleton().dispatch([protector = WTF::ensureGRef(src), request = WTFMove(request), requestNumber = members->requestNumber] {
        WebKitWebSrcPrivate* priv = protector->priv;
        DataMutexLocker members { priv->dataMutex };
        // Ignore this task (not making any HTTP request) if by now WebKitWebSrc streaming thread is already waiting
        // for a different request. There is no point anymore in sending this one.
        if (members->requestNumber != requestNumber) {
            GST_DEBUG_OBJECT(protector.get(), "Skipping R%u, current request number is %u", requestNumber, members->requestNumber);
            return;
        }

        PlatformMediaResourceLoader::LoadOptions loadOptions = 0;
        members->resource = members->loader->requestResource(ResourceRequest(request), loadOptions);
        if (members->resource) {
            members->resource->setClient(adoptRef(*new CachedResourceStreamingClient(protector.get(), ResourceRequest(request), requestNumber)));
            GST_DEBUG_OBJECT(protector.get(), "Started request R%u", requestNumber);
        } else {
            GST_ERROR_OBJECT(protector.get(), "Failed to setup streaming client to handle R%u", requestNumber);
            members->loader = nullptr;
        }
    });
}

static gboolean webKitWebSrcStop(GstBaseSrc* baseSrc)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(baseSrc);
    // basesrc will always call unLock() and unLockStop() before calling this. See gst_base_src_stop().
    DataMutexLocker members { src->priv->dataMutex };
    webkitWebSrcReset(src, members, ResetType::Hard);
    GST_DEBUG_OBJECT(src, "Stopped WebKitWebSrc");
    return TRUE;
}

static gboolean webKitWebSrcGetSize(GstBaseSrc* baseSrc, guint64* size)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(baseSrc);
    DataMutexLocker members { src->priv->dataMutex };

    GST_DEBUG_OBJECT(src, "haveSize: %s, size: %" G_GUINT64_FORMAT, boolForPrinting(members->size.has_value()), members->size.value_or(-1));
    if (members->size) {
        *size = *members->size;
        return TRUE;
    }

    return FALSE;
}

static gboolean webKitWebSrcIsSeekable(GstBaseSrc* baseSrc)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(baseSrc);
    DataMutexLocker members { src->priv->dataMutex };
    GST_DEBUG_OBJECT(src, "isSeekable: %s", boolForPrinting(members->isSeekable));
    return members->isSeekable;
}

static gboolean webKitWebSrcDoSeek(GstBaseSrc* baseSrc, GstSegment* segment)
{
    // This function is mutually exclusive with create(). It's only called when we're transitioning to >=PAUSED,
    // continuing seamless looping, or between flushes. In any case, basesrc holds the STREAM_LOCK, so we know create() is not running.
    // Also, both webKitWebSrcUnLock() and webKitWebSrcUnLockStop() are guaranteed to be called *before* this function.
    // [See gst_base_src_perform_seek()].
    // Except for the initial seek, this function is only called if isSeekable() returns true.
    ASSERT(GST_ELEMENT(baseSrc)->current_state < GST_STATE_PAUSED || webKitWebSrcIsSeekable(baseSrc));

    WebKitWebSrc* src = WEBKIT_WEB_SRC(baseSrc);
    DataMutexLocker members { src->priv->dataMutex };

    GST_DEBUG_OBJECT(src, "Seek segment: (%" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT ") Position previous to seek: %" G_GUINT64_FORMAT, segment->start, segment->stop, members->readPosition);

    // Before attempting to seek, basesrc will call isSeekable(). If isSeekable() is true, a flush will be made and
    // this function will be called. basesrc still gives us the chance here to return FALSE and cancel the seek.
    // We cannot afford to return FALSE in this function though unless we're going to fail on purpose, since at this
    // point we have already been flushed and cancelled the HTTP request that was feeding us data.

    if (segment->rate < 0.0 || segment->format != GST_FORMAT_BYTES) {
        GST_ERROR_OBJECT(src, "Invalid seek segment");
        return FALSE;
    }

    if (members->size && segment->start >= members->size)
        GST_WARNING_OBJECT(src, "Potentially seeking behind end of file, might EOS immediately");

    members->requestedPosition = members->readPosition = segment->start;
    members->stopPosition = segment->stop;
    return TRUE;
}

static gboolean webKitWebSrcQuery(GstBaseSrc* baseSrc, GstQuery* query)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(baseSrc);
    WebKitWebSrcPrivate* priv = src->priv;
    gboolean result = FALSE;

    if (GST_QUERY_TYPE(query) == GST_QUERY_URI) {
        gst_query_set_uri(query, priv->originalURI.data());
        DataMutexLocker members { src->priv->dataMutex };
        if (!members->redirectedURI.isNull())
            gst_query_set_uri_redirection(query, members->redirectedURI.data());
        result = TRUE;
    }

    if (!result)
        result = GST_BASE_SRC_CLASS(webkit_web_src_parent_class)->query(baseSrc, query);

    if (GST_QUERY_TYPE(query) == GST_QUERY_SCHEDULING) {
        GstSchedulingFlags flags;
        int minSize, maxSize, align;

        gst_query_parse_scheduling(query, &flags, &minSize, &maxSize, &align);
        gst_query_set_scheduling(query, static_cast<GstSchedulingFlags>(flags | GST_SCHEDULING_FLAG_BANDWIDTH_LIMITED), minSize, maxSize, align);
    }

    return result;
}

static gboolean webKitWebSrcEvent(GstBaseSrc* baseSrc, GstEvent* event)
{
    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_SEEK: {
        GstSeekFlags flags;
        gst_event_parse_seek(event, nullptr, nullptr, &flags, nullptr, nullptr, nullptr, nullptr);

        if (!(flags & GST_SEEK_FLAG_FLUSH)) {
            auto src = WEBKIT_WEB_SRC(baseSrc);
            GST_DEBUG_OBJECT(src, "Non-flushing seek requested, unlocking streaming thread that might be expecting a response.");

            DataMutexLocker members { src->priv->dataMutex };
            members->isFlushing = true;
            members->responseCondition.notifyOne();
        }
        break;
    }
    default:
        break;
    };
    return GST_BASE_SRC_CLASS(webkit_web_src_parent_class)->event(baseSrc, event);
}

static gboolean webKitWebSrcUnLock(GstBaseSrc* baseSrc)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(baseSrc);
    DataMutexLocker members { src->priv->dataMutex };

    GST_DEBUG_OBJECT(src, "Unlock");
    members->isFlushing = true;

    // If we have a network resource request open, we ask the main thread to close it.
    if (members->resource) {
        GST_DEBUG_OBJECT(src, "Resource request R%u will be stopped", members->requestNumber);
        RunLoop::mainSingleton().dispatch([resource = WTFMove(members->resource), requestNumber = members->requestNumber] {
            GST_DEBUG("Stopping resource request R%u", requestNumber);
            resource->shutdown();
        });
    }
    ASSERT(!members->resource);

    if (!src->priv->keepAlive)
        members->loader = nullptr;

    // Ensure all network callbacks from the old request don't feed data to WebKitWebSrc anymore.
    members->requestNumber++;

    // Wake up streaming thread.
    members->responseCondition.notifyOne();

    return TRUE;
}

static gboolean webKitWebSrcUnLockStop(GstBaseSrc* baseSrc)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(baseSrc);
    DataMutexLocker members { src->priv->dataMutex };
    GST_DEBUG_OBJECT(src, "Unlock stop");
    members->isFlushing = false;
    webkitWebSrcReset(src, members, ResetType::Soft);

    return TRUE;
}

static bool urlHasSupportedProtocol(const URL& url)
{
    return url.isValid() && (url.protocolIsInHTTPFamily() || url.protocolIsBlob());
}

// uri handler interface

static GstURIType webKitWebSrcUriGetType(GType)
{
    return GST_URI_SRC;
}

const gchar* const* webKitWebSrcGetProtocols(GType)
{
    static std::array<const char*, 4> protocols { "http", "https", "blob" };
    return protocols.data();
}

static URL convertPlaybinURI(const char* uriString)
{
    return URL { String::fromLatin1(uriString) };
}

static gchar* webKitWebSrcGetUri(GstURIHandler* handler)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(handler);
    gchar* ret = g_strdup(src->priv->originalURI.data());
    return ret;
}

static gboolean webKitWebSrcSetUri(GstURIHandler* handler, const gchar* uri, GError** error)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(handler);
    WebKitWebSrcPrivate* priv = src->priv;

    if (GST_STATE(src) >= GST_STATE_PAUSED) {
        GST_ERROR_OBJECT(src, "URI can only be set in states < PAUSED");
        return FALSE;
    }

    priv->originalURI = CString();
    if (!uri)
        return TRUE;

    if (priv->originalURI.length()) {
        GST_ERROR_OBJECT(src, "URI can only be set in states < PAUSED");
        return FALSE;
    }

    URL url = convertPlaybinURI(uri);

    if (!urlHasSupportedProtocol(url)) {
        g_set_error(error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI, "Invalid URI '%s'", uri);
        return FALSE;
    }

    priv->originalURI = url.string().utf8();
    return TRUE;
}

static void webKitWebSrcUriHandlerInit(gpointer gIface, gpointer)
{
    GstURIHandlerInterface* iface = (GstURIHandlerInterface *) gIface;

    iface->get_type = webKitWebSrcUriGetType;
    iface->get_protocols = webKitWebSrcGetProtocols;
    iface->get_uri = webKitWebSrcGetUri;
    iface->set_uri = webKitWebSrcSetUri;
}

void webKitWebSrcSetResourceLoader(WebKitWebSrc* src, WebCore::PlatformMediaResourceLoader& loader)
{
    DataMutexLocker members { src->priv->dataMutex };
    members->loader = &loader;
}

void webKitWebSrcSetReferrer(WebKitWebSrc* src, const String& referrer)
{
    DataMutexLocker members { src->priv->dataMutex };
    members->referrer = referrer;
}

bool webKitSrcPassedCORSAccessCheck(WebKitWebSrc* src)
{
    DataMutexLocker members { src->priv->dataMutex };
    return members->didPassAccessControlCheck;
}

CachedResourceStreamingClient::CachedResourceStreamingClient(WebKitWebSrc* src, ResourceRequest&& request, unsigned requestNumber)
    : m_requestNumber(requestNumber)
    , m_src(src)
    , m_request(WTFMove(request))
{
}

CachedResourceStreamingClient::~CachedResourceStreamingClient() = default;

void CachedResourceStreamingClient::checkUpdateBlocksize(unsigned bytesRead)
{
    ASSERT(isMainThread());
    auto src = m_src.get();
    if (!src)
        return;

    GstBaseSrc* baseSrc = GST_BASE_SRC_CAST(src.get());
    WebKitWebSrcPrivate* priv = src->priv;

    unsigned blocksize = gst_base_src_get_blocksize(baseSrc);
    GST_LOG_OBJECT(src.get(), "Checking to update blocksize. Read: %u, current blocksize: %u", bytesRead, blocksize);

    if (bytesRead > blocksize * s_growBlocksizeLimit) {
        m_reduceBlocksizeCount = 0;
        m_increaseBlocksizeCount++;

        if (m_increaseBlocksizeCount >= s_growBlocksizeCount) {
            blocksize *= s_growBlocksizeFactor;
            GST_DEBUG_OBJECT(src.get(), "Increased blocksize to %u", blocksize);
            gst_base_src_set_blocksize(baseSrc, blocksize);
            m_increaseBlocksizeCount = 0;
        }
    } else if (bytesRead < blocksize * s_reduceBlocksizeLimit) {
        m_reduceBlocksizeCount++;
        m_increaseBlocksizeCount = 0;

        if (m_reduceBlocksizeCount >= s_reduceBlocksizeCount) {
            blocksize *= s_reduceBlocksizeFactor;
            blocksize = std::max(blocksize, priv->minimumBlocksize);
            GST_DEBUG_OBJECT(src.get(), "Decreased blocksize to %u", blocksize);
            gst_base_src_set_blocksize(baseSrc, blocksize);
            m_reduceBlocksizeCount = 0;
        }
    } else {
        m_reduceBlocksizeCount = 0;
        m_increaseBlocksizeCount = 0;
    }
}

void CachedResourceStreamingClient::responseReceived(PlatformMediaResource&, const ResourceResponse& response, CompletionHandler<void(ShouldContinuePolicyCheck)>&& completionHandler)
{
    ASSERT(isMainThread());
    auto src = m_src.get();
    if (!src) {
        completionHandler(ShouldContinuePolicyCheck::No);
        return;
    }

    WebKitWebSrcPrivate* priv = src->priv;
    DataMutexLocker members { priv->dataMutex };
    if (members->requestNumber != m_requestNumber) {
        completionHandler(ShouldContinuePolicyCheck::No);
        return;
    }

    GST_DEBUG_OBJECT(src.get(), "R%u: Received response: %d", m_requestNumber, response.httpStatusCode());

    members->didPassAccessControlCheck = members->resource->didPassAccessControlCheck();
    members->origins.add(SecurityOrigin::create(response.url()));

    auto responseURI = response.url().string().utf8();
    if (priv->originalURI != responseURI)
        members->redirectedURI = WTFMove(responseURI);

    // length will be zero (unknown) if no Content-Length is provided or the response is compressed with Content-Encoding.
    uint64_t length = !response.httpHeaderFields().contains(HTTPHeaderName::ContentEncoding) ? response.expectedContentLength() : 0;

    // But in some cases, Content-Encoding: chunked can specify the total length. Use it.
    std::optional<uint64_t> contentLength;
    // There's a contentLength assignment (=) on purpose, plus an implicit comparison on the variable having a value.
    if (!length && response.httpHeaderFields().contains(HTTPHeaderName::TransferEncoding)
        && equalLettersIgnoringASCIICase(response.httpHeaderField(HTTPHeaderName::TransferEncoding), "chunked"_s)
        && (contentLength = parseInteger<uint64_t>(response.httpHeaderField(HTTPHeaderName::ContentLength))))
        length = contentLength.value();

    if (length > 0 && members->requestedPosition && response.httpStatusCode() == 206)
        length += members->requestedPosition;

    GUniquePtr<GstStructure> httpHeaders(gst_structure_new_empty("http-headers"));

    gst_structure_set(httpHeaders.get(), "uri", G_TYPE_STRING, priv->originalURI.data(),
        "http-status-code", G_TYPE_UINT, response.httpStatusCode(), nullptr);
    if (!members->redirectedURI.isNull())
        gst_structure_set(httpHeaders.get(), "redirection-uri", G_TYPE_STRING, members->redirectedURI.data(), nullptr);

    // Pack request headers in the http-headers structure.
    GUniquePtr<GstStructure> headers(gst_structure_new_empty("request-headers"));
    for (const auto& header : m_request.httpHeaderFields())
        gst_structure_set(headers.get(), header.key.utf8().data(), G_TYPE_STRING, header.value.utf8().data(), nullptr);
    GST_DEBUG_OBJECT(src.get(), "R%u: Request headers going downstream: %" GST_PTR_FORMAT, m_requestNumber, headers.get());
    gst_structure_set(httpHeaders.get(), "request-headers", GST_TYPE_STRUCTURE, headers.get(), nullptr);

    // Pack response headers in the http-headers structure.
    headers.reset(gst_structure_new_empty("response-headers"));
    for (const auto& header : response.httpHeaderFields()) {
        if (auto convertedValue = parseIntegerAllowingTrailingJunk<uint64_t>(header.value))
            gst_structure_set(headers.get(), header.key.utf8().data(), G_TYPE_UINT64, *convertedValue, nullptr);
        else
            gst_structure_set(headers.get(), header.key.utf8().data(), G_TYPE_STRING, header.value.utf8().data(), nullptr);
    }
    GST_DEBUG_OBJECT(src.get(), "R%u: Response headers going downstream: %" GST_PTR_FORMAT, m_requestNumber, headers.get());
    gst_structure_set(httpHeaders.get(), "response-headers", GST_TYPE_STRUCTURE, headers.get(), nullptr);

    members->pendingHttpHeadersMessage = adoptGRef(gst_message_new_element(GST_OBJECT_CAST(src.get()), gst_structure_copy(httpHeaders.get())));
    members->pendingHttpHeadersEvent = adoptGRef(gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_STICKY, httpHeaders.release()));

    if (response.httpStatusCode() >= 400) {
        GST_ELEMENT_ERROR(src.get(), RESOURCE, READ, ("R%u: Received %d HTTP error code", m_requestNumber, response.httpStatusCode()), (nullptr));
        members->doesHaveEOS = true;
        members->responseCondition.notifyOne();
        completionHandler(ShouldContinuePolicyCheck::No);
        return;
    }

    if (members->requestedPosition) {
        // Seeking ... we expect a 206 == PARTIAL_CONTENT
        if (response.httpStatusCode() != 206) {
            // Range request completely failed.
            GST_ELEMENT_ERROR(src.get(), RESOURCE, READ, ("R%u: Received unexpected %d HTTP status code for range request", m_requestNumber, response.httpStatusCode()), (nullptr));
            members->doesHaveEOS = true;
            members->responseCondition.notifyOne();
            completionHandler(ShouldContinuePolicyCheck::No);
            return;
        }
        GST_DEBUG_OBJECT(src.get(), "R%u: Range request succeeded", m_requestNumber);
    }

    members->isSeekable = length > 0 && g_ascii_strcasecmp("none", response.httpHeaderField(HTTPHeaderName::AcceptRanges).utf8().data());

    GST_DEBUG_OBJECT(src.get(), "R%u: Size: %" G_GUINT64_FORMAT ", isSeekable: %s", m_requestNumber, length, boolForPrinting(members->isSeekable));
    if (length > 0)
        members->size = length;
    else
        members->size = { };

    // Signal to downstream if this is an Icecast stream.
    GRefPtr<GstCaps> caps;
    if (auto metadataInterval = parseIntegerAllowingTrailingJunk<int>(response.httpHeaderField(HTTPHeaderName::IcyMetaInt)); metadataInterval && *metadataInterval > 0) {
        caps = adoptGRef(gst_caps_new_simple("application/x-icy", "metadata-interval", G_TYPE_INT, *metadataInterval, nullptr));

        String contentType = response.httpHeaderField(HTTPHeaderName::ContentType);
        GST_DEBUG_OBJECT(src.get(), "R%u: Response ContentType: %s", m_requestNumber, contentType.utf8().data());
        gst_caps_set_simple(caps.get(), "content-type", G_TYPE_STRING, contentType.utf8().data(), nullptr);
    }
    if (caps) {
        GST_DEBUG_OBJECT(src.get(), "R%u: Set caps to %" GST_PTR_FORMAT, m_requestNumber, caps.get());
        members->pendingCaps = WTFMove(caps);
    }

    members->wasResponseReceived = true;
    members->responseCondition.notifyOne();

    completionHandler(ShouldContinuePolicyCheck::Yes);
}

void CachedResourceStreamingClient::redirectReceived(PlatformMediaResource&, ResourceRequest&& request, const ResourceResponse& response, CompletionHandler<void(ResourceRequest&&)>&& completionHandler)
{
    ASSERT(isMainThread());
    auto src = m_src.get();
    if (!src) {
        completionHandler(WTFMove(request));
        return;
    }
    DataMutexLocker members { src->priv->dataMutex };
    members->origins.add(SecurityOrigin::create(response.url()));
    completionHandler(WTFMove(request));
}

void CachedResourceStreamingClient::recalculateLengthAndSeekableIfNeeded(DataMutexLocker<WebKitWebSrcPrivate::StreamingMembers>& members)
{
    ASSERT(isMainThread());
    auto src = m_src.get();
    if (!src)
        return;

    if (members->isSeekable)
        return;

    if (members->size && *members->size)
        return;

    if (!members->doesHaveEOS)
        return;

    members->size = members->readPosition;
    members->isSeekable = true;

    GstBaseSrc* baseSrc = GST_BASE_SRC_CAST(src.get());
    baseSrc->segment.duration = *members->size;

    RefPtr player = src->priv->player.get();
    if (player) {
        GST_DEBUG_OBJECT(src.get(), "setting as live stream %s", boolForPrinting(!members->isSeekable));
        player->setLiveStream(!members->isSeekable);
    } else
        ASSERT_NOT_REACHED();
}

void CachedResourceStreamingClient::dataReceived(PlatformMediaResource&, const SharedBuffer& data)
{
    ASSERT(isMainThread());
    auto src = m_src.get();
    if (!src)
        return;

    WebKitWebSrcPrivate* priv = src->priv;

    DataMutexLocker members { priv->dataMutex };
    if (members->requestNumber != m_requestNumber)
        return;

    // Rough bandwidth calculation. We ignore here the first data package because we would have to reset the counters when we issue the request and
    // that first package delivery would include the time of sending out the request and getting the data back. Since we can't distinguish the
    // sending time from the receiving time, it is better to ignore it.
    if (!members->downloadStartTime.isNaN()) {
        members->totalDownloadedBytes += data.size();
#ifndef GST_DISABLE_GST_DEBUG
        double timeSinceStart = (WallTime::now() - members->downloadStartTime).seconds();
        GST_TRACE_OBJECT(src.get(), "R%u: downloaded %" G_GUINT64_FORMAT " bytes in %f seconds =~ %1.0f bytes/second", m_requestNumber, members->totalDownloadedBytes, timeSinceStart
            , timeSinceStart ? members->totalDownloadedBytes / timeSinceStart : 0);
#endif
    } else {
        members->downloadStartTime = WallTime::now();
    }

    int length = data.size();
    GST_LOG_OBJECT(src.get(), "R%u: Have %d bytes of data", m_requestNumber, length);

    members->readPosition += length;
    ASSERT(!members->size || members->readPosition <= *members->size);

    gst_element_post_message(GST_ELEMENT_CAST(src.get()), gst_message_new_element(GST_OBJECT_CAST(src.get()),
        gst_structure_new("webkit-network-statistics", "read-position", G_TYPE_UINT64, members->readPosition, "size", G_TYPE_UINT64, members->size.value_or(0), nullptr)));

    checkUpdateBlocksize(length);
    auto dataSpan = data.span();
    GstBuffer* buffer = gstBufferNewWrappedFast(fastMemDup(dataSpan.data(), dataSpan.size()), length);
    gst_adapter_push(members->adapter.get(), buffer);

    stopLoaderIfNeeded(src.get(), members);
    members->responseCondition.notifyOne();
}

void CachedResourceStreamingClient::accessControlCheckFailed(PlatformMediaResource&, const ResourceError& error)
{
    ASSERT(isMainThread());
    auto src = m_src.get();
    if (!src)
        return;

    DataMutexLocker members { src->priv->dataMutex };
    if (members->requestNumber != m_requestNumber)
        return;

    GST_ELEMENT_ERROR(src.get(), RESOURCE, READ, ("R%u: %s", m_requestNumber, error.localizedDescription().utf8().data()), (nullptr));
    members->doesHaveEOS = true;
    members->responseCondition.notifyOne();
}

void CachedResourceStreamingClient::loadFailed(PlatformMediaResource&, const ResourceError& error)
{
    ASSERT(isMainThread());
    auto src = m_src.get();
    if (!src)
        return;

    DataMutexLocker members { src->priv->dataMutex };
    if (members->requestNumber != m_requestNumber)
        return;

    if (!error.isCancellation()) {
        GST_ERROR_OBJECT(src.get(), "R%u: Have failure: %s", m_requestNumber, error.localizedDescription().utf8().data());
        GST_ELEMENT_ERROR(src.get(), RESOURCE, FAILED, ("R%u: %s", m_requestNumber, error.localizedDescription().utf8().data()), (nullptr));
    } else
        GST_LOG_OBJECT(src.get(), "R%u: Request cancelled: %s", m_requestNumber, error.localizedDescription().utf8().data());

    members->doesHaveEOS = true;
    members->responseCondition.notifyOne();
}

void CachedResourceStreamingClient::loadFinished(PlatformMediaResource&, const NetworkLoadMetrics&)
{
    ASSERT(isMainThread());
    auto src = m_src.get();
    if (!src)
        return;

    DataMutexLocker members { src->priv->dataMutex };
    if (members->requestNumber != m_requestNumber)
        return;

    GST_LOG_OBJECT(src.get(), "R%u: Load finished. Read position: %" G_GUINT64_FORMAT, m_requestNumber, members->readPosition);

    members->doesHaveEOS = true;
    recalculateLengthAndSeekableIfNeeded(members);
    members->responseCondition.notifyOne();
}

bool webKitSrcIsCrossOrigin(WebKitWebSrc* src, const SecurityOrigin& origin)
{
    DataMutexLocker members { src->priv->dataMutex };

    for (auto& responseOrigin : members->origins) {
        if (!origin.isSameOriginDomain(*responseOrigin))
            return true;
    }
    return false;
}

bool webKitSrcIsSeekable(WebKitWebSrc* src)
{
    return webKitWebSrcIsSeekable(GST_BASE_SRC(src));
}

void webKitWebSrcSetPlayer(WebKitWebSrc* src, ThreadSafeWeakPtr<WebCore::MediaPlayerPrivateGStreamer>&& player)
{
    src->priv->player = player;
}

#undef GST_CAT_DEFAULT

#endif // ENABLE(VIDEO) && USE(GSTREAMER)
