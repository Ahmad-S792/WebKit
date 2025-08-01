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

#include "CSSColor.h"

namespace WebCore {

class Color;

namespace CSS {

struct PlatformColorResolutionState;

enum class LightDarkColorAppearance : bool { Light, Dark };

struct LightDarkColor {
    WTF_DEPRECATED_MAKE_STRUCT_FAST_ALLOCATED(LightDarkColor);

    Color lightColor;
    Color darkColor;

    bool operator==(const LightDarkColor&) const;
};

WebCore::Color createColor(const LightDarkColor&, PlatformColorResolutionState&);
bool containsCurrentColor(const LightDarkColor&);

constexpr bool containsColorSchemeDependentColor(const LightDarkColor&)
{
    return true;
}

template<> struct Serialize<LightDarkColor> { void operator()(StringBuilder&, const SerializationContext&, const LightDarkColor&); };
template<> struct ComputedStyleDependenciesCollector<LightDarkColor> { void operator()(ComputedStyleDependencies&, const LightDarkColor&); };
template<> struct CSSValueChildrenVisitor<LightDarkColor> { IterationStatus operator()(NOESCAPE const Function<IterationStatus(CSSValue&)>&, const LightDarkColor&); };

} // namespace CSS
} // namespace WebCore
