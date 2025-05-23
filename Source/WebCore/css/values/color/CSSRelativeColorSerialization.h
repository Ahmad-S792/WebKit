/*
 * Copyright (C) 2024 Apple Inc. All rights reserved.
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

#include "CSSColorDescriptors.h"
#include "CSSPrimitiveNumericTypes+Serialization.h"
#include "ColorSerialization.h"
#include <optional>
#include <wtf/text/StringBuilder.h>

namespace WebCore {
namespace CSS {

// https://drafts.csswg.org/css-color-5/#serial-relative-color
template<typename RelativeColorType>
void serializationForCSSRelativeColor(StringBuilder& builder, const CSS::SerializationContext& context, const RelativeColorType& relativeColor)
{
    using Descriptor = typename RelativeColorType::Descriptor;
    using ColorType = typename Descriptor::ColorType;

    if constexpr (Descriptor::usesColorFunctionForSerialization) {
        builder.append("color(from "_s);
        serializationForCSS(builder, context, relativeColor.origin);
        builder.append(' ');
        builder.append(serialization(ColorSpaceFor<ColorType>));
    } else {
        builder.append(Descriptor::serializationFunctionName);
        builder.append("(from "_s);
        serializationForCSS(builder, context, relativeColor.origin);
    }

    auto [c1, c2, c3, alpha] = relativeColor.components;

    builder.append(' ');
    serializationForCSS(builder, context, c1);
    builder.append(' ');
    serializationForCSS(builder, context, c2);
    builder.append(' ');
    serializationForCSS(builder, context, c3);

    if (alpha) {
        builder.append(" / "_s);
        serializationForCSS(builder, context, *alpha);
    }

    builder.append(')');
}

} // namespace CSS
} // namespace WebCore
