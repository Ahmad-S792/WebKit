/*
 * Copyright (C) 2021-2023 Apple Inc. All rights reserved.
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

#include "GPUAutoLayoutMode.h"
#include "GPUObjectDescriptorBase.h"
#include "GPUPipelineLayout.h"
#include "WebGPUPipelineDescriptorBase.h"


namespace WebCore {

using GPULayoutMode = Variant<
    RefPtr<GPUPipelineLayout>,
    GPUAutoLayoutMode
>;

static WebGPU::PipelineLayout& convertPipelineLayoutToBacking(const GPULayoutMode& layout, const Ref<GPUPipelineLayout>& autoLayout)
{
    return *WTF::switchOn(layout, [](auto pipelineLayout) {
        return &pipelineLayout->backing();
    }, [&autoLayout](GPUAutoLayoutMode) {
        return &autoLayout->backing();
    });
}

static uint64_t nextUniqueAutogeneratedPipelineIdentifier()
{
    static uint64_t nextUniqueAutogeneratedPipelineId = 0;
    return ++nextUniqueAutogeneratedPipelineId;
}

struct GPUPipelineDescriptorBase : public GPUObjectDescriptorBase {
    WebGPU::PipelineDescriptorBase convertToBacking(const Ref<GPUPipelineLayout>& autoLayout) const
    {
        return {
            { label },
            &convertPipelineLayoutToBacking(layout, autoLayout)
        };
    }

    uint64_t uniqueAutogeneratedId() const
    {
        return WTF::switchOn(layout, [](auto) -> uint64_t {
            return 0;
        }, [this](GPUAutoLayoutMode) mutable -> uint64_t {
            if (!m_uniqueId)
                m_uniqueId = nextUniqueAutogeneratedPipelineIdentifier();
            return m_uniqueId;
        });
    }
    GPULayoutMode layout { nullptr };
private:
    mutable uint64_t m_uniqueId { 0 };
};


}
