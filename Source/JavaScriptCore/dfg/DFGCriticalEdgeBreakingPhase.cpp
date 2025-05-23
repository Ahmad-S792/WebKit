/*
 * Copyright (C) 2013-2025 Apple Inc. All rights reserved.
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
#include "DFGCriticalEdgeBreakingPhase.h"

#if ENABLE(DFG_JIT)

#include "DFGBasicBlockInlines.h"
#include "DFGBlockInsertionSet.h"
#include "DFGGraph.h"
#include "DFGPhase.h"
#include "JSCJSValueInlines.h"

namespace JSC { namespace DFG {

class CriticalEdgeBreakingPhase : public Phase {
public:
    CriticalEdgeBreakingPhase(Graph& graph)
        : Phase(graph, "critical edge breaking"_s)
        , m_insertionSet(graph)
    {
    }
    
    bool run()
    {
        Vector<BasicBlock*> newJumpPads;
        for (BlockIndex blockIndex = 0; blockIndex < m_graph.numBlocks(); ++blockIndex) {
            BasicBlock* block = m_graph.block(blockIndex);
            if (!block)
                continue;
            
            // An edge A->B is critical if A has multiple successor and B has multiple
            // predecessors. Thus we fail early if we don't have multiple successors.
            
            if (block->numSuccessors() <= 1)
                continue;

            // Break critical edges by inserting a "Jump" pad block in place of each
            // unique A->B critical edge.
            UncheckedKeyHashMap<BasicBlock*, BasicBlock*> successorPads;

            for (unsigned i = block->numSuccessors(); i--;) {
                BasicBlock** successor = &block->successor(i);
                if ((*successor)->predecessors.size() <= 1)
                    continue;

                BasicBlock* pad = nullptr;
                auto iter = successorPads.find(*successor);

                if (iter == successorPads.end()) {
                    pad = m_insertionSet.insertBefore(*successor, (*successor)->executionCount);
                    pad->appendNode(
                        m_graph, SpecNone, Jump, (*successor)->at(0)->origin, OpInfo(*successor));
                    pad->predecessors.append(block);
                    (*successor)->replacePredecessor(block, pad);
                    successorPads.set(*successor, pad);
                    newJumpPads.append(pad);
                } else
                    pad = iter->value;

                *successor = pad;
            }
        }

        bool changed = m_insertionSet.execute();
        if (changed && m_graph.m_shouldFixAvailability)
            performFixJumpPadAvailability(newJumpPads);
        return changed;
    }

    // This finalizes variable availability and Phi placement for newly inserted jump pads.
    // It is necessary after loop unrolling and critical edge breaking to ensure SSA and OSR correctness.
    void performFixJumpPadAvailability(Vector<BasicBlock*>& pads)
    {
        for (BasicBlock* pad : pads) {
            ASSERT(pad->isJumpPad());
            BasicBlock* successor = pad->successor(0);
            for (unsigned i = successor->variablesAtHead.size(); i--;) {
                Node* node = successor->variablesAtHead[i];
                if (!node)
                    continue;

                VariableAccessData* variable = node->variableAccessData();
                Node* phi = m_graph.addNode(Phi, node->origin, OpInfo(variable));
                pad->phis.append(phi);
                switch (variable->operand().kind()) {
                case OperandKind::Argument: {
                    size_t index = variable->operand().toArgument();
                    pad->variablesAtHead.atFor<OperandKind::Argument>(index) = phi;
                    pad->variablesAtTail.atFor<OperandKind::Argument>(index) = phi;
                    break;
                }
                case OperandKind::Local: {
                    size_t index = variable->operand().toLocal();
                    pad->variablesAtHead.atFor<OperandKind::Local>(index) = phi;
                    pad->variablesAtTail.atFor<OperandKind::Local>(index) = phi;
                    break;
                }
                case OperandKind::Tmp: {
                    size_t index = variable->operand().value();
                    pad->variablesAtHead.atFor<OperandKind::Tmp>(index) = phi;
                    pad->variablesAtTail.atFor<OperandKind::Tmp>(index) = phi;
                    break;
                }
                }
            }

            pad->isExcludedFromFTLCodeSizeEstimation = true;
        }
    }
private:
    BlockInsertionSet m_insertionSet;
};

bool performCriticalEdgeBreaking(Graph& graph)
{
    return runPhase<CriticalEdgeBreakingPhase>(graph);
}

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)


