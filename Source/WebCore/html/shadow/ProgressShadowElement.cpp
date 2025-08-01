/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ProgressShadowElement.h"

#include "ContainerNodeInlines.h"
#include "HTMLNames.h"
#include "HTMLProgressElement.h"
#include "RenderProgress.h"
#include "RenderStyleInlines.h"
#include "UserAgentParts.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(ProgressShadowElement);
WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(ProgressInnerElement);
WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(ProgressBarElement);
WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(ProgressValueElement);

using namespace HTMLNames;

ProgressShadowElement::ProgressShadowElement(Document& document)
    : HTMLDivElement(HTMLNames::divTag, document)
{
}

HTMLProgressElement* ProgressShadowElement::progressElement() const
{
    return downcast<HTMLProgressElement>(shadowHost());
}

bool ProgressShadowElement::rendererIsNeeded(const RenderStyle& style)
{
    if (CheckedPtr progressRenderer = progressElement()->renderer())
        return !progressRenderer->style().hasUsedAppearance() && HTMLDivElement::rendererIsNeeded(style);
    return false;
}

ProgressInnerElement::ProgressInnerElement(Document& document)
    : ProgressShadowElement(document)
{
}

RenderPtr<RenderElement> ProgressInnerElement::createElementRenderer(RenderStyle&& style, const RenderTreePosition&)
{
    return createRenderer<RenderProgress>(*this, WTFMove(style));
}

bool ProgressInnerElement::rendererIsNeeded(const RenderStyle& style)
{
    CheckedPtr progressRenderer = progressElement()->renderer();
    return progressRenderer && !progressRenderer->style().hasUsedAppearance() && HTMLDivElement::rendererIsNeeded(style);
}

ProgressBarElement::ProgressBarElement(Document& document)
    : ProgressShadowElement(document)
{
}

ProgressValueElement::ProgressValueElement(Document& document)
    : ProgressShadowElement(document)
{
}

void ProgressValueElement::setInlineSizePercentage(double size)
{
    setInlineStyleProperty(CSSPropertyInlineSize, std::max(0.0, size), CSSUnitType::CSS_PERCENTAGE);
}

Ref<ProgressInnerElement> ProgressInnerElement::create(Document& document)
{
    Ref<ProgressInnerElement> result = adoptRef(*new ProgressInnerElement(document));
    result->setUserAgentPart(UserAgentParts::webkitProgressInnerElement());
    return result;
}

Ref<ProgressBarElement> ProgressBarElement::create(Document& document)
{
    Ref<ProgressBarElement> result = adoptRef(*new ProgressBarElement(document));
    result->setUserAgentPart(UserAgentParts::webkitProgressBar());
    return result;
}

Ref<ProgressValueElement> ProgressValueElement::create(Document& document)
{
    Ref<ProgressValueElement> result = adoptRef(*new ProgressValueElement(document));
    result->setUserAgentPart(UserAgentParts::webkitProgressValue());
    return result;
}

}
