/*
 * Copyright (C) 2012, 2013 Google Inc. All rights reserved.
 * Copyright (C) 2013-2024 Apple Inc. All rights reserved.
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
#include "HTMLTemplateElement.h"

#include "Document.h"
#include "DocumentFragment.h"
#include "ElementInlines.h"
#include "ElementRareData.h"
#include "HTMLNames.h"
#include "NodeInlines.h"
#include "NodeTraversal.h"
#include "SerializedNode.h"
#include "ShadowRoot.h"
#include "ShadowRootInit.h"
#include "SlotAssignmentMode.h"
#include "TemplateContentDocumentFragment.h"
#include "markup.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(HTMLTemplateElement);

using namespace HTMLNames;

inline HTMLTemplateElement::HTMLTemplateElement(const QualifiedName& tagName, Document& document)
    : HTMLElement(tagName, document, TypeFlag::HasDidMoveToNewDocument)
{
}

HTMLTemplateElement::~HTMLTemplateElement()
{
    if (m_content)
        m_content->clearHost();
}

Ref<HTMLTemplateElement> HTMLTemplateElement::create(const QualifiedName& tagName, Document& document)
{
    return adoptRef(*new HTMLTemplateElement(tagName, document));
}

DocumentFragment* HTMLTemplateElement::contentIfAvailable() const
{
    return m_content.get();
}

DocumentFragment& HTMLTemplateElement::fragmentForInsertion() const
{
    if (m_declarativeShadowRoot)
        return *m_declarativeShadowRoot;
    return content();
}

DocumentFragment& HTMLTemplateElement::content() const
{
    ASSERT(!m_declarativeShadowRoot);
    if (!m_content)
        m_content = TemplateContentDocumentFragment::create(document().ensureTemplateDocument(), *this);
    return *m_content;
}

const AtomString& HTMLTemplateElement::shadowRootMode() const
{
    static MainThreadNeverDestroyed<const AtomString> open("open"_s);
    static MainThreadNeverDestroyed<const AtomString> closed("closed"_s);

    auto modeString = attributeWithoutSynchronization(HTMLNames::shadowrootmodeAttr);
    if (equalLettersIgnoringASCIICase(modeString, "closed"_s))
        return closed;
    if (equalLettersIgnoringASCIICase(modeString, "open"_s))
        return open;
    return emptyAtom();
}

void HTMLTemplateElement::setDeclarativeShadowRoot(ShadowRoot& shadowRoot)
{
    m_declarativeShadowRoot = shadowRoot;
}

Ref<Node> HTMLTemplateElement::cloneNodeInternal(Document& document, CloningOperation type, CustomElementRegistry* registry) const
{
    RefPtr<Node> clone;
    switch (type) {
    case CloningOperation::SelfOnly:
        return cloneElementWithoutChildren(document, registry);
    case CloningOperation::SelfWithTemplateContent:
        clone = cloneElementWithoutChildren(document, registry);
        break;
    case CloningOperation::Everything:
        clone = cloneElementWithChildren(document, registry);
        break;
    }
    if (m_content) {
        auto& templateElement = downcast<HTMLTemplateElement>(*clone);
        Ref fragment = templateElement.content();
        content().cloneChildNodes(fragment->document(), nullptr, fragment);
    }
    return clone.releaseNonNull();
}

SerializedNode HTMLTemplateElement::serializeNode(CloningOperation type) const
{
    // FIXME: Implement CloningOperation::SelfWithTemplateContent and ShadowRoot serialization.
    Vector<SerializedNode> children;
    switch (type) {
    case CloningOperation::SelfOnly:
    case CloningOperation::SelfWithTemplateContent:
        break;
    case CloningOperation::Everything:
        children = serializeChildNodes();
        break;
    }

    auto attributes = this->elementData() ? WTF::map(this->attributes(), [] (const auto& attribute) {
        return SerializedNode::Element::Attribute { { attribute.name() }, attribute.value() };
    }) : Vector<SerializedNode::Element::Attribute>();

    return { SerializedNode::HTMLTemplateElement { { { WTFMove(children) }, { tagQName() }, WTFMove(attributes) } } };
}

void HTMLTemplateElement::didMoveToNewDocument(Document& oldDocument, Document& newDocument)
{
    HTMLElement::didMoveToNewDocument(oldDocument, newDocument);
    if (!m_content)
        return;
    ASSERT_WITH_SECURITY_IMPLICATION(&document() == &newDocument);
    m_content->setTreeScopeRecursively(newDocument.ensureTemplateDocument());
}

} // namespace WebCore
