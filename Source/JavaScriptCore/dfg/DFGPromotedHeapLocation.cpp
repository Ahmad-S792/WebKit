/*
 * Copyright (C) 2014-2016 Apple Inc. All rights reserved.
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
#include "DFGPromotedHeapLocation.h"

#if ENABLE(DFG_JIT)

#include "DFGGraph.h"
#include "JSCJSValueInlines.h"

namespace JSC { namespace DFG {

void PromotedLocationDescriptor::dump(PrintStream& out) const
{
    out.print(m_kind, "(", m_info, ")");
}

Node* PromotedHeapLocation::createHint(Graph& graph, NodeOrigin origin, Node* value)
{
    return graph.addNode(
        SpecNone, PutHint, origin, OpInfo(descriptor().imm1()), OpInfo(descriptor().imm2()),
        base()->defaultEdge(), value->defaultEdge());
}

void PromotedHeapLocation::dump(PrintStream& out) const
{
    out.print(kind(), "(", m_base, ", ", info(), ")");
}

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)

