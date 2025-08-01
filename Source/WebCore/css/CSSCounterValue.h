/*
 * Copyright (C) 2023 Apple Inc. All rights reserved.
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

#include "CSSValue.h"
#include <wtf/text/AtomString.h>

namespace WebCore {

class CSSCounterValue final : public CSSValue {
public:
    static Ref<CSSCounterValue> create(AtomString identifier, AtomString separator, Ref<CSSValue> counterStyle);

    const AtomString& identifier() const { return m_identifier; }
    const AtomString& separator() const { return m_separator; }
    Ref<CSSValue> counterStyle() const { return m_counterStyle; }
    String counterStyleCSSText() const;

    String customCSSText(const CSS::SerializationContext&) const;
    bool equals(const CSSCounterValue&) const;

    IterationStatus customVisitChildren(NOESCAPE const Function<IterationStatus(CSSValue&)>& func) const
    {
        if (func(m_counterStyle) == IterationStatus::Done)
            return IterationStatus::Done;
        return IterationStatus::Continue;
    }

private:
    CSSCounterValue(AtomString&& identifier, AtomString&& separator, Ref<CSSValue>&& counterStyle);

    AtomString m_identifier;
    AtomString m_separator;
    Ref<CSSValue> m_counterStyle;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_CSS_VALUE(CSSCounterValue, isCounter())
