/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
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

#include "APIObject.h"
#include "ITPThirdPartyDataForSpecificFirstParty.h"
#include <wtf/RunLoop.h>
#include <wtf/text/WTFString.h>

namespace API {

class ResourceLoadStatisticsFirstParty final : public ObjectImpl<Object::Type::ResourceLoadStatisticsFirstParty> {
public:
    static Ref<ResourceLoadStatisticsFirstParty> create(const WebKit::ITPThirdPartyDataForSpecificFirstParty& firstPartyData)
    {
        RELEASE_ASSERT(RunLoop::isMain());
        return adoptRef(*new ResourceLoadStatisticsFirstParty(firstPartyData));
    }

    ~ResourceLoadStatisticsFirstParty()
    {
        RELEASE_ASSERT(RunLoop::isMain());
    }

    const WTF::String& firstPartyDomain() const { return m_firstPartyData.firstPartyDomain.string(); }
    bool storageAccess() const { return m_firstPartyData.storageAccessGranted; }
    double timeLastUpdated() const { return m_firstPartyData.timeLastUpdated.value(); }

private:
    explicit ResourceLoadStatisticsFirstParty(const WebKit::ITPThirdPartyDataForSpecificFirstParty& firstPartyData)
        : m_firstPartyData(firstPartyData)
    {
    }

    const WebKit::ITPThirdPartyDataForSpecificFirstParty m_firstPartyData;
};

} // namespace API

SPECIALIZE_TYPE_TRAITS_API_OBJECT(ResourceLoadStatisticsFirstParty);
