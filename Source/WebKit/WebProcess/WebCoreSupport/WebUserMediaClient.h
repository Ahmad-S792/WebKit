/*
 * Copyright (C) 2014 Igalia S.L.
 * Copyright (C) 2016-2018 Apple Inc. All rights reserved.
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

#ifndef WebUserMediaClient_h
#define WebUserMediaClient_h

#if ENABLE(MEDIA_STREAM)

#include <WebCore/UserMediaClient.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakRef.h>

namespace WebKit {

class WebPage;

class WebUserMediaClient final: public WebCore::UserMediaClient, public RefCounted<WebUserMediaClient> {
    WTF_MAKE_TZONE_ALLOCATED(WebUserMediaClient);
public:

    static Ref<WebUserMediaClient> create(WebPage& page)
    {
        return adoptRef(*new WebUserMediaClient(page));
    }

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

private:
    explicit WebUserMediaClient(WebPage&);

    void requestUserMediaAccess(WebCore::UserMediaRequest&) override;
    void cancelUserMediaAccessRequest(WebCore::UserMediaRequest&) override;

    void enumerateMediaDevices(WebCore::Document&, WebCore::UserMediaClient::EnumerateDevicesCallback&&) final;

    DeviceChangeObserverToken addDeviceChangeObserver(WTF::Function<void()>&&) final;
    void removeDeviceChangeObserver(DeviceChangeObserverToken) final;
    void updateCaptureState(const WebCore::Document&, bool isActive, WebCore::MediaProducerMediaCaptureKind, CompletionHandler<void(std::optional<WebCore::Exception>&&)>&&) final;
    void setShouldListenToVoiceActivity(bool) final;

    void initializeFactories();

    WeakPtr<WebPage> m_page;
};

} // namespace WebKit

#endif // ENABLE(MEDIA_STREAM)

#endif // WebUserMediaClient_h
