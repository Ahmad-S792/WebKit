/*
 * Copyright (C) 2012-2025 Apple Inc. All rights reserved.
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

#pragma once

#if ENABLE(JIT)

#include "AdaptiveInferredPropertyValueWatchpointBase.h"
#include "CodeBlock.h"
#include "ObjectPropertyCondition.h"
#include "PackedCellPtr.h"
#include "Watchpoint.h"
#include <wtf/Bag.h>
#include <wtf/Noncopyable.h>
#include <wtf/TZoneMalloc.h>

namespace JSC {

class CodeBlock;
class StructureStubInfo;

class StructureStubInfoClearingWatchpoint final : public Watchpoint {
    WTF_MAKE_NONCOPYABLE(StructureStubInfoClearingWatchpoint);
    WTF_MAKE_TZONE_ALLOCATED(StructureStubInfoClearingWatchpoint);
public:
    StructureStubInfoClearingWatchpoint(CodeBlock* owner, StructureStubInfo& stubInfo)
        : Watchpoint(Watchpoint::Type::StructureStubInfoClearing)
        , m_owner(owner)
        , m_stubInfo(stubInfo)
    {
    }

    ~StructureStubInfoClearingWatchpoint();

    void fireInternal(VM&, const FireDetail&);

private:
    PackedCellPtr<CodeBlock> m_owner;
    StructureStubInfo& m_stubInfo;
};

class StructureTransitionStructureStubClearingWatchpoint final : public Watchpoint {
    WTF_MAKE_NONCOPYABLE(StructureTransitionStructureStubClearingWatchpoint);
    WTF_MAKE_TZONE_ALLOCATED(StructureTransitionStructureStubClearingWatchpoint);
public:
    StructureTransitionStructureStubClearingWatchpoint(PolymorphicAccessJITStubRoutine* owner, const ObjectPropertyCondition& key, WatchpointSet& watchpointSet)
        : Watchpoint(Watchpoint::Type::StructureTransitionStructureStubClearing)
        , m_owner(owner)
        , m_watchpointSet(watchpointSet)
        , m_key(key)
    {
    }

    void fireInternal(VM&, const FireDetail&);

private:
    PolymorphicAccessJITStubRoutine* m_owner;
    const Ref<WatchpointSet> m_watchpointSet;
    ObjectPropertyCondition m_key;
};

class AdaptiveValueStructureStubClearingWatchpoint final : public AdaptiveInferredPropertyValueWatchpointBase {
    using Base = AdaptiveInferredPropertyValueWatchpointBase;
    WTF_MAKE_NONCOPYABLE(AdaptiveValueStructureStubClearingWatchpoint);
    WTF_MAKE_TZONE_ALLOCATED(AdaptiveValueStructureStubClearingWatchpoint);

    void handleFire(VM&, const FireDetail&) final;

public:
    AdaptiveValueStructureStubClearingWatchpoint(PolymorphicAccessJITStubRoutine* owner, const ObjectPropertyCondition& key, WatchpointSet& watchpointSet)
        : Base(key)
        , m_owner(owner)
        , m_watchpointSet(watchpointSet)
    {
        RELEASE_ASSERT(key.condition().kind() == PropertyCondition::Equivalence);
    }


private:
    PolymorphicAccessJITStubRoutine* m_owner;
    const Ref<WatchpointSet> m_watchpointSet;
};

} // namespace JSC

#endif // ENABLE(JIT)
