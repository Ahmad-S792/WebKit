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

#include "AutosizeStatus.h"
#include "RenderStyleConstants.h"
#include <wtf/CheckedRef.h>
#include <wtf/OptionSet.h>

namespace WebCore {

class Document;
class Element;
class EventTarget;
class RenderStyle;
class SVGElement;
class Settings;

enum class AnimationImpact : uint8_t;

namespace Style {

class Update;

class Adjuster {
public:
    Adjuster(const Document&, const RenderStyle& parentStyle, const RenderStyle* parentBoxStyle, Element*);

    static void adjustFromBuilder(RenderStyle&);
    void adjust(RenderStyle&) const;
    void adjustAnimatedStyle(RenderStyle&, OptionSet<AnimationImpact>) const;

    static void adjustVisibilityForPseudoElement(RenderStyle&, const Element& host);
    static void adjustSVGElementStyle(RenderStyle&, const SVGElement&);
    static bool adjustEventListenerRegionTypesForRootStyle(RenderStyle&, const Document&);
    static void propagateToDocumentElementAndInitialContainingBlock(Update&, const Document&);
    static std::unique_ptr<RenderStyle> restoreUsedDocumentElementStyleToComputed(const RenderStyle&);

#if ENABLE(TEXT_AUTOSIZING)
    struct AdjustmentForTextAutosizing {
        std::optional<float> newFontSize;
        std::optional<float> newLineHeight;
        std::optional<AutosizeStatus> newStatus;
        explicit operator bool() const { return newFontSize || newLineHeight || newStatus; }
    };
    static AdjustmentForTextAutosizing adjustmentForTextAutosizing(const RenderStyle&, const Element&);
    static bool adjustForTextAutosizing(RenderStyle&, AdjustmentForTextAutosizing);
    static bool adjustForTextAutosizing(RenderStyle&, const Element&);
#endif

private:
    void adjustDisplayContentsStyle(RenderStyle&) const;
    void adjustForSiteSpecificQuirks(RenderStyle&) const;

    void adjustThemeStyle(RenderStyle&, const RenderStyle& parentStyle) const;

    static OptionSet<EventListenerRegionType> computeEventListenerRegionTypes(const Document&, const RenderStyle&, const EventTarget&, OptionSet<EventListenerRegionType>);

    CheckedRef<const Document> m_document;
    const RenderStyle& m_parentStyle;
    const RenderStyle& m_parentBoxStyle;
    RefPtr<Element> m_element;
};

}
}
