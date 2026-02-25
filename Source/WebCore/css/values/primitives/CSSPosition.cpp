/*
 * Copyright (C) 2024-2025 Samuel Weinig <sam@webkit.org>
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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "CSSPosition.h"

#include "CSSPrimitiveNumericTypes+Serialization.h"
#include <wtf/text/StringBuilder.h>

namespace WebCore {
namespace CSS {

bool isCenterPosition(const Position& position)
{
    auto isCenter = [](const auto& component) {
        return WTF::switchOn(component.offset,
            [](auto)            { return false; },
            [](Keyword::Center) { return true;  },
            [](const LengthPercentage<>& value) {
                return WTF::switchOn(value,
                    [](const LengthPercentage<>::Raw& raw) { return raw == 50_css_percentage; },
                    [](const LengthPercentage<>::Calc&) { return false; }
                );
            }
        );
    };

    return WTF::switchOn(position,
        [&](const TwoComponentPositionHorizontalVertical& components) {
            return isCenter(get<0>(components)) && isCenter(get<1>(components));
        },
        [&](const auto&) {
            return false;
        }
    );
}

static auto toTwoComponent(const ThreeComponentPositionHorizontal& component) -> TwoComponentPositionHorizontal
{
    return WTF::switchOn(component.offset, [](auto keyword) -> TwoComponentPositionHorizontal { return { keyword }; });
}

static auto toTwoComponent(const ThreeComponentPositionVertical& component) -> TwoComponentPositionVertical
{
    return WTF::switchOn(component.offset, [](auto keyword) -> TwoComponentPositionVertical { return { keyword }; });
}

std::pair<CSS::PositionX, CSS::PositionY> split(CSS::Position&& position)
{
    // CSS::PositionX and CSS::PositionY don't utilize the three component variants, so the
    // non-length containing item must be converted to its two component variant.

    return WTF::switchOn(WTF::move(position),
        [&](const auto& components) -> std::pair<CSS::PositionX, CSS::PositionY> {
            return { CSS::PositionX(get<0>(components)), CSS::PositionY(get<1>(components)) };
        },
        [&](const ThreeComponentPositionHorizontalVerticalLengthFirst& components) -> std::pair<CSS::PositionX, CSS::PositionY> {
            return { CSS::PositionX(get<0>(components)), CSS::PositionY(toTwoComponent(get<1>(components))) };
        },
        [&](const ThreeComponentPositionHorizontalVerticalLengthSecond& components) -> std::pair<CSS::PositionX, CSS::PositionY> {
            return { CSS::PositionX(toTwoComponent(get<0>(components))), CSS::PositionY(get<1>(components)) };
        }
    );
}

static void serializeHorizontalComponentAsPercentage(StringBuilder& builder, const SerializationContext& context, const TwoComponentPositionHorizontal& component)
{
    WTF::switchOn(component.offset,
        [&](Keyword::Left)    { serializationForCSS(builder, context, LengthPercentage<>::Raw { 0_css_percentage }); },
        [&](Keyword::Center)  { serializationForCSS(builder, context, LengthPercentage<>::Raw { 50_css_percentage }); },
        [&](Keyword::Right)   { serializationForCSS(builder, context, LengthPercentage<>::Raw { 100_css_percentage }); },
        [&](Keyword::XStart)  { serializationForCSS(builder, context, LengthPercentage<>::Raw { 0_css_percentage }); },
        [&](Keyword::XEnd)    { serializationForCSS(builder, context, LengthPercentage<>::Raw { 100_css_percentage }); },
        [&](const LengthPercentage<>& value) { serializationForCSS(builder, context, value); }
    );
}

static void serializeVerticalComponentAsPercentage(StringBuilder& builder, const SerializationContext& context, const TwoComponentPositionVertical& component)
{
    WTF::switchOn(component.offset,
        [&](Keyword::Top)     { serializationForCSS(builder, context, LengthPercentage<>::Raw { 0_css_percentage }); },
        [&](Keyword::Center)  { serializationForCSS(builder, context, LengthPercentage<>::Raw { 50_css_percentage }); },
        [&](Keyword::Bottom)  { serializationForCSS(builder, context, LengthPercentage<>::Raw { 100_css_percentage }); },
        [&](Keyword::YStart)  { serializationForCSS(builder, context, LengthPercentage<>::Raw { 0_css_percentage }); },
        [&](Keyword::YEnd)    { serializationForCSS(builder, context, LengthPercentage<>::Raw { 100_css_percentage }); },
        [&](const LengthPercentage<>& value) { serializationForCSS(builder, context, value); }
    );
}

void serializePositionAsPercentages(StringBuilder& builder, const SerializationContext& context, const Position& position)
{
    WTF::switchOn(position,
        [&](const TwoComponentPositionHorizontalVertical& components) {
            serializeHorizontalComponentAsPercentage(builder, context, get<0>(components));
            builder.append(' ');
            serializeVerticalComponentAsPercentage(builder, context, get<1>(components));
        },
        [&](const auto& components) {
            // For three and four component positions, fall back to generic serialization
            // as they already use explicit offsets from edges.
            serializationForCSS(builder, context, components);
        }
    );
}

} // namespace CSS
} // namespace WebCore
