/*
 * Copyright (C) 2013-2015 Apple Inc. All rights reserved.
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
#include "DFGBackwardsPropagationPhase.h"

#if ENABLE(DFG_JIT)

#include "DFGBlockMapInlines.h"
#include "DFGGraph.h"
#include "DFGPhase.h"
#include "JSCJSValueInlines.h"
#include <wtf/MathExtras.h>

namespace JSC { namespace DFG {

// This phase is run at the end of BytecodeParsing, so the graph isn't in a fully formed state.
// For example, we can't access the predecessor list of any basic blocks yet.
//
// Note that, so far, this phase should only be used in the byte code parsing phase
// or after the fix up phases. We don't want to validate graph since
// unreachable blocks won't be removed until the end of the parsing phase.
class BackwardsPropagationPhase : public Phase {
public:
    BackwardsPropagationPhase(Graph& graph)
        : Phase(graph, "backwards propagation"_s, !graph.afterFixup())
        , m_flagsAtHead(graph)
    {
    }

    bool run()
    {
        for (BasicBlock* block : m_graph.blocksInNaturalOrder()) {
            m_flagsAtHead[block] = Operands<NodeFlags>(OperandsLike, m_graph.block(0)->variablesAtHead);
            m_flagsAtHead[block].fill(0);
        }

        bool changed;
        do {
            changed = false;

            for (BlockIndex blockIndex = m_graph.numBlocks(); blockIndex--;) {
                BasicBlock* block = m_graph.block(blockIndex);
                if (!block)
                    continue;

                {
                    unsigned numSuccessors = block->numSuccessors();
                    if (!numSuccessors) {
                        m_currentFlags = Operands<NodeFlags>(OperandsLike, m_graph.block(0)->variablesAtHead);
                        m_currentFlags.fill(0);
                    } else {
                        m_currentFlags = m_flagsAtHead[block->successor(0)];
                        for (unsigned i = 1; i < numSuccessors; ++i) {
                            BasicBlock* successor = block->successor(i);
                            for (size_t i = 0; i < m_currentFlags.size(); ++i)
                                m_currentFlags[i] |= m_flagsAtHead[successor][i];
                        }
                    }
                }

            
                // Prevent a tower of overflowing additions from creating a value that is out of the
                // safe 2^48 range.
                m_allowNestedOverflowingAdditions = block->size() < (1 << 16);
            
                for (unsigned indexInBlock = block->size(); indexInBlock--;)
                    propagate(block->at(indexInBlock));

                if (m_flagsAtHead[block] != m_currentFlags) {
                    m_flagsAtHead[block] = m_currentFlags;
                    changed = true;
                }
            }
        } while (changed);
        
        return true;
    }

private:
    bool isNotNegZero(Node* node, unsigned timeToLive = 3)
    {
        if (!timeToLive)
            return false;

        switch (node->op()) {
        case DoubleConstant:
        case JSConstant:
        case Int52Constant: {
            if (!node->isNumberConstant())
                return false;
            double value = node->asNumber();
            return (value || 1.0 / value > 0.0);
        }

        case ValueBitAnd:
        case ValueBitOr:
        case ValueBitXor:
        case ValueBitLShift:
        case ValueBitRShift:
        case ValueBitURShift:
        case ArithBitAnd:
        case ArithBitOr:
        case ArithBitXor:
        case ArithBitLShift:
        case ArithBitRShift:
        case ArithBitURShift: {
            return true;
        }

        case ValueAdd:
        case ArithAdd: {
            if (isNotNegZero(node->child1().node(), timeToLive - 1) || isNotNegZero(node->child2().node(), timeToLive - 1))
                return true;
            return false;
        }

        case Int52Rep:
            // Do not decrease timeToLive since it is just propagating to the caller (not increasing the leaves of the tree).
            return isNotNegZero(node->child1().node(), timeToLive);

        default:
            return false;
        }
    }

    bool isNotPosZero(Node* node)
    {
        if (!node->isNumberConstant())
            return false;
        double value = node->asNumber();
        return (value || 1.0 / value < 0.0);
    }

    // Tests if the absolute value is strictly less than the power of two.
    template<int power>
    bool isWithinPowerOfTwoForConstant(Node* node)
    {
        JSValue immediateValue = node->asJSValue();
        if (!immediateValue.isNumber())
            return false;
        double immediate = immediateValue.asNumber();
        return immediate > -(static_cast<int64_t>(1) << power) && immediate < (static_cast<int64_t>(1) << power);
    }
    
    template<int power>
    bool isWithinPowerOfTwoNonRecursive(Node* node)
    {
        if (!node->isNumberConstant())
            return false;
        return isWithinPowerOfTwoForConstant<power>(node);
    }
    
    template<int power>
    bool isWithinPowerOfTwo(Node* node)
    {
        switch (node->op()) {
        case DoubleConstant:
        case JSConstant:
        case Int52Constant: {
            return isWithinPowerOfTwoForConstant<power>(node);
        }
            
        case ValueBitAnd:
        case ArithBitAnd: {
            if (power > 31)
                return true;
            
            return isWithinPowerOfTwoNonRecursive<power>(node->child1().node())
                || isWithinPowerOfTwoNonRecursive<power>(node->child2().node());
        }
            
        case ArithBitOr:
        case ArithBitXor:
        case ValueBitOr:
        case ValueBitXor:
        case ValueBitLShift:
        case ArithBitLShift: {
            return power > 31;
        }
            
        case ArithBitRShift:
        case ValueBitRShift:
        case ArithBitURShift:
        case ValueBitURShift: {
            if (power > 31)
                return true;
            
            Node* shiftAmount = node->child2().node();
            if (!node->isNumberConstant())
                return false;
            JSValue immediateValue = shiftAmount->asJSValue();
            if (!immediateValue.isInt32())
                return false;
            return immediateValue.asInt32() > 32 - power;
        }
            
        default:
            return false;
        }
    }

    template<int power>
    bool isWithinPowerOfTwo(Edge edge)
    {
        return isWithinPowerOfTwo<power>(edge.node());
    }

    static bool mergeFlags(NodeFlags& flagsRef, NodeFlags newFlags)
    {
        return checkAndSet(flagsRef, flagsRef | newFlags);
    }

    bool mergeDefaultFlags(Node* node)
    {
        bool changed = false;
        if (node->flags() & NodeHasVarArgs) {
            for (unsigned childIdx = node->firstChild();
                childIdx < node->firstChild() + node->numChildren();
                childIdx++) {
                if (!!m_graph.m_varArgChildren[childIdx])
                    changed |= m_graph.m_varArgChildren[childIdx]->mergeFlags(NodeBytecodeUsesAsValue);
            }
        } else {
            if (!node->child1())
                return changed;
            changed |= node->child1()->mergeFlags(NodeBytecodeUsesAsValue);
            if (!node->child2())
                return changed;
            changed |= node->child2()->mergeFlags(NodeBytecodeUsesAsValue);
            if (!node->child3())
                return changed;
            changed |= node->child3()->mergeFlags(NodeBytecodeUsesAsValue);
        }
        return changed;
    }
    
    static constexpr NodeFlags VariableIsUsed = 1 << (1 + WTF::getMSBSetConstexpr(NodeBytecodeBackPropMask));
    static_assert(!(VariableIsUsed & NodeBytecodeBackPropMask));
    static_assert(VariableIsUsed > NodeBytecodeBackPropMask, "Verify the above doesn't overflow");
    
    void propagate(Node* node)
    {
        NodeFlags flags = node->flags() & NodeBytecodeBackPropMask;
        
        switch (node->op()) {
        case GetLocal: {
            VariableAccessData* variableAccessData = node->variableAccessData();
            flags |= m_currentFlags.operand(variableAccessData->operand());
            flags |= VariableIsUsed;
            m_currentFlags.operand(variableAccessData->operand()) = flags;
            break;
        }
            
        case SetLocal: {
            VariableAccessData* variableAccessData = node->variableAccessData();

            Operand operand = variableAccessData->operand();
            NodeFlags flags = m_currentFlags.operand(operand);
            if (!(flags & VariableIsUsed))
                break;

            flags &= NodeBytecodeBackPropMask;
            flags &= ~NodeBytecodeUsesAsInt; // We don't care about cross-block uses-as-int.

            variableAccessData->mergeFlags(flags);
            // We union with NodeBytecodeUsesAsNumber to account for the fact that control flow may cause overflows that our modeling can't handle.
            // For example, a loop where we always add a constant value.
            node->child1()->mergeFlags(flags | NodeBytecodeUsesAsNumber); 

            m_currentFlags.operand(operand) = 0;
            break;
        }
            
        case Flush: {
            VariableAccessData* variableAccessData = node->variableAccessData();
            mergeFlags(m_currentFlags.operand(variableAccessData->operand()), NodeBytecodeUsesAsValue | VariableIsUsed);
            break;
        }

        case PhantomLocal: {
            VariableAccessData* variableAccessData = node->variableAccessData();
            mergeFlags(m_currentFlags.operand(variableAccessData->operand()), VariableIsUsed);
            break;
        }
            
        case MovHint:
        case Check:
        case CheckVarargs:
            break;
            
        case ValueBitNot:
        case ArithBitNot: {
            flags |= NodeBytecodeUsesAsInt;
            flags &= ~(NodeBytecodeUsesAsNumber | NodeBytecodeNeedsNegZero | NodeBytecodeNeedsNaNOrInfinity | NodeBytecodeUsesAsOther);
            flags &= ~NodeBytecodePrefersArrayIndex;
            node->child1()->mergeFlags(flags);
            break;
        }

        case ArithBitAnd:
        case ArithBitOr:
        case ArithBitXor:
        case ValueBitAnd:
        case ValueBitOr:
        case ValueBitXor:
        case ValueBitLShift:
        case ArithBitLShift:
        case ArithBitRShift:
        case ValueBitRShift:
        case ArithBitURShift:
        case ValueBitURShift:
        case ArithIMul: {
            flags |= NodeBytecodeUsesAsInt;
            flags &= ~(NodeBytecodeUsesAsNumber | NodeBytecodeNeedsNegZero | NodeBytecodeNeedsNaNOrInfinity | NodeBytecodeUsesAsOther);
            flags &= ~NodeBytecodePrefersArrayIndex;
            node->child1()->mergeFlags(flags);
            node->child2()->mergeFlags(flags);
            break;
        }
            
        case StringAt:
        case StringCharAt:
        case StringCharCodeAt:
        case StringCodePointAt: {
            node->child1()->mergeFlags(NodeBytecodeUsesAsValue);
            node->child2()->mergeFlags(NodeBytecodeUsesAsValue | NodeBytecodeUsesAsInt | NodeBytecodePrefersArrayIndex);
            break;
        }

        case StringIndexOf: {
            node->child1()->mergeFlags(NodeBytecodeUsesAsValue);
            node->child2()->mergeFlags(NodeBytecodeUsesAsValue);
            if (node->child3())
                node->child3()->mergeFlags(NodeBytecodeUsesAsValue | NodeBytecodeUsesAsInt | NodeBytecodePrefersArrayIndex);
            break;
        }

        case StringSlice:
        case StringSubstring: {
            node->child1()->mergeFlags(NodeBytecodeUsesAsValue);
            node->child2()->mergeFlags(NodeBytecodeUsesAsArrayIndex);
            if (node->child3())
                node->child3()->mergeFlags(NodeBytecodeUsesAsArrayIndex);
            break;
        }

        case ArraySlice: {
            m_graph.varArgChild(node, 0)->mergeFlags(NodeBytecodeUsesAsValue);

            if (node->numChildren() == 2)
                m_graph.varArgChild(node, 1)->mergeFlags(NodeBytecodeUsesAsValue);
            else if (node->numChildren() == 3) {
                m_graph.varArgChild(node, 1)->mergeFlags(NodeBytecodeUsesAsArrayIndex);
                m_graph.varArgChild(node, 2)->mergeFlags(NodeBytecodeUsesAsValue);
            } else if (node->numChildren() == 4) {
                m_graph.varArgChild(node, 1)->mergeFlags(NodeBytecodeUsesAsArrayIndex);
                m_graph.varArgChild(node, 2)->mergeFlags(NodeBytecodeUsesAsArrayIndex);
                m_graph.varArgChild(node, 3)->mergeFlags(NodeBytecodeUsesAsValue);
            }
            break;
        }

            
        case UInt32ToNumber: {
            node->child1()->mergeFlags(flags);
            break;
        }

        case ValueAdd: {
            if (isNotNegZero(node->child1().node()) || isNotNegZero(node->child2().node()))
                flags &= ~NodeBytecodeNeedsNegZero;
            if (node->child1()->hasNumericResult() || node->child2()->hasNumericResult() || node->child1()->hasNumberResult() || node->child2()->hasNumberResult())
                flags &= ~NodeBytecodeUsesAsOther;
            if (!isWithinPowerOfTwo<32>(node->child1()) && !isWithinPowerOfTwo<32>(node->child2()))
                flags |= NodeBytecodeUsesAsNumber;
            if (!m_allowNestedOverflowingAdditions)
                flags |= NodeBytecodeUsesAsNumber;
            flags |= NodeBytecodeNeedsNaNOrInfinity;
            
            node->child1()->mergeFlags(flags);
            node->child2()->mergeFlags(flags);
            break;
        }

        case ArithAdd: {
            flags &= ~NodeBytecodeUsesAsOther;
            if (isNotNegZero(node->child1().node()) || isNotNegZero(node->child2().node()))
                flags &= ~NodeBytecodeNeedsNegZero;
            if (!isWithinPowerOfTwo<32>(node->child1()) && !isWithinPowerOfTwo<32>(node->child2()))
                flags |= NodeBytecodeUsesAsNumber;
            if (!m_allowNestedOverflowingAdditions)
                flags |= NodeBytecodeUsesAsNumber;
            flags |= NodeBytecodeNeedsNaNOrInfinity;
            
            node->child1()->mergeFlags(flags);
            node->child2()->mergeFlags(flags);
            break;
        }

        case ArithClz32: {
            flags &= ~(NodeBytecodeUsesAsNumber | NodeBytecodeNeedsNegZero | NodeBytecodeNeedsNaNOrInfinity | NodeBytecodeUsesAsOther | NodeBytecodePrefersArrayIndex);
            flags |= NodeBytecodeUsesAsInt;
            node->child1()->mergeFlags(flags);
            break;
        }

        case ArithSub: {
            flags &= ~NodeBytecodeUsesAsOther;
            if (isNotNegZero(node->child1().node()) || isNotPosZero(node->child2().node()))
                flags &= ~NodeBytecodeNeedsNegZero;
            if (!isWithinPowerOfTwo<32>(node->child1()) && !isWithinPowerOfTwo<32>(node->child2()))
                flags |= NodeBytecodeUsesAsNumber;
            if (!m_allowNestedOverflowingAdditions)
                flags |= NodeBytecodeUsesAsNumber;
            flags |= NodeBytecodeNeedsNaNOrInfinity;
            
            node->child1()->mergeFlags(flags);
            node->child2()->mergeFlags(flags);
            break;
        }
            
        case ArithNegate: {
            // negation does not care about NaN, Infinity, -Infinity are converted into 0 if the result is evaluated under the integer context.
            flags &= ~NodeBytecodeUsesAsOther;

            node->child1()->mergeFlags(flags);
            break;
        }

        case Inc:
        case Dec: {
            flags &= ~NodeBytecodeNeedsNegZero;
            flags &= ~NodeBytecodeUsesAsOther;
            if (!isWithinPowerOfTwo<32>(node->child1()))
                flags |= NodeBytecodeUsesAsNumber;
            if (!m_allowNestedOverflowingAdditions)
                flags |= NodeBytecodeUsesAsNumber;
            flags |= NodeBytecodeNeedsNaNOrInfinity;

            node->child1()->mergeFlags(flags);
            break;
        }

        case ValueMul:
        case ArithMul: {
            // As soon as a multiply happens, we can easily end up in the part
            // of the double domain where the point at which you do truncation
            // can change the outcome. So, ArithMul always forces its inputs to
            // check for overflow. Additionally, it will have to check for overflow
            // itself unless we can prove that there is no way for the values
            // produced to cause double rounding.
            
            if (!isWithinPowerOfTwo<22>(node->child1().node())
                && !isWithinPowerOfTwo<22>(node->child2().node()))
                flags |= NodeBytecodeUsesAsNumber;
            
            node->mergeFlags(flags);
            
            flags |= NodeBytecodeUsesAsNumber | NodeBytecodeNeedsNegZero | NodeBytecodeNeedsNaNOrInfinity;
            flags &= ~NodeBytecodeUsesAsOther;

            node->child1()->mergeFlags(flags);
            node->child2()->mergeFlags(flags);
            break;
        }
            
        case ValueDiv:
        case ArithDiv: {
            // ArithDiv / ValueDiv need to have NodeBytecodeUsesAsNumber even if it is used in the context of integer.
            // For example,
            //     ((@x / @y) + @z) | 0
            // In this context, (@x / @y) can have integer context at first, but the result can be different if div
            // generates NaN. Div and Mod are operations that can produce NaN / Infinity though only taking binary Int32 operands.
            // Thus, we always need to check for overflow since it can affect downstream calculations.
            flags |= NodeBytecodeUsesAsNumber | NodeBytecodeNeedsNegZero | NodeBytecodeNeedsNaNOrInfinity;
            flags &= ~NodeBytecodeUsesAsOther;

            node->child1()->mergeFlags(flags);
            node->child2()->mergeFlags(flags);
            break;
        }
            
        case ValueMod:
        case ArithMod: {
            flags |= NodeBytecodeUsesAsNumber | NodeBytecodeNeedsNegZero | NodeBytecodeNeedsNaNOrInfinity;
            flags &= ~NodeBytecodeUsesAsOther;

            node->child1()->mergeFlags(flags);
            node->child2()->mergeFlags(flags & ~NodeBytecodeNeedsNegZero);
            break;
        }

        case MultiGetByVal:
        case EnumeratorGetByVal:
        case GetByVal:
        case GetByValMegamorphic: {
            m_graph.varArgChild(node, 0)->mergeFlags(NodeBytecodeUsesAsValue);
            m_graph.varArgChild(node, 1)->mergeFlags(NodeBytecodeUsesAsArrayIndex);
            break;
        }
            
        case NewTypedArray:
        case NewTypedArrayBuffer:
        case NewArrayWithSize:
        case NewArrayWithConstantSize:
        case NewArrayWithSpecies:
        case NewArrayWithSizeAndStructure: {
            // Negative zero is not observable. NaN versus undefined are only observable
            // in that you would get a different exception message. So, like, whatever: we
            // claim here that NaN v. undefined is observable.
            node->child1()->mergeFlags(NodeBytecodeUsesAsArrayIndex);
            break;
        }
            
        case ToString:
        case CallStringConstructor: {
            if (typeFilterFor(node->child1().useKind()) & SpecOther)
                node->child1()->mergeFlags(NodeBytecodeUsesAsOther);
            node->child1()->mergeFlags(NodeBytecodeUsesAsNumber | NodeBytecodeNeedsNaNOrInfinity);
            break;
        }
            
        case ToPrimitive:
        case ToNumber:
        case ToNumeric:
        case CallNumberConstructor: {
            node->child1()->mergeFlags(flags);
            break;
        }

        case CompareLess:
        case CompareLessEq:
        case CompareGreater:
        case CompareGreaterEq:
        case CompareBelow:
        case CompareBelowEq:
        case CompareEq:
        case CompareStrictEq: {
            node->child1()->mergeFlags(NodeBytecodeUsesAsNumber | NodeBytecodeUsesAsOther | NodeBytecodeNeedsNaNOrInfinity);
            node->child2()->mergeFlags(NodeBytecodeUsesAsNumber | NodeBytecodeUsesAsOther | NodeBytecodeNeedsNaNOrInfinity);
            break;
        }

        case EnumeratorPutByVal:
        case PutByValDirect:
        case PutByVal:
        case PutByValMegamorphic: {
            m_graph.varArgChild(node, 0)->mergeFlags(NodeBytecodeUsesAsValue);
            m_graph.varArgChild(node, 1)->mergeFlags(NodeBytecodeUsesAsArrayIndex);
            m_graph.varArgChild(node, 2)->mergeFlags(NodeBytecodeUsesAsValue);
            break;
        }
            
        case Switch: {
            SwitchData* data = node->switchData();
            switch (data->kind) {
            case SwitchImm:
                // We don't need NodeBytecodeNeedsNegZero because if the cases are all integers
                // then -0 and 0 are treated the same.  We don't need NodeBytecodeUsesAsOther
                // because if all of the cases are integers then NaN and undefined are
                // treated the same (i.e. they will take default).
                node->child1()->mergeFlags(NodeBytecodeUsesAsNumber | NodeBytecodeUsesAsInt | NodeBytecodeNeedsNaNOrInfinity);
                break;
            case SwitchChar: {
                // We don't need NodeBytecodeNeedsNegZero because if the cases are all strings
                // then -0 and 0 are treated the same.  We don't need NodeBytecodeUsesAsOther
                // because if all of the cases are single-character strings then NaN
                // and undefined are treated the same (i.e. they will take default).
                node->child1()->mergeFlags(NodeBytecodeUsesAsNumber | NodeBytecodeNeedsNaNOrInfinity);
                break;
            }
            case SwitchString:
                // We don't need NodeBytecodeNeedsNegZero because if the cases are all strings
                // then -0 and 0 are treated the same.
                node->child1()->mergeFlags(NodeBytecodeUsesAsNumber | NodeBytecodeUsesAsOther | NodeBytecodeNeedsNaNOrInfinity);
                break;
            case SwitchCell:
                // There is currently no point to being clever here since this is used for switching
                // on objects.
                mergeDefaultFlags(node);
                break;
            }
            break;
        }

        case Identity:
            ASSERT(m_graph.afterFixup());
            node->child1()->mergeFlags(flags);
            break;

        case Int52Rep: {
            ASSERT(m_graph.afterFixup());
            auto& edge = node->child1();
            if (edge->hasDoubleResult()) {
                if (bytecodeCanIgnoreNegativeZero(node->arithNodeFlags()))
                    edge.setUseKind(DoubleRepRealUse);
                else
                    edge.setUseKind(DoubleRepAnyIntUse);
            } else if (!edge->shouldSpeculateInt32ForArithmetic()) {
                if (bytecodeCanIgnoreNegativeZero(node->arithNodeFlags()))
                    edge.setUseKind(RealNumberUse);
                else
                    edge.setUseKind(AnyIntUse);
            }
            // The results of these nodes are pure unboxed integers. Then, we
            // should definitely tell their children that you will be used as an integer.
            flags |= NodeBytecodeUsesAsInt;
            node->child1()->mergeFlags(flags);
            break;
        }

        case ValueToInt32:
        case DoubleAsInt32:
            ASSERT(m_graph.afterFixup());
            // The results of these nodes are pure unboxed integers. Then, we
            // should definitely tell their children that you will be used as an integer.
            flags |= NodeBytecodeUsesAsInt;
            node->child1()->mergeFlags(flags);
            break;

        case DoubleRep:
        case PurifyNaN:
            ASSERT(m_graph.afterFixup());
            // The result of the node is pure unboxed floating point values.
            node->child1()->mergeFlags(NodeBytecodeUsesAsNumber);
            break;

        case BooleanToNumber:
            ASSERT(m_graph.afterFixup());
            // The result of BooleanToNumber can be either an unboxed integer or a JSValue.
            if (node->child1().useKind() == BooleanUse)
                node->child1()->mergeFlags(NodeBytecodeUsesAsInt);
            break;

        // Note: ArithSqrt, ArithUnary and other math intrinsics don't have special
        // rules in here because they are always followed by Phantoms to signify that if the
        // method call speculation fails, the bytecode may use the arguments in arbitrary ways.
        // This corresponds to that possibility of someone doing something like:
        // Math.sin = function(x) { doArbitraryThingsTo(x); }

        default:
            mergeDefaultFlags(node);
            break;
        }
    }
    
    bool m_allowNestedOverflowingAdditions;

    BlockMap<Operands<NodeFlags>> m_flagsAtHead;
    Operands<NodeFlags> m_currentFlags;
};

bool performBackwardsPropagation(Graph& graph)
{
    return runPhase<BackwardsPropagationPhase>(graph);
}

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)

