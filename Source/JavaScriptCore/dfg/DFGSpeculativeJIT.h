/*
 * Copyright (C) 2011-2024 Apple Inc. All rights reserved.
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

#if ENABLE(DFG_JIT)

#include "BlockDirectory.h"
#include "DFGAbstractInterpreter.h"
#include "DFGGenerationInfo.h"
#include "DFGInPlaceAbstractState.h"
#include "DFGJITCompiler.h"
#include "DFGOSRExit.h"
#include "DFGOSRExitJumpPlaceholder.h"
#include "DFGRegisterBank.h"
#include "DFGSilentRegisterSavePlan.h"
#include "JITMathIC.h"
#include "JITOperations.h"
#include "SpillRegistersMode.h"
#include "StructureStubInfo.h"
#include "ValueRecovery.h"
#include "VirtualRegister.h"
#include <wtf/TZoneMalloc.h>

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

namespace JSC { namespace DFG {

class GPRTemporary;
class JSValueRegsTemporary;
class JSValueOperand;
class SlowPathGenerator;
class SpeculativeJIT;
class SpeculateInt32Operand;
class SpeculateStrictInt32Operand;
class SpeculateDoubleOperand;
class SpeculateCellOperand;
class SpeculateBooleanOperand;

enum GeneratedOperandType { GeneratedOperandTypeUnknown, GeneratedOperandInteger, GeneratedOperandJSValue};

// === SpeculativeJIT ===
//
// The SpeculativeJIT is used to generate a fast, but potentially
// incomplete code path for the dataflow. When code generating
// we may make assumptions about operand types, dynamically check,
// and bail-out to an alternate code path if these checks fail.
// Importantly, the speculative code path cannot be reentered once
// a speculative check has failed. This allows the SpeculativeJIT
// to propagate type information (including information that has
// only speculatively been asserted) through the dataflow.
DECLARE_ALLOCATOR_WITH_HEAP_IDENTIFIER(SpeculativeJIT);
class SpeculativeJIT : public JITCompiler {
    using Base = JITCompiler;
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED_WITH_HEAP_IDENTIFIER(SpeculativeJIT, SpeculativeJIT);
    friend struct OSRExit;
private:
    typedef JITCompiler::TrustedImm32 TrustedImm32;
    typedef JITCompiler::Imm32 Imm32;
    typedef JITCompiler::ImmPtr ImmPtr;
    typedef JITCompiler::TrustedImm64 TrustedImm64;
    typedef JITCompiler::Imm64 Imm64;

    // These constants are used to set priorities for spill order for
    // the register allocator.
#if USE(JSVALUE64)
    enum SpillOrder {
        SpillOrderConstant = 1, // no spill, and cheap fill
        SpillOrderSpilled  = 2, // no spill
        SpillOrderJS       = 4, // needs spill
        SpillOrderCell     = 4, // needs spill
        SpillOrderStorage  = 4, // needs spill
        SpillOrderInteger  = 5, // needs spill and box
        SpillOrderBoolean  = 5, // needs spill and box
        SpillOrderDouble   = 6, // needs spill and convert
    };
#elif USE(JSVALUE32_64)
    enum SpillOrder {
        SpillOrderConstant = 1, // no spill, and cheap fill
        SpillOrderSpilled  = 2, // no spill
        SpillOrderJS       = 4, // needs spill
        SpillOrderStorage  = 4, // needs spill
        SpillOrderDouble   = 4, // needs spill
        SpillOrderInteger  = 5, // needs spill and box
        SpillOrderCell     = 5, // needs spill and box
        SpillOrderBoolean  = 5, // needs spill and box
    };
#endif

    enum UseChildrenMode { CallUseChildren, UseChildrenCalledExplicitly };
    
public:
    SpeculativeJIT(Graph& dfg);
    ~SpeculativeJIT();

    void compile();
    void compileFunction();

    struct TrustedImmPtr {
        template <typename T>
        explicit TrustedImmPtr(T* value)
            : m_value(value)
        {
            static_assert(!std::is_base_of<JSCell, T>::value, "To use a GC pointer, the graph must be aware of it. Use SpeculativeJIT::JITCompiler::LinkableConstant instead.");
        }

        explicit TrustedImmPtr(RegisteredStructure structure)
            : m_value(structure.get())
        { }
        
        explicit TrustedImmPtr(std::nullptr_t)
            : m_value(nullptr)
        { }

        explicit TrustedImmPtr(FrozenValue* value)
            : m_value(value->cell())
        {
            RELEASE_ASSERT(value->value().isCell());
        }

        explicit TrustedImmPtr(size_t value)
            : m_value(std::bit_cast<void*>(value))
        {
        }

        operator MacroAssembler::TrustedImmPtr() const { return m_value; }
        operator MacroAssembler::TrustedImm() const { return m_value; }

        intptr_t asIntptr()
        {
            return m_value.asIntptr();
        }

    private:
        MacroAssembler::TrustedImmPtr m_value;
    };

    void compileBody();
    
    void createOSREntries();
    void linkOSREntries(LinkBuffer&);
    Vector<VariableEvent> finalizeEventStream() { return m_stream.finalize(); }

    BasicBlock* nextBlock()
    {
        for (BlockIndex resultIndex = m_block->index + 1; ; resultIndex++) {
            if (resultIndex >= m_graph.numBlocks())
                return nullptr;
            if (BasicBlock* result = m_graph.block(resultIndex))
                return result;
        }
    }
    
#if USE(JSVALUE64)
    GPRReg fillJSValue(Edge);
#elif USE(JSVALUE32_64)
    bool fillJSValue(Edge, GPRReg&, GPRReg&, FPRReg&);
#endif
    GPRReg fillStorage(Edge);

    // lock and unlock GPR & FPR registers.
    void lock(GPRReg reg)
    {
        m_gprs.lock(reg);
    }
    void lock(FPRReg reg)
    {
        m_fprs.lock(reg);
    }
    void unlock(GPRReg reg)
    {
        m_gprs.unlock(reg);
    }
    void unlock(FPRReg reg)
    {
        m_fprs.unlock(reg);
    }

    // Used to check whether a child node is on its last use,
    // and its machine registers may be reused.
    bool canReuse(Node* node)
    {
        return generationInfo(node).useCount() == 1;
    }
    bool canReuse(Node* nodeA, Node* nodeB)
    {
        return nodeA == nodeB && generationInfo(nodeA).useCount() == 2;
    }
    bool canReuse(Edge nodeUse)
    {
        return canReuse(nodeUse.node());
    }
    GPRReg reuse(GPRReg reg)
    {
        m_gprs.lock(reg);
        return reg;
    }
    FPRReg reuse(FPRReg reg)
    {
        m_fprs.lock(reg);
        return reg;
    }

    // Allocate a gpr/fpr.
    GPRReg allocate()
    {
#if ENABLE(DFG_REGISTER_ALLOCATION_VALIDATION)
        addRegisterAllocationAtOffset(debugOffset());
#endif
        VirtualRegister spillMe;
        GPRReg gpr = m_gprs.allocate(spillMe);
        if (spillMe.isValid()) {
#if USE(JSVALUE32_64)
            GenerationInfo& info = generationInfoFromVirtualRegister(spillMe);
            if ((info.registerFormat() & DataFormatJS))
                m_gprs.release(info.tagGPR() == gpr ? info.payloadGPR() : info.tagGPR());
#endif
            spill(spillMe);
        }
        return gpr;
    }
    GPRReg allocate(GPRReg specific)
    {
        ASSERT(specific != InvalidGPRReg);
#if ENABLE(DFG_REGISTER_ALLOCATION_VALIDATION)
        addRegisterAllocationAtOffset(debugOffset());
#endif
        VirtualRegister spillMe = m_gprs.allocateSpecific(specific);
        if (spillMe.isValid()) {
#if USE(JSVALUE32_64)
            GenerationInfo& info = generationInfoFromVirtualRegister(spillMe);
            RELEASE_ASSERT(info.registerFormat() != DataFormatJSDouble);
            if ((info.registerFormat() & DataFormatJS))
                m_gprs.release(info.tagGPR() == specific ? info.payloadGPR() : info.tagGPR());
#endif
            spill(spillMe);
        }
        return specific;
    }
    GPRReg tryAllocate()
    {
        return m_gprs.tryAllocate();
    }
    FPRReg fprAllocate()
    {
#if ENABLE(DFG_REGISTER_ALLOCATION_VALIDATION)
        addRegisterAllocationAtOffset(debugOffset());
#endif
        VirtualRegister spillMe;
        FPRReg fpr = m_fprs.allocate(spillMe);
        if (spillMe.isValid())
            spill(spillMe);
        return fpr;
    }

    // Check whether a VirtualRegsiter is currently in a machine register.
    // We use this when filling operands to fill those that are already in
    // machine registers first (by locking VirtualRegsiters that are already
    // in machine register before filling those that are not we attempt to
    // avoid spilling values we will need immediately).
    bool isFilled(Node* node)
    {
        return generationInfo(node).registerFormat() != DataFormatNone;
    }
    bool isFilledDouble(Node* node)
    {
        return generationInfo(node).registerFormat() == DataFormatDouble;
    }

    // Called on an operand once it has been consumed by a parent node.
    void use(Node* node)
    {
        if (!node->hasResult())
            return;
        GenerationInfo& info = generationInfo(node);

        // use() returns true when the value becomes dead, and any
        // associated resources may be freed.
        if (!info.use(m_stream))
            return;

        // Release the associated machine registers.
        DataFormat registerFormat = info.registerFormat();
#if USE(JSVALUE64)
        if (registerFormat == DataFormatDouble)
            m_fprs.release(info.fpr());
        else if (registerFormat != DataFormatNone)
            m_gprs.release(info.gpr());
#elif USE(JSVALUE32_64)
        if (registerFormat == DataFormatDouble)
            m_fprs.release(info.fpr());
        else if (registerFormat & DataFormatJS) {
            m_gprs.release(info.tagGPR());
            m_gprs.release(info.payloadGPR());
        } else if (registerFormat != DataFormatNone)
            m_gprs.release(info.gpr());
#endif
    }
    void use(Edge nodeUse)
    {
        use(nodeUse.node());
    }
    
    RegisterSetBuilder usedRegisters();
    
    bool masqueradesAsUndefinedWatchpointSetIsStillValid()
    {
        return m_graph.isWatchingMasqueradesAsUndefinedWatchpointSet(m_currentNode);
    }

    void compileStoreBarrier(Node*);

    // Called by the speculative operand types, below, to fill operand to
    // machine registers, implicitly generating speculation checks as needed.
    GPRReg fillSpeculateInt32(Edge, DataFormat& returnFormat);
    GPRReg fillSpeculateInt32Strict(Edge);
    GPRReg fillSpeculateInt52(Edge, DataFormat desiredFormat);
    FPRReg fillSpeculateDouble(Edge);
    GPRReg fillSpeculateCell(Edge);
    GPRReg fillSpeculateBoolean(Edge);
#if USE(BIGINT32)
    GPRReg fillSpeculateBigInt32(Edge);
#endif
    GeneratedOperandType checkGeneratedTypeForToInt32(Node*);

    void addSlowPathGenerator(std::unique_ptr<SlowPathGenerator>);
    void addSlowPathGeneratorLambda(Function<void()>&&);
    void runSlowPathGenerators(PCToCodeOriginMapBuilder&);
    
    void compile(Node*);
    void noticeOSRBirth(Node*);
    void bail(AbortReason);
    void compileCurrentBlock();

    void exceptionCheck(GPRReg exceptionReg = InvalidGPRReg);
    CallSiteIndex recordCallSiteAndGenerateExceptionHandlingOSRExitIfNeeded(const CodeOrigin& callSiteCodeOrigin, unsigned eventStreamIndex);

    void checkArgumentTypes();

    void clearGenerationInfo();

    // These methods are used when generating 'unexpected'
    // calls out from JIT code to C++ helper routines -
    // they spill all live values to the appropriate
    // slots in the JSStack without changing any state
    // in the GenerationInfo.
    SilentRegisterSavePlan silentSavePlanForGPR(VirtualRegister spillMe, GPRReg source);
    SilentRegisterSavePlan silentSavePlanForFPR(VirtualRegister spillMe, FPRReg source);
    void silentSpillImpl(const SilentRegisterSavePlan&);
    void silentFillImpl(const SilentRegisterSavePlan&);

    RegisterSetBuilder spilledRegsForSilentSpillPlans(const auto& plans)
    {
        RegisterSetBuilder usedRegisters;
        for (auto& plan : plans)
            usedRegisters.add(plan.reg(), IgnoreVectors);
        return usedRegisters;
    }

    template<typename CollectionType>
    void silentSpill(const CollectionType& savePlans)
    {
        ASSERT(!m_underSilentSpill);
        m_underSilentSpill = true;
        for (unsigned i = 0; i < savePlans.size(); ++i)
            silentSpillImpl(savePlans[i]);
    }

    template<typename CollectionType>
    void silentFill(const CollectionType& savePlans)
    {
        ASSERT(m_underSilentSpill);
        for (unsigned i = savePlans.size(); i--;)
            silentFillImpl(savePlans[i]);
        m_underSilentSpill = false;
    }

    template<typename CollectionType>
    void silentSpillAllRegistersImpl(bool doSpill, CollectionType& plans, GPRReg exclude, GPRReg exclude2 = InvalidGPRReg, FPRReg fprExclude = InvalidFPRReg)
    {
        ASSERT(plans.isEmpty());
        ASSERT(!m_underSilentSpill);
        if (doSpill)
            m_underSilentSpill = true;
        for (gpr_iterator iter = m_gprs.begin(); iter != m_gprs.end(); ++iter) {
            GPRReg gpr = iter.regID();
            if (iter.name().isValid() && gpr != exclude && gpr != exclude2) {
                SilentRegisterSavePlan plan = silentSavePlanForGPR(iter.name(), gpr);
                if (doSpill)
                    silentSpillImpl(plan);
                plans.append(plan);
            }
        }
        for (fpr_iterator iter = m_fprs.begin(); iter != m_fprs.end(); ++iter) {
            if (iter.name().isValid() && iter.regID() != fprExclude) {
                SilentRegisterSavePlan plan = silentSavePlanForFPR(iter.name(), iter.regID());
                if (doSpill)
                    silentSpillImpl(plan);
                plans.append(plan);
            }
        }
    }
    template<typename CollectionType>
    void silentSpillAllRegistersImpl(bool doSpill, CollectionType& plans, NoResultTag)
    {
        silentSpillAllRegistersImpl(doSpill, plans, InvalidGPRReg, InvalidGPRReg, InvalidFPRReg);
    }
    template<typename CollectionType>
    void silentSpillAllRegistersImpl(bool doSpill, CollectionType& plans, FPRReg exclude)
    {
        silentSpillAllRegistersImpl(doSpill, plans, InvalidGPRReg, InvalidGPRReg, exclude);
    }
    template<typename CollectionType>
    void silentSpillAllRegistersImpl(bool doSpill, CollectionType& plans, JSValueRegs exclude)
    {
#if USE(JSVALUE32_64)
        silentSpillAllRegistersImpl(doSpill, plans, exclude.tagGPR(), exclude.payloadGPR());
#else
        silentSpillAllRegistersImpl(doSpill, plans, exclude.gpr());
#endif
    }
    
    void silentSpillAllRegisters(GPRReg exclude, GPRReg exclude2 = InvalidGPRReg, FPRReg fprExclude = InvalidFPRReg)
    {
        silentSpillAllRegistersImpl(true, m_plans, exclude, exclude2, fprExclude);
    }
    void silentSpillAllRegisters(FPRReg exclude)
    {
        silentSpillAllRegisters(InvalidGPRReg, InvalidGPRReg, exclude);
    }
    void silentSpillAllRegisters(JSValueRegs exclude)
    {
#if USE(JSVALUE64)
        silentSpillAllRegisters(exclude.payloadGPR());
#else
        silentSpillAllRegisters(exclude.payloadGPR(), exclude.tagGPR());
#endif
    }

    void silentFillAllRegisters()
    {
        silentFill(m_plans);
        m_plans.clear();
    }

    // These methods convert between doubles, and doubles boxed and JSValues.
#if USE(JSVALUE64)
    using Base::boxDouble;
    GPRReg boxDouble(FPRReg fpr)
    {
        return boxDouble(fpr, allocate());
    }
    
    using Base::boxInt52;
    void boxInt52(GPRReg sourceGPR, GPRReg targetGPR, DataFormat);
#endif

    // Spill a VirtualRegister to the JSStack.
    void spill(VirtualRegister spillMe)
    {
        GenerationInfo& info = generationInfoFromVirtualRegister(spillMe);

#if USE(JSVALUE32_64)
        if (info.registerFormat() == DataFormatNone) // it has been spilled. JS values which have two GPRs can reach here
            return;
#endif
        // Check the GenerationInfo to see if this value need writing
        // to the JSStack - if not, mark it as spilled & return.
        if (!info.needsSpill()) {
            info.setSpilled(m_stream, spillMe);
            return;
        }

        DataFormat spillFormat = info.registerFormat();
        switch (spillFormat) {
        case DataFormatStorage: {
            // This is special, since it's not a JS value - as in it's not visible to JS
            // code.
            storePtr(info.gpr(), JITCompiler::addressFor(spillMe));
            info.spill(m_stream, spillMe, DataFormatStorage);
            return;
        }

        case DataFormatInt32: {
            store32(info.gpr(), JITCompiler::payloadFor(spillMe));
            info.spill(m_stream, spillMe, DataFormatInt32);
            return;
        }

#if USE(JSVALUE64)
        case DataFormatDouble: {
            storeDouble(info.fpr(), JITCompiler::addressFor(spillMe));
            info.spill(m_stream, spillMe, DataFormatDouble);
            return;
        }

        case DataFormatInt52:
        case DataFormatStrictInt52: {
            store64(info.gpr(), JITCompiler::addressFor(spillMe));
            info.spill(m_stream, spillMe, spillFormat);
            return;
        }
            
        default:
            // The following code handles JSValues, int32s, and cells.
            RELEASE_ASSERT(spillFormat == DataFormatCell || spillFormat & DataFormatJS);
            
            GPRReg reg = info.gpr();
            // We need to box int32 and cell values ...
            // but on JSVALUE64 boxing a cell is a no-op!
            if (spillFormat == DataFormatInt32)
                or64(GPRInfo::numberTagRegister, reg);
            
            // Spill the value, and record it as spilled in its boxed form.
            store64(reg, JITCompiler::addressFor(spillMe));
            info.spill(m_stream, spillMe, (DataFormat)(spillFormat | DataFormatJS));
            return;
#elif USE(JSVALUE32_64)
        case DataFormatCell:
        case DataFormatBoolean: {
            store32(info.gpr(), JITCompiler::payloadFor(spillMe));
            info.spill(m_stream, spillMe, spillFormat);
            return;
        }

        case DataFormatDouble: {
            // On JSVALUE32_64 boxing a double is a no-op.
            storeDouble(info.fpr(), JITCompiler::addressFor(spillMe));
            info.spill(m_stream, spillMe, DataFormatDouble);
            return;
        }

        default:
            // The following code handles JSValues.
            RELEASE_ASSERT(spillFormat & DataFormatJS);
            store32(info.tagGPR(), JITCompiler::tagFor(spillMe));
            store32(info.payloadGPR(), JITCompiler::payloadFor(spillMe));
            info.spill(m_stream, spillMe, spillFormat);
            return;
#endif
        }
    }
    
    bool isKnownInteger(Node* node) { return m_state.forNode(node).isType(SpecInt32Only); }
    bool isKnownCell(Node* node) { return m_state.forNode(node).isType(SpecCell); }
    
    bool isKnownNotInteger(Node* node) { return !(m_state.forNode(node).m_type & SpecInt32Only); }
    bool isKnownNotNumber(Node* node) { return !(m_state.forNode(node).m_type & SpecFullNumber); }
    bool isKnownNotCell(Node* node) { return !(m_state.forNode(node).m_type & SpecCell); }
    bool isKnownNotOther(Node* node) { return !(m_state.forNode(node).m_type & SpecOther); }

    bool canBeRope(Edge);

    UniquedStringImpl* identifierUID(unsigned index)
    {
        return m_graph.identifiers()[index];
    }

    // Spill all VirtualRegisters back to the JSStack.
    void flushRegisters()
    {
        for (gpr_iterator iter = m_gprs.begin(); iter != m_gprs.end(); ++iter) {
            if (iter.name().isValid()) {
                spill(iter.name());
                iter.release();
            }
        }
        for (fpr_iterator iter = m_fprs.begin(); iter != m_fprs.end(); ++iter) {
            if (iter.name().isValid()) {
                spill(iter.name());
                iter.release();
            }
        }
    }

    // Used to ASSERT flushRegisters() has been called prior to
    // calling out from JIT code to a C helper function.
    bool isFlushed()
    {
        for (gpr_iterator iter = m_gprs.begin(); iter != m_gprs.end(); ++iter) {
            if (iter.name().isValid())
                return false;
        }
        for (fpr_iterator iter = m_fprs.begin(); iter != m_fprs.end(); ++iter) {
            if (iter.name().isValid())
                return false;
        }
        return true;
    }

#if USE(JSVALUE64)
    static Imm64 valueOfJSConstantAsImm64(Node* node)
    {
        return Imm64(JSValue::encode(node->asJSValue()));
    }
#endif

    // Helper functions to enable code sharing in implementations of bit/shift ops.
    void bitOp(NodeType op, int32_t imm, GPRReg op1, GPRReg result)
    {
        switch (op) {
        case ArithBitAnd:
            and32(Imm32(imm), op1, result);
            break;
        case ArithBitOr:
            or32(Imm32(imm), op1, result);
            break;
        case ArithBitXor:
            xor32(Imm32(imm), op1, result);
            break;
        default:
            RELEASE_ASSERT_NOT_REACHED();
        }
    }
    void bitOp(NodeType op, GPRReg op1, GPRReg op2, GPRReg result)
    {
        switch (op) {
        case ArithBitAnd:
            and32(op1, op2, result);
            break;
        case ArithBitOr:
            or32(op1, op2, result);
            break;
        case ArithBitXor:
            xor32(op1, op2, result);
            break;
        default:
            RELEASE_ASSERT_NOT_REACHED();
        }
    }
    void shiftOp(NodeType op, GPRReg op1, int32_t shiftAmount, GPRReg result)
    {
        switch (op) {
        case ArithBitRShift:
            rshift32(op1, Imm32(shiftAmount), result);
            break;
        case ArithBitLShift:
            lshift32(op1, Imm32(shiftAmount), result);
            break;
        case ArithBitURShift:
            urshift32(op1, Imm32(shiftAmount), result);
            break;
        default:
            RELEASE_ASSERT_NOT_REACHED();
        }
    }
    void shiftOp(NodeType op, GPRReg op1, GPRReg shiftAmount, GPRReg result)
    {
        switch (op) {
        case ArithBitRShift:
            rshift32(op1, shiftAmount, result);
            break;
        case ArithBitLShift:
            lshift32(op1, shiftAmount, result);
            break;
        case ArithBitURShift:
            urshift32(op1, shiftAmount, result);
            break;
        default:
            RELEASE_ASSERT_NOT_REACHED();
        }
    }
    
    // Returns the index of the branch node if peephole is okay, UINT_MAX otherwise.
    unsigned detectPeepHoleBranch()
    {
        // Check that no intervening nodes will be generated.
        for (unsigned index = m_indexInBlock + 1; index < m_block->size() - 1; ++index) {
            Node* node = m_block->at(index);
            if (!node->shouldGenerate())
                continue;
            // Check if it's a Phantom that can be safely ignored.
            if (node->op() == Phantom && !node->child1())
                continue;
            return UINT_MAX;
        }

        // Check if the lastNode is a branch on this node.
        Node* lastNode = m_block->terminal();
        return lastNode->op() == Branch && lastNode->child1() == m_currentNode ? m_block->size() - 1 : UINT_MAX;
    }
    
    void compileCheckTraps(Node*);

    void compileLoopHint(Node*);
    void compileMovHint(Node*);
    void compileMovHintAndCheck(Node*);

    void compileCheckDetached(Node*);

#if USE(JSVALUE64)
    void cachedGetById(Node*, CodeOrigin, JSValueRegs base, JSValueRegs result, CacheableIdentifier, bool needsBaseCellCheck, AccessType, CacheType);
    void cachedPutById(Node*, CodeOrigin, GPRReg baseGPR, JSValueRegs valueRegs, CacheableIdentifier, AccessType);
    void cachedGetByIdWithThis(Node*, CodeOrigin, JSValueRegs baseRegs, JSValueRegs thisRegs, JSValueRegs resultRegs, CacheableIdentifier, bool needsBaseAndThisCellCheck);
#elif USE(JSVALUE32_64)
    void cachedGetById(Node*, CodeOrigin, JSValueRegs base, JSValueRegs result, GPRReg stubInfoGPR, GPRReg scratchGPR, CacheableIdentifier, JITCompiler::Jump slowPathTarget, SpillRegistersMode, AccessType);
    void cachedPutById(Node*, CodeOrigin, GPRReg baseGPR, JSValueRegs valueRegs, GPRReg stubInfoGPR, GPRReg scratchGPR, GPRReg scratch2GPR, CacheableIdentifier, AccessType, JITCompiler::Jump slowPathTarget = JITCompiler::Jump(), SpillRegistersMode = NeedToSpill);
    void cachedGetById(Node*, CodeOrigin, GPRReg baseGPR, GPRReg resultGPR, GPRReg stubInfoGPR, GPRReg scratchGPR, CacheableIdentifier, JITCompiler::Jump slowPathTarget, SpillRegistersMode, AccessType);
    void cachedGetByIdWithThis(Node*, CodeOrigin, GPRReg baseGPR, GPRReg thisGPR, GPRReg resultGPR, GPRReg stubInfoGPR, GPRReg scratchGPR, CacheableIdentifier, const JITCompiler::JumpList& slowPathTarget = JITCompiler::JumpList());
    void cachedGetById(Node*, CodeOrigin, GPRReg baseTagGPROrNone, GPRReg basePayloadGPR, GPRReg resultTagGPR, GPRReg resultPayloadGPR, GPRReg stubInfoGPR, GPRReg scratchGPR, CacheableIdentifier, JITCompiler::Jump slowPathTarget, SpillRegistersMode, AccessType);
    void cachedGetByIdWithThis(Node*, CodeOrigin, GPRReg baseTagGPROrNone, GPRReg basePayloadGPR, GPRReg thisTagGPROrNone, GPRReg thisPayloadGPR, GPRReg resultTagGPR, GPRReg resultPayloadGPR, GPRReg stubInfoGPR, GPRReg scratchGPR, CacheableIdentifier, const JITCompiler::JumpList& slowPathTarget = JITCompiler::JumpList());
    void compileGetByIdFlush(Node*, AccessType);
    void compilePutByIdFlush(Node*);
    void compileInstanceOfForCells(Node*, JSValueRegs, JSValueRegs, GPRReg, GPRReg, Jump);
#endif

    void compileDeleteById(Node*);
    void compileDeleteByVal(Node*);
    void compilePushWithScope(Node*);
    void compileGetById(Node*, AccessType);
    void compileGetByIdMegamorphic(Node*);
    void compileGetByIdWithThisMegamorphic(Node*);
    void compileInById(Node*);
    void compileInByIdMegamorphic(Node*);
    void compileInByVal(Node*);
    void compileInByValMegamorphic(Node*);
    void compileHasPrivate(Node*, AccessType);
    void compileHasPrivateName(Node*);
    void compileHasPrivateBrand(Node*);

    void nonSpeculativeNonPeepholeCompareNullOrUndefined(Edge operand);
    void nonSpeculativePeepholeBranchNullOrUndefined(Edge operand, Node* branchNode);
    
    void genericJSValuePeepholeBranch(Node*, Node* branchNode, RelationalCondition, S_JITOperation_GJJ helperFunction);
    void genericJSValueNonPeepholeCompare(Node*, RelationalCondition, S_JITOperation_GJJ helperFunction);
    
    void nonSpeculativePeepholeStrictEq(Node*, Node* branchNode, bool invert = false);
    void genericJSValueNonPeepholeStrictEq(Node*, bool invert = false);
    bool genericJSValueStrictEq(Node*, bool invert = false);

    void compileInstanceOf(Node*);
    void compileInstanceOfCustom(Node*);
    void compileInstanceOfMegamorphic(Node*);
    void compileOverridesHasInstance(Node*);

    void compileIsCellWithType(Node*);
    void compileIsTypedArrayView(Node*);

    void emitCall(Node*);

    void emitAllocateButterfly(GPRReg storageGPR, GPRReg sizeGPR, GPRReg scratch1, GPRReg scratch2, GPRReg scratch3, JumpList& slowCases);
    void emitInitializeButterfly(GPRReg storageGPR, GPRReg sizeGPR, JSValueRegs emptyValueRegs, GPRReg scratchGPR);
    void compileAllocateNewArrayWithSize(Node*, GPRReg resultGPR, GPRReg sizeGPR, RegisteredStructure, bool shouldConvertLargeSizeToArrayStorage = true);
    void compileAllocateNewArrayWithSize(Node*, GPRReg resultGPR, GPRReg sizeGPR, IndexingType, bool shouldConvertLargeSizeToArrayStorage = true);
    
    // Called once a node has completed code generation but prior to setting
    // its result, to free up its children. (This must happen prior to setting
    // the nodes result, since the node may have the same VirtualRegister as
    // a child, and as such will use the same GeneratioInfo).
    void useChildren(Node*);

    // These method called to initialize the GenerationInfo
    // to describe the result of an operation.
    void strictInt32Result(GPRReg reg, Node* node, DataFormat format = DataFormatInt32, UseChildrenMode mode = CallUseChildren)
    {
        if (mode == CallUseChildren)
            useChildren(node);

        VirtualRegister virtualRegister = node->virtualRegister();
        GenerationInfo& info = generationInfoFromVirtualRegister(virtualRegister);

        if (format == DataFormatInt32) {
            jitAssertIsInt32(reg);
            m_gprs.retain(reg, virtualRegister, SpillOrderInteger);
            info.initInt32(node, node->refCount(), reg);
        } else {
#if USE(JSVALUE64)
            RELEASE_ASSERT(format == DataFormatJSInt32);
            jitAssertIsJSInt32(reg);
            m_gprs.retain(reg, virtualRegister, SpillOrderJS);
            info.initJSValue(node, node->refCount(), reg, format);
#elif USE(JSVALUE32_64)
            RELEASE_ASSERT_NOT_REACHED();
#endif
        }
    }
    void strictInt32Result(GPRReg reg, Node* node, UseChildrenMode mode)
    {
        strictInt32Result(reg, node, DataFormatInt32, mode);
    }
    void int52Result(GPRReg reg, Node* node, DataFormat format, UseChildrenMode mode = CallUseChildren)
    {
        if (mode == CallUseChildren)
            useChildren(node);

        VirtualRegister virtualRegister = node->virtualRegister();
        GenerationInfo& info = generationInfoFromVirtualRegister(virtualRegister);

        m_gprs.retain(reg, virtualRegister, SpillOrderJS);
        info.initInt52(node, node->refCount(), reg, format);
    }
    void int52Result(GPRReg reg, Node* node, UseChildrenMode mode = CallUseChildren)
    {
        int52Result(reg, node, DataFormatInt52, mode);
    }
    void strictInt52Result(GPRReg reg, Node* node, UseChildrenMode mode = CallUseChildren)
    {
        int52Result(reg, node, DataFormatStrictInt52, mode);
    }
    void noResult(Node* node, UseChildrenMode mode = CallUseChildren)
    {
        if (mode == UseChildrenCalledExplicitly)
            return;
        useChildren(node);
    }
    void cellResult(GPRReg reg, Node* node, UseChildrenMode mode = CallUseChildren)
    {
        if (mode == CallUseChildren)
            useChildren(node);

        VirtualRegister virtualRegister = node->virtualRegister();
        m_gprs.retain(reg, virtualRegister, SpillOrderCell);
        GenerationInfo& info = generationInfoFromVirtualRegister(virtualRegister);
        info.initCell(node, node->refCount(), reg);
    }
    void blessedBooleanResult(GPRReg reg, Node* node, UseChildrenMode mode = CallUseChildren)
    {
#if USE(JSVALUE64)
        jsValueResult(reg, node, DataFormatJSBoolean, mode);
#else
        booleanResult(reg, node, mode);
#endif
    }
    void unblessedBooleanResult(GPRReg reg, Node* node, UseChildrenMode mode = CallUseChildren)
    {
#if USE(JSVALUE64)
        blessBoolean(reg);
#endif
        blessedBooleanResult(reg, node, mode);
    }
#if USE(JSVALUE64)
    void jsValueResult(GPRReg reg, Node* node, DataFormat format = DataFormatJS, UseChildrenMode mode = CallUseChildren)
    {
        if (format == DataFormatJSInt32)
            jitAssertIsJSInt32(reg);
        
        if (mode == CallUseChildren)
            useChildren(node);

        VirtualRegister virtualRegister = node->virtualRegister();
        m_gprs.retain(reg, virtualRegister, SpillOrderJS);
        GenerationInfo& info = generationInfoFromVirtualRegister(virtualRegister);
        info.initJSValue(node, node->refCount(), reg, format);
    }
    void jsValueResult(GPRReg reg, Node* node, UseChildrenMode mode)
    {
        jsValueResult(reg, node, DataFormatJS, mode);
    }
#elif USE(JSVALUE32_64)
    void booleanResult(GPRReg reg, Node* node, UseChildrenMode mode = CallUseChildren)
    {
        if (mode == CallUseChildren)
            useChildren(node);

        VirtualRegister virtualRegister = node->virtualRegister();
        m_gprs.retain(reg, virtualRegister, SpillOrderBoolean);
        GenerationInfo& info = generationInfoFromVirtualRegister(virtualRegister);
        info.initBoolean(node, node->refCount(), reg);
    }
    void jsValueResult(GPRReg tag, GPRReg payload, Node* node, DataFormat format = DataFormatJS, UseChildrenMode mode = CallUseChildren)
    {
        if (mode == CallUseChildren)
            useChildren(node);

        VirtualRegister virtualRegister = node->virtualRegister();
        m_gprs.retain(tag, virtualRegister, SpillOrderJS);
        m_gprs.retain(payload, virtualRegister, SpillOrderJS);
        GenerationInfo& info = generationInfoFromVirtualRegister(virtualRegister);
        info.initJSValue(node, node->refCount(), tag, payload, format);
    }
    void jsValueResult(GPRReg tag, GPRReg payload, Node* node, UseChildrenMode mode)
    {
        jsValueResult(tag, payload, node, DataFormatJS, mode);
    }
#endif
    void jsValueResult(JSValueRegs regs, Node* node, DataFormat format = DataFormatJS, UseChildrenMode mode = CallUseChildren)
    {
#if USE(JSVALUE64)
        jsValueResult(regs.gpr(), node, format, mode);
#else
        jsValueResult(regs.tagGPR(), regs.payloadGPR(), node, format, mode);
#endif
    }
    void storageResult(GPRReg reg, Node* node, UseChildrenMode mode = CallUseChildren)
    {
        if (mode == CallUseChildren)
            useChildren(node);
        
        VirtualRegister virtualRegister = node->virtualRegister();
        m_gprs.retain(reg, virtualRegister, SpillOrderStorage);
        GenerationInfo& info = generationInfoFromVirtualRegister(virtualRegister);
        info.initStorage(node, node->refCount(), reg);
    }
    void doubleResult(FPRReg reg, Node* node, UseChildrenMode mode = CallUseChildren)
    {
        if (mode == CallUseChildren)
            useChildren(node);

        VirtualRegister virtualRegister = node->virtualRegister();
        m_fprs.retain(reg, virtualRegister, SpillOrderDouble);
        GenerationInfo& info = generationInfoFromVirtualRegister(virtualRegister);
        info.initDouble(node, node->refCount(), reg);
    }
    void initConstantInfo(Node* node)
    {
        ASSERT(node->hasConstant());
        generationInfo(node).initConstant(node, node->refCount());
    }

    void strictInt32TupleResultWithoutUsingChildren(GPRReg reg, Node* node, unsigned index, DataFormat format = DataFormatInt32)
    {
        ASSERT(index < node->tupleSize());
        unsigned refCount = m_graph.m_tupleData.at(node->tupleOffset() + index).refCount;
        if (!refCount)
            return;
        ASSERT(refCount == 1);
        VirtualRegister virtualRegister = m_graph.m_tupleData.at(node->tupleOffset() + index).virtualRegister;
        GenerationInfo& info = generationInfoFromVirtualRegister(virtualRegister);

        if (format == DataFormatInt32) {
            jitAssertIsInt32(reg);
            m_gprs.retain(reg, virtualRegister, SpillOrderInteger);
            info.initInt32(node, refCount, reg);
        } else {
#if USE(JSVALUE64)
            RELEASE_ASSERT(format == DataFormatJSInt32);
            jitAssertIsJSInt32(reg);
            m_gprs.retain(reg, virtualRegister, SpillOrderJS);
            info.initJSValue(node, refCount, reg, format);
#elif USE(JSVALUE32_64)
            RELEASE_ASSERT_NOT_REACHED();
#endif
        }
    }

    template<typename OperationType>
    void operationExceptionCheck()
    {
        using ResultType = typename FunctionTraits<OperationType>::ResultType;
        ASSERT(!m_underSilentSpill);
        exceptionCheck(operationExceptionRegister<ResultType>());
    }

    template<typename OperationType, typename ResultRegType, typename... Args>
    requires (OperationHasResult<OperationType>)
    JITCompiler::Call callOperation(OperationType operation, ResultRegType result, Args... args)
    {
        setupArguments<OperationType>(args...);
        auto call = appendCall(operation);
        operationExceptionCheck<OperationType>();
        setupResults(result);
        return call;
    }

    template<typename OperationType, typename... Args>
    requires (OperationIsVoid<OperationType>)
    JITCompiler::Call callOperation(OperationType operation, Args... args)
    {
        setupArguments<OperationType>(args...);
        auto call = appendCall(operation);
        operationExceptionCheck<OperationType>();
        return call;
    }

    template<typename OperationType, typename ResultRegType, typename... Args>
    requires (OperationHasResult<OperationType>)
    JITCompiler::Call callOperation(const CodePtr<OperationPtrTag> operation, ResultRegType result, Args... args)
    {
        setupArguments<OperationType>(args...);
        auto call = appendCall(operation);
        operationExceptionCheck<OperationType>();
        setupResults(result);
        return call;
    }

    template<typename OperationType, typename... Args>
    requires (OperationIsVoid<OperationType>)
    JITCompiler::Call callOperation(const CodePtr<OperationPtrTag> operation, Args... args)
    {
        setupArguments<OperationType>(args...);
        auto call = appendCall(operation);
        operationExceptionCheck<OperationType>();
        return call;
    }

    template<typename OperationType, typename ResultRegType, typename... Args>
    requires (OperationHasResult<OperationType>)
    void callOperation(Address address, ResultRegType result, Args... args)
    {
        setupArgumentsForIndirectCall<OperationType>(address, args...);
        appendCall(Address(GPRInfo::nonArgGPR0, address.offset));
        operationExceptionCheck<OperationType>();
        setupResults(result);
    }

    template<typename OperationType, typename... Args>
    requires (OperationIsVoid<OperationType>)
    void callOperation(Address address, Args... args)
    {
        setupArgumentsForIndirectCall<OperationType>(address, args...);
        appendCall(Address(GPRInfo::nonArgGPR0, address.offset));
        operationExceptionCheck<OperationType>();
    }

    template<typename OperationType, typename... Args>
    requires (OperationIsVoid<OperationType>
        && !isExceptionOperationResult<typename FunctionTraits<OperationType>::ResultType>) // Sanity check
    void callOperationWithoutExceptionCheck(Address address, Args... args)
    {
        setupArgumentsForIndirectCall<OperationType>(address, args...);
        appendCall(Address(GPRInfo::nonArgGPR0, address.offset));
    }

    template<typename OperationType, typename ResultRegType, typename... Args>
    requires (OperationHasResult<OperationType>
        && !isExceptionOperationResult<typename FunctionTraits<OperationType>::ResultType>) // Sanity check
    JITCompiler::Call callOperationWithoutExceptionCheck(OperationType operation, ResultRegType result, Args... args)
    {
        setupArguments<OperationType>(args...);
        auto call = appendCall(operation);
        setupResults(result);
        return call;
    }

    template<typename OperationType, typename... Args>
    requires (OperationIsVoid<OperationType>
        && !isExceptionOperationResult<typename FunctionTraits<OperationType>::ResultType>) // Sanity check
    JITCompiler::Call callOperationWithoutExceptionCheck(OperationType operation, Args... args)
    {
        setupArguments<OperationType>(args...);
        return appendCall(operation);
    }

    template<typename OperationType, typename ResultRegType, typename... Args>
    requires (OperationHasResult<OperationType>
        && !isExceptionOperationResult<typename FunctionTraits<OperationType>::ResultType>) // Sanity check
    void callOperationWithoutExceptionCheck(Address address, ResultRegType result, Args... args)
    {
        setupArgumentsForIndirectCall<OperationType>(address, args...);
        appendCall(Address(GPRInfo::nonArgGPR0, address.offset), result);
        setupResults(result);
    }

    // There are three cases here:
    // 1) nullopt the exception was handled
    // 2) valid GPRReg containing the exception that won't interfere with silentFill.
    // 3) InvalidGPRReg meaning the exception needs to be loaded from VM.
    template<typename OperationType, typename ResultRegType, typename... OtherSpilledRegTypes>
    std::optional<GPRReg> tryHandleOrGetExceptionUnderSilentSpill(const auto& plans, ResultRegType result, OtherSpilledRegTypes... otherSpilledRegs)
    {
        ASSERT(m_underSilentSpill);
        using ResultType = typename FunctionTraits<OperationType>::ResultType;
        GPRReg exceptionReg = operationExceptionRegister<ResultType>();
        CodeOrigin opCatchOrigin;
        HandlerInfo* exceptionHandler;
        bool willCatchException = m_graph.willCatchExceptionInMachineFrame(m_currentNode->origin.forExit, opCatchOrigin, exceptionHandler);
        // The simplest (and most common) case is when we're not going to catch in this frame, then we don't need to fill since
        // no one's going to look.
        if (!willCatchException) {
            exceptionCheck(exceptionReg);
            return std::nullopt;
        }

        if (exceptionReg != InvalidGPRReg) {
            RegisterSetBuilder spilledRegs = spilledRegsForSilentSpillPlans(plans);
            if constexpr (std::is_same_v<GPRReg, ResultRegType> || std::is_same_v<JSValueRegs, ResultRegType>) {
                spilledRegs.add(GPRInfo::returnValueGPR, IgnoreVectors);
                spilledRegs.add(result, IgnoreVectors);
            }

            if constexpr (sizeof...(OtherSpilledRegTypes) > 0) {
                constexpr auto addRegIfNeeded = [](auto& spilledRegs, auto& reg) ALWAYS_INLINE_LAMBDA {
                    static_assert(std::is_same_v<GPRReg, std::decay_t<decltype(reg)>> || std::is_same_v<JSValueRegs, std::decay_t<decltype(reg)>>);
                    spilledRegs.add(reg, IgnoreVectors);
                };
                (addRegIfNeeded(spilledRegs, otherSpilledRegs), ...);
            }

            if (spilledRegs.buildAndValidate().contains(exceptionReg, IgnoreVectors)) {
                // It would be nice if we could do m_gprs.tryAllocate() but we're possibly on a slow path and register allocation state is
                // probably garbage.
                constexpr RegisterSetBuilder registersInBank = decltype(m_gprs)::registersInBank();
                // Move to a non-constexpr local so we can call exclude.
                RegisterSetBuilder possibleRegisters = registersInBank;
                RegisterSet freeRegs = possibleRegisters.exclude(spilledRegs).buildAndValidate();
                auto iter = freeRegs.begin();
                if (iter != freeRegs.end()) {
                    move(exceptionReg, iter.gpr());
                    exceptionReg = iter.gpr();
                } else {
                    // We tried but there were no free regs.
                    exceptionReg = InvalidGPRReg;
                }
            }
        }

        return exceptionReg;
    }

    template<typename OperationType, typename ResultRegType, typename... Args>
    requires (OperationHasResult<OperationType>)
    JITCompiler::Call callOperationWithSilentSpill(OperationType operation, ResultRegType result, Args... args)
    {
        silentSpillAllRegisters(result);
        setupArguments<OperationType>(args...);
        auto call = appendCall(operation);

        std::optional<GPRReg> exceptionReg = tryHandleOrGetExceptionUnderSilentSpill<OperationType>(m_plans, result);

        setupResults(result);
        silentFillAllRegisters();
        if (exceptionReg)
            exceptionCheck(*exceptionReg);

        return call;
    }

    template<typename OperationType, typename ResultRegType, typename... Args>
    requires (OperationHasResult<OperationType>)
    JITCompiler::Call callOperationWithSilentSpill(std::span<const SilentRegisterSavePlan> plans, OperationType operation, ResultRegType result, Args... args)
    {
        silentSpill(plans);
        setupArguments<OperationType>(args...);
        auto call = appendCall(operation);

        std::optional<GPRReg> exceptionReg = tryHandleOrGetExceptionUnderSilentSpill<OperationType>(plans, result);

        setupResults(result);
        silentFill(plans);
        if (exceptionReg)
            exceptionCheck(*exceptionReg);

        return call;
    }

    template<typename OperationType, typename... Args>
    requires (OperationIsVoid<OperationType>)
    JITCompiler::Call callOperationWithSilentSpill(OperationType operation, Args... args)
    {
        silentSpillAllRegisters(InvalidGPRReg);
        setupArguments<OperationType>(args...);
        auto call = appendCall(operation);

        std::optional<GPRReg> exceptionReg = tryHandleOrGetExceptionUnderSilentSpill<OperationType>(m_plans, NoResult);

        silentFillAllRegisters();
        if (exceptionReg)
            exceptionCheck(*exceptionReg);

        return call;
    }

    template<typename OperationType, typename... Args>
    requires (OperationIsVoid<OperationType>)
    JITCompiler::Call callOperationWithSilentSpill(std::span<const SilentRegisterSavePlan> plans, OperationType operation, Args... args)
    {
        silentSpill(plans);
        setupArguments<OperationType>(args...);
        auto call = appendCall(operation);

        std::optional<GPRReg> exceptionReg = tryHandleOrGetExceptionUnderSilentSpill<OperationType>(plans, NoResult);

        silentFill(plans);
        if (exceptionReg)
            exceptionCheck(*exceptionReg);

        return call;
    }

    void prepareForExternalCall()
    {
#if !defined(NDEBUG) && !CPU(ARM_THUMB2)
        // We're about to call out to a "native" helper function. The helper
        // function is expected to set topCallFrame itself with the CallFrame
        // that is passed to it.
        //
        // We explicitly trash topCallFrame here so that we'll know if some of
        // the helper functions are not setting topCallFrame when they should
        // be doing so. Note: the previous value in topcallFrame was not valid
        // anyway since it was not being updated by JIT'ed code by design.

        for (unsigned i = 0; i < sizeof(void*) / 4; i++)
            store32(TrustedImm32(0xbadbeef), reinterpret_cast<char*>(&vm().topCallFrame) + i * 4);
#endif
        prepareCallOperation(vm());
    }

    // These methods add call instructions, optionally setting results, and optionally rolling back the call frame on an exception.
    JITCompiler::Call appendCall(const CodePtr<OperationPtrTag> function)
    {
        prepareForExternalCall();
        emitStoreCodeOrigin(m_currentNode->origin.semantic);
        return Base::appendCall(function);
    }

    void appendCall(Address address)
    {
        prepareForExternalCall();
        emitStoreCodeOrigin(m_currentNode->origin.semantic);
        Base::appendCall(address);
    }

    JITCompiler::Call appendOperationCall(const CodePtr<OperationPtrTag> function)
    {
        prepareForExternalCall();
        emitStoreCodeOrigin(m_currentNode->origin.semantic);
        return Base::appendOperationCall(function);
    }

    // FIXME: We can remove this when we don't support MSVC since on clang-cl we could use systemV ABI for JIT operations.
    JITCompiler::Call appendCallSetResult(const CodePtr<OperationPtrTag> function, GPRReg result1, GPRReg result2)
    {
        JITCompiler::Call call = appendCall(function);
        setupResults(result1, result2);
        return call;
    }

    using Base::branchDouble;
    void branchDouble(JITCompiler::DoubleCondition cond, FPRReg left, FPRReg right, BasicBlock* destination)
    {
        return addBranch(Base::branchDouble(cond, left, right), destination);
    }
    
    using Base::branchDoubleNonZero;
    void branchDoubleNonZero(FPRReg value, FPRReg scratch, BasicBlock* destination)
    {
        return addBranch(Base::branchDoubleNonZero(value, scratch), destination);
    }

    using Base::branchDoubleZeroOrNaN;
    void branchDoubleZeroOrNaN(FPRReg value, FPRReg scratch, BasicBlock* destination)
    {
        return addBranch(Base::branchDoubleZeroOrNaN(value, scratch), destination);
    }
    
    using Base::branch32;
    template<typename T, typename U>
    void branch32(JITCompiler::RelationalCondition cond, T left, U right, BasicBlock* destination)
    {
        return addBranch(Base::branch32(cond, left, right), destination);
    }
    
    using Base::branchTest32;
    template<typename T, typename U>
    void branchTest32(JITCompiler::ResultCondition cond, T value, U mask, BasicBlock* destination)
    {
        return addBranch(Base::branchTest32(cond, value, mask), destination);
    }
    
    template<typename T>
    void branchTest32(JITCompiler::ResultCondition cond, T value, BasicBlock* destination)
    {
        return addBranch(Base::branchTest32(cond, value), destination);
    }
    
#if USE(JSVALUE64)
    using Base::branch64;
    template<typename T, typename U>
    void branch64(JITCompiler::RelationalCondition cond, T left, U right, BasicBlock* destination)
    {
        return addBranch(Base::branch64(cond, left, right), destination);
    }
#endif
    
    using Base::branch8;
    template<typename T, typename U>
    void branch8(JITCompiler::RelationalCondition cond, T left, U right, BasicBlock* destination)
    {
        return addBranch(Base::branch8(cond, left, right), destination);
    }
    
    using Base::branchPtr;
    template<typename T, typename U>
    void branchPtr(JITCompiler::RelationalCondition cond, T left, U right, BasicBlock* destination)
    {
        return addBranch(Base::branchPtr(cond, left, right), destination);
    }

    using Base::branchLinkableConstant;
    template<typename T, typename U>
    void branchLinkableConstant(JITCompiler::RelationalCondition cond, T left, U right, BasicBlock* destination)
    {
        return addBranch(Base::branchLinkableConstant(cond, left, right), destination);
    }
    
    using Base::branchTestPtr;
    template<typename T, typename U>
    void branchTestPtr(JITCompiler::ResultCondition cond, T value, U mask, BasicBlock* destination)
    {
        return addBranch(Base::branchTestPtr(cond, value, mask), destination);
    }
    
    template<typename T>
    void branchTestPtr(JITCompiler::ResultCondition cond, T value, BasicBlock* destination)
    {
        return addBranch(Base::branchTestPtr(cond, value), destination);
    }
    
    using Base::branchTest8;
    template<typename T, typename U>
    void branchTest8(JITCompiler::ResultCondition cond, T value, U mask, BasicBlock* destination)
    {
        return addBranch(Base::branchTest8(cond, value, mask), destination);
    }
    
    template<typename T>
    void branchTest8(JITCompiler::ResultCondition cond, T value, BasicBlock* destination)
    {
        return addBranch(Base::branchTest8(cond, value), destination);
    }
    
    enum FallThroughMode {
        AtFallThroughPoint,
        ForceJump
    };

    using Base::jump;
    void jump(BasicBlock* destination, FallThroughMode fallThroughMode = AtFallThroughPoint)
    {
        if (destination == nextBlock()
            && fallThroughMode == AtFallThroughPoint)
            return;
        addBranch(jump(), destination);
    }
    
    void addBranch(const Jump& jump, BasicBlock* destination)
    {
        m_branches.append(BranchRecord(jump, destination));
    }
    void addBranch(const JumpList&, BasicBlock* destination);

    void linkBranches();

    void dump(const char* label = nullptr);

    bool betterUseStrictInt52(Node* node)
    {
        return !generationInfo(node).isInt52();
    }
    bool betterUseStrictInt52(Edge edge)
    {
        return betterUseStrictInt52(edge.node());
    }
    
    bool compare(Node*, RelationalCondition, DoubleCondition, S_JITOperation_GJJ);
    void compileCompareUnsigned(Node*, RelationalCondition);
    bool compilePeepHoleBranch(Node*, RelationalCondition, DoubleCondition, S_JITOperation_GJJ);
    void compilePeepHoleInt32Branch(Node*, Node* branchNode, JITCompiler::RelationalCondition);
    void compilePeepHoleInt52Branch(Node*, Node* branchNode, JITCompiler::RelationalCondition);
#if USE(BIGINT32)
    void compilePeepHoleBigInt32Branch(Node*, Node* branchNode, JITCompiler::RelationalCondition);
#endif
    void compilePeepHoleBooleanBranch(Node*, Node* branchNode, JITCompiler::RelationalCondition);
    void compilePeepHoleDoubleBranch(Node*, Node* branchNode, JITCompiler::DoubleCondition);
    void compilePeepHoleObjectEquality(Node*, Node* branchNode);
    void compilePeepHoleObjectStrictEquality(Edge objectChild, Edge otherChild, Node* branchNode);
    void compilePeepHoleObjectToObjectOrOtherEquality(Edge leftChild, Edge rightChild, Node* branchNode);
    void compileObjectEquality(Node*);
    void compileObjectStrictEquality(Edge objectChild, Edge otherChild);
    void compileObjectToObjectOrOtherEquality(Edge leftChild, Edge rightChild);
    void compileToBoolean(Node*, bool invert);
    void compileToBooleanObjectOrOther(Edge value, bool invert);
    void compileToBooleanString(Node*, bool invert);
    void compileToBooleanStringOrOther(Node*, bool invert);
    void compileStringEquality(
        Node*, GPRReg leftGPR, GPRReg rightGPR, GPRReg lengthGPR,
        GPRReg leftTempGPR, GPRReg rightTempGPR, GPRReg leftTemp2GPR,
        GPRReg rightTemp2GPR, const JITCompiler::JumpList& fastTrue,
        const JITCompiler::JumpList& fastSlow);
    void compileStringEquality(Node*);
    void compileStringIdentEquality(Node*);
    void compileStringToUntypedEquality(Node*, Edge stringEdge, Edge untypedEdge);
    void compileStringIdentToNotStringVarEquality(Node*, Edge stringEdge, Edge notStringVarEdge);
    void compileBitwiseStrictEq(Node*);

    void compileSymbolEquality(Node*);
    void compileHeapBigIntEquality(Node*);
    void compilePeepHoleSymbolEquality(Node*, Node* branchNode);
#if USE(JSVALUE64)
    void compileNeitherDoubleNorHeapBigIntToNotDoubleStrictEquality(Node*, Edge neitherDoubleNorHeapBigInt, Edge notDouble);
#endif
    void emitBitwiseJSValueEquality(JSValueRegs&, JSValueRegs&, GPRReg& result);
    void emitBranchOnBitwiseJSValueEquality(JSValueRegs&, JSValueRegs&, BasicBlock* taken, BasicBlock* notTaken);
    void compileNotDoubleNeitherDoubleNorHeapBigIntNorStringStrictEquality(Node*, Edge notDoubleEdge, Edge neitherDoubleNorHeapBigIntNorStringEdge);
    void compilePeepHoleNotDoubleNeitherDoubleNorHeapBigIntNorStringStrictEquality(Node*, Node* branchNode, Edge notDoubleEdge, Edge neitherDoubleNorHeapBigIntNorStringEdge);
    void compileSymbolUntypedEquality(Node*, Edge symbolEdge, Edge untypedEdge);

    void emitObjectOrOtherBranch(Edge value, BasicBlock* taken, BasicBlock* notTaken);
    void emitStringBranch(Edge value, BasicBlock* taken, BasicBlock* notTaken);
    void emitStringOrOtherBranch(Edge value, BasicBlock* taken, BasicBlock* notTaken);
    void emitUntypedBranch(Edge value, BasicBlock* taken, BasicBlock* notTaken);
    void emitBranch(Node*);
    
    struct StringSwitchCase {
        StringSwitchCase() { }
        
        StringSwitchCase(StringImpl* string, BasicBlock* target)
            : string(string)
            , target(target)
        {
        }
        
        bool operator<(const StringSwitchCase& other) const
        {
            return stringLessThan(*string, *other.string);
        }
        
        StringImpl* string;
        BasicBlock* target;
    };
    
    void emitSwitchIntJump(SwitchData*, GPRReg value, GPRReg scratch);
    void emitSwitchImm(Node*, SwitchData*);
    void emitSwitchCharStringJump(Node*, SwitchData*, GPRReg value, GPRReg scratch, Edge stringEdge);
    void emitSwitchChar(Node*, SwitchData*);
    void emitBinarySwitchStringRecurse(
        SwitchData*, const Vector<StringSwitchCase>&, unsigned numChecked,
        unsigned begin, unsigned end, GPRReg buffer, GPRReg length, GPRReg temp,
        unsigned alreadyCheckedLength, bool checkedExactLength);
    void emitSwitchStringOnString(Node*, SwitchData*, GPRReg string, Edge stringEdge);
    void emitSwitchString(Node*, SwitchData*);
    void emitSwitch(Node*);
    
    void compileToStringOrCallStringConstructorOrStringValueOf(Node*);
    void compileFunctionToString(Node*);
    void compileFunctionBind(Node*);
    void compileNumberToStringWithRadix(Node*);
    void compileNumberToStringWithValidRadixConstant(Node*);
    void compileNumberToStringWithValidRadixConstant(Node*, int32_t radix);
    void compileNewStringObject(Node*);
    void compileNewSymbol(Node*);
    void compileNewMap(Node*);
    void compileNewSet(Node*);
    void compileNewRegExpUntyped(Node*);

    void emitNewTypedArrayWithSizeInRegister(Node*, TypedArrayType, RegisteredStructure, GPRReg sizeGPR);
    void compileNewTypedArrayWithSize(Node*);
#if USE(LARGE_TYPED_ARRAYS)
    void compileNewTypedArrayWithInt52Size(Node*);
#endif

    void compileInt32Compare(Node*, RelationalCondition);
    void compileInt52Compare(Node*, RelationalCondition);
#if USE(BIGINT32)
    void compileBigInt32Compare(Node*, RelationalCondition);
#endif
    void compileBooleanCompare(Node*, RelationalCondition);
    void compileDoubleCompare(Node*, DoubleCondition);
    void compileStringCompare(Node*, RelationalCondition);
    void compileStringIdentCompare(Node*, RelationalCondition);
    
    bool compileStrictEq(Node*);

    void compileSameValue(Node*);
    
    void compileAllocatePropertyStorage(Node*);
    void compileReallocatePropertyStorage(Node*);
    void compileNukeStructureAndSetButterfly(Node*);
    void compileGetButterfly(Node*);
    void compileCallDOMGetter(Node*);
    void compileCallDOM(Node*);
    void compileCheckJSCast(Node*);
    void compileCallCustomAccessorGetter(Node*);
    void compileCallCustomAccessorSetter(Node*);
    void compileNormalizeMapKey(Node*);
    template<typename MapOrSet>
    ALWAYS_INLINE void compileMapGetImpl(Node*);
    void compileMapGet(Node*);
    void compileLoadMapValue(Node*);
    void compileIsEmptyStorage(Node*);
    void compileMapIteratorNext(Node*);
    void compileMapIteratorKey(Node*);
    void compileMapIteratorValue(Node*);
    template<typename Operation>
    ALWAYS_INLINE void compileMapStorageImpl(Node*, Operation, Operation);
    void compileMapStorage(Node*);
    void compileMapStorageOrSentinel(Node*);
    void compileMapIterationNext(Node*);
    void compileMapIterationEntry(Node*);
    void compileMapIterationEntryKey(Node*);
    void compileMapIterationEntryValue(Node*);
    void compileSetAdd(Node*);
    void compileMapSet(Node*);
    void compileMapOrSetDelete(Node*);
    void compileWeakMapGet(Node*);
    void compileWeakSetAdd(Node*);
    void compileWeakMapSet(Node*);
    void compileExtractValueFromWeakMapGet(Node*);
    void compileGetPrototypeOf(Node*);
    void compileGetWebAssemblyInstanceExports(Node*);
    void compileIdentity(Node*);
    
    void compileContiguousPutByVal(Node*);
    void compileDoublePutByVal(Node*);
    bool putByValWillNeedExtraRegister(ArrayMode arrayMode)
    {
        return arrayMode.mayStoreToHole();
    }
    GPRReg temporaryRegisterForPutByVal(GPRTemporary&, ArrayMode);
    GPRReg temporaryRegisterForPutByVal(GPRTemporary& temporary, Node* node)
    {
        return temporaryRegisterForPutByVal(temporary, node->arrayMode());
    }
    
    void compilePutByVal(Node*);
    void compilePutByValMegamorphic(Node*);

    // We use a scopedLambda to placate register allocation validation.
    void compileGetByVal(Node*, const ScopedLambda<std::tuple<JSValueRegs, DataFormat>(DataFormat preferredFormat, bool needsFlush)>& prefix);

    void compileGetCharCodeAt(Node*);
    void compileGetByValOnString(Node*, const ScopedLambda<std::tuple<JSValueRegs, DataFormat>(DataFormat preferredFormat, bool needsFlush)>& prefix);
    void compileFromCharCode(Node*); 
    void compileGetByValMegamorphic(Node*);

    void compileGetByValOnDirectArguments(Node*, const ScopedLambda<std::tuple<JSValueRegs, DataFormat>(DataFormat preferredFormat, bool needsFlush)>& prefix);
    void compileGetByValOnScopedArguments(Node*, const ScopedLambda<std::tuple<JSValueRegs, DataFormat>(DataFormat preferredFormat, bool needsFlush)>& prefix);

    void compileGetPrivateName(Node*);
    void compileGetPrivateNameById(Node*);
    void compileGetPrivateNameByVal(Node*, JSValueRegs base, JSValueRegs property);

    void compileGetScopeOrGetEvalScope(Node*);
    void compileSkipScope(Node*);
    void compileGetGlobalObject(Node*);
    void compileGetGlobalThis(Node*);
    void compileUnwrapGlobalProxy(Node*);

    void compileGetArrayLength(Node*);
#if USE(LARGE_TYPED_ARRAYS)
    void compileGetTypedArrayLengthAsInt52(Node*);
#endif
    void compileDataViewGetByteLength(Node*);
#if USE(LARGE_TYPED_ARRAYS)
    void compileDataViewGetByteLengthAsInt52(Node*);
#endif

    void compileCheckTypeInfoFlags(Node*);
    void compileCheckIdent(Node*);
    void compileHasStructureWithFlags(Node*);

    void compileParseInt(Node*);
    
    void compileValueRep(Node*);
    void compileDoubleRep(Node*);
    
    void compileValueToInt32(Node*);
    void compileUInt32ToNumber(Node*);
    void compileDoubleAsInt32(Node*);

    void compileValueBitNot(Node*);
    void compileBitwiseNot(Node*);

    template<typename SnippetGenerator, J_JITOperation_GJJ slowPathFunction>
    void emitUntypedOrAnyBigIntBitOp(Node*);
    void compileBitwiseOp(Node*);
    void compileValueBitwiseOp(Node*);

    void emitUntypedOrBigIntRightShiftBitOp(Node*);
    void compileValueLShiftOp(Node*);
    void compileValueBitRShift(Node*);
    void compileValueBitURShift(Node*);
    void compileShiftOp(Node*);

    template <typename Generator, typename RepatchingFunction, typename NonRepatchingFunction>
    void compileMathIC(Node*, JITBinaryMathIC<Generator>*, RepatchingFunction, NonRepatchingFunction);
    template <typename Generator, typename RepatchingFunction, typename NonRepatchingFunction>
    void compileMathIC(Node*, JITUnaryMathIC<Generator>*, RepatchingFunction, NonRepatchingFunction);

    void compileArithDoubleUnaryOp(Node*, Arith::UnaryFunction, Arith::UnaryOperation);
    void compileValueAdd(Node*);
    void compileValueSub(Node*);
    void compileArithAdd(Node*);
    void compileMakeRope(Node*);
    void compileMakeAtomString(Node*);
    void compileArithAbs(Node*);
    void compileArithClz32(Node*);
    void compileArithSub(Node*);
    void compileIncOrDec(Node*);
    void compileValueNegate(Node*);
    void compileArithNegate(Node*);
    void compileValueMul(Node*);
    void compileArithMul(Node*);
    void compileValueDiv(Node*);
    void compileArithDiv(Node*);
    void compileArithFRound(Node*);
    void compileArithF16Round(Node*);
    void compileValueMod(Node*);
    void compileArithMod(Node*);
    void compileArithPow(Node*);
    void compileValuePow(Node*);
    void compileArithRounding(Node*);
    void compileArithRandom(Node*);
    void compileArithUnary(Node*);
    void compileArithSqrt(Node*);
    void compileArithMinMax(Node*);
    void compilePurifyNaN(Node*);
    void compileConstantStoragePointer(Node*);
    void compileGetIndexedPropertyStorage(Node*);
    void compileResolveRope(Node*);
    JITCompiler::Jump jumpForTypedArrayOutOfBounds(Node*, GPRReg baseGPR, GPRReg indexGPR, GPRReg scratchGPR, GPRReg scratch2GPR);
    void compileGetTypedArrayByteOffset(Node*);
#if USE(LARGE_TYPED_ARRAYS)
    void compileGetTypedArrayByteOffsetAsInt52(Node*);
#endif
    void compileGetByValOnIntTypedArray(Node*, TypedArrayType, const ScopedLambda<std::tuple<JSValueRegs, DataFormat>(DataFormat preferredFormat, bool needsFlush)>& prefix);
    void compilePutByValForIntTypedArray(Node*, TypedArrayType);
    void compileGetByValOnFloatTypedArray(Node*, TypedArrayType, const ScopedLambda<std::tuple<JSValueRegs, DataFormat>(DataFormat preferredFormat, bool needsFlush)>& prefix);
    void compilePutByValForFloatTypedArray(Node*, TypedArrayType);
    void compileGetByValForObjectWithString(Node*, const ScopedLambda<std::tuple<JSValueRegs, DataFormat>(DataFormat preferredFormat, bool needsFlush)>& prefix);
    void compileGetByValForObjectWithSymbol(Node*, const ScopedLambda<std::tuple<JSValueRegs, DataFormat>(DataFormat preferredFormat, bool needsFlush)>& prefix);
    void compilePutByValForCellWithString(Node*);
    void compilePutByValForCellWithSymbol(Node*);
    void compileGetByValWithThis(Node*);
    void compileGetByValWithThisMegamorphic(Node*);
    void compilePutPrivateName(Node*);
    void compilePutPrivateNameById(Node*);
    void compileCheckPrivateBrand(Node*);
    void compileSetPrivateBrand(Node*);
    void compileGetByOffset(Node*);
    void compilePutByOffset(Node*);
    void compileMatchStructure(Node*);
    // If this returns false it means that we terminated speculative execution.
    bool getIntTypedArrayStoreOperand(
        GPRTemporary& value,
        GPRReg property,
#if USE(JSVALUE32_64)
        GPRTemporary& propertyTag,
        GPRTemporary& valueTag,
#endif
        Edge valueUse, JITCompiler::JumpList& slowPathCases, bool isClamped = false);
    bool getIntTypedArrayStoreOperandForAtomics(
        GPRTemporary& value,
        GPRReg property,
#if USE(JSVALUE32_64)
        GPRTemporary& propertyTag,
        GPRTemporary& valueTag,
#endif
        Edge valueUse);
    void loadFromIntTypedArray(GPRReg storageReg, GPRReg propertyReg, GPRReg resultReg, TypedArrayType);
    void setIntTypedArrayLoadResult(Node*, JSValueRegs resultRegs, TypedArrayType, bool canSpeculate, bool shouldBox, FPRReg, Jump);
    template <typename ClassType> void compileNewFunctionCommon(GPRReg, RegisteredStructure, GPRReg, GPRReg, GPRReg, JumpList&, size_t, FunctionExecutable*);
    void compileNewFunction(Node*);
    void compileSetFunctionName(Node*);
    void compileNewBoundFunction(Node*);
    void compileNewRegExp(Node*);
    void compileForwardVarargs(Node*);
    void compileVarargsLength(Node*);
    void compileLoadVarargs(Node*);
    void compileCreateActivation(Node*);
    void compileCreateDirectArguments(Node*);
    void compileGetFromArguments(Node*);
    void compilePutToArguments(Node*);
    void compileGetArgument(Node*);
    void compileCreateScopedArguments(Node*);
    void compileCreateClonedArguments(Node*);
    void compileCreateRest(Node*);
    void compileSpread(Node*);
    void compileNewArray(Node*);
    void compileNewArrayWithSpread(Node*);
    void compileGetRestLength(Node*);
    void compileArraySlice(Node*);
    void compileArraySplice(Node*);
    void compileArrayIndexOfOrArrayIncludes(Node*);
    void compileArrayPush(Node*);
    void compileNotifyWrite(Node*);
    void compileRegExpExec(Node*);
    void compileRegExpExecNonGlobalOrSticky(Node*);
    void compileRegExpMatchFast(Node*);
    void compileRegExpMatchFastGlobal(Node*);
    void compileRegExpTest(Node*);
    void compileRegExpTestInline(Node*);
    void compileRegExpSearch(Node*);
    void compileStringReplace(Node*);
    void compileStringReplaceAll(Node*);
    void compileStringReplaceString(Node*);
    void compileIsObject(Node*);
    void compileTypeOfIsObject(Node*);
    void compileIsCallable(Node*, S_JITOperation_GC);
    void compileIsConstructor(Node*);
    void compileTypeOf(Node*);
    void compileCheckIsConstant(Node*);
    void compileCheckNotEmpty(Node*);
    void compileCheckStructure(Node*);
    void emitStructureCheck(Node*, GPRReg cellGPR, GPRReg tempGPR);
    void compilePutAccessorById(Node*);
    void compilePutGetterSetterById(Node*);
    void compilePutAccessorByVal(Node*);
    void compileGetRegExpObjectLastIndex(Node*);
    void compileSetRegExpObjectLastIndex(Node*);
    void compileLazyJSConstant(Node*);
    void compileMaterializeNewObject(Node*);
    void compileMaterializeNewArrayWithConstantSize(Node*);
    void compileRecordRegExpCachedResult(Node*);
    void compileToObjectOrCallObjectConstructor(Node*);
    void compileResolveScope(Node*);
    void compileResolveScopeForHoistingFuncDeclInEval(Node*);
    void compileGetGlobalVariable(Node*);
    void compilePutGlobalVariable(Node*);
    void compileGetDynamicVar(Node*);
    void compilePutDynamicVar(Node*);
    void compileGetClosureVar(Node*);
    void compilePutClosureVar(Node*);
    void compileGetInternalField(Node*);
    void compilePutInternalField(Node*);
    void compileCompareEqPtr(Node*);
    void compileDefineDataProperty(Node*);
    void compileDefineAccessorProperty(Node*);
    void compileStringSlice(Node*);
    void compileStringSubstring(Node*);
    void compileToLowerCase(Node*);
    void compileThrow(Node*);
    void compileThrowStaticError(Node*);

    void compileExtractFromTuple(Node*);
    void compileEnumeratorNextUpdateIndexAndMode(Node*);
    void compileEnumeratorNextUpdatePropertyName(Node*);
    void compileEnumeratorGetByVal(Node*);
    template<typename SlowPathFunctionType>
    void compileEnumeratorHasProperty(Node*, SlowPathFunctionType);
    void compileEnumeratorInByVal(Node*);
    void compileEnumeratorHasOwnProperty(Node*);
    void compileEnumeratorPutByVal(Node*);

    void compilePutById(Node*);
    void compilePutByIdDirect(Node*);
    void compilePutByIdWithThis(Node*);
    void compilePutByIdMegamorphic(Node*);
    void compileGetPropertyEnumerator(Node*);
    void compileGetExecutable(Node*);
    void compileGetGetter(Node*);
    void compileGetSetter(Node*);
    void compileGetCallee(Node*);
    void compileSetCallee(Node*);
    void compileGetArgumentCountIncludingThis(Node*);
    void compileSetArgumentCountIncludingThis(Node*);
    void compileStrCat(Node*);
    void compileNewArrayBuffer(Node*);
    void compileNewArrayWithSize(Node*);
    void compileNewArrayWithConstantSizeImpl(Node*, GPRReg, GPRReg);
    void compileNewArrayWithConstantSize(Node*);
    void compileNewArrayWithSpecies(Node*);
    void compileNewArrayWithSizeAndStructure(Node*);
    void compileNewTypedArray(Node*);
    void compileNewTypedArrayBuffer(Node*);
    void compileToThis(Node*);
    void compileOwnPropertyKeysVariant(Node*);
    void compileObjectAssign(Node*);
    void compileObjectCreate(Node*);
    void compileObjectToString(Node*);
    void compileCreateThis(Node*);
    void compileCreatePromise(Node*);
    void compileCreateGenerator(Node*);
    void compileCreateAsyncGenerator(Node*);
    void compileNewObject(Node*);
    void compileNewGenerator(Node*);
    void compileNewAsyncGenerator(Node*);
    void compileNewInternalFieldObject(Node*);
    void compileToPrimitive(Node*);
    void compileToPropertyKey(Node*);
    void compileToPropertyKeyOrNumber(Node*);
    void compileToNumeric(Node*);
    void compileCallNumberConstructor(Node*);
    void compileLogShadowChickenPrologue(Node*);
    void compileLogShadowChickenTail(Node*);
    void compileHasIndexedProperty(Node*, S_JITOperation_GCZ, const ScopedLambda<std::tuple<GPRReg, GPRReg>()>& prefix, bool = false);
    void compileExtractCatchLocal(Node*);
    void compileClearCatchLocals(Node*);
    void compileProfileType(Node*);
    void compileStringCodePointAt(Node*);
    void compileStringLocaleCompare(Node*);
    void compileStringIndexOf(Node*);
    void compileDateGet(Node*);
    void compileDateSet(Node*);
    void compileGlobalIsNaN(Node*);
    void compileNumberIsNaN(Node*);
    void compileGlobalIsFinite(Node*);
    void compileNumberIsFinite(Node*);
    void compileNumberIsSafeInteger(Node*);
    void compileToIntegerOrInfinity(Node*);
    void compileToLength(Node*);

    template<typename JSClass, typename Operation>
    void compileCreateInternalFieldObject(Node*, Operation);
    template<typename JSClass, typename Operation>
    void compileNewInternalFieldObjectImpl(Node*, Operation);

    void moveTrueTo(GPRReg);
    void moveFalseTo(GPRReg);
    void blessBoolean(GPRReg);

    using Base::emitAllocateJSCell;
    // Allocator for a cell of a specific size.
    template <typename StructureType> // StructureType can be GPR or ImmPtr.
    void emitAllocateJSCell(
        GPRReg resultGPR, const JITAllocator& allocator, GPRReg allocatorGPR, StructureType structure,
        GPRReg scratchGPR, JumpList& slowPath, SlowAllocationResult slowAllocationResult = SlowAllocationResult::ClearToNull)
    {
        Base::emitAllocateJSCell(resultGPR, allocator, allocatorGPR, structure, scratchGPR, slowPath, slowAllocationResult);
    }

    using Base::emitAllocateJSObject;
    // Allocator for an object of a specific size.
    template <typename StructureType, typename StorageType> // StructureType and StorageType can be GPR or ImmPtr.
    void emitAllocateJSObject(
        GPRReg resultGPR, const JITAllocator& allocator, GPRReg allocatorGPR, StructureType structure,
        StorageType storage, GPRReg scratchGPR, JumpList& slowPath, SlowAllocationResult slowAllocationResult = SlowAllocationResult::ClearToNull)
    {
        Base::emitAllocateJSObject(
            resultGPR, allocator, allocatorGPR, structure, storage, scratchGPR, slowPath, slowAllocationResult);
    }

    using Base::emitAllocateJSObjectWithKnownSize;
    template <typename ClassType, typename StructureType, typename StorageType> // StructureType and StorageType can be GPR or ImmPtr.
    void emitAllocateJSObjectWithKnownSize(
        GPRReg resultGPR, StructureType structure, StorageType storage, GPRReg scratchGPR1,
        GPRReg scratchGPR2, JumpList& slowPath, size_t size, SlowAllocationResult slowAllocationResult = SlowAllocationResult::ClearToNull)
    {
        emitAllocateJSObjectWithKnownSize<ClassType>(vm(), resultGPR, structure, storage, scratchGPR1, scratchGPR2, slowPath, size, slowAllocationResult);
    }

    // Convenience allocator for a built-in object.
    template <typename ClassType, typename StructureType, typename StorageType> // StructureType and StorageType can be GPR or ImmPtr.
    void emitAllocateJSObject(GPRReg resultGPR, StructureType structure, StorageType storage,
        GPRReg scratchGPR1, GPRReg scratchGPR2, JumpList& slowPath, SlowAllocationResult slowAllocationResult = SlowAllocationResult::ClearToNull)
    {
        emitAllocateJSObject<ClassType>(vm(), resultGPR, structure, storage, scratchGPR1, scratchGPR2, slowPath, slowAllocationResult);
    }

    using Base::emitAllocateVariableSizedJSObject;
    template <typename ClassType, typename StructureType> // StructureType and StorageType can be GPR or ImmPtr.
    void emitAllocateVariableSizedJSObject(GPRReg resultGPR, StructureType structure, GPRReg allocationSize, GPRReg scratchGPR1, GPRReg scratchGPR2, JumpList& slowPath, SlowAllocationResult slowAllocationResult = SlowAllocationResult::ClearToNull)
    {
        emitAllocateVariableSizedJSObject<ClassType>(vm(), resultGPR, structure, allocationSize, scratchGPR1, scratchGPR2, slowPath, slowAllocationResult);
    }

    void emitAllocateRawObject(GPRReg resultGPR, RegisteredStructure, GPRReg storageGPR, unsigned numElements, unsigned vectorLength);
    
    void emitGetLength(InlineCallFrame*, GPRReg lengthGPR, bool includeThis = false);
    void emitGetLength(CodeOrigin, GPRReg lengthGPR, bool includeThis = false);
    void emitGetCallee(CodeOrigin, GPRReg calleeGPR);
    void emitGetArgumentStart(CodeOrigin, GPRReg startGPR);
    void emitPopulateSliceIndex(Edge&, std::optional<GPRReg> indexGPR, GPRReg lengthGPR, GPRReg resultGPR);
    
    // Generate an OSR exit fuzz check. Returns Jump() if OSR exit fuzz is not enabled, or if
    // it's in training mode.
    Jump emitOSRExitFuzzCheck();
    
    // Add a speculation check.
    void speculationCheck(ExitKind, JSValueSource, Node*, Jump jumpToFail);
    void speculationCheck(ExitKind, JSValueSource, Node*, const JumpList& jumpsToFail);

    // Add a speculation check without additional recovery, and with a promise to supply a jump later.
    OSRExitJumpPlaceholder speculationCheck(ExitKind, JSValueSource, Node*);
    OSRExitJumpPlaceholder speculationCheck(ExitKind, JSValueSource, Edge);
    void speculationCheck(ExitKind, JSValueSource, Edge, Jump jumpToFail);
    void speculationCheck(ExitKind, JSValueSource, Edge, const JumpList& jumpsToFail);
    // Add a speculation check with additional recovery.
    void speculationCheck(ExitKind, JSValueSource, Node*, Jump jumpToFail, const SpeculationRecovery&);
    void speculationCheck(ExitKind, JSValueSource, Edge, Jump jumpToFail, const SpeculationRecovery&);

    void speculationCheckOutOfMemory(JSValueSource, Node*, const JumpList&);
    
    void compileInvalidationPoint(Node*);
    
    void unreachable(Node*);

    // Called when we statically determine that a speculation will fail.
    void terminateUnreachableNode();
    void terminateSpeculativeExecution(ExitKind, JSValueRegs, Node*);
    void terminateSpeculativeExecution(ExitKind, JSValueRegs, Edge);
    
    // Helpers for performing type checks on an edge stored in the given registers.
    bool needsTypeCheck(Edge edge, SpeculatedType typesPassedThrough) { return m_interpreter.needsTypeCheck(edge, typesPassedThrough); }
    void typeCheck(JSValueSource, Edge, SpeculatedType typesPassedThrough, Jump jumpToFail, ExitKind = BadType);
    void typeCheck(JSValueSource, Edge, SpeculatedType typesPassedThrough, JumpList jumpListToFail, ExitKind = BadType);
    
    void speculateCellTypeWithoutTypeFiltering(Edge, GPRReg cellGPR, JSType);
    void speculateCellType(Edge, GPRReg cellGPR, SpeculatedType, JSType);
    
    void speculateInt32(Edge);
    void speculateInt32(Edge, JSValueRegs);
#if USE(JSVALUE64)
    void convertAnyInt(Edge, GPRReg resultGPR, bool canIgnoreNegativeZero);
    void speculateAnyInt(Edge);
    void speculateDoubleRepAnyInt(Edge);
#endif // USE(JSVALUE64)
#if USE(BIGINT32)
    void speculateBigInt32(Edge);
    void speculateAnyBigInt(Edge);
#endif // USE(BIGINT32)
    void speculateNumber(Edge);
    void speculateRealNumber(Edge);
    void speculateDoubleRepReal(Edge);
    void speculateBoolean(Edge);
    void speculateCell(Edge);
    void speculateCellOrOther(Edge);
    void speculateObject(Edge, GPRReg cell);
    void speculateObject(Edge);
    void speculateArray(Edge, GPRReg cell);
    void speculateArray(Edge);
    void speculateFunction(Edge, GPRReg cell);
    void speculateFunction(Edge);
    void speculateFinalObject(Edge, GPRReg cell);
    void speculateFinalObject(Edge);
    void speculateRegExpObject(Edge, GPRReg cell);
    void speculateRegExpObject(Edge);
    void speculatePromiseObject(Edge);
    void speculatePromiseObject(Edge, GPRReg cell);
    void speculateProxyObject(Edge, GPRReg cell);
    void speculateProxyObject(Edge);
    void speculateGlobalProxy(Edge, GPRReg cell);
    void speculateGlobalProxy(Edge);
    void speculateDerivedArray(Edge, GPRReg cell);
    void speculateDerivedArray(Edge);
    void speculateDateObject(Edge);
    void speculateDateObject(Edge, GPRReg cell);
    void speculateMapObject(Edge);
    void speculateImmutableButterfly(Edge, GPRReg);
    void speculateImmutableButterfly(Edge);
    void speculateMapObject(Edge, GPRReg cell);
    void speculateSetObject(Edge);
    void speculateSetObject(Edge, GPRReg cell);
    void speculateMapIteratorObject(Edge);
    void speculateMapIteratorObject(Edge, GPRReg cell);
    void speculateSetIteratorObject(Edge);
    void speculateSetIteratorObject(Edge, GPRReg cell);
    void speculateWeakMapObject(Edge);
    void speculateWeakMapObject(Edge, GPRReg cell);
    void speculateWeakSetObject(Edge);
    void speculateWeakSetObject(Edge, GPRReg cell);
    void speculateDataViewObject(Edge);
    void speculateDataViewObject(Edge, GPRReg cell);
    void speculateObjectOrOther(Edge);
    void speculateString(Edge edge, GPRReg cell);
    void speculateStringIdentAndLoadStorage(Edge edge, GPRReg string, GPRReg storage);
    void speculateStringIdent(Edge edge, GPRReg string);
    void speculateStringIdent(Edge);
    void speculateString(Edge);
    void speculateStringOrOther(Edge, JSValueRegs, GPRReg scratch);
    void speculateStringOrOther(Edge);
    void speculateNotStringVar(Edge);
    void speculateNotSymbol(Edge);
    void speculateStringObject(Edge, GPRReg);
    void speculateStringObject(Edge);
    void speculateStringOrStringObject(Edge);
    void speculateSymbol(Edge, GPRReg cell);
    void speculateSymbol(Edge);
    void speculateHeapBigInt(Edge, GPRReg cell);
    void speculateHeapBigInt(Edge);
    void speculateNotCell(Edge, JSValueRegs);
    void speculateNotCell(Edge);
    void speculateNotCellNorBigInt(Edge);
    void speculateNotDouble(Edge, JSValueRegs, GPRReg temp);
    void speculateNotDouble(Edge);
    void speculateNeitherDoubleNorHeapBigInt(Edge, JSValueRegs, GPRReg temp);
    void speculateNeitherDoubleNorHeapBigInt(Edge);
    void speculateNeitherDoubleNorHeapBigIntNorString(Edge, JSValueRegs, GPRReg temp);
    void speculateNeitherDoubleNorHeapBigIntNorString(Edge);
    void speculateOther(Edge, JSValueRegs, GPRReg temp);
    void speculateOther(Edge, JSValueRegs);
    void speculateOther(Edge);
    void speculateMisc(Edge, JSValueRegs);
    void speculateMisc(Edge);
    void speculate(Node*, Edge);
    
    JITCompiler::JumpList jumpSlowForUnwantedArrayMode(GPRReg tempWithIndexingTypeReg, ArrayMode);
    void checkArray(Node*);
    void arrayify(Node*, GPRReg baseReg, GPRReg propertyReg);
    void arrayify(Node*);

    unsigned appendOSRExit(OSRExit&&, bool isExceptionHandler = false);
    unsigned appendExceptionHandlingOSRExit(ExitKind, unsigned eventStreamIndex, CodeOrigin, HandlerInfo* exceptionHandler, CallSiteIndex, MacroAssembler::JumpList jumpsToFail = MacroAssembler::JumpList());

#if USE(JSVALUE64)
    void unboxRealNumberDouble(Node*, FPRReg boxedFPR, FPRReg resultFPR, GPRReg scratchGPR);
    void boxDoubleAsDouble(FPRReg inputFPR, FPRReg resultFPR);
#endif

    template<bool strict>
    GPRReg fillSpeculateInt32Internal(Edge, DataFormat& returnFormat);
    
    void cageTypedArrayStorage(GPRReg, GPRReg);
    
    void recordSetLocal(
        Operand bytecodeReg, VirtualRegister machineReg, DataFormat format)
    {
        ASSERT(!bytecodeReg.isArgument() || bytecodeReg.virtualRegister().toArgument() >= 0);
        m_stream.appendAndLog(VariableEvent::setLocal(bytecodeReg, machineReg, format));
    }
    
    void recordSetLocal(DataFormat format)
    {
        VariableAccessData* variable = m_currentNode->variableAccessData();
        recordSetLocal(variable->operand(), variable->machineLocal(), format);
    }

    GenerationInfo& generationInfoFromVirtualRegister(VirtualRegister virtualRegister)
    {
        return m_generationInfo[virtualRegister.toLocal()];
    }
    
    GenerationInfo& generationInfo(Node* node)
    {
        return generationInfoFromVirtualRegister(node->virtualRegister());
    }
    
    GenerationInfo& generationInfo(Edge edge)
    {
        return generationInfo(edge.node());
    }

    Graph& m_graph;

    // The current node being generated.
    BasicBlock* m_block;
    Node* m_currentNode;
    NodeType m_lastGeneratedNode;
    unsigned m_indexInBlock;

    // Virtual and physical register maps.
    Vector<GenerationInfo, 32> m_generationInfo;
    RegisterBank<GPRInfo> m_gprs;
    RegisterBank<FPRInfo> m_fprs;

    // It is possible, during speculative generation, to reach a situation in which we
    // can statically determine a speculation will fail (for example, when two nodes
    // will make conflicting speculations about the same operand). In such cases this
    // flag is cleared, indicating no further code generation should take place.
    bool m_compileOkay;

    Vector<Label> m_osrEntryHeads;
    
    struct BranchRecord {
        BranchRecord(Jump jump, BasicBlock* destination)
            : jump(jump)
            , destination(destination)
        {
        }

        Jump jump;
        BasicBlock* destination;
    };
    Vector<BranchRecord, 8> m_branches;

    NodeOrigin m_origin;
    
    InPlaceAbstractState m_state;
    AbstractInterpreter<InPlaceAbstractState> m_interpreter;
    
    VariableEventStreamBuilder m_stream;
    MinifiedGraph* m_minifiedGraph;
    
    Vector<std::unique_ptr<SlowPathGenerator>, 8> m_slowPathGenerators;
    struct SlowPathLambda {
        Function<void()> generator;
        Node* currentNode;
        unsigned streamIndex;
    };
    Vector<SlowPathLambda> m_slowPathLambdas;
    Vector<SilentRegisterSavePlan> m_plans;
    bool m_underSilentSpill { false };
    std::optional<unsigned> m_outOfLineStreamIndex;
};


// === Operand types ===
//
// These classes are used to lock the operands to a node into machine
// registers. These classes implement of pattern of locking a value
// into register at the point of construction only if it is already in
// registers, and otherwise loading it lazily at the point it is first
// used. We do so in order to attempt to avoid spilling one operand
// in order to make space available for another.

class JSValueOperand {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(JSValueOperand);
public:
    explicit JSValueOperand(SpeculativeJIT* jit, Edge edge, OperandSpeculationMode mode = AutomaticOperandSpeculation)
        : m_jit(jit)
        , m_edge(edge)
#if USE(JSVALUE64)
        , m_gprOrInvalid(InvalidGPRReg)
#elif USE(JSVALUE32_64)
        , m_isDouble(false)
#endif
    {
        ASSERT(m_jit);
        if (!edge)
            return;
        ASSERT_UNUSED(mode, mode == ManualOperandSpeculation || edge.useKind() == UntypedUse);
#if USE(JSVALUE64)
        if (jit->isFilled(node()))
            gpr();
#elif USE(JSVALUE32_64)
        m_register.pair.tagGPR = InvalidGPRReg;
        m_register.pair.payloadGPR = InvalidGPRReg;
        if (jit->isFilled(node()))
            fill();
#endif
    }

    explicit JSValueOperand(JSValueOperand&& other)
        : m_jit(other.m_jit)
        , m_edge(other.m_edge)
    {
#if USE(JSVALUE64)
        m_gprOrInvalid = other.m_gprOrInvalid;
#elif USE(JSVALUE32_64)
        m_register.pair.tagGPR = InvalidGPRReg;
        m_register.pair.payloadGPR = InvalidGPRReg;
        m_isDouble = other.m_isDouble;

        if (m_edge) {
            if (m_isDouble)
                m_register.fpr = other.m_register.fpr;
            else
                m_register.pair = other.m_register.pair;
        }
#endif
        other.m_edge = Edge();
#if USE(JSVALUE64)
        other.m_gprOrInvalid = InvalidGPRReg;
#elif USE(JSVALUE32_64)
        other.m_isDouble = false;
#endif
    }

    ~JSValueOperand()
    {
        if (!m_edge)
            return;
#if USE(JSVALUE64)
        ASSERT(m_gprOrInvalid != InvalidGPRReg);
        m_jit->unlock(m_gprOrInvalid);
#elif USE(JSVALUE32_64)
        if (m_isDouble) {
            ASSERT(m_register.fpr != InvalidFPRReg);
            m_jit->unlock(m_register.fpr);
        } else {
            ASSERT(m_register.pair.tagGPR != InvalidGPRReg && m_register.pair.payloadGPR != InvalidGPRReg);
            m_jit->unlock(m_register.pair.tagGPR);
            m_jit->unlock(m_register.pair.payloadGPR);
        }
#endif
    }
    
    Edge edge() const
    {
        return m_edge;
    }

    Node* node() const
    {
        return edge().node();
    }

    JSValueRegs regs() { return jsValueRegs(); }

#if USE(JSVALUE64)
    GPRReg gpr()
    {
        if (m_gprOrInvalid == InvalidGPRReg)
            m_gprOrInvalid = m_jit->fillJSValue(m_edge);
        return m_gprOrInvalid;
    }
    JSValueRegs jsValueRegs()
    {
        return JSValueRegs(gpr());
    }
#elif USE(JSVALUE32_64)
    bool isDouble() { return m_isDouble; }

    void fill()
    {
        if (m_register.pair.tagGPR == InvalidGPRReg && m_register.pair.payloadGPR == InvalidGPRReg)
            m_isDouble = !m_jit->fillJSValue(m_edge, m_register.pair.tagGPR, m_register.pair.payloadGPR, m_register.fpr);
    }

    GPRReg tagGPR()
    {
        fill();
        ASSERT(!m_isDouble);
        return m_register.pair.tagGPR;
    } 

    GPRReg payloadGPR()
    {
        fill();
        ASSERT(!m_isDouble);
        return m_register.pair.payloadGPR;
    }
    
    JSValueRegs jsValueRegs()
    {
        return JSValueRegs(tagGPR(), payloadGPR());
    }

    GPRReg gpr(WhichValueWord which = PayloadWord)
    {
        return jsValueRegs().gpr(which);
    }

    FPRReg fpr()
    {
        fill();
        ASSERT(m_isDouble);
        return m_register.fpr;
    }
#endif

    void use()
    {
        m_jit->use(node());
    }

private:
    SpeculativeJIT* m_jit;
    Edge m_edge;
#if USE(JSVALUE64)
    GPRReg m_gprOrInvalid;
#elif USE(JSVALUE32_64)
    union {
        struct {
            GPRReg tagGPR;
            GPRReg payloadGPR;
        } pair;
        FPRReg fpr;
    } m_register;
    bool m_isDouble;
#endif
};

class StorageOperand {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(StorageOperand);
public:
    StorageOperand() = default;

    explicit StorageOperand(SpeculativeJIT* jit, Edge edge)
        : m_jit(jit)
        , m_edge(edge)
        , m_gprOrInvalid(InvalidGPRReg)
    {
        emplace(jit, edge);
    }
    
    ~StorageOperand()
    {
        if (m_gprOrInvalid != InvalidGPRReg)
            m_jit->unlock(m_gprOrInvalid);
    }
    
    Edge edge() const
    {
        return m_edge;
    }
    
    Node* node() const
    {
        return edge().node();
    }
    
    GPRReg gpr()
    {
        if (m_gprOrInvalid == InvalidGPRReg)
            m_gprOrInvalid = m_jit->fillStorage(edge());
        return m_gprOrInvalid;
    }

    void emplace(SpeculativeJIT* jit, Edge edge)
    {
        m_jit = jit;
        m_edge = edge;
        ASSERT(m_gprOrInvalid == InvalidGPRReg);
        ASSERT(m_jit);
        ASSERT(edge.useKind() == UntypedUse || edge.useKind() == KnownCellUse);
        if (jit->isFilled(node()))
            gpr();
    }
    
    void use()
    {
        m_jit->use(node());
    }
    
private:
    SpeculativeJIT* m_jit { nullptr };
    Edge m_edge;
    GPRReg m_gprOrInvalid { InvalidGPRReg };
};


// === Temporaries ===
//
// These classes are used to allocate temporary registers.
// A mechanism is provided to attempt to reuse the registers
// currently allocated to child nodes whose value is consumed
// by, and not live after, this operation.

enum ReuseTag { Reuse };

class GPRTemporary {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(GPRTemporary);
public:
    GPRTemporary();
    GPRTemporary(SpeculativeJIT*);
    GPRTemporary(SpeculativeJIT*, GPRReg specific);
    template<typename T>
    GPRTemporary(SpeculativeJIT* jit, ReuseTag, T& operand)
        : m_jit(jit)
        , m_gpr(InvalidGPRReg)
    {
        if (m_jit->canReuse(operand.node()))
            m_gpr = m_jit->reuse(operand.gpr());
        else
            m_gpr = m_jit->allocate();
    }
    template<typename T1, typename T2>
    GPRTemporary(SpeculativeJIT* jit, ReuseTag, T1& op1, T2& op2)
        : m_jit(jit)
        , m_gpr(InvalidGPRReg)
    {
        if (m_jit->canReuse(op1.node()))
            m_gpr = m_jit->reuse(op1.gpr());
        else if (m_jit->canReuse(op2.node()))
            m_gpr = m_jit->reuse(op2.gpr());
        else if (m_jit->canReuse(op1.node(), op2.node()) && op1.gpr() == op2.gpr())
            m_gpr = m_jit->reuse(op1.gpr());
        else
            m_gpr = m_jit->allocate();
    }
    GPRTemporary(SpeculativeJIT*, ReuseTag, JSValueOperand&, WhichValueWord);

    GPRTemporary(const GPRTemporary&) = delete;

    GPRTemporary(GPRTemporary&& other)
    {
        ASSERT(other.m_jit);
        ASSERT(other.m_gpr != InvalidGPRReg);
        m_jit = other.m_jit;
        m_gpr = other.m_gpr;
        other.m_jit = nullptr;
        other.m_gpr = InvalidGPRReg;
    }

    GPRTemporary& operator=(GPRTemporary&& other)
    {
        ASSERT(!m_jit);
        ASSERT(m_gpr == InvalidGPRReg);
        std::swap(m_jit, other.m_jit);
        std::swap(m_gpr, other.m_gpr);
        return *this;
    }

    void adopt(GPRTemporary&);

    ~GPRTemporary()
    {
        if (m_jit && m_gpr != InvalidGPRReg)
            m_jit->unlock(gpr());
    }

    GPRReg gpr()
    {
        return m_gpr;
    }

private:
    SpeculativeJIT* m_jit;
    GPRReg m_gpr;
};

class JSValueRegsTemporary {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(JSValueRegsTemporary);
public:
    JSValueRegsTemporary();
    JSValueRegsTemporary(SpeculativeJIT*);
    template<typename T>
    JSValueRegsTemporary(SpeculativeJIT*, ReuseTag, T& operand, WhichValueWord resultRegWord = PayloadWord);
    JSValueRegsTemporary(SpeculativeJIT*, ReuseTag, JSValueOperand&);
    ~JSValueRegsTemporary();
    
    explicit operator bool() { return !!regs(); }

    JSValueRegsTemporary& operator=(JSValueRegsTemporary&&) = default;

    JSValueRegs regs();

private:
#if USE(JSVALUE64)
    GPRTemporary m_gpr;
#else
    GPRTemporary m_payloadGPR;
    GPRTemporary m_tagGPR;
#endif
};

#if USE(JSVALUE64)
template<typename T>
JSValueRegsTemporary::JSValueRegsTemporary(SpeculativeJIT* jit, ReuseTag, T& operand, WhichValueWord)
    : m_gpr(jit, Reuse, operand)
{
}
#else
template<typename T>
JSValueRegsTemporary::JSValueRegsTemporary(SpeculativeJIT* jit, ReuseTag, T& operand, WhichValueWord resultWord)
{
    if (resultWord == PayloadWord) {
        m_payloadGPR = GPRTemporary(jit, Reuse, operand);
        m_tagGPR = GPRTemporary(jit);
    } else {
        m_payloadGPR = GPRTemporary(jit);
        m_tagGPR = GPRTemporary(jit, Reuse, operand);
    }
}
#endif

class FPRTemporary {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(FPRTemporary);
public:
    FPRTemporary(FPRTemporary&&);
    FPRTemporary(SpeculativeJIT*);
    FPRTemporary(SpeculativeJIT*, SpeculateDoubleOperand&);
    FPRTemporary(SpeculativeJIT*, SpeculateDoubleOperand&, SpeculateDoubleOperand&);
#if USE(JSVALUE32_64)
    FPRTemporary(SpeculativeJIT*, JSValueOperand&);
#endif

    ~FPRTemporary()
    {
        if (m_jit) [[likely]]
            m_jit->unlock(fpr());
    }

    FPRReg fpr() const
    {
        ASSERT(m_jit);
        ASSERT(m_fpr != InvalidFPRReg);
        return m_fpr;
    }

protected:
    FPRTemporary(SpeculativeJIT* jit, FPRReg lockedFPR)
        : m_jit(jit)
        , m_fpr(lockedFPR)
    {
    }

private:
    SpeculativeJIT* m_jit;
    FPRReg m_fpr;
};


// === Results ===
//
// These classes lock the result of a call to a C++ helper function.

class GPRFlushedCallResult : public GPRTemporary {
public:
    GPRFlushedCallResult(SpeculativeJIT* jit)
        : GPRTemporary(jit, GPRInfo::returnValueGPR)
    {
    }
};

class GPRFlushedCallResult2 : public GPRTemporary {
public:
    GPRFlushedCallResult2(SpeculativeJIT* jit)
        : GPRTemporary(jit, GPRInfo::returnValueGPR2)
    {
    }
};

class FPRResult : public FPRTemporary {
public:
    FPRResult(SpeculativeJIT* jit)
        : FPRTemporary(jit, lockedResult(jit))
    {
    }

private:
    static FPRReg lockedResult(SpeculativeJIT* jit)
    {
        jit->lock(FPRInfo::returnValueFPR);
        return FPRInfo::returnValueFPR;
    }
};

class JSValueRegsFlushedCallResult {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(JSValueRegsFlushedCallResult);
public:
    JSValueRegsFlushedCallResult(SpeculativeJIT* jit)
#if USE(JSVALUE64)
        : m_gpr(jit)
#else
        : m_payloadGPR(jit)
        , m_tagGPR(jit)
#endif
    {
    }

    JSValueRegs regs()
    {
#if USE(JSVALUE64)
        return JSValueRegs { m_gpr.gpr() };
#else
        return JSValueRegs { m_tagGPR.gpr(), m_payloadGPR.gpr() };
#endif
    }

private:
#if USE(JSVALUE64)
    GPRFlushedCallResult m_gpr;
#else
    GPRFlushedCallResult m_payloadGPR;
    GPRFlushedCallResult2 m_tagGPR;
#endif
};


// === Speculative Operand types ===
//
// SpeculateInt32Operand, SpeculateStrictInt32Operand and SpeculateCellOperand.
//
// These are used to lock the operands to a node into machine registers within the
// SpeculativeJIT. The classes operate like those above, however these will
// perform a speculative check for a more restrictive type than we can statically
// determine the operand to have. If the operand does not have the requested type,
// a bail-out to the non-speculative path will be taken.

class SpeculateInt32Operand {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(SpeculateInt32Operand);
public:
    explicit SpeculateInt32Operand(SpeculativeJIT* jit, Edge edge, OperandSpeculationMode mode = AutomaticOperandSpeculation)
        : m_jit(jit)
        , m_edge(edge)
        , m_gprOrInvalid(InvalidGPRReg)
#ifndef NDEBUG
        , m_format(DataFormatNone)
#endif
    {
        ASSERT(m_jit);
        ASSERT_UNUSED(mode, mode == ManualOperandSpeculation || (edge.useKind() == Int32Use || edge.useKind() == KnownInt32Use));
        if (jit->isFilled(node()))
            gpr();
    }

    ~SpeculateInt32Operand()
    {
        ASSERT(m_gprOrInvalid != InvalidGPRReg);
        m_jit->unlock(m_gprOrInvalid);
    }
    
    Edge edge() const
    {
        return m_edge;
    }

    Node* node() const
    {
        return edge().node();
    }

    DataFormat format()
    {
        gpr(); // m_format is set when m_gpr is locked.
        ASSERT(m_format == DataFormatInt32 || m_format == DataFormatJSInt32);
        return m_format;
    }

    GPRReg gpr()
    {
        if (m_gprOrInvalid == InvalidGPRReg)
            m_gprOrInvalid = m_jit->fillSpeculateInt32(edge(), m_format);
        return m_gprOrInvalid;
    }
    
    void use()
    {
        m_jit->use(node());
    }

private:
    SpeculativeJIT* m_jit;
    Edge m_edge;
    GPRReg m_gprOrInvalid;
    DataFormat m_format;
};

class SpeculateStrictInt32Operand {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(SpeculateStrictInt32Operand);
public:
    explicit SpeculateStrictInt32Operand(SpeculativeJIT* jit, Edge edge, OperandSpeculationMode mode = AutomaticOperandSpeculation)
        : m_jit(jit)
        , m_edge(edge)
        , m_gprOrInvalid(InvalidGPRReg)
    {
        ASSERT(m_jit);
        ASSERT_UNUSED(mode, mode == ManualOperandSpeculation || (edge.useKind() == Int32Use || edge.useKind() == KnownInt32Use));
        if (jit->isFilled(node()))
            gpr();
    }

    ~SpeculateStrictInt32Operand()
    {
        ASSERT(m_gprOrInvalid != InvalidGPRReg);
        m_jit->unlock(m_gprOrInvalid);
    }
    
    Edge edge() const
    {
        return m_edge;
    }

    Node* node() const
    {
        return edge().node();
    }

    GPRReg gpr()
    {
        if (m_gprOrInvalid == InvalidGPRReg)
            m_gprOrInvalid = m_jit->fillSpeculateInt32Strict(edge());
        return m_gprOrInvalid;
    }
    
    void use()
    {
        m_jit->use(node());
    }

private:
    SpeculativeJIT* m_jit;
    Edge m_edge;
    GPRReg m_gprOrInvalid;
};

// Gives you a canonical Int52 (i.e. it's left-shifted by 12, low bits zero).
class SpeculateInt52Operand {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(SpeculateInt52Operand);
public:
    explicit SpeculateInt52Operand(SpeculativeJIT* jit, Edge edge)
        : m_jit(jit)
        , m_edge(edge)
        , m_gprOrInvalid(InvalidGPRReg)
    {
        RELEASE_ASSERT(edge.useKind() == Int52RepUse);
        if (jit->isFilled(node()))
            gpr();
    }
    
    ~SpeculateInt52Operand()
    {
        ASSERT(m_gprOrInvalid != InvalidGPRReg);
        m_jit->unlock(m_gprOrInvalid);
    }
    
    Edge edge() const
    {
        return m_edge;
    }
    
    Node* node() const
    {
        return edge().node();
    }
    
    GPRReg gpr()
    {
        if (m_gprOrInvalid == InvalidGPRReg)
            m_gprOrInvalid = m_jit->fillSpeculateInt52(edge(), DataFormatInt52);
        return m_gprOrInvalid;
    }
    
    void use()
    {
        m_jit->use(node());
    }
    
private:
    SpeculativeJIT* m_jit;
    Edge m_edge;
    GPRReg m_gprOrInvalid;
};

// Gives you a strict Int52 (i.e. the payload is in the low 52 bits, high 12 bits are sign-extended).
class SpeculateStrictInt52Operand {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(SpeculateStrictInt52Operand);
public:
    explicit SpeculateStrictInt52Operand(SpeculativeJIT* jit, Edge edge)
        : m_jit(jit)
        , m_edge(edge)
        , m_gprOrInvalid(InvalidGPRReg)
    {
        RELEASE_ASSERT(edge.useKind() == Int52RepUse);
        if (jit->isFilled(node()))
            gpr();
    }
    
    ~SpeculateStrictInt52Operand()
    {
        ASSERT(m_gprOrInvalid != InvalidGPRReg);
        m_jit->unlock(m_gprOrInvalid);
    }
    
    Edge edge() const
    {
        return m_edge;
    }
    
    Node* node() const
    {
        return edge().node();
    }
    
    GPRReg gpr()
    {
        if (m_gprOrInvalid == InvalidGPRReg)
            m_gprOrInvalid = m_jit->fillSpeculateInt52(edge(), DataFormatStrictInt52);
        return m_gprOrInvalid;
    }
    
    void use()
    {
        m_jit->use(node());
    }
    
private:
    SpeculativeJIT* m_jit;
    Edge m_edge;
    GPRReg m_gprOrInvalid;
};

enum OppositeShiftTag { OppositeShift };

class SpeculateWhicheverInt52Operand {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(SpeculateWhicheverInt52Operand);
public:
    explicit SpeculateWhicheverInt52Operand(SpeculativeJIT* jit, Edge edge)
        : m_jit(jit)
        , m_edge(edge)
        , m_gprOrInvalid(InvalidGPRReg)
        , m_strict(jit->betterUseStrictInt52(edge))
    {
        RELEASE_ASSERT(edge.useKind() == Int52RepUse);
        if (jit->isFilled(node()))
            gpr();
    }
    
    explicit SpeculateWhicheverInt52Operand(SpeculativeJIT* jit, Edge edge, const SpeculateWhicheverInt52Operand& other)
        : m_jit(jit)
        , m_edge(edge)
        , m_gprOrInvalid(InvalidGPRReg)
        , m_strict(other.m_strict)
    {
        RELEASE_ASSERT(edge.useKind() == Int52RepUse);
        if (jit->isFilled(node()))
            gpr();
    }
    
    explicit SpeculateWhicheverInt52Operand(SpeculativeJIT* jit, Edge edge, OppositeShiftTag, const SpeculateWhicheverInt52Operand& other)
        : m_jit(jit)
        , m_edge(edge)
        , m_gprOrInvalid(InvalidGPRReg)
        , m_strict(!other.m_strict)
    {
        RELEASE_ASSERT(edge.useKind() == Int52RepUse);
        if (jit->isFilled(node()))
            gpr();
    }
    
    ~SpeculateWhicheverInt52Operand()
    {
        ASSERT(m_gprOrInvalid != InvalidGPRReg);
        m_jit->unlock(m_gprOrInvalid);
    }
    
    Edge edge() const
    {
        return m_edge;
    }
    
    Node* node() const
    {
        return edge().node();
    }
    
    GPRReg gpr()
    {
        if (m_gprOrInvalid == InvalidGPRReg) {
            m_gprOrInvalid = m_jit->fillSpeculateInt52(
                edge(), m_strict ? DataFormatStrictInt52 : DataFormatInt52);
        }
        return m_gprOrInvalid;
    }
    
    void use()
    {
        m_jit->use(node());
    }
    
    DataFormat format() const
    {
        return m_strict ? DataFormatStrictInt52 : DataFormatInt52;
    }

private:
    SpeculativeJIT* m_jit;
    Edge m_edge;
    GPRReg m_gprOrInvalid;
    bool m_strict;
};

class SpeculateDoubleOperand {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(SpeculateDoubleOperand);
public:
    explicit SpeculateDoubleOperand(SpeculativeJIT* jit, Edge edge)
        : m_jit(jit)
        , m_edge(edge)
        , m_fprOrInvalid(InvalidFPRReg)
    {
        ASSERT(m_jit);
        RELEASE_ASSERT(isDouble(edge.useKind()));
        if (jit->isFilled(node()))
            fpr();
    }

    ~SpeculateDoubleOperand()
    {
        ASSERT(m_fprOrInvalid != InvalidFPRReg);
        m_jit->unlock(m_fprOrInvalid);
    }
    
    Edge edge() const
    {
        return m_edge;
    }

    Node* node() const
    {
        return edge().node();
    }

    FPRReg fpr()
    {
        if (m_fprOrInvalid == InvalidFPRReg)
            m_fprOrInvalid = m_jit->fillSpeculateDouble(edge());
        return m_fprOrInvalid;
    }
    
    void use()
    {
        m_jit->use(node());
    }

private:
    SpeculativeJIT* m_jit;
    Edge m_edge;
    FPRReg m_fprOrInvalid;
};

class SpeculateCellOperand {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(SpeculateCellOperand);

public:
    explicit SpeculateCellOperand(SpeculativeJIT* jit, Edge edge, OperandSpeculationMode mode = AutomaticOperandSpeculation)
        : m_jit(jit)
        , m_edge(edge)
        , m_gprOrInvalid(InvalidGPRReg)
    {
        ASSERT(m_jit);
        if (!edge)
            return;
        ASSERT_UNUSED(mode, mode == ManualOperandSpeculation || isCell(edge.useKind()));
        if (jit->isFilled(node()))
            gpr();
    }

    explicit SpeculateCellOperand(SpeculateCellOperand&& other)
    {
        m_jit = other.m_jit;
        m_edge = other.m_edge;
        m_gprOrInvalid = other.m_gprOrInvalid;

        other.m_gprOrInvalid = InvalidGPRReg;
        other.m_edge = Edge();
    }

    ~SpeculateCellOperand()
    {
        if (!m_edge)
            return;
        ASSERT(m_gprOrInvalid != InvalidGPRReg);
        m_jit->unlock(m_gprOrInvalid);
    }
    
    Edge edge() const
    {
        return m_edge;
    }

    Node* node() const
    {
        return edge().node();
    }

    GPRReg gpr()
    {
        ASSERT(m_edge);
        if (m_gprOrInvalid == InvalidGPRReg)
            m_gprOrInvalid = m_jit->fillSpeculateCell(edge());
        return m_gprOrInvalid;
    }
    
    void use()
    {
        ASSERT(m_edge);
        m_jit->use(node());
    }

private:
    SpeculativeJIT* m_jit;
    Edge m_edge;
    GPRReg m_gprOrInvalid;
};

class SpeculateBooleanOperand {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(SpeculateBooleanOperand);
public:
    explicit SpeculateBooleanOperand(SpeculativeJIT* jit, Edge edge, OperandSpeculationMode mode = AutomaticOperandSpeculation)
        : m_jit(jit)
        , m_edge(edge)
        , m_gprOrInvalid(InvalidGPRReg)
    {
        ASSERT(m_jit);
        ASSERT_UNUSED(mode, mode == ManualOperandSpeculation || edge.useKind() == BooleanUse || edge.useKind() == KnownBooleanUse);
        if (jit->isFilled(node()))
            gpr();
    }
    
    ~SpeculateBooleanOperand()
    {
        ASSERT(m_gprOrInvalid != InvalidGPRReg);
        m_jit->unlock(m_gprOrInvalid);
    }
    
    Edge edge() const
    {
        return m_edge;
    }
    
    Node* node() const
    {
        return edge().node();
    }
    
    GPRReg gpr()
    {
        if (m_gprOrInvalid == InvalidGPRReg)
            m_gprOrInvalid = m_jit->fillSpeculateBoolean(edge());
        return m_gprOrInvalid;
    }
    
    void use()
    {
        m_jit->use(node());
    }

private:
    SpeculativeJIT* m_jit;
    Edge m_edge;
    GPRReg m_gprOrInvalid;
};

#if USE(BIGINT32)
class SpeculateBigInt32Operand {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED(SpeculateBigInt32Operand);
public:
    explicit SpeculateBigInt32Operand(SpeculativeJIT* jit, Edge edge, OperandSpeculationMode mode = AutomaticOperandSpeculation)
        : m_jit(jit)
        , m_edge(edge)
        , m_gprOrInvalid(InvalidGPRReg)
    {
        ASSERT(m_jit);
        ASSERT_UNUSED(mode, mode == ManualOperandSpeculation || edge.useKind() == BigInt32Use);
        if (jit->isFilled(node()))
            gpr();
    }

    ~SpeculateBigInt32Operand()
    {
        ASSERT(m_gprOrInvalid != InvalidGPRReg);
        m_jit->unlock(m_gprOrInvalid);
    }

    Edge edge() const
    {
        return m_edge;
    }

    Node* node() const
    {
        return edge().node();
    }

    GPRReg gpr()
    {
        if (m_gprOrInvalid == InvalidGPRReg)
            m_gprOrInvalid = m_jit->fillSpeculateBigInt32(edge());
        return m_gprOrInvalid;
    }

    void use()
    {
        m_jit->use(node());
    }

private:
    SpeculativeJIT* m_jit;
    Edge m_edge;
    GPRReg m_gprOrInvalid;
};
#endif // USE(BIGINT32)

#define DFG_TYPE_CHECK_WITH_EXIT_KIND(exitKind, source, edge, typesPassedThrough, jumpToFail) do { \
        JSValueSource _dtc_source = (source);                           \
        Edge _dtc_edge = (edge);                                        \
        SpeculatedType _dtc_typesPassedThrough = typesPassedThrough;    \
        if (!needsTypeCheck(_dtc_edge, _dtc_typesPassedThrough))        \
            break;                                                      \
        typeCheck(_dtc_source, _dtc_edge, _dtc_typesPassedThrough, (jumpToFail), exitKind); \
    } while (0)

#define DFG_TYPE_CHECK(source, edge, typesPassedThrough, jumpToFail) \
    DFG_TYPE_CHECK_WITH_EXIT_KIND(BadType, source, edge, typesPassedThrough, jumpToFail)

} } // namespace JSC::DFG

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

#endif
