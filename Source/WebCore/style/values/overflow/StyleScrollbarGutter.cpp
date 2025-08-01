/*
 * Copyright (C) 2023 Apple Inc. All rights reserved.
 * Copyright (C) 2025 Samuel Weinig <sam@webkit.org>
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
#include "StyleScrollbarGutter.h"

#include "StyleBuilderChecking.h"

namespace WebCore {
namespace Style {

// MARK: - Conversion

auto CSSValueConversion<ScrollbarGutter>::operator()(BuilderState& state, const CSSValue& value) -> ScrollbarGutter
{
    if (RefPtr primitiveValue = dynamicDowncast<CSSPrimitiveValue>(value)) {
        if (primitiveValue->isValueID()) {
            switch (primitiveValue->valueID()) {
            case CSSValueAuto:
                return CSS::Keyword::Auto { };
            case CSSValueStable:
                return CSS::Keyword::Stable { };
            default:
                break;
            }
        }

        state.setCurrentPropertyInvalidAtComputedValueTime();
        return CSS::Keyword::Auto { };
    }

    auto pair = requiredPairDowncast<CSSPrimitiveValue>(state, value);
    if (!pair)
        return CSS::Keyword::Auto { };

    if (isValueID(pair->first.get(), CSSValueStable) && isValueID(pair->second.get(), CSSValueBothEdges))
        return { CSS::Keyword::Stable { }, CSS::Keyword::BothEdges { } };

    state.setCurrentPropertyInvalidAtComputedValueTime();
    return CSS::Keyword::Auto { };
}

} // namespace Style
} // namespace WebCore
