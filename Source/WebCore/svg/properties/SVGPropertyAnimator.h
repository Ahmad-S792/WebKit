/*
 * Copyright (C) 2019-2024 Apple Inc. All rights reserved.
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

#include "CSSPropertyParser.h"
#include "CSSSerializationContext.h"
#include "SVGAttributeAnimator.h"
#include "SVGElement.h"
#include "StyleExtractor.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {
    
template<typename AnimationFunction>
class SVGPropertyAnimator : public SVGAttributeAnimator {
    WTF_MAKE_TZONE_ALLOCATED_TEMPLATE(SVGPropertyAnimator);
public:
    bool isDiscrete() const override { return m_function.isDiscrete(); }

    void setFromAndToValues(SVGElement& targetElement, const String& from, const String& to) override
    {
        m_function.setFromAndToValues(targetElement, adjustForInheritance(targetElement, from), adjustForInheritance(targetElement, to));
    }

    void setFromAndByValues(SVGElement& targetElement, const String& from, const String& by) override
    {
        m_function.setFromAndByValues(targetElement, from, by);
    }

    void setToAtEndOfDurationValue(const String& toAtEndOfDuration) override
    {
        m_function.setToAtEndOfDurationValue(toAtEndOfDuration);
    }

protected:
    template<typename... Arguments>
    SVGPropertyAnimator(const QualifiedName& attributeName, Arguments&&... arguments)
        : SVGAttributeAnimator(attributeName)
        , m_function(std::forward<Arguments>(arguments)...)
    {
    }

    void stop(SVGElement& targetElement) override
    {
        removeAnimatedStyleProperty(targetElement);
    }

    std::optional<float> calculateDistance(SVGElement& targetElement, const String& from, const String& to) const override
    {
        return m_function.calculateDistance(targetElement, from, to);
    }

    String adjustForInheritance(SVGElement& targetElement, const String& value) const
    {
        static MainThreadNeverDestroyed<const AtomString> inherit("inherit"_s);
        return value == inherit ? computeInheritedCSSPropertyValue(targetElement) : value;
    }

    String computeCSSPropertyValue(SVGElement& targetElement, CSSPropertyID id) const
    {
        Ref protector = targetElement;

        // Don't include any properties resulting from CSS Transitions/Animations or SMIL animations, as we want to retrieve the "base value".
        targetElement.setUseOverrideComputedStyle(true);
        auto serialization = Style::Extractor(&targetElement).propertyValueSerialization(id, CSS::defaultSerializationContext());
        targetElement.setUseOverrideComputedStyle(false);

        return serialization;
    }

    String computeInheritedCSSPropertyValue(SVGElement& targetElement) const
    {
        RefPtr svgParent = dynamicDowncast<SVGElement>(targetElement.parentElement());
        if (!svgParent)
            return emptyString();
        return computeCSSPropertyValue(*svgParent, cssPropertyID(m_attributeName.localName()));
    }

    AnimationFunction m_function;
};

WTF_MAKE_TZONE_ALLOCATED_TEMPLATE_IMPL(template<typename AnimationFunction>, SVGPropertyAnimator<AnimationFunction>);

} // namespace WebCore
