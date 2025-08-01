/*
 * Copyright (C) 2013-2021 Apple Inc. All rights reserved.
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

#include "DFGAbstractHeap.h"
#include "DFGGraph.h"
#include "DFGHeapLocation.h"
#include "DFGLazyNode.h"
#include "DFGPureValue.h"
#include "DOMJITCallDOMGetterSnippet.h"
#include "DOMJITSignature.h"
#include "InlineCallFrame.h"
#include "JSImmutableButterfly.h"

namespace JSC { namespace DFG {

template<typename ReadFunctor, typename WriteFunctor, typename DefFunctor>
void clobberize(Graph& graph, Node* node, const ReadFunctor& read, const WriteFunctor& write, const DefFunctor& def)
{
    clobberize(graph, node, read, write, def, [] { });
}

template<typename ReadFunctor, typename WriteFunctor, typename DefFunctor, typename ClobberTopFunctor>
void clobberize(Graph& graph, Node* node, const ReadFunctor& read, const WriteFunctor& write, const DefFunctor& def, const ClobberTopFunctor& clobberTopFunctor)
{
    // Some notes:
    //
    // - The canonical way of clobbering the world is to read world and write
    //   heap. This is because World subsumes Heap and Stack, and Stack can be
    //   read by anyone but only written to by explicit stack writing operations.
    //   Of course, claiming to also write World is not wrong; it'll just
    //   pessimise some important optimizations.
    //
    // - We cannot hoist, or sink, anything that has effects. This means that the
    //   easiest way of indicating that something cannot be hoisted is to claim
    //   that it side-effects some miscellaneous thing.
    //
    // - Some nodes lie, and claim that they do not read the JSCell_structureID,
    //   JSCell_typeInfoFlags, etc. These are nodes that use the structure in a way
    //   that does not depend on things that change under structure transitions.
    //
    // - It's implicitly understood that OSR exits read the world. This is why we
    //   generally don't move or eliminate stores. Every node can exit, so the
    //   read set does not reflect things that would be read if we exited.
    //   Instead, the read set reflects what the node will have to read if it
    //   *doesn't* exit.
    //
    // - Broadly, we don't say that we're reading something if that something is
    //   immutable.
    //
    // - This must be sound even prior to type inference. We use this as early as
    //   bytecode parsing to determine at which points in the program it's legal to
    //   OSR exit.
    //
    // - If you do read(Stack) or read(World), then make sure that readTop() in
    //   PreciseLocalClobberize is correct.
    
    // While read() and write() are fairly self-explanatory - they track what sorts of things the
    // node may read or write - the def() functor is more tricky. It tells you the heap locations
    // (not just abstract heaps) that are defined by a node. A heap location comprises an abstract
    // heap, some nodes, and a LocationKind. Briefly, a location defined by a node is a location
    // whose value can be deduced from looking at the node itself. The locations returned must obey
    // the following properties:
    //
    // - If someone wants to CSE a load from the heap, then a HeapLocation object should be
    //   sufficient to find a single matching node.
    //
    // - The abstract heap is the only abstract heap that could be clobbered to invalidate any such
    //   CSE attempt. I.e. if clobberize() reports that on every path between some node and a node
    //   that defines a HeapLocation that it wanted, there were no writes to any abstract heap that
    //   overlap the location's heap, then we have a sound match. Effectively, the semantics of
    //   write() and def() are intertwined such that for them to be sound they must agree on what
    //   is CSEable.
    //
    // read(), write(), and def() for heap locations is enough to do GCSE on effectful things. To
    // keep things simple, this code will also def() pure things. def() must be overloaded to also
    // accept PureValue. This way, a client of clobberize() can implement GCSE entirely using the
    // information that clobberize() passes to write() and def(). Other clients of clobberize() can
    // just ignore def() by using a NoOpClobberize functor.

    // We allow the runtime to perform a stack scan at any time. We don't model which nodes get implemented
    // by calls into the runtime. For debugging we might replace the implementation of any node with a call
    // to the runtime, and that call may walk stack. Therefore, each node must read() anything that a stack
    // scan would read. That's what this does.
    for (InlineCallFrame* inlineCallFrame = node->origin.semantic.inlineCallFrame(); inlineCallFrame; inlineCallFrame = inlineCallFrame->directCaller.inlineCallFrame()) {
        if (inlineCallFrame->isClosureCall)
            read(AbstractHeap(Stack, VirtualRegister(inlineCallFrame->stackOffset + CallFrameSlot::callee)));
        if (inlineCallFrame->isVarargs())
            read(AbstractHeap(Stack, VirtualRegister(inlineCallFrame->stackOffset + CallFrameSlot::argumentCountIncludingThis)));
    }

    // We don't want to specifically account which nodes can read from the scope
    // when the debugger is enabled. It's helpful to just claim all nodes do.
    // Specifically, if a node allocates, this may call into the debugger's machinery.
    // The debugger's machinery is free to take a stack trace and try to read from
    // a scope which is expected to be flushed to the stack.
    if (graph.hasDebuggerEnabled()) {
        ASSERT(!node->origin.semantic.inlineCallFrame());
        read(AbstractHeap(Stack, graph.m_codeBlock->scopeRegister()));
    }

    auto clobberTop = [&] {
        if (Options::validateDFGClobberize())
            clobberTopFunctor();
        read(World);
        write(Heap);
    };

    // Since Fixup can widen our ArrayModes based on profiling from other nodes we pessimistically assume
    // all nodes with an ArrayMode can clobber top. We allow some nodes like CheckArray because they can
    // only exit.
    if (graph.m_planStage < PlanStage::AfterFixup && node->hasArrayMode()) {
        switch (node->op()) {
        case CheckArray:
        case CheckArrayOrEmpty:
            break;
        case EnumeratorNextUpdateIndexAndMode:
        case EnumeratorGetByVal:
        case EnumeratorPutByVal:
        case EnumeratorInByVal:
        case EnumeratorHasOwnProperty:
        case GetIndexedPropertyStorage:
        case DataViewGetByteLength:
        case DataViewGetByteLengthAsInt52:
        case GetArrayLength:
        case GetUndetachedTypeArrayLength:
        case GetTypedArrayLengthAsInt52:
        case GetTypedArrayByteOffset:
        case GetTypedArrayByteOffsetAsInt52:
        case GetVectorLength:
        case InByVal:
        case InByValMegamorphic:
        case PutByValDirect:
        case PutByVal:
        case PutByValAlias:
        case PutByValMegamorphic:
        case GetByVal:
        case GetByValMegamorphic:
        case MultiGetByVal:
        case MultiPutByVal:
        case StringAt:
        case StringCharAt:
        case StringCharCodeAt:
        case StringCodePointAt:
        case Arrayify:
        case ArrayifyToStructure:
        case ArrayPush:
        case ArrayPop:
        case ArrayIncludes:
        case ArrayIndexOf:
        case HasIndexedProperty:
        case AtomicsAdd:
        case AtomicsAnd:
        case AtomicsCompareExchange:
        case AtomicsExchange:
        case AtomicsLoad:
        case AtomicsOr:
        case AtomicsStore:
        case AtomicsSub:
        case AtomicsXor:
        case NewArrayWithSpecies:
            return clobberTop();
        default:
            DFG_CRASH(graph, node, "Unhandled ArrayMode opcode.");
        }
    }
    
    switch (node->op()) {
    case JSConstant:
    case DoubleConstant:
    case Int52Constant:
        def(PureValue(node, node->constant()));
        return;

    case Identity:
    case IdentityWithProfile:
    case Phantom:
    case Check:
    case CheckVarargs:
    case ExtractOSREntryLocal:
    case CheckStructureImmediate:
        return;

    case ExtractCatchLocal:
        read(AbstractHeap(CatchLocals, node->catchOSREntryIndex()));
        return;

    case ClearCatchLocals:
        write(CatchLocals);
        return;
        
    case LazyJSConstant:
        // We should enable CSE of LazyJSConstant. It's a little annoying since LazyJSValue has
        // more bits than we currently have in PureValue.
        return;

    case CompareEqPtr:
        def(PureValue(node, node->cellOperand()->cell()));
        return;

    case UnwrapGlobalProxy:
        read(JSGlobalProxy_target);
        def(HeapLocation(GlobalProxyTargetLoc, JSGlobalProxy_target, node->child1()), LazyNode(node));
        return;

    case ArithIMul:
    case ArithPow:
    case GetScope:
    case SkipScope:
    case GetGlobalObject:
    case StringCharCodeAt:
    case StringCodePointAt:
    case StringIndexOf:
    case CompareStrictEq:
    case SameValue:
    case IsEmpty:
    case IsEmptyStorage:
    case TypeOfIsUndefined:
    case IsUndefinedOrNull:
    case IsBoolean:
    case IsNumber:
    case IsBigInt:
    case NumberIsInteger:
    case IsObject:
    case IsTypedArrayView:
    case ToBoolean:
    case LogicalNot:
    case CheckInBounds:
    case CheckInBoundsInt52:
    case DoubleRep:
    case PurifyNaN:
    case ValueRep:
    case Int52Rep:
    case BooleanToNumber:
    case FiatInt52:
    case MakeRope:
    case MakeAtomString:
    case StrCat:
    case ValueToInt32:
    case GetExecutable:
    case BottomValue:
    case TypeOf:
        def(PureValue(node));
        return;

    // JSCallee for Eval can change the scope field.
    case GetEvalScope:
        read(World);
        return;

    case NumberIsFinite:
    case NumberIsNaN:
    case NumberIsSafeInteger:
        def(PureValue(node));
        return;

    case GlobalIsFinite:
    case GlobalIsNaN:
        ASSERT(node->child1().useKind() == UntypedUse);
        clobberTop();
        return;

    case StringLocaleCompare:
        read(World);
        write(SideState);
        def(PureValue(node));
        return;

    case ArithMin:
    case ArithMax:
        def(PureValue(graph, node));
        return;

    case GetGlobalThis:
        read(World);
        return;

    case AtomicsIsLockFree:
        if (graph.child(node, 0).useKind() == Int32Use)
            def(PureValue(graph, node));
        else
            clobberTop();
        return;
        
    case ArithUnary:
        if (node->child1().useKind() == DoubleRepUse)
            def(PureValue(node, static_cast<std::underlying_type<Arith::UnaryType>::type>(node->arithUnaryType())));
        else
            clobberTop();
        return;

    case ArithFRound:
    case ArithF16Round:
    case ArithSqrt:
        if (node->child1().useKind() == DoubleRepUse)
            def(PureValue(node));
        else
            clobberTop();
        return;

    case ArithAbs:
        if (node->child1().useKind() == Int32Use || node->child1().useKind() == DoubleRepUse)
            def(PureValue(node, node->arithMode()));
        else
            clobberTop();
        return;

    case ArithClz32:
        if (node->child1().useKind() == Int32Use || node->child1().useKind() == KnownInt32Use)
            def(PureValue(node));
        else
            clobberTop();
        return;

    case ArithNegate:
        if (node->child1().useKind() == Int32Use
            || node->child1().useKind() == DoubleRepUse
            || node->child1().useKind() == Int52RepUse)
            def(PureValue(node, node->arithMode()));
        else
            clobberTop();
        return;

    case IsCellWithType:
        def(PureValue(node, node->queriedType()));
        return;

    case ValueBitNot:
        if (node->child1().useKind() == AnyBigIntUse || node->child1().useKind() == BigInt32Use || node->child1().useKind() == HeapBigIntUse) {
            def(PureValue(node));
            return;
        }
        clobberTop();
        return;

    case ArithBitNot:
        if (node->child1().useKind() == UntypedUse) {
            clobberTop();
            return;
        }
        def(PureValue(node));
        return;

    case ArithBitAnd:
    case ArithBitOr:
    case ArithBitXor:
    case ArithBitLShift:
    case ArithBitRShift:
    case ArithBitURShift:
        if (node->child1().useKind() == UntypedUse || node->child2().useKind() == UntypedUse) {
            clobberTop();
            return;
        }
        def(PureValue(node));
        return;

    case ArithRandom:
        read(MathDotRandomState);
        write(MathDotRandomState);
        return;

    case EnumeratorNextUpdatePropertyName: {
        def(PureValue(node, node->enumeratorMetadata().toRaw()));
        return;
    }

    case ExtractFromTuple: {
        def(PureValue(node, node->extractOffset()));
        return;
    }

    case EnumeratorNextUpdateIndexAndMode:
    case HasIndexedProperty: {
        if (node->op() == EnumeratorNextUpdateIndexAndMode) {
            if (node->enumeratorMetadata() == JSPropertyNameEnumerator::OwnStructureMode && graph.varArgChild(node, 0).useKind() == CellUse) {
                read(JSObject_butterfly);
                read(NamedProperties);
                read(JSCell_structureID);
                return;
            }

            if (node->enumeratorMetadata() != JSPropertyNameEnumerator::IndexedMode) {
                clobberTop();
                return;
            }
        }

        read(JSObject_butterfly);
        ArrayMode mode = node->arrayMode();
        LocationKind locationKind = node->op() == EnumeratorNextUpdateIndexAndMode ? EnumeratorNextUpdateIndexAndModeLoc : HasIndexedPropertyLoc;
        switch (mode.type()) {
        case Array::ForceExit: {
            write(SideState);
            return;
        }
        case Array::Int32: {
            if (mode.isInBounds()) {
                read(Butterfly_publicLength);
                read(IndexedInt32Properties);
                def(HeapLocation(locationKind, IndexedInt32Properties, graph.varArgChild(node, 0), graph.varArgChild(node, 1)), LazyNode(node));
                return;
            }
            break;
        }
            
        case Array::Double: {
            if (mode.isInBounds()) {
                read(Butterfly_publicLength);
                read(IndexedDoubleProperties);
                def(HeapLocation(locationKind, IndexedDoubleProperties, graph.varArgChild(node, 0), graph.varArgChild(node, 1)), LazyNode(node));
                return;
            }
            break;
        }
            
        case Array::Contiguous: {
            if (mode.isInBounds()) {
                read(Butterfly_publicLength);
                read(IndexedContiguousProperties);
                def(HeapLocation(locationKind, IndexedContiguousProperties, graph.varArgChild(node, 0), graph.varArgChild(node, 1)), LazyNode(node));
                return;
            }
            break;
        }

        case Array::ArrayStorage: {
            if (mode.isInBounds()) {
                read(Butterfly_vectorLength);
                read(IndexedArrayStorageProperties);
                return;
            }
            break;
        }

        default:
            break;
        }

        clobberTop();
        return;
    }

    case StringFromCharCode:
        switch (node->child1().useKind()) {
        case Int32Use:
        case KnownInt32Use:
            def(PureValue(node));
            return;
        case UntypedUse:
            clobberTop();
            return;
        default:
            DFG_CRASH(graph, node, "Bad use kind");
        }
        return;

    case ArithAdd:
    case ArithMod:
    case DoubleAsInt32:
    case UInt32ToNumber:
        def(PureValue(node, node->arithMode()));
        return;

    case ArithDiv:
    case ArithMul:
    case ArithSub:
        switch (node->binaryUseKind()) {
        case Int32Use:
        case Int52RepUse:
        case DoubleRepUse:
            def(PureValue(node, node->arithMode()));
            return;
        case UntypedUse:
            clobberTop();
            return;
        default:
            DFG_CRASH(graph, node, "Bad use kind");
        }

    case ArithRound:
    case ArithFloor:
    case ArithCeil:
    case ArithTrunc:
        if (node->child1().useKind() == DoubleRepUse)
            def(PureValue(node, static_cast<uintptr_t>(node->arithRoundingMode())));
        else
            clobberTop();
        return;

    case CheckIsConstant:
        def(PureValue(CheckIsConstant, AdjacencyList(AdjacencyList::Fixed, node->child1()), node->constant()));
        return;

    case CheckNotEmpty:
        def(PureValue(CheckNotEmpty, AdjacencyList(AdjacencyList::Fixed, node->child1())));
        return;

    case AssertInBounds:
    case AssertNotEmpty:
        write(SideState);
        return;

    case CheckIdent:
        def(PureValue(CheckIdent, AdjacencyList(AdjacencyList::Fixed, node->child1()), node->uidOperand()));
        return;

    case ConstantStoragePointer:
        def(PureValue(node, node->storagePointer()));
        return;

    case KillStack:
        write(AbstractHeap(Stack, node->unlinkedOperand()));
        return;
         
    case MovHint:
    case ZombieHint:
    case ExitOK:
    case Upsilon:
    case Phi:
    case PhantomLocal:
    case SetArgumentDefinitely:
    case SetArgumentMaybe:
    case Jump:
    case Branch:
    case Switch:
    case EntrySwitch:
    case ForceOSRExit:
    case CPUIntrinsic:
    case CheckBadValue:
    case Return:
    case Unreachable:
    case CheckTierUpInLoop:
    case CheckTierUpAtReturn:
    case CheckTierUpAndOSREnter:
    case LoopHint:
    case ProfileType:
    case ProfileControlFlow:
    case PutHint:
    case InitializeEntrypointArguments:
    case FilterCallLinkStatus:
    case FilterGetByStatus:
    case FilterPutByStatus:
    case FilterInByStatus:
    case FilterDeleteByStatus:
    case FilterCheckPrivateBrandStatus:
    case FilterSetPrivateBrandStatus:
        write(SideState);
        return;
        
    case StoreBarrier:
        read(JSCell_cellState);
        write(JSCell_cellState);
        return;
        
    case FencedStoreBarrier:
        read(Heap);
        write(JSCell_cellState);
        return;

    case CheckTraps:
        read(InternalState);
        write(InternalState);
        return;

    case InvalidationPoint:
        write(SideState);
        def(HeapLocation(InvalidationPointLoc, Watchpoint_fire), LazyNode(node));
        return;

    case Flush:
        read(AbstractHeap(Stack, node->operand()));
        write(SideState);
        return;

    case NotifyWrite:
        write(Watchpoint_fire);
        write(SideState);
        return;

    case PushWithScope: {
        read(World);
        write(HeapObjectCount);
        return;
    }

    case CreateActivation: {
        SymbolTable* table = node->castOperand<SymbolTable*>();
        if (table->singleton().isStillValid())
            write(Watchpoint_fire);
        read(HeapObjectCount);
        write(HeapObjectCount);
        return;
    }

    case CreateDirectArguments:
    case CreateScopedArguments:
    case CreateClonedArguments:
        read(Stack);
        read(HeapObjectCount);
        write(HeapObjectCount);
        return;

    case PhantomDirectArguments:
    case PhantomClonedArguments:
        // DFG backend requires that the locals that this reads are flushed. FTL backend can handle those
        // locals being promoted.
        if (!graph.m_plan.isFTL())
            read(Stack);
        
        // Even though it's phantom, it still has the property that one can't be replaced with another.
        read(HeapObjectCount);
        write(HeapObjectCount);
        return;

    case PhantomSpread:
    case PhantomNewArrayWithSpread:
    case PhantomNewArrayBuffer:
    case PhantomCreateRest:
        // Even though it's phantom, it still has the property that one can't be replaced with another.
        read(HeapObjectCount);
        write(HeapObjectCount);
        return;

    case CallObjectConstructor:
        read(HeapObjectCount);
        write(HeapObjectCount);
        return;

    case ToThis:
        read(MiscFields);
        read(HeapObjectCount);
        write(HeapObjectCount);
        return;

    case TypeOfIsObject:
        read(MiscFields);
        def(HeapLocation(TypeOfIsObjectLoc, MiscFields, node->child1()), LazyNode(node));
        return;

    case TypeOfIsFunction:
        read(MiscFields);
        def(HeapLocation(TypeOfIsFunctionLoc, MiscFields, node->child1()), LazyNode(node));
        return;
        
    case IsCallable:
        read(MiscFields);
        def(HeapLocation(IsCallableLoc, MiscFields, node->child1()), LazyNode(node));
        return;

    case IsConstructor:
        read(MiscFields);
        def(HeapLocation(IsConstructorLoc, MiscFields, node->child1()), LazyNode(node));
        return;
        
    case MatchStructure:
        read(JSCell_structureID);
        return;

    case ArraySlice:
        read(MiscFields);
        read(JSCell_indexingType);
        read(JSCell_structureID);
        read(JSObject_butterfly);
        read(Butterfly_publicLength);
        read(IndexedDoubleProperties);
        read(IndexedInt32Properties);
        read(IndexedContiguousProperties);
        read(HeapObjectCount);
        write(HeapObjectCount);
        return;

    case ArrayIncludes:
    case ArrayIndexOf: {
        // FIXME: Should support a CSE rule.
        // https://bugs.webkit.org/show_bug.cgi?id=173173
        read(MiscFields);
        read(JSCell_indexingType);
        read(JSCell_structureID);
        read(JSObject_butterfly);
        read(Butterfly_publicLength);
        switch (node->arrayMode().type()) {
        case Array::Double:
            read(IndexedDoubleProperties);
            return;
        case Array::Int32:
            read(IndexedInt32Properties);
            return;
        case Array::Contiguous:
            read(IndexedContiguousProperties);
            return;
        default:
            RELEASE_ASSERT_NOT_REACHED();
            return;
        }
        return;
    }

    case TryGetById:
        read(World);
#define ABSTRACT_HEAP_NOT_RegExpObject_lastIndex(name) if (name != InvalidAbstractHeap && \
    name != InvalidAbstractHeap && \
    name != World && \
    name != Stack && \
    name != Heap && \
    name != RegExpObject_lastIndex) \
        write(name);
    FOR_EACH_ABSTRACT_HEAP_KIND(ABSTRACT_HEAP_NOT_RegExpObject_lastIndex)
#undef ABSTRACT_HEAP_NOT_RegExpObject_lastIndex
        return;

    case GetById:
    case GetByIdFlush:
    case GetByIdMegamorphic:
    case GetByIdWithThis:
    case GetByIdWithThisMegamorphic:
    case GetByIdDirect:
    case GetByIdDirectFlush:
    case GetByValWithThis:
    case GetByValWithThisMegamorphic:
    case PutById:
    case PutByIdMegamorphic:
    case PutByIdWithThis:
    case PutByValWithThis:
    case PutByIdFlush:
    case PutByIdDirect:
    case PutGetterById:
    case PutSetterById:
    case PutGetterSetterById:
    case PutGetterByVal:
    case PutSetterByVal:
    case PutPrivateName:
    case PutPrivateNameById:
    case GetPrivateName:
    case GetPrivateNameById:
    // FIXME: We should have a better cloberize rule for both CheckPrivateBrand and SetPrivateBrand
    // https://bugs.webkit.org/show_bug.cgi?id=221571
    case CheckPrivateBrand:
    case SetPrivateBrand:
    case DefineDataProperty:
    case DefineAccessorProperty:
    case DeleteById:
    case DeleteByVal:
    case ArrayPush:
    case ArrayPop:
    case ArraySplice:
    case Call:
    case DirectCall:
    case TailCallInlinedCaller:
    case DirectTailCallInlinedCaller:
    case Construct:
    case DirectConstruct:
    case CallVarargs:
    case CallForwardVarargs:
    case TailCallVarargsInlinedCaller:
    case TailCallForwardVarargsInlinedCaller:
    case ConstructVarargs:
    case ConstructForwardVarargs:
    case CallDirectEval:
    case CallWasm:
    case CallCustomAccessorGetter:
    case CallCustomAccessorSetter:
    case ToPrimitive:
    case ToPropertyKey:
    case ToPropertyKeyOrNumber:
    case InByVal:
    case InByValMegamorphic:
    case EnumeratorInByVal:
    case EnumeratorHasOwnProperty:
    case InById:
    case InByIdMegamorphic:
    case HasPrivateName:
    case HasPrivateBrand:
    case HasOwnProperty:
    case ValueNegate:
    case SetFunctionName:
    case GetDynamicVar:
    case PutDynamicVar:
    case ResolveScopeForHoistingFuncDeclInEval:
    case ResolveScope:
    case ToObject:
    case GetPropertyEnumerator:
    case InstanceOfCustom:
    case ToNumeric:
    case NumberToStringWithRadix:
    case CreateThis:
    case CreatePromise:
    case CreateGenerator:
    case CreateAsyncGenerator:
    case InstanceOf:
    case InstanceOfMegamorphic:
    case ObjectKeys:
    case ObjectGetOwnPropertyNames:
    case ObjectGetOwnPropertySymbols:
    case ObjectToString:
    case ReflectOwnKeys:
        clobberTop();
        return;

    case StringValueOf:
        switch (node->child1().useKind()) {
        case StringOrOtherUse:
            read(World);
            write(SideState);
            def(PureValue(node));
            return;
        default:
            clobberTop();
            return;
        }

    case ToNumber:
        switch (node->child1().useKind()) {
        case StringUse:
            def(PureValue(node));
            return;
        default:
            clobberTop();
            return;
        }

    case CallNumberConstructor:
        switch (node->child1().useKind()) {
        case BigInt32Use:
            def(PureValue(node));
            return;
        case UntypedUse:
            clobberTop();
            return;
        default:
            DFG_CRASH(graph, node, "Bad use kind");
        }

    case Inc:
    case Dec:
        switch (node->child1().useKind()) {
        case Int32Use:
        case Int52RepUse:
        case DoubleRepUse:
        case BigInt32Use:
        case HeapBigIntUse:
        case AnyBigIntUse:
            def(PureValue(node));
            return;
        case UntypedUse:
            clobberTop();
            return;
        default:
            DFG_CRASH(graph, node, "Bad use kind");
        }

    case ValueBitAnd:
    case ValueBitXor:
    case ValueBitOr:
    case ValueAdd:
    case ValueSub:
    case ValueMul:
    case ValueDiv:
    case ValueMod:
    case ValuePow:
    case ValueBitLShift:
    case ValueBitRShift:
        // FIXME: this use of single-argument isBinaryUseKind would prevent us from specializing (for example) for a HeapBigInt left-operand and a BigInt32 right-operand.
        if (node->isBinaryUseKind(AnyBigIntUse) || node->isBinaryUseKind(BigInt32Use) || node->isBinaryUseKind(HeapBigIntUse)) {
            read(World);
            write(SideState);
            def(PureValue(node));
            return;
        }
        clobberTop();
        return;

    case ValueBitURShift:
        // URShift >>> does not accept BigInt.
        clobberTop();
        return;

    case AtomicsAdd:
    case AtomicsAnd:
    case AtomicsCompareExchange:
    case AtomicsExchange:
    case AtomicsLoad:
    case AtomicsOr:
    case AtomicsStore:
    case AtomicsSub:
    case AtomicsXor: {
        unsigned numExtraArgs = numExtraAtomicsArgs(node->op());
        Edge storageEdge = graph.child(node, 2 + numExtraArgs);
        if (!storageEdge) {
            clobberTop();
            return;
        }
        read(TypedArrayProperties);
        read(MiscFields);
        write(TypedArrayProperties);
        return;
    }

    case Throw:
    case ThrowStaticError:
    case TailCall:
    case DirectTailCall:
    case TailCallVarargs:
    case TailCallForwardVarargs:
        read(World);
        write(SideState);
        return;
        
    case GetGetter:
        read(GetterSetter_getter);
        def(HeapLocation(GetterLoc, GetterSetter_getter, node->child1()), LazyNode(node));
        return;
        
    case GetSetter:
        read(GetterSetter_setter);
        def(HeapLocation(SetterLoc, GetterSetter_setter, node->child1()), LazyNode(node));
        return;
        
    case GetCallee:
        read(AbstractHeap(Stack, VirtualRegister(CallFrameSlot::callee)));
        def(HeapLocation(StackLoc, AbstractHeap(Stack, VirtualRegister(CallFrameSlot::callee))), LazyNode(node));
        return;

    case SetCallee:
        write(AbstractHeap(Stack, VirtualRegister(CallFrameSlot::callee)));
        return;
        
    case GetArgumentCountIncludingThis: {
        auto heap = AbstractHeap(Stack, remapOperand(node->argumentsInlineCallFrame(), VirtualRegister(CallFrameSlot::argumentCountIncludingThis)));
        read(heap);
        def(HeapLocation(StackPayloadLoc, heap), LazyNode(node));
        return;
    }

    case SetArgumentCountIncludingThis:
        write(AbstractHeap(Stack, VirtualRegister(CallFrameSlot::argumentCountIncludingThis)));
        return;

    case GetRestLength:
        read(Stack);
        return;
        
    case GetLocal:
        read(AbstractHeap(Stack, node->operand()));
        def(HeapLocation(StackLoc, AbstractHeap(Stack, node->operand())), LazyNode(node));
        return;
        
    case SetLocal:
        write(AbstractHeap(Stack, node->operand()));
        def(HeapLocation(StackLoc, AbstractHeap(Stack, node->operand())), LazyNode(node->child1().node()));
        return;
        
    case GetStack: {
        AbstractHeap heap(Stack, node->stackAccessData()->operand);
        read(heap);
        def(HeapLocation(StackLoc, heap), LazyNode(node));
        return;
    }
        
    case PutStack: {
        AbstractHeap heap(Stack, node->stackAccessData()->operand);
        write(heap);
        def(HeapLocation(StackLoc, heap), LazyNode(node->child1().node()));
        return;
    }
        
    case VarargsLength: {
        clobberTop();
        return;  
    }

    case LoadVarargs: {
        if (node->argumentsChild().useKind() != OtherUse)
            clobberTop();
        LoadVarargsData* data = node->loadVarargsData();
        write(AbstractHeap(Stack, data->count));
        for (unsigned i = data->limit; i--;)
            write(AbstractHeap(Stack, data->start + static_cast<int>(i)));
        return;
    }
        
    case ForwardVarargs: {
        // We could be way more precise here.
        read(Stack);
        
        LoadVarargsData* data = node->loadVarargsData();
        write(AbstractHeap(Stack, data->count));
        for (unsigned i = data->limit; i--;)
            write(AbstractHeap(Stack, data->start + static_cast<int>(i)));
        return;
    }

    case EnumeratorGetByVal: {
        clobberTop();
        return;
    }
        
    case GetByVal:
    case GetByValMegamorphic: {
        ArrayMode mode = node->arrayMode();
        LocationKind indexedPropertyLoc = indexedPropertyLocForResultType(node->result());
        switch (mode.type()) {
        case Array::SelectUsingPredictions:
        case Array::Unprofiled:
        case Array::SelectUsingArguments:
            // Assume the worst since we don't have profiling yet.
            clobberTop();
            return;
            
        case Array::ForceExit:
            write(SideState);
            return;
            
        case Array::Generic:
        case Array::BigInt64Array:
        case Array::BigUint64Array:
            clobberTop();
            return;
            
        case Array::String:
            if (mode.isOutOfBounds()) {
                clobberTop();
                return;
            }
            // This appears to read nothing because it's only reading immutable data.
            def(PureValue(graph, node, mode.asWord()));
            return;
            
        case Array::DirectArguments:
            if (mode.isInBounds()) {
                read(DirectArgumentsProperties);
                def(HeapLocation(indexedPropertyLoc, DirectArgumentsProperties, graph.varArgChild(node, 0), graph.varArgChild(node, 1)), LazyNode(node));
                return;
            }
            clobberTop();
            return;
            
        case Array::ScopedArguments:
            read(ScopeProperties);
            def(HeapLocation(indexedPropertyLoc, ScopeProperties, graph.varArgChild(node, 0), graph.varArgChild(node, 1)), LazyNode(node));
            return;
            
        case Array::Int32:
            if (mode.isInBounds() || mode.isOutOfBoundsSaneChain()) {
                read(Butterfly_publicLength);
                read(IndexedInt32Properties);
                LocationKind kind = mode.isOutOfBoundsSaneChain() ? IndexedPropertyInt32OutOfBoundsSaneChainLoc : indexedPropertyLoc;
                def(HeapLocation(kind, IndexedInt32Properties, graph.varArgChild(node, 0), graph.varArgChild(node, 1)), LazyNode(node));
                return;
            }
            clobberTop();
            return;
            
        case Array::Double:
            if (mode.isInBounds() || mode.isOutOfBoundsSaneChain()) {
                read(Butterfly_publicLength);
                read(IndexedDoubleProperties);
                LocationKind kind;
                if (node->hasDoubleResult()) {
                    if (mode.isInBoundsSaneChain())
                        kind = IndexedPropertyDoubleSaneChainLoc;
                    else if (mode.isOutOfBoundsSaneChain())
                        kind = IndexedPropertyDoubleOutOfBoundsSaneChainLoc;
                    else
                        kind = IndexedPropertyDoubleLoc;
                } else {
                    ASSERT(mode.isOutOfBoundsSaneChain());
                    kind = IndexedPropertyDoubleOrOtherOutOfBoundsSaneChainLoc;
                }
                def(HeapLocation(kind, IndexedDoubleProperties, graph.varArgChild(node, 0), graph.varArgChild(node, 1)), LazyNode(node));
                return;
            }
            clobberTop();
            return;
            
        case Array::Contiguous:
            if (mode.isInBounds() || mode.isOutOfBoundsSaneChain()) {
                read(Butterfly_publicLength);
                read(IndexedContiguousProperties);
                def(HeapLocation(mode.isOutOfBoundsSaneChain() ? IndexedPropertyJSOutOfBoundsSaneChainLoc : indexedPropertyLoc, IndexedContiguousProperties, graph.varArgChild(node, 0), graph.varArgChild(node, 1)), LazyNode(node));
                return;
            }
            clobberTop();
            return;

        case Array::Undecided:
            def(PureValue(graph, node));
            return;
            
        case Array::ArrayStorage:
        case Array::SlowPutArrayStorage:
            if (mode.isInBounds()) {
                read(Butterfly_vectorLength);
                read(IndexedArrayStorageProperties);
                return;
            }
            clobberTop();
            return;
            
        case Array::Int8Array:
        case Array::Int16Array:
        case Array::Int32Array:
        case Array::Uint8Array:
        case Array::Uint8ClampedArray:
        case Array::Uint16Array:
        case Array::Uint32Array:
        case Array::Float16Array:
        case Array::Float32Array:
        case Array::Float64Array:
            // Even if we hit out-of-bounds, this is fine. TypedArray does not propagate access to its [[Prototype]] when out-of-bounds access happens.
            read(TypedArrayProperties);
            read(MiscFields);
            if (mode.mayBeResizableOrGrowableSharedTypedArray()) {
                write(MiscFields);
                write(TypedArrayProperties);
            } else {
                if (mode.isOutOfBounds())
                    indexedPropertyLoc = indexedPropertyLocToOutOfBoundsSaneChain(indexedPropertyLoc);
                def(HeapLocation(indexedPropertyLoc, TypedArrayProperties, graph.varArgChild(node, 0), graph.varArgChild(node, 1)), LazyNode(node));
            }
            return;
        // We should not get an AnyTypedArray in a GetByVal as AnyTypedArray is only created from intrinsics, which
        // are only added from Inline Caching a GetById.
        case Array::AnyTypedArray:
            DFG_CRASH(graph, node, "impossible array mode for get");
            return;
        }
        RELEASE_ASSERT_NOT_REACHED();
        return;
    }

    case MultiGetByVal: {
        ArrayMode mode = node->arrayMode();
        LocationKind indexedPropertyLoc = indexedPropertyLocForResultType(node->result());
        bool canUseCSE = true;
        for (unsigned i = 0; i < sizeof(ArrayModes) * CHAR_BIT; ++i) {
            ArrayModes oneArrayMode = 1ULL << i;
            if (node->arrayModes() & oneArrayMode) {
                switch (oneArrayMode) {
                case asArrayModesIgnoringTypedArrays(ArrayWithInt32): {
                    if (mode.isInBounds() || mode.isOutOfBoundsSaneChain()) {
                        read(Butterfly_publicLength);
                        read(IndexedInt32Properties);
                        break;
                    }
                    clobberTop();
                    break;
                }
                case asArrayModesIgnoringTypedArrays(ArrayWithDouble): {
                    if (mode.isInBounds() || mode.isOutOfBoundsSaneChain()) {
                        read(Butterfly_publicLength);
                        read(IndexedDoubleProperties);
                        break;
                    }
                    clobberTop();
                    break;
                }
                case asArrayModesIgnoringTypedArrays(ArrayWithContiguous): {
                    if (mode.isInBounds() || mode.isOutOfBoundsSaneChain()) {
                        read(Butterfly_publicLength);
                        read(IndexedContiguousProperties);
                        break;
                    }
                    clobberTop();
                    break;
                }
                case Int8ArrayMode:
                case Int16ArrayMode:
                case Int32ArrayMode:
                case Uint8ArrayMode:
                case Uint8ClampedArrayMode:
                case Float16ArrayMode:
                case Uint16ArrayMode:
                case Uint32ArrayMode:
                case Float32ArrayMode:
                case Float64ArrayMode:
                case BigInt64ArrayMode:
                case BigUint64ArrayMode:
                    // Even if we hit out-of-bounds, this is fine. TypedArray does not propagate access to its [[Prototype]] when out-of-bounds access happens.
                    read(TypedArrayProperties);
                    read(MiscFields);
                    if (mode.mayBeResizableOrGrowableSharedTypedArray()) {
                        canUseCSE = false;
                        write(MiscFields);
                        write(TypedArrayProperties);
                    }
                    break;
                default:
                    DFG_CRASH(graph, node, "impossible array mode for MultiGetByVal");
                    break;
                }
            }
        }
        if (!mode.isOutOfBounds() && canUseCSE)
            def(HeapLocation(indexedPropertyLoc, IndexedProperties, graph.child(node, 0).node(), LazyNode(graph.child(node, 1).node()), nullptr, std::bit_cast<void*>(static_cast<uintptr_t>(node->arrayModes()))), LazyNode(node));
        return;
    }

    case GetMyArgumentByVal:
    case GetMyArgumentByValOutOfBounds: {
        read(Stack);
        // FIXME: It would be trivial to have a def here.
        // https://bugs.webkit.org/show_bug.cgi?id=143077
        return;
    }

    case PutByValDirect:
    case PutByVal:
    case PutByValAlias:
    case PutByValMegamorphic: {
        ArrayMode mode = node->arrayMode();
        Node* base = graph.varArgChild(node, 0).node();
        Node* index = graph.varArgChild(node, 1).node();
        Node* value = graph.varArgChild(node, 2).node();
        LocationKind indexedPropertyLoc = indexedPropertyLocForResultType(node->result());

        switch (mode.modeForPut().type()) {
        case Array::SelectUsingPredictions:
        case Array::SelectUsingArguments:
        case Array::Unprofiled:
        case Array::Undecided:
            // Assume the worst since we don't have profiling yet.
            clobberTop();
            return;
            
        case Array::ForceExit:
            write(SideState);
            return;
            
        case Array::Generic:
        case Array::BigInt64Array:
        case Array::BigUint64Array:
            clobberTop();
            return;
            
        case Array::Int32:
            if (mode.isOutOfBounds()) {
                clobberTop();
                return;
            }
            read(Butterfly_publicLength);
            read(Butterfly_vectorLength);
            read(IndexedInt32Properties);
            write(IndexedInt32Properties);
            if (mode.mayStoreToHole())
                write(Butterfly_publicLength);
            def(HeapLocation(indexedPropertyLoc, IndexedInt32Properties, base, index), LazyNode(value));
            def(HeapLocation(IndexedPropertyInt32OutOfBoundsSaneChainLoc, IndexedInt32Properties, base, index), LazyNode(value));
            return;
            
        case Array::Double:
            if (mode.isOutOfBounds()) {
                clobberTop();
                return;
            }
            read(Butterfly_publicLength);
            read(Butterfly_vectorLength);
            read(IndexedDoubleProperties);
            write(IndexedDoubleProperties);
            if (mode.mayStoreToHole())
                write(Butterfly_publicLength);
            def(HeapLocation(IndexedPropertyDoubleLoc, IndexedDoubleProperties, base, index), LazyNode(value));
            def(HeapLocation(IndexedPropertyDoubleSaneChainLoc, IndexedDoubleProperties, base, index), LazyNode(value));
            def(HeapLocation(IndexedPropertyDoubleOutOfBoundsSaneChainLoc, IndexedDoubleProperties, base, index), LazyNode(value));
            return;
            
        case Array::Contiguous:
            if (mode.isOutOfBounds()) {
                clobberTop();
                return;
            }
            read(Butterfly_publicLength);
            read(Butterfly_vectorLength);
            read(IndexedContiguousProperties);
            write(IndexedContiguousProperties);
            if (mode.mayStoreToHole())
                write(Butterfly_publicLength);
            def(HeapLocation(indexedPropertyLoc, IndexedContiguousProperties, base, index), LazyNode(value));
            def(HeapLocation(IndexedPropertyJSOutOfBoundsSaneChainLoc, IndexedContiguousProperties, base, index), LazyNode(value));
            return;
            
        case Array::ArrayStorage:
            if (node->arrayMode().isOutOfBounds()) {
                clobberTop();
                return;
            }
            read(Butterfly_publicLength);
            read(Butterfly_vectorLength);
            read(IndexedArrayStorageProperties);
            write(IndexedArrayStorageProperties);
            if (mode.mayStoreToHole())
                write(Butterfly_publicLength);
            return;

        case Array::SlowPutArrayStorage:
            if (mode.mayStoreToHole()) {
                clobberTop();
                return;
            }
            read(Butterfly_publicLength);
            read(Butterfly_vectorLength);
            read(IndexedArrayStorageProperties);
            write(IndexedArrayStorageProperties);
            return;

        case Array::Int8Array:
        case Array::Int16Array:
        case Array::Int32Array:
        case Array::Uint8Array:
        case Array::Uint8ClampedArray:
        case Array::Uint16Array:
        case Array::Uint32Array:
        case Array::Float16Array:
        case Array::Float32Array:
        case Array::Float64Array:
            if (mode.mayBeResizableOrGrowableSharedTypedArray()) {
                read(TypedArrayProperties);
                read(MiscFields);
                write(TypedArrayProperties);
                write(MiscFields);
            } else {
                read(MiscFields);
                write(TypedArrayProperties);
                // FIXME: We can't def() anything here because these operations truncate their inputs.
                // https://bugs.webkit.org/show_bug.cgi?id=134737
            }
            return;
        case Array::AnyTypedArray:
        case Array::String:
        case Array::DirectArguments:
        case Array::ScopedArguments:
            DFG_CRASH(graph, node, "impossible array mode for put");
            return;
        }
        RELEASE_ASSERT_NOT_REACHED();
        return;
    }

    case MultiPutByVal: {
        ArrayMode mode = node->arrayMode();
        for (unsigned i = 0; i < sizeof(ArrayModes) * CHAR_BIT; ++i) {
            ArrayModes oneArrayMode = 1ULL << i;
            if (node->arrayModes() & oneArrayMode) {
                switch (oneArrayMode) {
                case asArrayModesIgnoringTypedArrays(ArrayWithInt32): {
                    if (mode.isOutOfBounds()) {
                        clobberTop();
                        break;
                    }
                    read(Butterfly_publicLength);
                    read(Butterfly_vectorLength);
                    read(IndexedInt32Properties);
                    write(IndexedInt32Properties);
                    if (mode.mayStoreToHole())
                        write(Butterfly_publicLength);
                    break;
                }
                case asArrayModesIgnoringTypedArrays(ArrayWithDouble): {
                    if (mode.isOutOfBounds()) {
                        clobberTop();
                        break;
                    }
                    read(Butterfly_publicLength);
                    read(Butterfly_vectorLength);
                    read(IndexedDoubleProperties);
                    write(IndexedDoubleProperties);
                    if (mode.mayStoreToHole())
                        write(Butterfly_publicLength);
                    break;
                }
                case asArrayModesIgnoringTypedArrays(ArrayWithContiguous): {
                    if (mode.isOutOfBounds()) {
                        clobberTop();
                        break;
                    }
                    read(Butterfly_publicLength);
                    read(Butterfly_vectorLength);
                    read(IndexedContiguousProperties);
                    write(IndexedContiguousProperties);
                    if (mode.mayStoreToHole())
                        write(Butterfly_publicLength);
                    break;
                }
                case Int8ArrayMode:
                case Int16ArrayMode:
                case Int32ArrayMode:
                case Uint8ArrayMode:
                case Uint8ClampedArrayMode:
                case Float16ArrayMode:
                case Uint16ArrayMode:
                case Uint32ArrayMode:
                case Float32ArrayMode:
                case Float64ArrayMode:
                case BigInt64ArrayMode:
                case BigUint64ArrayMode:
                    // Even if we hit out-of-bounds, this is fine. TypedArray does not propagate access to its [[Prototype]] when out-of-bounds access happens.
                    if (mode.mayBeResizableOrGrowableSharedTypedArray()) {
                        read(TypedArrayProperties);
                        read(MiscFields);
                        write(TypedArrayProperties);
                        write(MiscFields);
                    } else {
                        read(MiscFields);
                        write(TypedArrayProperties);
                    }
                    break;
                default:
                    DFG_CRASH(graph, node, "impossible array mode for MultiPutByVal");
                    break;
                }
            }
        }
        return;
    }

    case EnumeratorPutByVal: {
        clobberTop();
        return;
    }

    case CheckStructureOrEmpty:
    case CheckStructure:
        read(JSCell_structureID);
        return;

    case CheckArrayOrEmpty:
    case CheckArray:
        read(JSCell_indexingType);
        read(JSCell_structureID);
        return;

    case CheckDetached:
        read(MiscFields);
        return; 
        
    case CheckTypeInfoFlags:
        read(JSCell_typeInfoFlags);
        def(HeapLocation(CheckTypeInfoFlagsLoc, JSCell_typeInfoFlags, node->child1()), LazyNode(node));
        return;

    case HasStructureWithFlags:
        read(World);
        return;

    case ParseInt:
        // Note: We would have eliminated a ParseInt that has just a single child as an Int32Use inside fixup.
        if (node->child1().useKind() == StringUse || node->child1().useKind() == DoubleRepUse || node->child1().useKind() == Int32Use) {
            if (!node->child2() || node->child2().useKind() == Int32Use) {
                def(PureValue(node));
                return;
            }
        }

        clobberTop();
        return;

    case ToIntegerOrInfinity:
    case ToLength: {
        if (node->child1().useKind() == UntypedUse)
            clobberTop();
        else
            def(PureValue(node));
        return;
    }

    case OverridesHasInstance:
        read(JSCell_typeInfoFlags);
        def(HeapLocation(OverridesHasInstanceLoc, JSCell_typeInfoFlags, node->child1()), LazyNode(node));
        return;

    case PutStructure:
        read(JSObject_butterfly);
        write(JSCell_structureID);
        write(JSCell_typeInfoFlags);
        write(JSCell_indexingType);

        if (node->transition()->next->transitionKind() == TransitionKind::PropertyDeletion) {
            // We use this "delete fence" to model the proper aliasing of future stores.
            // Both in DFG and when we lower to B3, we model aliasing of properties by
            // property  name. In a world without delete, that also models {base, propertyOffset}.
            // However, with delete, we may reuse property offsets for different names.
            // Those potential stores that come after this delete won't properly model
            // that they are dependent on the prior name stores. For example, if we didn't model this,
            // it could give when doing things like store elimination, since we don't see
            // writes to the new field name as having dependencies on the old field name.
            // This node makes it so we properly model those dependencies.
            write(NamedProperties);
        }
            
        return;
        
    case AllocatePropertyStorage:
    case ReallocatePropertyStorage:
        read(HeapObjectCount);
        write(HeapObjectCount);
        return;
        
    case NukeStructureAndSetButterfly:
        write(JSObject_butterfly);
        write(JSCell_structureID);
        def(HeapLocation(ButterflyLoc, JSObject_butterfly, node->child1()), LazyNode(node->child2().node()));
        return;
        
    case GetButterfly:
        read(JSObject_butterfly);
        def(HeapLocation(ButterflyLoc, JSObject_butterfly, node->child1()), LazyNode(node));
        return;

    case CheckJSCast:
    case CheckNotJSCast:
        def(PureValue(node, node->classInfo()));
        return;

    case CallDOMGetter: {
        DOMJIT::CallDOMGetterSnippet* snippet = node->callDOMGetterData()->snippet;
        if (!snippet) {
            clobberTop();
            return;
        }
        DOMJIT::Effect effect = snippet->effect;
        if (effect.reads) {
            if (effect.reads == DOMJIT::HeapRange::top())
                read(World);
            else
                read(AbstractHeap(DOMState, effect.reads.rawRepresentation()));
        }
        if (effect.writes) {
            if (effect.writes == DOMJIT::HeapRange::top()) {
                if (Options::validateDFGClobberize())
                    clobberTopFunctor();
                write(Heap);
            } else
                write(AbstractHeap(DOMState, effect.writes.rawRepresentation()));
        }
        if (effect.def != DOMJIT::HeapRange::top()) {
            DOMJIT::HeapRange range = effect.def;
            if (range == DOMJIT::HeapRange::none())
                def(PureValue(node, std::bit_cast<uintptr_t>(node->callDOMGetterData()->customAccessorGetter)));
            else {
                // Def with heap location. We do not include "GlobalObject" for that since this information is included in the base node.
                // We only see the DOMJIT getter here. So just including "base" is ok.
                def(HeapLocation(DOMStateLoc, AbstractHeap(DOMState, range.rawRepresentation()), node->child1()), LazyNode(node));
            }
        }
        return;
    }

    case CallDOM: {
        const DOMJIT::Signature* signature = node->signature();
        DOMJIT::Effect effect = signature->effect;
        if (effect.reads) {
            if (effect.reads == DOMJIT::HeapRange::top())
                read(World);
            else
                read(AbstractHeap(DOMState, effect.reads.rawRepresentation()));
        }
        if (effect.writes) {
            if (effect.writes == DOMJIT::HeapRange::top()) {
                if (Options::validateDFGClobberize())
                    clobberTopFunctor();
                write(Heap);
            } else
                write(AbstractHeap(DOMState, effect.writes.rawRepresentation()));
        }
        ASSERT_WITH_MESSAGE(effect.def == DOMJIT::HeapRange::top(), "Currently, we do not accept any def for CallDOM.");
        return;
    }

    case Arrayify:
    case ArrayifyToStructure:
        read(JSCell_structureID);
        read(JSCell_indexingType);
        read(JSObject_butterfly);
        write(JSCell_structureID);
        write(JSCell_indexingType);
        write(JSObject_butterfly);
        write(Watchpoint_fire);
        return;
        
    case GetIndexedPropertyStorage:
        ASSERT(node->arrayMode().type() != Array::String);
        read(MiscFields);
        def(HeapLocation(IndexedPropertyStorageLoc, MiscFields, node->child1()), LazyNode(node));
        return;

    case ResolveRope:
        def(PureValue(node));
        return;

    case GetTypedArrayByteOffset: {
        ArrayMode mode = node->arrayMode();
        DFG_ASSERT(graph, node, mode.isSomeTypedArrayView() || mode.type() == Array::ForceExit);
        switch (mode.type()) {
        case Array::ForceExit:
            write(SideState);
            return;
        default:
            read(MiscFields);
            if (node->arrayMode().mayBeResizableOrGrowableSharedTypedArray())
                write(MiscFields);
            else
                def(HeapLocation(TypedArrayByteOffsetLoc, MiscFields, node->child1()), LazyNode(node));
            return;
        }
        return;
    }

    case GetTypedArrayByteOffsetAsInt52: {
        ArrayMode mode = node->arrayMode();
        DFG_ASSERT(graph, node, mode.isSomeTypedArrayView() || mode.type() == Array::ForceExit);
        switch (mode.type()) {
        case Array::ForceExit:
            write(SideState);
            return;
        default:
            read(MiscFields);
            if (node->arrayMode().mayBeResizableOrGrowableSharedTypedArray())
                write(MiscFields);
            else
                def(HeapLocation(TypedArrayByteOffsetInt52Loc, MiscFields, node->child1()), LazyNode(node));
            return;
        }
        return;
    }

    case GetWebAssemblyInstanceExports:
        def(PureValue(node));
        return;

    case GetPrototypeOf: {
        switch (node->child1().useKind()) {
        case ArrayUse:
        case FunctionUse:
        case FinalObjectUse:
            read(JSCell_structureID);
            read(JSObject_butterfly);
            read(NamedProperties); // Poly proto could load prototype from its slot.
            def(HeapLocation(PrototypeLoc, NamedProperties, node->child1()), LazyNode(node));
            return;
        default:
            clobberTop();
            return;
        }
    }
        
    case GetByOffset:
    case GetGetterSetterByOffset: {
        unsigned identifierNumber = node->storageAccessData().identifierNumber;
        AbstractHeap heap(NamedProperties, identifierNumber);
        read(heap);

        // Since LICM might break the uniqueness assumption of HeapLocation for
        // *byOffset nodes. Then, the HeapLocation constructor with an extra state
        // is introduced and applied in this phase in order to resolve the potential
        // HeapLocation collisions for *byteOffset nodes after LICM phase. Note
        // that the constructor with an extra state should be used only after LICM
        // since it might affect performance.
        auto location = node->hasDoubleResult() ? NamedPropertyDoubleLoc : NamedPropertyLoc;
        if (graph.m_planStage >= PlanStage::LICMAndLater)
            def(HeapLocation(location, heap, node->child2(), &node->storageAccessData()), LazyNode(node));
        else
            def(HeapLocation(location, heap, node->child2()), LazyNode(node));
        return;
    }

    case MultiGetByOffset: {
        read(JSCell_structureID);
        read(JSObject_butterfly);
        AbstractHeap heap(NamedProperties, node->multiGetByOffsetData().identifierNumber);
        read(heap);
        auto location = node->hasDoubleResult() ? NamedPropertyDoubleLoc : NamedPropertyLoc;
        if (graph.m_planStage >= PlanStage::LICMAndLater)
            def(HeapLocation(location, heap, node->child1(), &node->multiGetByOffsetData()), LazyNode(node));
        else
            def(HeapLocation(location, heap, node->child1()), LazyNode(node));
        return;
    }
        
    case MultiPutByOffset: {
        read(JSCell_structureID);
        read(JSObject_butterfly);
        AbstractHeap heap(NamedProperties, node->multiPutByOffsetData().identifierNumber);
        write(heap);
        if (node->multiPutByOffsetData().writesStructures())
            write(JSCell_structureID);
        if (node->multiPutByOffsetData().reallocatesStorage())
            write(JSObject_butterfly);
        auto location = node->child2().useKind() == DoubleRepUse ? NamedPropertyDoubleLoc : NamedPropertyLoc;
        if (graph.m_planStage >= PlanStage::LICMAndLater)
            def(HeapLocation(location, heap, node->child1(), &node->multiPutByOffsetData()), LazyNode(node->child2().node()));
        else
            def(HeapLocation(location, heap, node->child1()), LazyNode(node->child2().node()));
        return;
    }

    case MultiDeleteByOffset: {
        read(JSCell_structureID);
        read(JSObject_butterfly);
        AbstractHeap heap(NamedProperties, node->multiDeleteByOffsetData().identifierNumber);
        write(heap);
        if (node->multiDeleteByOffsetData().writesStructures()) {
            write(JSCell_structureID);
            // See comment in PutStructure about why this is needed for proper
            // alias analysis.
            write(NamedProperties);
        }
        return;
    }
        
    case PutByOffset: {
        unsigned identifierNumber = node->storageAccessData().identifierNumber;
        AbstractHeap heap(NamedProperties, identifierNumber);
        write(heap);
        auto location = node->child3().useKind() == DoubleRepUse ? NamedPropertyDoubleLoc : NamedPropertyLoc;
        if (graph.m_planStage >= PlanStage::LICMAndLater)
            def(HeapLocation(location, heap, node->child2(), &node->storageAccessData()), LazyNode(node->child3().node()));
        else
            def(HeapLocation(location, heap, node->child2()), LazyNode(node->child3().node()));
        return;
    }
        
    case GetArrayLength: {
        ArrayMode mode = node->arrayMode();
        switch (mode.type()) {
        case Array::Undecided:
        case Array::Int32:
        case Array::Double:
        case Array::Contiguous:
        case Array::ArrayStorage:
        case Array::SlowPutArrayStorage:
            read(Butterfly_publicLength);
            def(HeapLocation(ArrayLengthLoc, Butterfly_publicLength, node->child1()), LazyNode(node));
            return;
            
        case Array::String:
            def(PureValue(node, mode.asWord()));
            return;

        case Array::DirectArguments:
        case Array::ScopedArguments:
            read(MiscFields);
            def(HeapLocation(ArrayLengthLoc, MiscFields, node->child1()), LazyNode(node));
            return;

        case Array::ForceExit: {
            write(SideState);
            return;
        }

        default:
            DFG_ASSERT(graph, node, mode.isSomeTypedArrayView());
            read(MiscFields);
            if (mode.mayBeResizableOrGrowableSharedTypedArray())
                write(MiscFields);
            else
                def(HeapLocation(ArrayLengthLoc, MiscFields, node->child1()), LazyNode(node));
            return;
        }
    }

    case DataViewGetByteLength:
    case DataViewGetByteLengthAsInt52: {
        read(MiscFields);
        if (node->mayBeResizableOrGrowableSharedArrayBuffer())
            write(MiscFields);
        else {
            auto location = node->op() == DataViewGetByteLength ? DataViewByteLengthLoc : DataViewByteLengthAsInt52Loc;
            def(HeapLocation(location, MiscFields, node->child1()), LazyNode(node));
        }
        return;
    }

    case GetUndetachedTypeArrayLength: {
        ArrayMode mode = node->arrayMode();
        DFG_ASSERT(graph, node, mode.isSomeTypedArrayView());
        DFG_ASSERT(graph, node, !mode.mayBeResizableOrGrowableSharedTypedArray());
        mode = mode.withAction(Array::Action::Read); // Force action to Read to prevent incorrect optimizations in equality checks.
        def(PureValue(node, mode.asWord()));
        return;
    }

    case GetTypedArrayLengthAsInt52: {
        ArrayMode mode = node->arrayMode();
        DFG_ASSERT(graph, node, mode.isSomeTypedArrayView() || mode.type() == Array::ForceExit);
        switch (mode.type()) {
        case Array::ForceExit:
            write(SideState);
            return;
        default:
            read(MiscFields);
            if (mode.mayBeResizableOrGrowableSharedTypedArray())
                write(MiscFields);
            else
                def(HeapLocation(TypedArrayLengthInt52Loc, MiscFields, node->child1()), LazyNode(node));
            return;
        }
    }

    case GetVectorLength: {
        ArrayMode mode = node->arrayMode();
        switch (mode.type()) {
        case Array::ArrayStorage:
        case Array::SlowPutArrayStorage:
            read(Butterfly_vectorLength);
            def(HeapLocation(VectorLengthLoc, Butterfly_vectorLength, node->child1()), LazyNode(node));
            return;

        default:
            RELEASE_ASSERT_NOT_REACHED();
            return;
        }
    }
        
    case GetClosureVar: {
        auto location = node->hasDoubleResult() ? ClosureVariableDoubleLoc : ClosureVariableLoc;
        read(AbstractHeap(ScopeProperties, node->scopeOffset().offset()));
        def(HeapLocation(location, AbstractHeap(ScopeProperties, node->scopeOffset().offset()), node->child1()), LazyNode(node));
        return;
    }
        
    case PutClosureVar: {
        auto location = node->child2().useKind() == DoubleRepUse ? ClosureVariableDoubleLoc : ClosureVariableLoc;
        write(AbstractHeap(ScopeProperties, node->scopeOffset().offset()));
        def(HeapLocation(location, AbstractHeap(ScopeProperties, node->scopeOffset().offset()), node->child1()), LazyNode(node->child2().node()));
        return;
    }

    case GetInternalField: {
        AbstractHeap heap(JSInternalFields, node->internalFieldIndex());
        read(heap);
        def(HeapLocation(InternalFieldObjectLoc, heap, node->child1()), LazyNode(node));
        return;
    }

    case PutInternalField: {
        AbstractHeap heap(JSInternalFields, node->internalFieldIndex());
        write(heap);
        def(HeapLocation(InternalFieldObjectLoc, heap, node->child1()), LazyNode(node->child2().node()));
        return;
    }

    case GetRegExpObjectLastIndex:
        read(RegExpObject_lastIndex);
        def(HeapLocation(RegExpObjectLastIndexLoc, RegExpObject_lastIndex, node->child1()), LazyNode(node));
        return;

    case SetRegExpObjectLastIndex:
        write(RegExpObject_lastIndex);
        def(HeapLocation(RegExpObjectLastIndexLoc, RegExpObject_lastIndex, node->child1()), LazyNode(node->child2().node()));
        return;

    case RecordRegExpCachedResult:
        write(RegExpState);
        return;
        
    case GetFromArguments: {
        AbstractHeap heap(DirectArgumentsProperties, node->capturedArgumentsOffset().offset());
        read(heap);
        def(HeapLocation(DirectArgumentsLoc, heap, node->child1()), LazyNode(node));
        return;
    }
        
    case PutToArguments: {
        AbstractHeap heap(DirectArgumentsProperties, node->capturedArgumentsOffset().offset());
        write(heap);
        def(HeapLocation(DirectArgumentsLoc, heap, node->child1()), LazyNode(node->child2().node()));
        return;
    }

    case GetArgument: {
        read(Stack);
        // FIXME: It would be trivial to have a def here.
        // https://bugs.webkit.org/show_bug.cgi?id=143077
        return;
    }
        
    case GetGlobalVar:
    case GetGlobalLexicalVariable: {
        auto location = node->hasDoubleResult() ? GlobalVariableDoubleLoc : GlobalVariableLoc;
        read(AbstractHeap(Absolute, node->variablePointer()));
        def(HeapLocation(location, AbstractHeap(Absolute, node->variablePointer())), LazyNode(node));
        return;
    }
        
    case PutGlobalVariable: {
        write(AbstractHeap(Absolute, node->variablePointer()));
        auto location = node->child2().useKind() == DoubleRepUse ? GlobalVariableDoubleLoc : GlobalVariableLoc;
        def(HeapLocation(location, AbstractHeap(Absolute, node->variablePointer())), LazyNode(node->child2().node()));
        return;
    }

    case NewArrayWithSpecies:
        clobberTop();
        return;

    case NewArrayWithSize:
    case NewArrayWithSizeAndStructure:
        read(HeapObjectCount);
        write(HeapObjectCount);
        return;

    case NewArrayWithConstantSize:
    case PhantomNewArrayWithConstantSize:
    case MaterializeNewArrayWithConstantSize:
        read(HeapObjectCount);
        write(HeapObjectCount);
        def(HeapLocation(ArrayLengthLoc, Butterfly_publicLength, node), LazyNode(graph.freeze(jsNumber(node->newArraySize()))));
        return;

    case NewTypedArray:
    case NewTypedArrayBuffer:
        switch (node->child1().useKind()) {
        case Int32Use:
        case Int52RepUse:
            read(HeapObjectCount);
            write(HeapObjectCount);
            return;
        case UntypedUse:
            clobberTop();
            return;
        default:
            DFG_CRASH(graph, node, "Bad use kind");
        }
        break;

    case NewArrayWithSpread: {
        read(HeapObjectCount);
        // This appears to read nothing because it's only reading immutable butterfly data.
        for (unsigned i = 0; i < node->numChildren(); i++) {
            Node* child = graph.varArgChild(node, i).node();
            if (child->op() == PhantomSpread) {
                read(Stack);
                break;
            }
        }
        write(HeapObjectCount);
        return;
    }

    case Spread: {
        if (node->child1()->op() == PhantomNewArrayBuffer) {
            read(MiscFields);
            return;
        }

        if (node->child1()->op() == PhantomCreateRest) {
            read(Stack);
            write(HeapObjectCount);
            return;
        }

        clobberTop();
        return;
    }

    case NewArray: {
        read(HeapObjectCount);
        write(HeapObjectCount);

        unsigned numElements = node->numChildren();

        def(HeapLocation(ArrayLengthLoc, Butterfly_publicLength, node),
            LazyNode(graph.freeze(jsNumber(numElements))));

        if (!numElements)
            return;

        AbstractHeap heap;
        LocationKind indexedPropertyLoc;
        switch (node->indexingType()) {
        case ALL_DOUBLE_INDEXING_TYPES:
            heap = IndexedDoubleProperties;
            indexedPropertyLoc = IndexedPropertyDoubleLoc;
            break;

        case ALL_INT32_INDEXING_TYPES:
            heap = IndexedInt32Properties;
            indexedPropertyLoc = IndexedPropertyJSLoc;
            break;

        case ALL_CONTIGUOUS_INDEXING_TYPES:
            heap = IndexedContiguousProperties;
            indexedPropertyLoc = IndexedPropertyJSLoc;
            break;

        default:
            return;
        }

        if (numElements < graph.m_uint32ValuesInUse.size()) {
            for (unsigned operandIdx = 0; operandIdx < numElements; ++operandIdx) {
                Edge use = graph.m_varArgChildren[node->firstChild() + operandIdx];
                def(HeapLocation(indexedPropertyLoc, heap, node, LazyNode(graph.freeze(jsNumber(operandIdx)))),
                    LazyNode(use.node()));
            }
        } else {
            for (uint32_t operandIdx : graph.m_uint32ValuesInUse) {
                if (operandIdx >= numElements)
                    continue;
                Edge use = graph.m_varArgChildren[node->firstChild() + operandIdx];
                // operandIdx comes from graph.m_uint32ValuesInUse and thus is guaranteed to be already frozen
                def(HeapLocation(indexedPropertyLoc, heap, node, LazyNode(graph.freeze(jsNumber(operandIdx)))),
                    LazyNode(use.node()));
            }
        }
        return;
    }

    case NewArrayBuffer: {
        read(HeapObjectCount);
        write(HeapObjectCount);

        auto* array = node->castOperand<JSImmutableButterfly*>();
        unsigned numElements = array->length();
        def(HeapLocation(ArrayLengthLoc, Butterfly_publicLength, node),
            LazyNode(graph.freeze(jsNumber(numElements))));

        AbstractHeap heap;
        LocationKind indexedPropertyLoc;
        NodeType op = JSConstant;
        switch (node->indexingType()) {
        case ALL_DOUBLE_INDEXING_TYPES:
            heap = IndexedDoubleProperties;
            indexedPropertyLoc = IndexedPropertyDoubleLoc;
            op = DoubleConstant;
            break;

        case ALL_INT32_INDEXING_TYPES:
            heap = IndexedInt32Properties;
            indexedPropertyLoc = IndexedPropertyJSLoc;
            break;

        case ALL_CONTIGUOUS_INDEXING_TYPES:
            heap = IndexedContiguousProperties;
            indexedPropertyLoc = IndexedPropertyJSLoc;
            break;

        default:
            return;
        }

        if (numElements < graph.m_uint32ValuesInUse.size()) {
            for (unsigned index = 0; index < numElements; ++index) {
                def(HeapLocation(indexedPropertyLoc, heap, node, LazyNode(graph.freeze(jsNumber(index)))),
                    LazyNode(graph.freeze(array->get(index)), op));
            }
        } else {
            Vector<uint32_t> possibleIndices;
            for (uint32_t index : graph.m_uint32ValuesInUse) {
                if (index >= numElements)
                    continue;
                possibleIndices.append(index);
            }
            for (uint32_t index : possibleIndices) {
                def(HeapLocation(indexedPropertyLoc, heap, node, LazyNode(graph.freeze(jsNumber(index)))),
                    LazyNode(graph.freeze(array->get(index)), op));
            }
        }
        return;
    }

    case CreateRest: {
        if (!graph.isWatchingHavingABadTimeWatchpoint(node)) {
            // This means we're already having a bad time.
            clobberTop();
            return;
        }
        read(Stack);
        read(HeapObjectCount);
        write(HeapObjectCount);
        return;
    }

    case ObjectAssign: {
        clobberTop();
        return;
    }

    case ObjectCreate: {
        switch (node->child1().useKind()) {
        case ObjectUse:
            read(HeapObjectCount);
            write(HeapObjectCount);
            write(JSCell_structureID); // prototype object can be transitioned.
            return;
        case UntypedUse:
            clobberTop();
            return;
        default:
            RELEASE_ASSERT_NOT_REACHED();
            return;
        }
    }

    case NewSymbol:
        if (!node->child1() || node->child1().useKind() == StringUse) {
            read(HeapObjectCount);
            write(HeapObjectCount);
        } else
            clobberTop();
        return;

    case NewRegExpUntyped:
        if (node->child1().useKind() == StringUse && node->child2().useKind() == StringUse) {
            // SyntaxError may happen.
            read(World);
            write(SideState);
            write(HeapObjectCount);
        } else
            clobberTop();
        return;

    case NewObject:
    case NewGenerator:
    case NewAsyncGenerator:
    case NewInternalFieldObject:
    case NewRegExp:
    case NewStringObject:
    case NewMap:
    case NewSet:
    case PhantomNewObject:
    case MaterializeNewObject:
    case PhantomNewFunction:
    case PhantomNewGeneratorFunction:
    case PhantomNewAsyncFunction:
    case PhantomNewAsyncGeneratorFunction:
    case PhantomNewInternalFieldObject:
    case MaterializeNewInternalFieldObject:
    case PhantomCreateActivation:
    case MaterializeCreateActivation:
    case PhantomNewRegExp:
        read(HeapObjectCount);
        write(HeapObjectCount);
        return;

    case NewFunction:
    case NewGeneratorFunction:
    case NewAsyncGeneratorFunction:
    case NewAsyncFunction:
        if (node->castOperand<FunctionExecutable*>()->singleton().isStillValid())
            write(Watchpoint_fire);
        read(HeapObjectCount);
        write(HeapObjectCount);
        return;

    case NewBoundFunction:
        read(HeapObjectCount);
        write(HeapObjectCount);
        return;

    case RegExpSearch:
    case RegExpExec:
    case RegExpTest:
    case RegExpTestInline:
        // Even if we've proven known input types as RegExpObject and String,
        // accessing lastIndex is effectful if it's a global regexp.
        clobberTop();
        return;

    case RegExpMatchFast:
        read(RegExpState);
        read(RegExpObject_lastIndex);
        write(RegExpState);
        write(RegExpObject_lastIndex);
        return;

    case RegExpExecNonGlobalOrSticky:
    case RegExpMatchFastGlobal:
        read(RegExpState);
        write(RegExpState);
        return;

    case StringReplace:
    case StringReplaceAll:
    case StringReplaceRegExp:
        if (node->child1().useKind() == StringUse
            && node->child2().useKind() == RegExpObjectUse
            && node->child3().useKind() == StringUse) {
            read(RegExpState);
            read(RegExpObject_lastIndex);
            write(RegExpState);
            write(RegExpObject_lastIndex);
            return;
        }
        clobberTop();
        return;

    case StringReplaceString:
        if (node->child3().useKind() == StringUse)
            return;
        clobberTop();
        return;

    case StringAt:
    case StringCharAt:
        def(PureValue(node));
        return;

    case CompareBelow:
    case CompareBelowEq:
        def(PureValue(node));
        return;
        
    case CompareEq:
    case CompareLess:
    case CompareLessEq:
    case CompareGreater:
    case CompareGreaterEq:
        if (node->isBinaryUseKind(StringUse)) {
            read(HeapObjectCount);
            write(HeapObjectCount);
            return;
        }

        if (node->isBinaryUseKind(UntypedUse)) {
            clobberTop();
            return;
        }

        def(PureValue(node));
        return;

    case ToString:
    case CallStringConstructor:
        switch (node->child1().useKind()) {
        case CellUse:
        case UntypedUse:
            clobberTop();
            return;

        case KnownPrimitiveUse:
            write(SideState);
            return;

        case StringObjectUse:
        case StringOrStringObjectUse:
            // These two StringObjectUse's are pure because if we emit this node with either
            // of these UseKinds, we'll first emit a StructureCheck ensuring that we're the
            // original String or StringObject structure. Therefore, we don't have an overridden
            // valueOf, etc.

        case StringOrOtherUse:
        case Int32Use:
        case Int52RepUse:
        case DoubleRepUse:
        case NotCellUse:
            def(PureValue(node));
            return;
            
        default:
            RELEASE_ASSERT_NOT_REACHED();
            return;
        }

    case FunctionToString:
        def(PureValue(node));
        return;

    case FunctionBind:
        clobberTop(); // Slow path can clobber top.
        return;
        
    case CountExecution:
    case SuperSamplerBegin:
    case SuperSamplerEnd:
        read(InternalState);
        write(InternalState);
        return;
        
    case LogShadowChickenPrologue:
    case LogShadowChickenTail:
        write(SideState);
        return;

    case MapHash:
        def(PureValue(node));
        return;

    case NormalizeMapKey:
        def(PureValue(node));
        return;

    case MapGet: {
        Edge& mapEdge = node->child1();
        Edge& keyEdge = node->child2();
        Edge& hashEdge = node->child3();
        AbstractHeapKind heap = (mapEdge.useKind() == MapObjectUse) ? JSMapFields : JSSetFields;
        read(heap);
        def(HeapLocation(MapEntryKeyLoc, heap, mapEdge, keyEdge, hashEdge), LazyNode(node));
        return;
    }
    case LoadMapValue: {
        Edge& keySlotEdge = node->child1();
        AbstractHeapKind heap = JSMapFields;
        read(heap);
        def(HeapLocation(LoadMapValueLoc, heap, keySlotEdge), LazyNode(node));
        return;
    }

    case MapIteratorNext: {
        Edge& mapIteratorEdge = node->child1();
        AbstractHeapKind heap = (mapIteratorEdge.useKind() == MapIteratorObjectUse) ? JSMapIteratorFields : JSSetIteratorFields;
        read(heap);
        write(heap);
        def(HeapLocation(MapIteratorNextLoc, heap, mapIteratorEdge), LazyNode(node));
        return;
    }
    case MapIteratorKey: {
        Edge& mapIteratorEdge = node->child1();
        AbstractHeapKind heap = (mapIteratorEdge.useKind() == MapIteratorObjectUse) ? JSMapIteratorFields : JSSetIteratorFields;
        read(heap);
        def(HeapLocation(MapIteratorKeyLoc, heap, mapIteratorEdge), LazyNode(node));
        return;
    }
    case MapIteratorValue: {
        Edge& mapIteratorEdge = node->child1();
        AbstractHeapKind heap = (mapIteratorEdge.useKind() == MapIteratorObjectUse) ? JSMapIteratorFields : JSSetIteratorFields;
        read(heap);
        def(HeapLocation(MapIteratorValueLoc, heap, mapIteratorEdge), LazyNode(node));
        return;
    }

    case MapStorage:
    case MapStorageOrSentinel: {
        Edge& mapEdge = node->child1();
        AbstractHeapKind heap = (mapEdge.useKind() == MapObjectUse) ? JSMapFields : JSSetFields;
        read(heap);
        def(HeapLocation(MapStorageLoc, heap, mapEdge), LazyNode(node));
        return;
    }
    case MapIterationNext: {
        Edge& mapEdge = node->child1();
        Edge& entryEdge = node->child2();
        AbstractHeapKind heap = (node->bucketOwnerType() == BucketOwnerType::Map) ? JSMapFields : JSSetFields;
        read(heap);
        write(heap);
        def(HeapLocation(MapIterationNextLoc, heap, mapEdge, entryEdge), LazyNode(node));
        return;
    }
    case MapIterationEntry: {
        Edge& mapEdge = node->child1();
        AbstractHeapKind heap = (node->bucketOwnerType() == BucketOwnerType::Map) ? JSMapFields : JSSetFields;
        read(heap);
        def(HeapLocation(MapIterationEntryLoc, heap, mapEdge), LazyNode(node));
        return;
    }
    case MapIterationEntryKey: {
        Edge& mapEdge = node->child1();
        AbstractHeapKind heap = (node->bucketOwnerType() == BucketOwnerType::Map) ? JSMapFields : JSSetFields;
        read(heap);
        def(HeapLocation(MapIterationEntryKeyLoc, heap, mapEdge), LazyNode(node));
        return;
    }
    case MapIterationEntryValue: {
        Edge& mapEdge = node->child1();
        AbstractHeapKind heap = (node->bucketOwnerType() == BucketOwnerType::Map) ? JSMapFields : JSSetFields;
        read(heap);
        def(HeapLocation(MapIterationEntryValueLoc, heap, mapEdge), LazyNode(node));
        return;
    }

    case WeakMapGet: {
        Edge& mapEdge = node->child1();
        Edge& keyEdge = node->child2();
        AbstractHeapKind heap = (mapEdge.useKind() == WeakMapObjectUse) ? JSWeakMapFields : JSWeakSetFields;
        read(heap);
        def(HeapLocation(WeakMapGetLoc, heap, mapEdge, keyEdge), LazyNode(node));
        return;
    }

    case SetAdd: {
        Edge& mapEdge = node->child1();
        Edge& keyEdge = node->child2();
        write(JSSetFields);
        def(HeapLocation(MapEntryValueLoc, JSSetFields, mapEdge, keyEdge), LazyNode(node));
        return;
    }

    case MapSet: {
        Edge& mapEdge = graph.varArgChild(node, 0);
        Edge& keyEdge = graph.varArgChild(node, 1);
        write(JSMapFields);
        def(HeapLocation(MapEntryValueLoc, JSMapFields, mapEdge, keyEdge), LazyNode(node));
        return;
    }

    case MapOrSetDelete: {
        Edge& mapEdge = node->child1();
        AbstractHeapKind heap = (mapEdge.useKind() == MapObjectUse) ? JSMapFields : JSSetFields;
        write(heap);
        return;
    }

    case WeakSetAdd: {
        Edge& mapEdge = node->child1();
        Edge& keyEdge = node->child2();
        if (keyEdge.useKind() != ObjectUse) {
            read(World);
            write(SideState);
        }
        write(JSWeakSetFields);
        def(HeapLocation(WeakMapGetLoc, JSWeakSetFields, mapEdge, keyEdge), LazyNode(keyEdge.node()));
        return;
    }

    case WeakMapSet: {
        Edge& mapEdge = graph.varArgChild(node, 0);
        Edge& keyEdge = graph.varArgChild(node, 1);
        Edge& valueEdge = graph.varArgChild(node, 2);
        if (keyEdge.useKind() != ObjectUse) {
            read(World);
            write(SideState);
        }
        write(JSWeakMapFields);
        def(HeapLocation(WeakMapGetLoc, JSWeakMapFields, mapEdge, keyEdge), LazyNode(valueEdge.node()));
        return;
    }

    case ExtractValueFromWeakMapGet:
        def(PureValue(node));
        return;

    case StringSlice:
    case StringSubstring:
        def(PureValue(node));
        return;

    case ToLowerCase:
        def(PureValue(node));
        return;

    case NumberToStringWithValidRadixConstant:
        def(PureValue(node, node->validRadixConstant()));
        return;

    case DateGetTime:
    case DateGetInt32OrNaN: {
        read(JSDateFields);
        def(HeapLocation(DateFieldLoc, AbstractHeap(JSDateFields, static_cast<uint64_t>(node->intrinsic())), node->child1()), LazyNode(node));
        return;
    }

    case DateSetTime: {
        write(JSDateFields);
        return;
    }

    case DataViewGetFloat:
    case DataViewGetInt: {
        read(MiscFields);
        read(TypedArrayProperties);
        if (node->dataViewData().isResizable) {
            write(MiscFields);
            write(TypedArrayProperties);
        } else {
            LocationKind indexedPropertyLoc = indexedPropertyLocToOutOfBoundsSaneChain(indexedPropertyLocForResultType(node->result()));
            def(HeapLocation(indexedPropertyLoc, AbstractHeap(TypedArrayProperties, node->dataViewData().asQuadWord), node->child1(), node->child2(), node->child3()), LazyNode(node));
        }
        return;
    }

    case DataViewSet: {
        read(MiscFields);
        read(TypedArrayProperties);
        if (node->dataViewData().isResizable)
            write(MiscFields);
        write(TypedArrayProperties);
        return;
    }

    case LastNodeType:
        RELEASE_ASSERT_NOT_REACHED();
        return;
    }
    
    DFG_CRASH(graph, node, toCString("Unrecognized node type: ", Graph::opName(node->op())).data());
}

class NoOpClobberize {
public:
    NoOpClobberize() { }
    template<typename... T>
    void operator()(T...) const { }
};

class CheckClobberize {
public:
    CheckClobberize()
        : m_result(false)
    {
    }
    
    template<typename... T>
    void operator()(T...) const { m_result = true; }
    
    bool result() const { return m_result; }
    
private:
    mutable bool m_result;
};

bool doesWrites(Graph&, Node*);

class AbstractHeapOverlaps {
public:
    AbstractHeapOverlaps(AbstractHeap heap)
        : m_heap(heap)
        , m_result(false)
    {
    }
    
    void operator()(AbstractHeap otherHeap) const
    {
        if (m_result)
            return;
        m_result = m_heap.overlaps(otherHeap);
    }
    
    bool result() const { return m_result; }

private:
    AbstractHeap m_heap;
    mutable bool m_result;
};

bool accessesOverlap(Graph&, Node*, AbstractHeap);
bool writesOverlap(Graph&, Node*, AbstractHeap);

bool clobbersHeap(Graph&, Node*);

// We would have used bind() for these, but because of the overlaoding that we are doing,
// it's quite a bit of clearer to just write this out the traditional way.

template<typename T>
class ReadMethodClobberize {
public:
    ReadMethodClobberize(T& value)
        : m_value(value)
    {
    }
    
    void operator()(AbstractHeap heap) const
    {
        m_value.read(heap);
    }
private:
    T& m_value;
};

template<typename T>
class WriteMethodClobberize {
public:
    WriteMethodClobberize(T& value)
        : m_value(value)
    {
    }
    
    void operator()(AbstractHeap heap) const
    {
        m_value.write(heap);
    }
private:
    T& m_value;
};

template<typename T>
class DefMethodClobberize {
public:
    DefMethodClobberize(T& value)
        : m_value(value)
    {
    }
    
    void operator()(PureValue value) const
    {
        m_value.def(value);
    }
    
    void operator()(HeapLocation location, LazyNode node) const
    {
        m_value.def(location, node);
    }

private:
    T& m_value;
};

template<typename Adaptor>
void clobberize(Graph& graph, Node* node, Adaptor& adaptor)
{
    ReadMethodClobberize<Adaptor> read(adaptor);
    WriteMethodClobberize<Adaptor> write(adaptor);
    DefMethodClobberize<Adaptor> def(adaptor);
    clobberize(graph, node, read, write, def);
}

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)
