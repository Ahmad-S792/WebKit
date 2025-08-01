/*
 *  Copyright (C) 2003-2025 Apple Inc. All rights reserved.
 *  Copyright (C) 2007 Eric Seidel <eric@webkit.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "config.h"
#include "Heap.h"

#include "BuiltinExecutables.h"
#include "CodeBlock.h"
#include "CodeBlockSetInlines.h"
#include "CollectingScope.h"
#include "ConservativeRoots.h"
#include "EdenGCActivityCallback.h"
#include "Exception.h"
#include "FastMallocAlignedMemoryAllocator.h"
#include "FullGCActivityCallback.h"
#include "FunctionExecutableInlines.h"
#include "GCActivityCallback.h"
#include "GCIncomingRefCountedInlines.h"
#include "GCIncomingRefCountedSetInlines.h"
#include "GCSegmentedArrayInlines.h"
#include "GCTypeMap.h"
#include "GigacageAlignedMemoryAllocator.h"
#include "HasOwnPropertyCache.h"
#include "HeapHelperPool.h"
#include "HeapIterationScope.h"
#include "HeapProfiler.h"
#include "HeapSnapshot.h"
#include "HeapSubspaceTypes.h"
#include "HeapVerifier.h"
#include "IncrementalSweeper.h"
#include "Interpreter.h"
#include "IsoCellSetInlines.h"
#include "IsoInlinedHeapCellTypeInlines.h"
#include "JITStubRoutineSet.h"
#include "JITWorklistInlines.h"
#include "JSFinalizationRegistry.h"
#include "JSIterator.h"
#include "JSRawJSONObject.h"
#include "JSRemoteFunction.h"
#include "JSVirtualMachineInternal.h"
#include "JSWeakMap.h"
#include "JSWeakObjectRef.h"
#include "JSWeakSet.h"
#include "MachineStackMarker.h"
#include "MarkStackMergingConstraint.h"
#include "MarkedJSValueRefArray.h"
#include "MarkedSpaceInlines.h"
#include "MarkingConstraintSet.h"
#include "MegamorphicCache.h"
#include "NumberObject.h"
#include "PreventCollectionScope.h"
#include "SamplingProfiler.h"
#include "ShadowChicken.h"
#include "SpaceTimeMutatorScheduler.h"
#include "StochasticSpaceTimeMutatorScheduler.h"
#include "StopIfNecessaryTimer.h"
#include "StructureAlignedMemoryAllocator.h"
#include "SubspaceInlines.h"
#include "SuperSampler.h"
#include "SweepingScope.h"
#include "SymbolTableInlines.h"
#include "SynchronousStopTheWorldMutatorScheduler.h"
#include "TypeProfiler.h"
#include "TypeProfilerLog.h"
#include "VM.h"
#include "VerifierSlotVisitorInlines.h"
#include "WasmCallee.h"
#include "WeakMapImplInlines.h"
#include "WeakSetInlines.h"
#include <algorithm>
#include <wtf/CryptographicallyRandomNumber.h>
#include <wtf/ListDump.h>
#include <wtf/RAMSize.h>
#include <wtf/Scope.h>
#include <wtf/SimpleStats.h>
#include <wtf/SystemTracing.h>
#include <wtf/Threading.h>

#if USE(BMALLOC_MEMORY_FOOTPRINT_API)
#include <bmalloc/bmalloc.h>
#endif

#if USE(FOUNDATION)
#include <wtf/spi/cocoa/objcSPI.h>
#endif

#ifdef JSC_GLIB_API_ENABLED
#include "JSCGLibWrapperObject.h"
#endif

namespace JSC {

namespace HeapInternal {
static constexpr bool verbose = false;
static constexpr bool verboseStop = false;
}

namespace {

static double maxPauseMS(double thisPauseMS)
{
    static double maxPauseMS;
    maxPauseMS = std::max(thisPauseMS, maxPauseMS);
    return maxPauseMS;
}

static GrowthMode growthMode(size_t ramSize)
{
    // An Aggressive heap uses more memory to go faster.
    // We do this for machines with enough RAM.
    size_t aggressiveHeapThresholdInBytes = static_cast<size_t>(Options::aggressiveHeapThresholdInMB()) * MB;
    if (ramSize >= aggressiveHeapThresholdInBytes)
        return GrowthMode::Aggressive;
    return GrowthMode::Default;
}

static size_t minHeapSize(HeapType heapType, size_t ramSize)
{
    switch (heapType) {
    case HeapType::Large:
        return static_cast<size_t>(std::min(
            static_cast<double>(Options::largeHeapSize()),
            ramSize * Options::smallHeapRAMFraction()));
    case HeapType::Medium:
        return Options::mediumHeapSize();
    case HeapType::Small:
        return Options::smallHeapSize();
    default:
        RELEASE_ASSERT_NOT_REACHED();
        break;
    }
}

static size_t maxEdenSizeForRateLimiting(GrowthMode growthMode, size_t minBytesPerCycle)
{
    // Only do rate limiting for Aggressive heaps.
    if (growthMode == GrowthMode::Aggressive)
        return Options::maxEdenSizeForRateLimitingMultiplier() * minBytesPerCycle;
    return 0.0;
}

static size_t proportionalHeapSize(size_t heapSize, GrowthMode growthMode, size_t ramSize)
{
    if (VM::isInMiniMode())
        return Options::miniVMHeapGrowthFactor() * heapSize;

    bool useNewHeapGrowthFactor = growthMode == GrowthMode::Aggressive;

    // Use new heuristic function for Aggressive heaps (machines >= 16GB RAM).
    // https://www.mathway.com/en/Algebra?asciimath=2%20*%20e%5E(-1%20*%20x)%20%2B%201%20%3Dy
    // Disable it for Darwin Intel machine.
#if OS(DARWIN) && CPU(X86_64)
    useNewHeapGrowthFactor = false;
#endif

    if (useNewHeapGrowthFactor) {
        double x = static_cast<double>(std::min(heapSize, ramSize)) / ramSize;
        double ratio = Options::heapGrowthMaxIncrease() * std::exp(-(Options::heapGrowthSteepnessFactor() * x)) + 1;
        return ratio * heapSize;
    }

#if USE(BMALLOC_MEMORY_FOOTPRINT_API)
    size_t memoryFootprint = bmalloc::api::memoryFootprint();
    if (memoryFootprint < ramSize * Options::smallHeapRAMFraction())
        return Options::smallHeapGrowthFactor() * heapSize;
    if (memoryFootprint < ramSize * Options::mediumHeapRAMFraction())
        return Options::mediumHeapGrowthFactor() * heapSize;
#else
    if (heapSize < ramSize * Options::smallHeapRAMFraction())
        return Options::smallHeapGrowthFactor() * heapSize;
    if (heapSize < ramSize * Options::mediumHeapRAMFraction())
        return Options::mediumHeapGrowthFactor() * heapSize;
#endif
    return Options::largeHeapGrowthFactor() * heapSize;
}

static void recordType(TypeCountSet& set, JSCell* cell)
{
    auto typeName = "[unknown]"_s;
    const ClassInfo* info = cell->classInfo();
    if (info && info->className)
        typeName = info->className;
    set.add(typeName);
}

constexpr bool measurePhaseTiming()
{
    return false;
}

UncheckedKeyHashMap<const char*, GCTypeMap<SimpleStats>>& timingStats()
{
    static UncheckedKeyHashMap<const char*, GCTypeMap<SimpleStats>>* result;
    static std::once_flag once;
    std::call_once(
        once,
        [] {
            result = new UncheckedKeyHashMap<const char*, GCTypeMap<SimpleStats>>();
        });
    return *result;
}

SimpleStats& timingStats(const char* name, CollectionScope scope)
{
    return timingStats().add(name, GCTypeMap<SimpleStats>()).iterator->value[scope];
}

class TimingScope {
public:
    TimingScope(std::optional<CollectionScope> scope, ASCIILiteral name)
        : m_scope(scope)
        , m_name(name)
    {
        if (measurePhaseTiming())
            m_before = MonotonicTime::now();
    }
    
    TimingScope(JSC::Heap& heap, ASCIILiteral name)
        : TimingScope(heap.collectionScope(), name)
    {
    }
    
    void setScope(std::optional<CollectionScope> scope)
    {
        m_scope = scope;
    }
    
    void setScope(JSC::Heap& heap)
    {
        setScope(heap.collectionScope());
    }
    
    ~TimingScope()
    {
        if (measurePhaseTiming()) {
            MonotonicTime after = MonotonicTime::now();
            Seconds timing = after - m_before;
            SimpleStats& stats = timingStats(m_name, *m_scope);
            stats.add(timing.milliseconds());
            dataLog("[GC:", *m_scope, "] ", m_name, " took: ", timing.milliseconds(), "ms (average ", stats.mean(), "ms).\n");
        }
    }
private:
    std::optional<CollectionScope> m_scope;
    MonotonicTime m_before;
    ASCIILiteral m_name;
};

} // anonymous namespace

class Heap::HeapThread final : public AutomaticThread {
public:
    HeapThread(const AbstractLocker& locker, JSC::Heap& heap)
        : AutomaticThread(locker, heap.m_threadLock, heap.m_threadCondition.copyRef())
        , m_heap(heap)
    {
    }

    ASCIILiteral name() const final
    {
        return "JSC Heap Collector Thread"_s;
    }
    
private:
    PollResult poll(const AbstractLocker& locker) final
    {
        if (m_heap.m_threadShouldStop) {
            m_heap.notifyThreadStopping(locker);
            return PollResult::Stop;
        }
        if (m_heap.shouldCollectInCollectorThread(locker)) {
            m_heap.m_collectorThreadIsRunning = true;
            return PollResult::Work;
        }
        m_heap.m_collectorThreadIsRunning = false;
        return PollResult::Wait;
    }
    
    WorkResult work() final
    {
        m_heap.collectInCollectorThread();
        return WorkResult::Continue;
    }
    
    void threadDidStart() final
    {
        Thread::registerGCThread(GCThreadType::Main);
    }

    void threadIsStopping(const AbstractLocker&) final
    {
        m_heap.m_collectorThreadIsRunning = false;
    }

    JSC::Heap& m_heap;
};

#define INIT_SERVER_ISO_SUBSPACE(name, heapCellType, type) \
    , name ISO_SUBSPACE_INIT(*this, heapCellType, type)

#define INIT_SERVER_STRUCTURE_ISO_SUBSPACE(name, heapCellType, type) \
    , name(#name, *this, heapCellType, WTF::roundUpToMultipleOf<type::atomSize>(sizeof(type)), type::numberOfLowerTierPreciseCells, makeUnique<StructureAlignedMemoryAllocator>())

Heap::Heap(VM& vm, HeapType heapType)
    : m_heapType(heapType)
    , m_ramSize(Options::forceRAMSize() ? Options::forceRAMSize() : ramSize())
    , m_growthMode(growthMode(m_ramSize))
    , m_minBytesPerCycle(minHeapSize(m_heapType, m_ramSize))
    , m_maxEdenSizeForRateLimiting(maxEdenSizeForRateLimiting(m_growthMode, m_minBytesPerCycle))
    , m_maxEdenSize(m_minBytesPerCycle)
    , m_maxHeapSize(m_minBytesPerCycle)
    , m_objectSpace(this)
    , m_machineThreads(makeUnique<MachineThreads>())
    , m_collectorSlotVisitor(makeUnique<SlotVisitor>(*this, "C"_s))
    , m_mutatorSlotVisitor(makeUnique<SlotVisitor>(*this, "M"_s))
    , m_mutatorMarkStack(makeUnique<MarkStackArray>())
    , m_raceMarkStack(makeUnique<MarkStackArray>())
    , m_constraintSet(makeUnique<MarkingConstraintSet>(*this))
    , m_handleSet(vm)
    , m_codeBlocks(makeUnique<CodeBlockSet>())
    , m_jitStubRoutines(makeUnique<JITStubRoutineSet>())
    // We seed with 10ms so that GCActivityCallback::didAllocate doesn't continuously
    // schedule the timer if we've never done a collection.
    , m_fullActivityCallback(FullGCActivityCallback::tryCreate(*this))
    , m_edenActivityCallback(EdenGCActivityCallback::tryCreate(*this))
    , m_sweeper(adoptRef(*new IncrementalSweeper(this)))
    , m_stopIfNecessaryTimer(adoptRef(*new StopIfNecessaryTimer(vm)))
    , m_sharedCollectorMarkStack(makeUnique<MarkStackArray>())
    , m_sharedMutatorMarkStack(makeUnique<MarkStackArray>())
    , m_helperClient(&heapHelperPool())
    , m_threadLock(Box<Lock>::create())
    , m_threadCondition(AutomaticThreadCondition::create())

    // HeapCellTypes
    , auxiliaryHeapCellType(CellAttributes(DoesNotNeedDestruction, HeapCell::Auxiliary))
    , immutableButterflyHeapCellType(CellAttributes(DoesNotNeedDestruction, HeapCell::JSCellWithIndexingHeader))
    , cellHeapCellType(CellAttributes(DoesNotNeedDestruction, HeapCell::JSCell))
    , destructibleCellHeapCellType(CellAttributes(NeedsDestruction, HeapCell::JSCell))
    , apiGlobalObjectHeapCellType(IsoHeapCellType::Args<JSAPIGlobalObject>())
    , callbackConstructorHeapCellType(IsoHeapCellType::Args<JSCallbackConstructor>())
    , callbackGlobalObjectHeapCellType(IsoHeapCellType::Args<JSCallbackObject<JSGlobalObject>>())
    , callbackObjectHeapCellType(IsoHeapCellType::Args<JSCallbackObject<JSNonFinalObject>>())
    , customGetterFunctionHeapCellType(IsoHeapCellType::Args<JSCustomGetterFunction>())
    , customSetterFunctionHeapCellType(IsoHeapCellType::Args<JSCustomSetterFunction>())
    , dateInstanceHeapCellType(IsoHeapCellType::Args<DateInstance>())
    , errorInstanceHeapCellType(IsoHeapCellType::Args<ErrorInstance>())
    , finalizationRegistryCellType(IsoHeapCellType::Args<JSFinalizationRegistry>())
    , globalLexicalEnvironmentHeapCellType(IsoHeapCellType::Args<JSGlobalLexicalEnvironment>())
    , globalObjectHeapCellType(IsoHeapCellType::Args<JSGlobalObject>())
    , injectedScriptHostSpaceHeapCellType(IsoHeapCellType::Args<Inspector::JSInjectedScriptHost>())
    , javaScriptCallFrameHeapCellType(IsoHeapCellType::Args<Inspector::JSJavaScriptCallFrame>())
    , jsModuleRecordHeapCellType(IsoHeapCellType::Args<JSModuleRecord>())
    , syntheticModuleRecordHeapCellType(IsoHeapCellType::Args<SyntheticModuleRecord>())
    , moduleNamespaceObjectHeapCellType(IsoHeapCellType::Args<JSModuleNamespaceObject>())
    , nativeStdFunctionHeapCellType(IsoHeapCellType::Args<JSNativeStdFunction>())
    , weakMapHeapCellType(IsoHeapCellType::Args<JSWeakMap>())
    , weakSetHeapCellType(IsoHeapCellType::Args<JSWeakSet>())
#if JSC_OBJC_API_ENABLED
    , apiWrapperObjectHeapCellType(IsoHeapCellType::Args<JSCallbackObject<JSAPIWrapperObject>>())
    , objCCallbackFunctionHeapCellType(IsoHeapCellType::Args<ObjCCallbackFunction>())
#endif
#ifdef JSC_GLIB_API_ENABLED
    , apiWrapperObjectHeapCellType(IsoHeapCellType::Args<JSCallbackObject<JSAPIWrapperObject>>())
    , callbackAPIWrapperGlobalObjectHeapCellType(IsoHeapCellType::Args<JSCallbackObject<JSAPIWrapperGlobalObject>>())
    , jscCallbackFunctionHeapCellType(IsoHeapCellType::Args<JSCCallbackFunction>())
#endif
    , intlCollatorHeapCellType(IsoHeapCellType::Args<IntlCollator>())
    , intlDateTimeFormatHeapCellType(IsoHeapCellType::Args<IntlDateTimeFormat>())
    , intlDisplayNamesHeapCellType(IsoHeapCellType::Args<IntlDisplayNames>())
    , intlDurationFormatHeapCellType(IsoHeapCellType::Args<IntlDurationFormat>())
    , intlListFormatHeapCellType(IsoHeapCellType::Args<IntlListFormat>())
    , intlLocaleHeapCellType(IsoHeapCellType::Args<IntlLocale>())
    , intlNumberFormatHeapCellType(IsoHeapCellType::Args<IntlNumberFormat>())
    , intlPluralRulesHeapCellType(IsoHeapCellType::Args<IntlPluralRules>())
    , intlRelativeTimeFormatHeapCellType(IsoHeapCellType::Args<IntlRelativeTimeFormat>())
    , intlSegmentIteratorHeapCellType(IsoHeapCellType::Args<IntlSegmentIterator>())
    , intlSegmenterHeapCellType(IsoHeapCellType::Args<IntlSegmenter>())
    , intlSegmentsHeapCellType(IsoHeapCellType::Args<IntlSegments>())
#if ENABLE(WEBASSEMBLY)
    , webAssemblyArrayHeapCellType(IsoHeapCellType::Args<JSWebAssemblyArray>())
    , webAssemblyExceptionHeapCellType(IsoHeapCellType::Args<JSWebAssemblyException>())
    , webAssemblyFunctionHeapCellType(IsoHeapCellType::Args<WebAssemblyFunction>())
    , webAssemblyGlobalHeapCellType(IsoHeapCellType::Args<JSWebAssemblyGlobal>())
    , webAssemblyInstanceHeapCellType(IsoHeapCellType::Args<JSWebAssemblyInstance>())
    , webAssemblyMemoryHeapCellType(IsoHeapCellType::Args<JSWebAssemblyMemory>())
    , webAssemblyStructHeapCellType(IsoHeapCellType::Args<JSWebAssemblyStruct>())
    , webAssemblyModuleHeapCellType(IsoHeapCellType::Args<JSWebAssemblyModule>())
    , webAssemblyModuleRecordHeapCellType(IsoHeapCellType::Args<WebAssemblyModuleRecord>())
    , webAssemblyTableHeapCellType(IsoHeapCellType::Args<JSWebAssemblyTable>())
    , webAssemblyTagHeapCellType(IsoHeapCellType::Args<JSWebAssemblyTag>())
#endif

    // AlignedMemoryAllocators
    , fastMallocAllocator(makeUnique<FastMallocAlignedMemoryAllocator>())
    , primitiveGigacageAllocator(makeUnique<GigacageAlignedMemoryAllocator>(Gigacage::Primitive))

    // Subspaces
    , primitiveGigacageAuxiliarySpace("Primitive Gigacage Auxiliary"_s, *this, auxiliaryHeapCellType, primitiveGigacageAllocator.get()) // Hash:0x3e7cd762
    , auxiliarySpace("Auxiliary"_s, *this, auxiliaryHeapCellType, fastMallocAllocator.get()) // Hash:0x96255ba1
    , immutableButterflyAuxiliarySpace("ImmutableButterfly JSCellWithIndexingHeader"_s, *this, immutableButterflyHeapCellType, fastMallocAllocator.get()) // Hash:0xaadcb3c1
    , cellSpace("JSCell"_s, *this, cellHeapCellType, fastMallocAllocator.get()) // Hash:0xadfb5a79
    , variableSizedCellSpace("Variable Sized JSCell"_s, *this, cellHeapCellType, fastMallocAllocator.get()) // Hash:0xbcd769cc
    , destructibleObjectSpace("JSDestructibleObject"_s, *this, destructibleObjectHeapCellType, fastMallocAllocator.get()) // Hash:0x4f5ed7a9
    FOR_EACH_JSC_COMMON_ISO_SUBSPACE(INIT_SERVER_ISO_SUBSPACE)
    FOR_EACH_JSC_STRUCTURE_ISO_SUBSPACE(INIT_SERVER_STRUCTURE_ISO_SUBSPACE)
    , codeBlockSpaceAndSet ISO_SUBSPACE_INIT(*this, destructibleCellHeapCellType, CodeBlock) // Hash:0x2b743c6a
    , functionExecutableSpaceAndSet ISO_SUBSPACE_INIT(*this, destructibleCellHeapCellType, FunctionExecutable) // Hash:0xbcb36268
    , programExecutableSpaceAndSet ISO_SUBSPACE_INIT(*this, destructibleCellHeapCellType, ProgramExecutable) // Hash:0x4c9208f7
    , unlinkedFunctionExecutableSpaceAndSet ISO_SUBSPACE_INIT(*this, destructibleCellHeapCellType, UnlinkedFunctionExecutable) // Hash:0x3ba0f4e1

{
    m_worldState.store(0);

    for (unsigned i = 0, numberOfParallelThreads = heapHelperPool().numberOfThreads(); i < numberOfParallelThreads; ++i) {
        std::unique_ptr<SlotVisitor> visitor = makeUnique<SlotVisitor>(*this, toCString("P", i + 1));
        if (Options::optimizeParallelSlotVisitorsForStoppedMutator())
            visitor->optimizeForStoppedMutator();
        m_availableParallelSlotVisitors.append(visitor.get());
        m_parallelSlotVisitors.append(WTFMove(visitor));
    }
    
    if (Options::useConcurrentGC()) {
        if (Options::useStochasticMutatorScheduler())
            m_scheduler = makeUnique<StochasticSpaceTimeMutatorScheduler>(*this);
        else
            m_scheduler = makeUnique<SpaceTimeMutatorScheduler>(*this);
    } else {
        // We simulate turning off concurrent GC by making the scheduler say that the world
        // should always be stopped when the collector is running.
        m_scheduler = makeUnique<SynchronousStopTheWorldMutatorScheduler>();
    }
    
    if (Options::verifyHeap())
        m_verifier = makeUnique<HeapVerifier>(this, Options::numberOfGCCyclesToRecordForVerification());
    
    m_collectorSlotVisitor->optimizeForStoppedMutator();

    // When memory is critical, allow allocating 25% of the amount above the critical threshold before collecting.
    size_t memoryAboveCriticalThreshold = static_cast<size_t>(static_cast<double>(m_ramSize) * (1.0 - Options::criticalGCMemoryThreshold()));
    m_maxEdenSizeWhenCritical = memoryAboveCriticalThreshold / 4;

    Locker locker { *m_threadLock };
    m_thread = adoptRef(new HeapThread(locker, *this));
}

#undef INIT_SERVER_ISO_SUBSPACE
#undef INIT_SERVER_STRUCTURE_ISO_SUBSPACE

Heap::~Heap()
{
    // Scribble m_worldState to make it clear that the heap has already been destroyed if we crash in checkConn
    m_worldState.store(0xbadbeeffu);

    forEachSlotVisitor(
        [&] (SlotVisitor& visitor) {
            visitor.clearMarkStacks();
        });
    m_mutatorMarkStack->clear();
    m_raceMarkStack->clear();
    
    for (WeakBlock* block : m_logicallyEmptyWeakBlocks)
        WeakBlock::destroy(*this, block);
}

bool Heap::isPagedOut()
{
    return m_objectSpace.isPagedOut();
}

void Heap::dumpHeapStatisticsAtVMDestruction()
{
    unsigned counter = 0;
    HeapIterationScope iterationScope(*this);
    m_objectSpace.forEachBlock([&] (MarkedBlock::Handle* block) {
        unsigned live = 0;
        block->forEachLiveCell([&] (size_t, HeapCell*, HeapCell::Kind) {
            live++;
            return IterationStatus::Continue;
        });
        dataLogLn("[", counter++, "] ", block->cellSize(), ", ", live, " / ", block->cellsPerBlock(), " ", static_cast<double>(live) / block->cellsPerBlock() * 100, "% ", block->attributes(), " ", block->subspace()->name());
        block->forEachLiveCell([&] (size_t, HeapCell* heapCell, HeapCell::Kind kind) {
            if (kind == HeapCell::Kind::JSCell) {
                auto* cell = static_cast<JSCell*>(heapCell);
                if (cell->isObject())
                    dataLogLn("    ", JSValue((JSObject*)cell));
                else
                    dataLogLn("    ", *cell);
            }
            return IterationStatus::Continue;
        });
    });
}

// The VM is being destroyed and the collector will never run again.
// Run all pending finalizers now because we won't get another chance.
void Heap::lastChanceToFinalize()
{
    MonotonicTime before;
    if (Options::logGC()) [[unlikely]] {
        before = MonotonicTime::now();
        dataLog("[GC<", RawPointer(this), ">: shutdown ");
    }
    
    m_isShuttingDown = true;
    
    RELEASE_ASSERT(!vm().entryScope);
    RELEASE_ASSERT(m_mutatorState == MutatorState::Running);
    
    if (m_collectContinuouslyThread) {
        {
            Locker locker { m_collectContinuouslyLock };
            m_shouldStopCollectingContinuously = true;
            m_collectContinuouslyCondition.notifyOne();
        }
        m_collectContinuouslyThread->waitForCompletion();
    }

    dataLogIf(Options::logGC(), "1");
    
    // Prevent new collections from being started. This is probably not even necessary, since we're not
    // going to call into anything that starts collections. Still, this makes the algorithm more
    // obviously sound.
    m_isSafeToCollect = false;
    
    dataLogIf(Options::logGC(), "2");

    bool isCollecting;
    {
        Locker locker { *m_threadLock };
        RELEASE_ASSERT(m_lastServedTicket <= m_lastGrantedTicket);
        isCollecting = m_lastServedTicket < m_lastGrantedTicket;
    }
    if (isCollecting) {
        dataLogIf(Options::logGC(), "...]\n");
        
        // Wait for the current collection to finish.
        waitForCollector(
            [&] (const AbstractLocker&) -> bool {
                RELEASE_ASSERT(m_lastServedTicket <= m_lastGrantedTicket);
                return m_lastServedTicket == m_lastGrantedTicket;
            });
        
        dataLogIf(Options::logGC(), "[GC<", RawPointer(this), ">: shutdown ");
    }
    dataLogIf(Options::logGC(), "3");

    RELEASE_ASSERT(m_requests.isEmpty());
    RELEASE_ASSERT(m_lastServedTicket == m_lastGrantedTicket);
    
    // Carefully bring the thread down.
    bool stopped = false;
    {
        Locker locker { *m_threadLock };
        stopped = m_thread->tryStop(locker);
        m_threadShouldStop = true;
        if (!stopped)
            m_threadCondition->notifyOne(locker);
    }

    dataLogIf(Options::logGC(), "4");
    
    if (!stopped)
        m_thread->join();
    
    dataLogIf(Options::logGC(), "5 ");

    if (Options::dumpHeapStatisticsAtVMDestruction()) [[unlikely]]
        dumpHeapStatisticsAtVMDestruction();
    
    m_arrayBuffers.lastChanceToFinalize();
    m_objectSpace.stopAllocatingForGood();
    m_objectSpace.lastChanceToFinalize();
    releaseDelayedReleasedObjects();

    sweepAllLogicallyEmptyWeakBlocks();
    
    m_objectSpace.freeMemory();
    
    dataLogIf(Options::logGC(), (MonotonicTime::now() - before).milliseconds(), "ms]\n");
}

void Heap::releaseDelayedReleasedObjects()
{
#if USE(FOUNDATION) || defined(JSC_GLIB_API_ENABLED)
    // We need to guard against the case that releasing an object can create more objects due to the
    // release calling into JS. When those JS call(s) exit and all locks are being dropped we end up
    // back here and could try to recursively release objects. We guard that with a recursive entry
    // count. Only the initial call will release objects, recursive calls simple return and let the
    // the initial call to the function take care of any objects created during release time.
    // This also means that we need to loop until there are no objects in m_delayedReleaseObjects
    // and use a temp Vector for the actual releasing.
    if (!m_delayedReleaseRecursionCount++) {
        while (!m_delayedReleaseObjects.isEmpty()) {
            ASSERT(vm().currentThreadIsHoldingAPILock());

            auto objectsToRelease = WTFMove(m_delayedReleaseObjects);

            {
                // We need to drop locks before calling out to arbitrary code.
                JSLock::DropAllLocks dropAllLocks(vm());

#if USE(FOUNDATION)
                void* context = objc_autoreleasePoolPush();
#endif
                objectsToRelease.clear();
#if USE(FOUNDATION)
                objc_autoreleasePoolPop(context);
#endif
            }
        }
    }
    m_delayedReleaseRecursionCount--;
#endif
}

void Heap::reportExtraMemoryAllocatedPossiblyFromAlreadyMarkedCell(const JSCell* cell, size_t size)
{
    ASSERT(cell);

    // Increasing extraMemory of already marked objects will not be visible as a retained memory.
    // We need to report this additionally to tell GC that we get additional extra memory now,
    // and GC needs to consider scheduling GC based on this increase.

    if (mutatorShouldBeFenced()) [[unlikely]] {
        // In this case, the barrierThreshold is the tautological threshold, so cell could still be
        // not black. But we can't know for sure until we fire off a fence.
        WTF::storeLoadFence();
        if (cell->cellState() != CellState::PossiblyBlack)
            return;

        WTF::loadLoadFence();
        if (!isMarked(cell)) {
            // During a full collection a store into an unmarked object that had surivived past
            // collections will manifest as a store to an unmarked PossiblyBlack object. If the
            // object gets marked at some time after this then it will go down the normal marking
            // path. So, we don't have to remember this object. We could return here. But we go
            // further and attempt to re-white the object.
            ASSERT(m_collectionScope && m_collectionScope.value() == CollectionScope::Full);
            return;
        }
    } else
        ASSERT(isMarked(cell));

    // It could be that the object was *just* marked. This means that the collector may set the
    // state to DefinitelyGrey and then to PossiblyOldOrBlack at any time. It's OK for us to
    // race with the collector here. If we win then this is accurate because the object _will_
    // get scanned again. If we lose then someone else will barrier the object again. That would
    // be unfortunate but not the end of the world.
    reportExtraMemoryVisited(size);
}

void Heap::reportExtraMemoryAllocatedSlowCase(GCDeferralContext* deferralContext, const JSCell* cell, size_t size)
{
    didAllocate(size);
    if (cell) {
        if (isWithinThreshold(cell->cellState(), barrierThreshold())) [[unlikely]]
            reportExtraMemoryAllocatedPossiblyFromAlreadyMarkedCell(cell, size);
    }
    collectIfNecessaryOrDefer(deferralContext);
}

void Heap::deprecatedReportExtraMemorySlowCase(size_t size)
{
    // FIXME: Change this to use SaturatedArithmetic when available.
    // https://bugs.webkit.org/show_bug.cgi?id=170411
    CheckedSize checkedNewSize = m_deprecatedExtraMemorySize;
    checkedNewSize += size;
    size_t newSize = std::numeric_limits<size_t>::max();
    if (!checkedNewSize.hasOverflowed()) [[likely]]
        newSize = checkedNewSize.value();
    m_deprecatedExtraMemorySize = newSize;
    reportExtraMemoryAllocatedSlowCase(nullptr, nullptr, size);
}

bool Heap::overCriticalMemoryThreshold(MemoryThresholdCallType memoryThresholdCallType)
{
#if USE(BMALLOC_MEMORY_FOOTPRINT_API)
    if (memoryThresholdCallType == MemoryThresholdCallType::Direct || ++m_percentAvailableMemoryCachedCallCount >= 100) {
        m_overCriticalMemoryThreshold = bmalloc::api::percentAvailableMemoryInUse() > Options::criticalGCMemoryThreshold();
        m_percentAvailableMemoryCachedCallCount = 0;
    }

    return m_overCriticalMemoryThreshold;
#else
    UNUSED_PARAM(memoryThresholdCallType);
    return false;
#endif
}

void Heap::reportAbandonedObjectGraph()
{
    // Our clients don't know exactly how much memory they
    // are abandoning so we just guess for them.
    size_t abandonedBytes = static_cast<size_t>(0.1 * capacity());

    // We want to accelerate the next collection. Because memory has just 
    // been abandoned, the next collection has the potential to 
    // be more profitable. Since allocation is the trigger for collection, 
    // we hasten the next collection by pretending that we've allocated more memory. 
    if (m_fullActivityCallback) {
        m_fullActivityCallback->didAllocate(*this,
            m_sizeAfterLastCollect - m_sizeAfterLastFullCollect + totalBytesAllocatedThisCycle() + m_bytesAbandonedSinceLastFullCollect);
    }
    m_bytesAbandonedSinceLastFullCollect += abandonedBytes;
}

void Heap::protect(JSValue k)
{
    ASSERT(k);
    ASSERT(vm().currentThreadIsHoldingAPILock());

    if (!k.isCell())
        return;

    m_protectedValues.add(k.asCell());
}

bool Heap::unprotect(JSValue k)
{
    ASSERT(k);
    ASSERT(vm().currentThreadIsHoldingAPILock());

    if (!k.isCell())
        return false;

    return m_protectedValues.remove(k.asCell());
}

void Heap::addReference(JSCell* cell, ArrayBuffer* buffer)
{
    if (m_arrayBuffers.addReference(cell, buffer)) {
        collectIfNecessaryOrDefer();
        didAllocate(buffer->gcSizeEstimateInBytes());
    }
}

template<typename CellType, typename CellSet>
void Heap::finalizeMarkedUnconditionalFinalizers(CellSet& cellSet, CollectionScope collectionScope)
{
    cellSet.forEachMarkedCell(
        [&] (HeapCell* cell, HeapCell::Kind) {
            static_cast<CellType*>(cell)->finalizeUnconditionally(vm(), collectionScope);
        });
}

void Heap::finalizeUnconditionalFinalizers()
{
    CollectionScope collectionScope = this->collectionScope().value_or(CollectionScope::Full);

    {
        // We run this before CodeBlock's unconditional finalizer since CodeBlock looks at the owner executable's installed CodeBlock in its finalizeUnconditionally.

        // FunctionExecutable requires all live instances to run finalizers. Thus, we do not use finalizer set.
        finalizeMarkedUnconditionalFinalizers<FunctionExecutable>(functionExecutableSpaceAndSet.space, collectionScope);

        finalizeMarkedUnconditionalFinalizers<ProgramExecutable>(programExecutableSpaceAndSet.finalizerSet, collectionScope);
        if (m_evalExecutableSpace)
            finalizeMarkedUnconditionalFinalizers<EvalExecutable>(m_evalExecutableSpace->finalizerSet, collectionScope);
        if (m_moduleProgramExecutableSpace)
            finalizeMarkedUnconditionalFinalizers<ModuleProgramExecutable>(m_moduleProgramExecutableSpace->finalizerSet, collectionScope);
    }

    finalizeMarkedUnconditionalFinalizers<SymbolTable>(symbolTableSpace, collectionScope);

    forEachCodeBlockSpace(
        [&] (auto& space) {
            this->finalizeMarkedUnconditionalFinalizers<CodeBlock>(space.set, collectionScope);
        });
    if (collectionScope == CollectionScope::Full) {
        finalizeMarkedUnconditionalFinalizers<Structure>(structureSpace, collectionScope);
        finalizeMarkedUnconditionalFinalizers<BrandedStructure>(brandedStructureSpace, collectionScope);
    }
    finalizeMarkedUnconditionalFinalizers<StructureRareData>(structureRareDataSpace, collectionScope);
    finalizeMarkedUnconditionalFinalizers<UnlinkedFunctionExecutable>(unlinkedFunctionExecutableSpaceAndSet.set, collectionScope);
    if (m_weakSetSpace)
        finalizeMarkedUnconditionalFinalizers<JSWeakSet>(*m_weakSetSpace, collectionScope);
    if (m_weakMapSpace)
        finalizeMarkedUnconditionalFinalizers<JSWeakMap>(*m_weakMapSpace, collectionScope);
    if (m_weakObjectRefSpace)
        finalizeMarkedUnconditionalFinalizers<JSWeakObjectRef>(*m_weakObjectRefSpace, collectionScope);
    if (m_errorInstanceSpace)
        finalizeMarkedUnconditionalFinalizers<ErrorInstance>(*m_errorInstanceSpace, collectionScope);

    // FinalizationRegistries currently rely on serial finalization because they can post tasks to the deferredWorkTimer, which normally expects tasks to only be posted by the API lock holder.
    if (m_finalizationRegistrySpace)
        finalizeMarkedUnconditionalFinalizers<JSFinalizationRegistry>(*m_finalizationRegistrySpace, collectionScope);

#if ENABLE(WEBASSEMBLY)
    if (m_webAssemblyInstanceSpace)
        finalizeMarkedUnconditionalFinalizers<JSWebAssemblyInstance>(*m_webAssemblyInstanceSpace, collectionScope);
#endif
}

void Heap::willStartIterating()
{
    m_objectSpace.willStartIterating();
}

void Heap::didFinishIterating()
{
    m_objectSpace.didFinishIterating();
}

void Heap::completeAllJITPlans()
{
    if (!Options::useJIT())
        return;
#if ENABLE(JIT)
    JITWorklist::ensureGlobalWorklist().completeAllPlansForVM(vm());
#endif // ENABLE(JIT)
}

template<typename Visitor>
void Heap::iterateExecutingAndCompilingCodeBlocks(Visitor& visitor, NOESCAPE const Function<void(CodeBlock*)>& func)
{
    m_codeBlocks->iterateCurrentlyExecuting(func);
#if ENABLE(JIT)
    if (Options::useJIT())
        JITWorklist::ensureGlobalWorklist().iterateCodeBlocksForGC(visitor, vm(), func);
#else
    UNUSED_PARAM(visitor);
#endif // ENABLE(JIT)
}

template<typename Func, typename Visitor>
void Heap::iterateExecutingAndCompilingCodeBlocksWithoutHoldingLocks(Visitor& visitor, const Func& func)
{
    Vector<CodeBlock*, 256> codeBlocks;
    iterateExecutingAndCompilingCodeBlocks(visitor,
        [&] (CodeBlock* codeBlock) {
            codeBlocks.append(codeBlock);
        });
    for (CodeBlock* codeBlock : codeBlocks)
        func(codeBlock);
}

void Heap::assertMarkStacksEmpty()
{
    bool ok = true;
    
    if (!m_sharedCollectorMarkStack->isEmpty()) {
        dataLog("FATAL: Shared collector mark stack not empty! It has ", m_sharedCollectorMarkStack->size(), " elements.\n");
        ok = false;
    }
    
    if (!m_sharedMutatorMarkStack->isEmpty()) {
        dataLog("FATAL: Shared mutator mark stack not empty! It has ", m_sharedMutatorMarkStack->size(), " elements.\n");
        ok = false;
    }
    
    forEachSlotVisitor(
        [&] (SlotVisitor& visitor) {
            if (visitor.isEmpty())
                return;
            
            dataLog("FATAL: Visitor ", RawPointer(&visitor), " is not empty!\n");
            ok = false;
        });
    
    RELEASE_ASSERT(ok);
}

void Heap::gatherStackRoots(ConservativeRoots& roots)
{
    m_machineThreads->gatherConservativeRoots(roots, *m_jitStubRoutines, *m_codeBlocks, m_currentThreadState, m_currentThread);
}

void Heap::gatherJSStackRoots(ConservativeRoots& roots)
{
#if ENABLE(C_LOOP)
    vm().interpreter.cloopStack().gatherConservativeRoots(roots, *m_jitStubRoutines, *m_codeBlocks);
#else
    UNUSED_PARAM(roots);
#endif
}

void Heap::gatherScratchBufferRoots(ConservativeRoots& roots)
{
#if ENABLE(DFG_JIT)
    if (!Options::useJIT())
        return;
    VM& vm = this->vm();
    vm.gatherScratchBufferRoots(roots);
    vm.scanSideState(roots);
#else
    UNUSED_PARAM(roots);
#endif
}

void Heap::beginMarking()
{
    TimingScope timingScope(*this, "Heap::beginMarking"_s);
    m_jitStubRoutines->clearMarks();
    m_objectSpace.beginMarking();
    vm().beginMarking();
    setMutatorShouldBeFenced(true);
}

void Heap::removeDeadCompilerWorklistEntries()
{
    if (!Options::useJIT())
        return;
#if ENABLE(JIT)
    JITWorklist::ensureGlobalWorklist().removeDeadPlans(vm());
#endif // ENABLE(JIT)
}

struct GatherExtraHeapData : MarkedBlock::CountFunctor {
    GatherExtraHeapData(HeapAnalyzer& analyzer)
        : m_analyzer(analyzer)
    {
    }

    IterationStatus operator()(HeapCell* heapCell, HeapCell::Kind kind) const
    {
        if (isJSCellKind(kind)) {
            JSCell* cell = static_cast<JSCell*>(heapCell);
            cell->methodTable()->analyzeHeap(cell, m_analyzer);
        }
        return IterationStatus::Continue;
    }

    HeapAnalyzer& m_analyzer;
};

void Heap::gatherExtraHeapData(HeapProfiler& heapProfiler)
{
    if (auto* analyzer = heapProfiler.activeHeapAnalyzer()) {
        HeapIterationScope heapIterationScope(*this);
        GatherExtraHeapData functor(*analyzer);
        m_objectSpace.forEachLiveCell(heapIterationScope, functor);
    }
}

struct RemoveDeadHeapSnapshotNodes : MarkedBlock::CountFunctor {
    RemoveDeadHeapSnapshotNodes(HeapSnapshot& snapshot)
        : m_snapshot(snapshot)
    {
    }

    IterationStatus operator()(HeapCell* cell, HeapCell::Kind kind) const
    {
        if (isJSCellKind(kind))
            m_snapshot.sweepCell(static_cast<JSCell*>(cell));
        return IterationStatus::Continue;
    }

    HeapSnapshot& m_snapshot;
};

void Heap::removeDeadHeapSnapshotNodes(HeapProfiler& heapProfiler)
{
    if (HeapSnapshot* snapshot = heapProfiler.mostRecentSnapshot()) {
        HeapIterationScope heapIterationScope(*this);
        RemoveDeadHeapSnapshotNodes functor(*snapshot);
        m_objectSpace.forEachDeadCell(heapIterationScope, functor);
        snapshot->shrinkToFit();
    }
}

void Heap::updateObjectCounts()
{
    if (m_collectionScope && m_collectionScope.value() == CollectionScope::Full) {
        m_totalBytesVisitedAfterLastFullCollect = m_totalBytesVisited;
        m_totalBytesVisited = 0;
    }

    m_totalBytesVisitedThisCycle = bytesVisited();
    
    m_totalBytesVisited += m_totalBytesVisitedThisCycle;
}

void Heap::endMarking()
{
    forEachSlotVisitor(
        [&] (SlotVisitor& visitor) {
            visitor.reset();
        });

    assertMarkStacksEmpty();

    RELEASE_ASSERT(m_raceMarkStack->isEmpty());
    
    m_objectSpace.endMarking();
    setMutatorShouldBeFenced(Options::forceFencedBarrier());
}

size_t Heap::objectCount()
{
    return m_objectSpace.objectCount();
}

size_t Heap::extraMemorySize()
{
    // FIXME: Change this to use SaturatedArithmetic when available.
    // https://bugs.webkit.org/show_bug.cgi?id=170411
    CheckedSize checkedTotal = m_extraMemorySize;
    checkedTotal += m_deprecatedExtraMemorySize;
    checkedTotal += m_arrayBuffers.size();
    size_t total = std::numeric_limits<size_t>::max();
    if (!checkedTotal.hasOverflowed()) [[likely]]
        total = checkedTotal.value();

    // It would be nice to have `ASSERT(m_objectSpace.capacity() >= m_objectSpace.size());` here but `m_objectSpace.size()`
    // requires having heap access which thread might not. Specifically, we might be called from the resource usage thread.
    return std::min(total, std::numeric_limits<size_t>::max() - m_objectSpace.capacity());
}

size_t Heap::size()
{
    return m_objectSpace.size() + extraMemorySize();
}

size_t Heap::capacity()
{
    return m_objectSpace.capacity() + extraMemorySize();
}

size_t Heap::protectedGlobalObjectCount()
{
    size_t result = 0;
    forEachProtectedCell(
        [&] (JSCell* cell) {
            if (cell->isObject() && asObject(cell)->isGlobalObject())
                result++;
        });
    return result;
}

size_t Heap::globalObjectCount()
{
    HeapIterationScope iterationScope(*this);
    size_t result = 0;
    m_objectSpace.forEachLiveCell(
        iterationScope,
        [&] (HeapCell* heapCell, HeapCell::Kind kind) -> IterationStatus {
            if (!isJSCellKind(kind))
                return IterationStatus::Continue;
            JSCell* cell = static_cast<JSCell*>(heapCell);
            if (cell->isObject() && asObject(cell)->isGlobalObject())
                result++;
            return IterationStatus::Continue;
        });
    return result;
}

size_t Heap::protectedObjectCount()
{
    size_t result = 0;
    forEachProtectedCell(
        [&] (JSCell*) {
            result++;
        });
    return result;
}

TypeCountSet Heap::protectedObjectTypeCounts()
{
    TypeCountSet result;
    forEachProtectedCell(
        [&] (JSCell* cell) {
            recordType(result, cell);
        });
    return result;
}

TypeCountSet Heap::objectTypeCounts()
{
    TypeCountSet result;
    HeapIterationScope iterationScope(*this);
    m_objectSpace.forEachLiveCell(
        iterationScope,
        [&] (HeapCell* cell, HeapCell::Kind kind) -> IterationStatus {
            if (isJSCellKind(kind))
                recordType(result, static_cast<JSCell*>(cell));
            return IterationStatus::Continue;
        });
    return result;
}

void Heap::deleteAllCodeBlocks(DeleteAllCodeEffort effort)
{
    if (m_collectionScope && effort == DeleteAllCodeIfNotCollecting)
        return;

    VM& vm = this->vm();
    PreventCollectionScope preventCollectionScope(*this);
    
    // If JavaScript is running, it's not safe to delete all JavaScript code, since
    // we'll end up returning to deleted code.
    RELEASE_ASSERT(!vm.entryScope);
    RELEASE_ASSERT(!m_collectionScope);

    completeAllJITPlans();

    forEachScriptExecutableSpace(
        [&] (auto& spaceAndSet) {
            HeapIterationScope heapIterationScope(*this);
            auto& set = spaceAndSet.clearableCodeSet;
            set.forEachLiveCell(
                [&] (HeapCell* cell, HeapCell::Kind) {
                    ScriptExecutable* executable = static_cast<ScriptExecutable*>(cell);
                    executable->clearCode(set);
                });
        });

#if ENABLE(WEBASSEMBLY)
    {
        // We must ensure that we clear the JS call ICs from Wasm. Otherwise, Wasm will
        // have no idea that we cleared the code from all of the Executables in the
        // VM. This could leave Wasm in an inconsistent state where it has an IC that
        // points into a CodeBlock that could be dead. The IC will still succeed because
        // it uses a callee check, but then it will call into dead code.

        // PreciseAllocations are always eagerly swept so we don't have to worry about handling instances pending destruction thus need a HeapIterationScope
        if (m_webAssemblyInstanceSpace) {
            m_webAssemblyInstanceSpace->forEachLiveCell([&] (HeapCell* cell, HeapCell::Kind kind) {
                ASSERT_UNUSED(kind, kind == HeapCell::JSCell);
                static_cast<JSWebAssemblyInstance*>(cell)->clearJSCallICs(vm);
            });
        }
    }
#endif
}

void Heap::deleteAllUnlinkedCodeBlocks(DeleteAllCodeEffort effort)
{
    if (m_collectionScope && effort == DeleteAllCodeIfNotCollecting)
        return;

    VM& vm = this->vm();
    PreventCollectionScope preventCollectionScope(*this);

    RELEASE_ASSERT(!m_collectionScope);

    HeapIterationScope heapIterationScope(*this);
    unlinkedFunctionExecutableSpaceAndSet.set.forEachLiveCell(
        [&] (HeapCell* cell, HeapCell::Kind) {
            UnlinkedFunctionExecutable* executable = static_cast<UnlinkedFunctionExecutable*>(cell);
            executable->clearCode(vm);
        });
}

void Heap::deleteUnmarkedCompiledCode()
{
    m_jitStubRoutines->deleteUnmarkedJettisonedStubRoutines(vm());
}

void Heap::addToRememberedSet(const JSCell* constCell)
{
    JSCell* cell = const_cast<JSCell*>(constCell);
    ASSERT(cell);
    ASSERT(!Options::useConcurrentJIT() || !isCompilationThread());
    m_barriersExecuted++;
    if (m_mutatorShouldBeFenced) {
        WTF::loadLoadFence();
        if (!isMarked(cell)) {
            // During a full collection a store into an unmarked object that had surivived past
            // collections will manifest as a store to an unmarked PossiblyBlack object. If the
            // object gets marked at some time after this then it will go down the normal marking
            // path. So, we don't have to remember this object. We could return here. But we go
            // further and attempt to re-white the object.
            
            RELEASE_ASSERT(m_collectionScope && m_collectionScope.value() == CollectionScope::Full);
            
            if (cell->atomicCompareExchangeCellStateStrong(CellState::PossiblyBlack, CellState::DefinitelyWhite) == CellState::PossiblyBlack) {
                // Now we protect against this race:
                //
                //     1) Object starts out black + unmarked.
                //     --> We do isMarked here.
                //     2) Object is marked and greyed.
                //     3) Object is scanned and blacked.
                //     --> We do atomicCompareExchangeCellStateStrong here.
                //
                // In this case we would have made the object white again, even though it should
                // be black. This check lets us correct our mistake. This relies on the fact that
                // isMarked converges monotonically to true.
                if (isMarked(cell)) {
                    // It's difficult to work out whether the object should be grey or black at
                    // this point. We say black conservatively.
                    cell->setCellState(CellState::PossiblyBlack);
                }
                
                // Either way, we can return. Most likely, the object was not marked, and so the
                // object is now labeled white. This means that future barrier executions will not
                // fire. In the unlikely event that the object had become marked, we can still
                // return anyway, since we proved that the object was not marked at the time that
                // we executed this slow path.
            }
            
            return;
        }
    } else
        ASSERT(isMarked(cell));
    // It could be that the object was *just* marked. This means that the collector may set the
    // state to DefinitelyGrey and then to PossiblyOldOrBlack at any time. It's OK for us to
    // race with the collector here. If we win then this is accurate because the object _will_
    // get scanned again. If we lose then someone else will barrier the object again. That would
    // be unfortunate but not the end of the world.
    cell->setCellState(CellState::PossiblyGrey);
    m_mutatorMarkStack->append(cell);
}

void Heap::sweepSynchronously()
{
    if (!Options::useGC()) [[unlikely]]
        return;

    MonotonicTime before { };
    if (Options::logGC()) [[unlikely]] {
        dataLog("Full sweep: ", capacity() / 1024, "kb ");
        before = MonotonicTime::now();
    }
    m_objectSpace.sweepBlocks();
    m_objectSpace.shrink();
    if (Options::logGC()) [[unlikely]] {
        MonotonicTime after = MonotonicTime::now();
        dataLog("=> ", capacity() / 1024, "kb, ", (after - before).milliseconds(), "ms");
    }
}

void Heap::collect(Synchronousness synchronousness, GCRequest request)
{
    if (!Options::useGC()) [[unlikely]]
        return;

    switch (synchronousness) {
    case Async: {
        collectAsync(request);
        return;
    }
    case Sync:
        collectSync(request);
        return;
    }
    RELEASE_ASSERT_NOT_REACHED();
}

void Heap::collectNow(Synchronousness synchronousness, GCRequest request)
{
    if (!Options::useGC()) [[unlikely]]
        return;

    if constexpr (validateDFGDoesGC)
        vm().verifyCanGC();

    switch (synchronousness) {
    case Async: {
        collectAsync(request);
        stopIfNecessary();
        return;
    }
        
    case Sync: {
        collectSync(request);
        
        DeferGCForAWhile deferGC(vm());
        if (Options::useImmortalObjects()) [[unlikely]]
            sweeper().stopSweeping();
        
        bool alreadySweptInCollectSync = shouldSweepSynchronously();
        if (!alreadySweptInCollectSync) {
            dataLogIf(Options::logGC(), "[GC<", RawPointer(this), ">: ");
            sweepSynchronously();
            dataLogIf(Options::logGC(), "]\n");
        }
        m_objectSpace.assertNoUnswept();
        
        sweepAllLogicallyEmptyWeakBlocks();
        return;
    } }
    RELEASE_ASSERT_NOT_REACHED();
}

void Heap::collectAsync(GCRequest request)
{
    if (!Options::useGC()) [[unlikely]]
        return;

    if constexpr (validateDFGDoesGC)
        vm().verifyCanGC();

    if (!m_isSafeToCollect)
        return;

    bool alreadyRequested = false;
    {
        Locker locker { *m_threadLock };
        for (const GCRequest& previousRequest : m_requests) {
            if (request.subsumedBy(previousRequest)) {
                alreadyRequested = true;
                break;
            }
        }
    }
    if (alreadyRequested)
        return;

    requestCollection(request);
}

void Heap::collectSync(GCRequest request)
{
    if (!Options::useGC()) [[unlikely]]
        return;

    if constexpr (validateDFGDoesGC)
        vm().verifyCanGC();

    if (!m_isSafeToCollect)
        return;

    waitForCollection(requestCollection(request));
}

bool Heap::shouldCollectInCollectorThread(const AbstractLocker&)
{
    RELEASE_ASSERT(m_requests.isEmpty() == (m_lastServedTicket == m_lastGrantedTicket));
    RELEASE_ASSERT(m_lastServedTicket <= m_lastGrantedTicket);
    dataLogLnIf(HeapInternal::verbose, "Mutator has the conn = ", !!(m_worldState.load() & mutatorHasConnBit));
    
    return !m_requests.isEmpty() && !(m_worldState.load() & mutatorHasConnBit);
}

void Heap::collectInCollectorThread()
{
    for (;;) {
        RunCurrentPhaseResult result = runCurrentPhase(GCConductor::Collector, nullptr);
        switch (result) {
        case RunCurrentPhaseResult::Finished:
            return;
        case RunCurrentPhaseResult::Continue:
            break;
        case RunCurrentPhaseResult::NeedCurrentThreadState:
            RELEASE_ASSERT_NOT_REACHED();
            break;
        }
    }
}

ALWAYS_INLINE int asInt(CollectorPhase phase)
{
    return static_cast<int>(phase);
}

void Heap::checkConn(GCConductor conn)
{
    unsigned worldState = m_worldState.load();
    switch (conn) {
    case GCConductor::Mutator:
        RELEASE_ASSERT(worldState & mutatorHasConnBit, worldState, asInt(m_lastPhase), asInt(m_currentPhase), asInt(m_nextPhase), vm().identifier().toUInt64(), vm().isEntered());
        return;
    case GCConductor::Collector:
        RELEASE_ASSERT(!(worldState & mutatorHasConnBit), worldState, asInt(m_lastPhase), asInt(m_currentPhase), asInt(m_nextPhase), vm().identifier().toUInt64(), vm().isEntered());
        return;
    }
    RELEASE_ASSERT_NOT_REACHED();
}

auto Heap::runCurrentPhase(GCConductor conn, CurrentThreadState* currentThreadState) -> RunCurrentPhaseResult
{
    checkConn(conn);
    m_currentThreadState = currentThreadState;
    m_currentThread = &Thread::currentSingleton();
    
    if (conn == GCConductor::Mutator)
        sanitizeStackForVM(vm());
    
    // If the collector transfers the conn to the mutator, it leaves us in between phases.
    if (!finishChangingPhase(conn)) {
        // A mischevious mutator could repeatedly relinquish the conn back to us. We try to avoid doing
        // this, but it's probably not the end of the world if it did happen.
        dataLogLnIf(HeapInternal::verbose, "Conn bounce-back.");
        return RunCurrentPhaseResult::Finished;
    }
    
    bool result = false;
    switch (m_currentPhase) {
    case CollectorPhase::NotRunning:
        result = runNotRunningPhase(conn);
        break;
        
    case CollectorPhase::Begin:
        result = runBeginPhase(conn);
        break;
        
    case CollectorPhase::Fixpoint:
        if (!currentThreadState && conn == GCConductor::Mutator)
            return RunCurrentPhaseResult::NeedCurrentThreadState;
        
        result = runFixpointPhase(conn);
        break;
        
    case CollectorPhase::Concurrent:
        result = runConcurrentPhase(conn);
        break;
        
    case CollectorPhase::Reloop:
        result = runReloopPhase(conn);
        break;
        
    case CollectorPhase::End:
        result = runEndPhase(conn);
        break;
    }

    return result ? RunCurrentPhaseResult::Continue : RunCurrentPhaseResult::Finished;
}

NEVER_INLINE bool Heap::runNotRunningPhase(GCConductor conn)
{
    // Check m_requests since the mutator calls this to poll what's going on.
    {
        Locker locker { *m_threadLock };
        if (m_requests.isEmpty())
            return false;
    }
    
    return changePhase(conn, CollectorPhase::Begin);
}

NEVER_INLINE bool Heap::runBeginPhase(GCConductor conn)
{
    m_currentGCStartTime = MonotonicTime::now();
    
    {
        Locker locker { *m_threadLock };
        RELEASE_ASSERT(!m_requests.isEmpty());
        m_currentRequest = m_requests.first();
    }

    dataLogIf(Options::logGC(), "[GC<", RawPointer(this), ">: START ", gcConductorShortName(conn), " ", capacity() / 1024, "kb ");

    m_beforeGC = MonotonicTime::now();

    if (!Options::seedOfVMRandomForFuzzer())
        vm().random().setSeed(cryptographicallyRandomNumber<uint32_t>());

    if (m_collectionScope) {
        dataLogLn("Collection scope already set during GC: ", *m_collectionScope);
        RELEASE_ASSERT_NOT_REACHED();
    }
    
    willStartCollection();
        
    if (m_verifier) [[unlikely]] {
        // Verify that live objects from the last GC cycle haven't been corrupted by
        // mutators before we begin this new GC cycle.
        m_verifier->verify(HeapVerifier::Phase::BeforeGC);
            
        m_verifier->startGC();
        m_verifier->gatherLiveCells(HeapVerifier::Phase::BeforeMarking);
    }

    ASSERT(m_collectionScope);
    bool isFullGC = m_collectionScope.value() == CollectionScope::Full;
    if (Options::useGCSignpost()) [[unlikely]] {
        StringPrintStream stream;
        stream.print("GC:(", RawPointer(this), "),mode:(", (isFullGC ? "Full" : "Eden"), "),version:(", m_gcVersion, "),conn:(", gcConductorShortName(conn), "),capacity(", capacity() / 1024, "kb)");
        m_signpostMessage = stream.toCString();
        WTFBeginSignpost(this, JSCGarbageCollector, "%" PUBLIC_LOG_STRING, m_signpostMessage.data() ? m_signpostMessage.data() : "(nullptr)");
    }

    prepareForMarking();
        
    if (isFullGC) {
        m_opaqueRoots.clear();
        m_collectorSlotVisitor->clearMarkStacks();
        m_mutatorMarkStack->clear();
    } else
        m_bytesAllocatedBeforeLastEdenCollect = totalBytesAllocatedThisCycle();

    RELEASE_ASSERT(m_raceMarkStack->isEmpty());

    beginMarking();

    forEachSlotVisitor(
        [&] (SlotVisitor& visitor) {
            visitor.didStartMarking();
        });

    m_parallelMarkersShouldExit = false;

    m_helperClient.setFunction(
        [this] () {
            SlotVisitor* visitor;
            {
                Locker locker { m_parallelSlotVisitorLock };
                RELEASE_ASSERT_WITH_MESSAGE(!m_availableParallelSlotVisitors.isEmpty(), "Parallel SlotVisitors are allocated apriori");
                visitor = m_availableParallelSlotVisitors.takeLast();
            }

            Thread::registerGCThread(GCThreadType::Helper);

            {
                ParallelModeEnabler parallelModeEnabler(*visitor);
                visitor->drainFromShared(SlotVisitor::HelperDrain);
            }

            {
                Locker locker { m_parallelSlotVisitorLock };
                m_availableParallelSlotVisitors.append(visitor);
            }
        });

    SlotVisitor& visitor = *m_collectorSlotVisitor;

    m_constraintSet->didStartMarking();
    
    m_scheduler->beginCollection();
    if (Options::logGC()) [[unlikely]]
        m_scheduler->log();
    
    // After this, we will almost certainly fall through all of the "visitor.isEmpty()"
    // checks because bootstrap would have put things into the visitor. So, we should fall
    // through to draining.
    
    if (!visitor.didReachTermination()) {
        dataLog("Fatal: SlotVisitor should think that GC should terminate before constraint solving, but it does not think this.\n");
        dataLog("visitor.isEmpty(): ", visitor.isEmpty(), "\n");
        dataLog("visitor.collectorMarkStack().isEmpty(): ", visitor.collectorMarkStack().isEmpty(), "\n");
        dataLog("visitor.mutatorMarkStack().isEmpty(): ", visitor.mutatorMarkStack().isEmpty(), "\n");
        dataLog("m_numberOfActiveParallelMarkers: ", m_numberOfActiveParallelMarkers, "\n");
        dataLog("m_sharedCollectorMarkStack->isEmpty(): ", m_sharedCollectorMarkStack->isEmpty(), "\n");
        dataLog("m_sharedMutatorMarkStack->isEmpty(): ", m_sharedMutatorMarkStack->isEmpty(), "\n");
        dataLog("visitor.didReachTermination(): ", visitor.didReachTermination(), "\n");
        RELEASE_ASSERT_NOT_REACHED();
    }
        
    return changePhase(conn, CollectorPhase::Fixpoint);
}

NEVER_INLINE bool Heap::runFixpointPhase(GCConductor conn)
{
    RELEASE_ASSERT(conn == GCConductor::Collector || m_currentThreadState);
    
    SlotVisitor& visitor = *m_collectorSlotVisitor;
    
    if (Options::logGC()) [[unlikely]] {
        UncheckedKeyHashMap<const char*, size_t> visitMap;
        forEachSlotVisitor(
            [&] (SlotVisitor& visitor) {
                visitMap.add(visitor.codeName(), visitor.bytesVisited() / 1024);
            });
        
WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN
        auto perVisitorDump = sortedMapDump(
            visitMap,
            [] (const char* a, const char* b) -> bool {
                return strcmp(a, b) < 0;
            },
            ":"_s, " "_s);
WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
        
        dataLog("v=", bytesVisited() / 1024, "kb (", perVisitorDump, ") o=", m_opaqueRoots.size(), " b=", m_barriersExecuted, " ");
    }
        
    if (visitor.didReachTermination()) {
        m_opaqueRoots.deleteOldTables();
        
        m_scheduler->didReachTermination();
        
        assertMarkStacksEmpty();
            
        // FIXME: Take m_mutatorDidRun into account when scheduling constraints. Most likely,
        // we don't have to execute root constraints again unless the mutator did run. At a
        // minimum, we could use this for work estimates - but it's probably more than just an
        // estimate.
        // https://bugs.webkit.org/show_bug.cgi?id=166828
            
        // Wondering what this does? Look at Heap::addCoreConstraints(). The DOM and others can also
        // add their own using Heap::addMarkingConstraint().
        bool converged = m_constraintSet->executeConvergence(visitor);
        
        // FIXME: The visitor.isEmpty() check is most likely not needed.
        // https://bugs.webkit.org/show_bug.cgi?id=180310
        if (converged && visitor.isEmpty()) {
            assertMarkStacksEmpty();
            return changePhase(conn, CollectorPhase::End);
        }
            
        m_scheduler->didExecuteConstraints();
    }
        
    dataLogIf(Options::logGC(), visitor.collectorMarkStack().size(), "+", m_mutatorMarkStack->size() + visitor.mutatorMarkStack().size(), " ");
        
    {
        ParallelModeEnabler enabler(visitor);
        visitor.drainInParallel(m_scheduler->timeToResume());
    }
        
    m_scheduler->synchronousDrainingDidStall();

    // This is kinda tricky. The termination check looks at:
    //
    // - Whether the marking threads are active. If they are not, this means that the marking threads'
    //   SlotVisitors are empty.
    // - Whether the collector's slot visitor is empty.
    // - Whether the shared mark stacks are empty.
    //
    // This doesn't have to check the mutator SlotVisitor because that one becomes empty after every GC
    // work increment, so it must be empty now.
    if (visitor.didReachTermination())
        return true; // This is like relooping to the top of runFixpointPhase().
        
    if (!m_scheduler->shouldResume())
        return true;

    m_scheduler->willResume();
        
    if (Options::logGC()) [[unlikely]] {
        double thisPauseMS = (MonotonicTime::now() - m_stopTime).milliseconds();
        dataLog("p=", thisPauseMS, "ms (max ", maxPauseMS(thisPauseMS), ")...]\n");
    }

    // Forgive the mutator for its past failures to keep up.
    // FIXME: Figure out if moving this to different places results in perf changes.
    m_incrementBalance = 0;
        
    return changePhase(conn, CollectorPhase::Concurrent);
}

NEVER_INLINE bool Heap::runConcurrentPhase(GCConductor conn)
{
    SlotVisitor& visitor = *m_collectorSlotVisitor;

    switch (conn) {
    case GCConductor::Mutator: {
        // When the mutator has the conn, we poll runConcurrentPhase() on every time someone says
        // stopIfNecessary(), so on every allocation slow path. When that happens we poll if it's time
        // to stop and do some work.
        if (visitor.didReachTermination()
            || m_scheduler->shouldStop())
            return changePhase(conn, CollectorPhase::Reloop);
        
        // We could be coming from a collector phase that stuffed our SlotVisitor, so make sure we donate
        // everything. This is super cheap if the SlotVisitor is already empty.
        visitor.donateAll();
        return false;
    }
    case GCConductor::Collector: {
        {
            ParallelModeEnabler enabler(visitor);
            visitor.drainInParallelPassively(m_scheduler->timeToStop());
        }
        return changePhase(conn, CollectorPhase::Reloop);
    } }
    
    RELEASE_ASSERT_NOT_REACHED();
    return false;
}

NEVER_INLINE bool Heap::runReloopPhase(GCConductor conn)
{
    dataLogIf(Options::logGC(), "[GC<", RawPointer(this), ">: ", gcConductorShortName(conn), " ");
    
    m_scheduler->didStop();
    
    if (Options::logGC()) [[unlikely]]
        m_scheduler->log();
    
    return changePhase(conn, CollectorPhase::Fixpoint);
}

NEVER_INLINE bool Heap::runEndPhase(GCConductor conn)
{
    m_scheduler->endCollection();
        
    {
        Locker locker { m_markingMutex };
        m_parallelMarkersShouldExit = true;
        m_markingConditionVariable.notifyAll();
    }
    m_helperClient.finish();
    
    ASSERT(m_mutatorMarkStack->isEmpty());
    ASSERT(m_raceMarkStack->isEmpty());

    SlotVisitor& visitor = *m_collectorSlotVisitor;
    iterateExecutingAndCompilingCodeBlocks(visitor,
        [&] (CodeBlock* codeBlock) {
            writeBarrier(codeBlock);
        });

    updateObjectCounts();
    endMarking();

    if (Options::verifyGC()) [[unlikely]]
        verifyGC();

    if (m_verifier) [[unlikely]] {
        m_verifier->gatherLiveCells(HeapVerifier::Phase::AfterMarking);
        m_verifier->verify(HeapVerifier::Phase::AfterMarking);
    }
        
    {
        auto* previous = Thread::currentSingleton().setCurrentAtomStringTable(nullptr);
        auto scopeExit = makeScopeExit([&] {
            Thread::currentSingleton().setCurrentAtomStringTable(previous);
        });

        if (vm().typeProfiler())
            vm().typeProfiler()->invalidateTypeSetCache(vm());

        cancelDeferredWorkIfNeeded();
        reapWeakHandles();
        pruneStaleEntriesFromWeakGCHashTables();
        sweepArrayBuffers();
        snapshotUnswept();
        finalizeUnconditionalFinalizers(); // We rely on these unconditional finalizers running before clearCurrentlyExecuting since CodeBlock's finalizer relies on querying currently executing.
        removeDeadCompilerWorklistEntries();
    }

    // Keep in mind that we may use AtomStringTable, and this is totally OK since the main thread is suspended.
    // End phase itself can run on main thread or concurrent collector thread. But whenever running this,
    // mutator is suspended so there is no race condition.
    deleteUnmarkedCompiledCode();

    notifyIncrementalSweeper();
    
    m_codeBlocks->iterateCurrentlyExecuting(
        [&] (CodeBlock* codeBlock) {
            writeBarrier(codeBlock);
        });
    m_codeBlocks->clearCurrentlyExecutingAndRemoveDeadCodeBlocks(vm());

    m_objectSpace.prepareForAllocation();
    updateAllocationLimits();

    if (m_verifier) [[unlikely]] {
        m_verifier->trimDeadCells();
        m_verifier->verify(HeapVerifier::Phase::AfterGC);
    }

    auto endingCollectionScope = *m_collectionScope;

    didFinishCollection();
    
    if (m_currentRequest.didFinishEndPhase)
        m_currentRequest.didFinishEndPhase->run();
    
    if (HeapInternal::verbose) {
        dataLogLn(HeapInternal::verbose, "Heap state after GC:");
        m_objectSpace.dumpBits();
    }
    
    if (Options::logGC()) [[unlikely]] {
        double thisPauseMS = (m_afterGC - m_stopTime).milliseconds();
        dataLog("p=", thisPauseMS, "ms (max ", maxPauseMS(thisPauseMS), "), cycle ", (m_afterGC - m_beforeGC).milliseconds(), "ms END]\n");
    }
    
    {
        Locker locker { *m_threadLock };
        m_requests.removeFirst();
        m_lastServedTicket++;
        clearMutatorWaiting();
    }
    ParkingLot::unparkAll(&m_worldState);

    dataLogLnIf(Options::logGC(), "GC END!");
    if (Options::useGCSignpost()) [[unlikely]] {
        WTFEndSignpost(this, JSCGarbageCollector, "%" PUBLIC_LOG_STRING, m_signpostMessage.data() ? m_signpostMessage.data() : "(nullptr)");
        m_signpostMessage = { };
    }

    setNeedFinalize();

    MonotonicTime now = MonotonicTime::now();
    if (m_maxEdenSizeForRateLimiting) {
        m_gcRateLimitingValue = projectedGCRateLimitingValue(now);
        m_gcRateLimitingValue += 1.0;
    }
    m_lastGCStartTime = m_currentGCStartTime;
    m_lastGCEndTime = now;
    m_totalGCTime += m_lastGCEndTime - m_lastGCStartTime;
    if (endingCollectionScope == CollectionScope::Full)
        m_lastFullGCEndTime = m_lastGCEndTime;
    return changePhase(conn, CollectorPhase::NotRunning);
}

bool Heap::changePhase(GCConductor conn, CollectorPhase nextPhase)
{
    checkConn(conn);

    m_lastPhase = m_currentPhase;
    m_nextPhase = nextPhase;

    return finishChangingPhase(conn);
}

NEVER_INLINE bool Heap::finishChangingPhase(GCConductor conn)
{
    checkConn(conn);
    
    if (m_nextPhase == m_currentPhase)
        return true;

    dataLogLnIf(HeapInternal::verbose, conn, ": Going to phase: ", m_nextPhase, " (from ", m_currentPhase, ")");

    m_phaseVersion++;
    
    bool suspendedBefore = worldShouldBeSuspended(m_currentPhase);
    bool suspendedAfter = worldShouldBeSuspended(m_nextPhase);
    
    if (suspendedBefore != suspendedAfter) {
        if (suspendedBefore) {
            RELEASE_ASSERT(!suspendedAfter);
            
            resumeThePeriphery();
            if (conn == GCConductor::Collector)
                resumeTheMutator();
            else
                handleNeedFinalize();
        } else {
            RELEASE_ASSERT(!suspendedBefore);
            RELEASE_ASSERT(suspendedAfter);
            
            if (conn == GCConductor::Collector) {
                waitWhileNeedFinalize();
                if (!stopTheMutator()) {
                    dataLogLnIf(HeapInternal::verbose, "Returning false.");
                    return false;
                }
            } else {
                sanitizeStackForVM(vm());
                handleNeedFinalize();
            }
            stopThePeriphery(conn);
        }
    }
    
    m_currentPhase = m_nextPhase;
    return true;
}

void Heap::stopThePeriphery(GCConductor conn)
{
    if (m_worldIsStopped) {
        dataLog("FATAL: world already stopped.\n");
        RELEASE_ASSERT_NOT_REACHED();
    }
    
    if (m_mutatorDidRun)
        m_mutatorExecutionVersion++;
    
    m_mutatorDidRun = false;

    m_isCompilerThreadsSuspended = suspendCompilerThreads();
    m_worldIsStopped = true;

    forEachSlotVisitor(
        [&] (SlotVisitor& visitor) {
            visitor.updateMutatorIsStopped(NoLockingNecessary);
        });

    UNUSED_PARAM(conn);
    
    if (auto* shadowChicken = vm().shadowChicken())
        shadowChicken->update(vm(), vm().topCallFrame);
    
    m_objectSpace.stopAllocating();
    
    m_stopTime = MonotonicTime::now();
}

NEVER_INLINE void Heap::resumeThePeriphery()
{
    // Calling resumeAllocating does the Right Thing depending on whether this is the end of a
    // collection cycle or this is just a concurrent phase within a collection cycle:
    // - At end of collection cycle: it's a no-op because prepareForAllocation already cleared the
    //   last active block.
    // - During collection cycle: it reinstates the last active block.
    m_objectSpace.resumeAllocating();
    
    m_barriersExecuted = 0;
    
    if (!m_worldIsStopped) {
        dataLog("Fatal: collector does not believe that the world is stopped.\n");
        RELEASE_ASSERT_NOT_REACHED();
    }
    m_worldIsStopped = false;
    
    // FIXME: This could be vastly improved: we want to grab the locks in the order in which they
    // become available. We basically want a lockAny() method that will lock whatever lock is available
    // and tell you which one it locked. That would require teaching ParkingLot how to park on multiple
    // queues at once, which is totally achievable - it would just require memory allocation, which is
    // suboptimal but not a disaster. Alternatively, we could replace the SlotVisitor rightToRun lock
    // with a DLG-style handshake mechanism, but that seems not as general.
    Vector<SlotVisitor*, 8> visitorsToUpdate;

    forEachSlotVisitor(
        [&] (SlotVisitor& visitor) {
            visitorsToUpdate.append(&visitor);
        });
    
    for (unsigned countdown = 40; !visitorsToUpdate.isEmpty() && countdown--;) {
        for (unsigned index = 0; index < visitorsToUpdate.size(); ++index) {
            SlotVisitor& visitor = *visitorsToUpdate[index];
            bool remove = false;
            if (visitor.hasAcknowledgedThatTheMutatorIsResumed())
                remove = true;
            else if (visitor.rightToRun().tryLock()) {
                Locker locker { AdoptLock, visitor.rightToRun() };
                visitor.updateMutatorIsStopped(locker);
                remove = true;
            }
            if (remove) {
                visitorsToUpdate[index--] = visitorsToUpdate.last();
                visitorsToUpdate.takeLast();
            }
        }
        Thread::yield();
    }
    
    for (SlotVisitor* visitor : visitorsToUpdate)
        visitor->updateMutatorIsStopped();

    if (std::exchange(m_isCompilerThreadsSuspended, false))
        resumeCompilerThreads();
}

bool Heap::stopTheMutator()
{
    for (;;) {
        unsigned oldState = m_worldState.load();
        if (oldState & stoppedBit) {
            RELEASE_ASSERT(!(oldState & hasAccessBit));
            RELEASE_ASSERT(!(oldState & mutatorWaitingBit));
            RELEASE_ASSERT(!(oldState & mutatorHasConnBit));
            return true;
        }
        
        if (oldState & mutatorHasConnBit) {
            RELEASE_ASSERT(!(oldState & hasAccessBit));
            RELEASE_ASSERT(!(oldState & stoppedBit));
            return false;
        }

        if (!(oldState & hasAccessBit)) {
            RELEASE_ASSERT(!(oldState & mutatorHasConnBit));
            RELEASE_ASSERT(!(oldState & mutatorWaitingBit));
            // We can stop the world instantly.
            if (m_worldState.compareExchangeWeak(oldState, oldState | stoppedBit))
                return true;
            continue;
        }
        
        // Transfer the conn to the mutator and bail.
        RELEASE_ASSERT(oldState & hasAccessBit);
        RELEASE_ASSERT(!(oldState & stoppedBit));
        unsigned newState = (oldState | mutatorHasConnBit) & ~mutatorWaitingBit;
        if (m_worldState.compareExchangeWeak(oldState, newState)) {
            dataLogLnIf(HeapInternal::verbose, "Handed off the conn.");
            m_stopIfNecessaryTimer->scheduleSoon();
            ParkingLot::unparkAll(&m_worldState);
            return false;
        }
    }
}

NEVER_INLINE void Heap::resumeTheMutator()
{
    dataLogLnIf(HeapInternal::verbose, "Resuming the mutator.");
    for (;;) {
        unsigned oldState = m_worldState.load();
        if (!!(oldState & hasAccessBit) != !(oldState & stoppedBit)) {
            dataLog("Fatal: hasAccess = ", !!(oldState & hasAccessBit), ", stopped = ", !!(oldState & stoppedBit), "\n");
            RELEASE_ASSERT_NOT_REACHED();
        }
        if (oldState & mutatorHasConnBit) {
            dataLog("Fatal: mutator has the conn.\n");
            RELEASE_ASSERT_NOT_REACHED();
        }
        
        if (!(oldState & stoppedBit)) {
            dataLogLnIf(HeapInternal::verbose, "Returning because not stopped.");
            return;
        }
        
        if (m_worldState.compareExchangeWeak(oldState, oldState & ~stoppedBit)) {
            dataLogLnIf(HeapInternal::verbose, "CASing and returning.");
            ParkingLot::unparkAll(&m_worldState);
            return;
        }
    }
}

void Heap::stopIfNecessarySlow()
{
    if constexpr (validateDFGDoesGC)
        vm().verifyCanGC();

    while (stopIfNecessarySlow(m_worldState.load())) { }
    
    RELEASE_ASSERT(m_worldState.load() & hasAccessBit);
    RELEASE_ASSERT(!(m_worldState.load() & stoppedBit));
    
    handleNeedFinalize();
    m_mutatorDidRun = true;
}

bool Heap::stopIfNecessarySlow(unsigned oldState)
{
    if constexpr (validateDFGDoesGC)
        vm().verifyCanGC();

    RELEASE_ASSERT(oldState & hasAccessBit);
    RELEASE_ASSERT(!(oldState & stoppedBit));
    
    // It's possible for us to wake up with finalization already requested but the world not yet
    // resumed. If that happens, we can't run finalization yet.
    if (handleNeedFinalize(oldState))
        return true;

    // FIXME: When entering the concurrent phase, we could arrange for this branch not to fire, and then
    // have the SlotVisitor do things to the m_worldState to make this branch fire again. That would
    // prevent us from polling this so much. Ideally, stopIfNecessary would ignore the mutatorHasConnBit
    // and there would be some other bit indicating whether we were in some GC phase other than the
    // NotRunning or Concurrent ones.
    if (oldState & mutatorHasConnBit)
        collectInMutatorThread();
    
    return false;
}

NEVER_INLINE void Heap::collectInMutatorThread()
{
    CollectingScope collectingScope(*this);
    for (;;) {
        RunCurrentPhaseResult result = runCurrentPhase(GCConductor::Mutator, nullptr);
        switch (result) {
        case RunCurrentPhaseResult::Finished:
            return;
        case RunCurrentPhaseResult::Continue:
            break;
        case RunCurrentPhaseResult::NeedCurrentThreadState:
            sanitizeStackForVM(vm());
            auto lambda = [&] (CurrentThreadState& state) {
                for (;;) {
                    RunCurrentPhaseResult result = runCurrentPhase(GCConductor::Mutator, &state);
                    switch (result) {
                    case RunCurrentPhaseResult::Finished:
                        return;
                    case RunCurrentPhaseResult::Continue:
                        break;
                    case RunCurrentPhaseResult::NeedCurrentThreadState:
                        RELEASE_ASSERT_NOT_REACHED();
                        break;
                    }
                }
            };
            callWithCurrentThreadState(scopedLambda<void(CurrentThreadState&)>(WTFMove(lambda)));
            return;
        }
    }
}

template<typename Func>
void Heap::waitForCollector(const Func& func)
{
    for (;;) {
        bool done;
        {
            Locker locker { *m_threadLock };
            done = func(locker);
            if (!done) {
                setMutatorWaiting();
                
                // At this point, the collector knows that we intend to wait, and he will clear the
                // waiting bit and then unparkAll when the GC cycle finishes. Clearing the bit
                // prevents us from parking except if there is also stop-the-world. Unparking after
                // clearing means that if the clearing happens after we park, then we will unpark.
            }
        }
        
        // If we're in a stop-the-world scenario, we need to wait for that even if done is true.
        unsigned oldState = m_worldState.load();
        if (stopIfNecessarySlow(oldState))
            continue;
        
        m_mutatorDidRun = true;
        // FIXME: We wouldn't need this if stopIfNecessarySlow() had a mode where it knew to just
        // do the collection.
        relinquishConn();

        if (done) {
            clearMutatorWaiting(); // Clean up just in case.
            return;
        }
        
        // If mutatorWaitingBit is still set then we want to wait.
        ParkingLot::compareAndPark(&m_worldState, oldState | mutatorWaitingBit);
    }
}

void Heap::acquireAccessSlow()
{
    for (;;) {
        unsigned oldState = m_worldState.load();
        RELEASE_ASSERT(!(oldState & hasAccessBit));
        
        if (oldState & stoppedBit) {
            if (HeapInternal::verboseStop) {
                dataLogLn("Stopping in acquireAccess!");
                WTFReportBacktrace();
            }
            // Wait until we're not stopped anymore.
            ParkingLot::compareAndPark(&m_worldState, oldState);
            continue;
        }
        
        RELEASE_ASSERT(!(oldState & stoppedBit));
        unsigned newState = oldState | hasAccessBit;
        if (m_worldState.compareExchangeWeak(oldState, newState)) {
            handleNeedFinalize();
            m_mutatorDidRun = true;
            stopIfNecessary();
            return;
        }
    }
}

void Heap::releaseAccessSlow()
{
    for (;;) {
        unsigned oldState = m_worldState.load();
        if (!(oldState & hasAccessBit)) {
            dataLog("FATAL: Attempting to release access but the mutator does not have access.\n");
            RELEASE_ASSERT_NOT_REACHED();
        }
        if (oldState & stoppedBit) {
            dataLog("FATAL: Attempting to release access but the mutator is stopped.\n");
            RELEASE_ASSERT_NOT_REACHED();
        }
        
        if (handleNeedFinalize(oldState))
            continue;
        
        unsigned newState = oldState & ~(hasAccessBit | mutatorHasConnBit);
        
        if ((oldState & mutatorHasConnBit)
            && m_nextPhase != m_currentPhase) {
            // This means that the collector thread had given us the conn so that we would do something
            // for it. Stop ourselves as we release access. This ensures that acquireAccess blocks. In
            // the meantime, since we're handing the conn over, the collector will be awoken and it is
            // sure to have work to do.
            newState |= stoppedBit;
        }

        if (m_worldState.compareExchangeWeak(oldState, newState)) {
            if (oldState & mutatorHasConnBit)
                finishRelinquishingConn();
            return;
        }
    }
}

bool Heap::relinquishConn(unsigned oldState)
{
    RELEASE_ASSERT(oldState & hasAccessBit);
    RELEASE_ASSERT(!(oldState & stoppedBit));
    
    if (!(oldState & mutatorHasConnBit))
        return false; // Done.
    
    if (m_threadShouldStop)
        return false;
    
    if (!m_worldState.compareExchangeWeak(oldState, oldState & ~mutatorHasConnBit))
        return true; // Loop around.
    
    finishRelinquishingConn();
    return true;
}

void Heap::finishRelinquishingConn()
{
    dataLogLnIf(HeapInternal::verbose, "Relinquished the conn.");
    
    sanitizeStackForVM(vm());
    
    Locker locker { *m_threadLock };
    if (!m_requests.isEmpty())
        m_threadCondition->notifyOne(locker);
    ParkingLot::unparkAll(&m_worldState);
}

void Heap::relinquishConn()
{
    while (relinquishConn(m_worldState.load())) { }
}

NEVER_INLINE bool Heap::handleNeedFinalize(unsigned oldState)
{
    RELEASE_ASSERT(oldState & hasAccessBit);
    RELEASE_ASSERT(!(oldState & stoppedBit));
    
    if (!(oldState & needFinalizeBit))
        return false;
    if (m_worldState.compareExchangeWeak(oldState, oldState & ~needFinalizeBit)) {
        finalize();
        // Wake up anyone waiting for us to finalize. Note that they may have woken up already, in
        // which case they would be waiting for us to release heap access.
        ParkingLot::unparkAll(&m_worldState);
        return true;
    }
    return true;
}

void Heap::handleNeedFinalize()
{
    while (handleNeedFinalize(m_worldState.load())) { }
}

void Heap::setNeedFinalize()
{
    m_worldState.exchangeOr(needFinalizeBit);
    ParkingLot::unparkAll(&m_worldState);
    m_stopIfNecessaryTimer->scheduleSoon();
}

void Heap::waitWhileNeedFinalize()
{
    for (;;) {
        unsigned oldState = m_worldState.load();
        if (!(oldState & needFinalizeBit)) {
            // This means that either there was no finalize request or the main thread will finalize
            // with heap access, so a subsequent call to stopTheWorld() will return only when
            // finalize finishes.
            return;
        }
        ParkingLot::compareAndPark(&m_worldState, oldState);
    }
}

void Heap::setMutatorWaiting()
{
    m_worldState.exchangeOr(mutatorWaitingBit);
}

void Heap::clearMutatorWaiting()
{
    m_worldState.exchangeAnd(~mutatorWaitingBit);
}

void Heap::notifyThreadStopping(const AbstractLocker&)
{
    clearMutatorWaiting();
    ParkingLot::unparkAll(&m_worldState);
}

void Heap::finalize()
{
    MonotonicTime before;
    if (Options::logGC()) [[unlikely]] {
        before = MonotonicTime::now();
        dataLog("[GC<", RawPointer(this), ">: finalize ");
    }
    
    {
        SweepingScope sweepingScope(*this);
        deleteSourceProviderCaches();
        sweepInFinalize();
    }
    
    if (HasOwnPropertyCache* cache = vm().hasOwnPropertyCache())
        cache->clear();
    if (auto* cache = vm().megamorphicCache())
        cache->age(m_lastCollectionScope && m_lastCollectionScope.value() == CollectionScope::Full ? CollectionScope::Full : CollectionScope::Eden);

    if (m_lastCollectionScope && m_lastCollectionScope.value() == CollectionScope::Full) {
        vm().jsonAtomStringCache.clear();
        vm().numericStrings.clearOnGarbageCollection();
        vm().stringReplaceCache.clear();
    }
    vm().keyAtomStringCache.clear();
    vm().stringSplitCache.clear();

    m_possiblyAccessedStringsFromConcurrentThreads.clear();

    immutableButterflyToStringCache.clear();
    
    for (const HeapFinalizerCallback& callback : m_heapFinalizerCallbacks)
        callback.run(vm());
    
    if (shouldSweepSynchronously())
        sweepSynchronously();

    if (Options::logGC()) [[unlikely]] {
        MonotonicTime after = MonotonicTime::now();
        dataLog((after - before).milliseconds(), "ms]\n");
    }
}

Heap::Ticket Heap::requestCollection(GCRequest request)
{
    stopIfNecessary();
    
    ASSERT(vm().currentThreadIsHoldingAPILock());
    RELEASE_ASSERT(vm().atomStringTable() == Thread::currentSingleton().atomStringTable());
    
    Locker locker { *m_threadLock };
    // We may be able to steal the conn. That only works if the collector is definitely not running
    // right now. This is an optimization that prevents the collector thread from ever starting in most
    // cases.
    ASSERT(m_lastServedTicket <= m_lastGrantedTicket);
    if ((m_lastServedTicket == m_lastGrantedTicket) && !m_collectorThreadIsRunning) {
        dataLogLnIf(HeapInternal::verbose, "Taking the conn.");
        m_worldState.exchangeOr(mutatorHasConnBit);
    }
    
    m_requests.append(request);
    m_lastGrantedTicket++;
    if (!(m_worldState.load() & mutatorHasConnBit))
        m_threadCondition->notifyOne(locker);
    return m_lastGrantedTicket;
}

void Heap::waitForCollection(Ticket ticket)
{
    waitForCollector(
        [&] (const AbstractLocker&) -> bool {
            return m_lastServedTicket >= ticket;
        });
}

void Heap::sweepInFinalize()
{
    m_objectSpace.sweepPreciseAllocations();
#if ENABLE(WEBASSEMBLY)
    // We hold onto a lot of memory, so it makes a lot of sense to be swept eagerly.
    if (m_webAssemblyMemorySpace)
        m_webAssemblyMemorySpace->sweep();
#endif
}

bool Heap::suspendCompilerThreads()
{
#if ENABLE(JIT)
    // We ensure the worklists so that it's not possible for the mutator to start a new worklist
    // after we have suspended the ones that he had started before. That's not very expensive since
    // the worklists use AutomaticThreads anyway.
    if (!Options::useJIT())
        return false;
    if (!vm().numberOfActiveJITPlans())
        return false;
    JITWorklist::ensureGlobalWorklist().suspendAllThreads();
    return true;
#else
    return false;
#endif
}

void Heap::willStartCollection()
{
    ++m_gcVersion;
    if (Options::verifyGC()) [[unlikely]] {
        m_verifierSlotVisitor = makeUnique<VerifierSlotVisitor>(*this);
        ASSERT(!m_isMarkingForGCVerifier);
    }

    dataLogIf(Options::logGC(), "=> ");
    
    if (shouldDoFullCollection()) {
        m_collectionScope = CollectionScope::Full;
        m_shouldDoFullCollection = false;
        dataLogIf(Options::logGC(), "FullCollection, ");
    } else {
        m_collectionScope = CollectionScope::Eden;
        dataLogIf(Options::logGC(), "EdenCollection, ");
    }
    if (m_collectionScope.value() == CollectionScope::Full) {
        m_sizeBeforeLastFullCollect = m_sizeAfterLastCollect + totalBytesAllocatedThisCycle();
        m_extraMemorySize = 0;
        m_deprecatedExtraMemorySize = 0;
#if ENABLE(RESOURCE_USAGE)
        m_externalMemorySize = 0;
#endif
        m_shouldDoOpportunisticFullCollection = false;
        if (m_fullActivityCallback)
            m_fullActivityCallback->willCollect();
    } else {
        ASSERT(m_collectionScope && m_collectionScope.value() == CollectionScope::Eden);
        m_sizeBeforeLastEdenCollect = m_sizeAfterLastCollect + totalBytesAllocatedThisCycle();
    }

    if (m_edenActivityCallback)
        m_edenActivityCallback->willCollect();

    for (auto* observer : m_observers)
        observer->willGarbageCollect();
}

void Heap::prepareForMarking()
{
    m_objectSpace.prepareForMarking();
}

void Heap::cancelDeferredWorkIfNeeded()
{
    vm().deferredWorkTimer->cancelPendingWork(vm());
}

void Heap::reapWeakHandles()
{
    m_objectSpace.reapWeakSets();
}

void Heap::pruneStaleEntriesFromWeakGCHashTables()
{
    if (!m_collectionScope || m_collectionScope.value() != CollectionScope::Full)
        return;
    for (auto* weakGCHashTable : m_weakGCHashTables)
        weakGCHashTable->pruneStaleEntries();
}

void Heap::sweepArrayBuffers()
{
    m_arrayBuffers.sweep(vm(), collectionScope().value_or(CollectionScope::Eden));
}

void Heap::snapshotUnswept()
{
    TimingScope timingScope(*this, "Heap::snapshotUnswept"_s);
    m_objectSpace.snapshotUnswept();
}

void Heap::deleteSourceProviderCaches()
{
    if (m_lastCollectionScope && m_lastCollectionScope.value() == CollectionScope::Full)
        vm().clearSourceProviderCaches();
}

void Heap::notifyIncrementalSweeper()
{
    if (m_collectionScope && m_collectionScope.value() == CollectionScope::Full) {
        if (!m_logicallyEmptyWeakBlocks.isEmpty())
            m_indexOfNextLogicallyEmptyWeakBlockToSweep = 0;
    }

    m_sweeper->startSweeping(*this);
}

double Heap::projectedGCRateLimitingValue(MonotonicTime now)
{
    if (!m_lastGCEndTime) {
        ASSERT(!m_gcRateLimitingValue);
        return 0.0;
    }
    Seconds timeSinceLastGC = now - m_lastGCEndTime;
    return m_gcRateLimitingValue * pow(0.5, timeSinceLastGC.milliseconds() / Options::gcRateLimitingHalfLifeInMS());
}

void Heap::updateAllocationLimits()
{
    constexpr bool verbose = false;
    
    dataLogLnIf(verbose, "\nnonOversizedBytesAllocatedThisCycle = ", m_nonOversizedBytesAllocatedThisCycle, ", oversizedBytesAllocatedThisCycle", m_oversizedBytesAllocatedThisCycle);
    
    // Calculate our current heap size threshold for the purpose of figuring out when we should
    // run another collection. This isn't the same as either size() or capacity(), though it should
    // be somewhere between the two. The key is to match the size calculations involved calls to
    // didAllocate(), while never dangerously underestimating capacity(). In extreme cases of
    // fragmentation, we may have size() much smaller than capacity().
    size_t currentHeapSize = 0;

    // For marked space, we use the total number of bytes visited. This matches the logic for
    // BlockDirectory's calls to didAllocate(), which effectively accounts for the total size of
    // objects allocated rather than blocks used. This will underestimate capacity(), and in case
    // of fragmentation, this may be substantial. Fortunately, marked space rarely fragments because
    // cells usually have a narrow range of sizes. So, the underestimation is probably OK.
    currentHeapSize += m_totalBytesVisited;
    dataLogLnIf(verbose, "totalBytesVisited = ", m_totalBytesVisited, ", currentHeapSize = ", currentHeapSize);

    // It's up to the user to ensure that extraMemorySize() ends up corresponding to allocation-time
    // extra memory reporting.
    auto computedExtraMemorySize = extraMemorySize();
    currentHeapSize += computedExtraMemorySize;
    if (ASSERT_ENABLED) {
        CheckedSize checkedCurrentHeapSize = m_totalBytesVisited;
        checkedCurrentHeapSize += computedExtraMemorySize;
        ASSERT(!checkedCurrentHeapSize.hasOverflowed() && checkedCurrentHeapSize == currentHeapSize);
    }

    dataLogLnIf(verbose, "extraMemorySize() = ", computedExtraMemorySize, ", currentHeapSize = ", currentHeapSize);

    if (m_collectionScope && m_collectionScope.value() == CollectionScope::Full) {
        // To avoid pathological GC churn in very small and very large heaps, we set
        // the new allocation limit based on the current size of the heap, with a
        // fixed minimum.
        size_t lastMaxHeapSize = m_maxHeapSize;
        m_maxHeapSize = std::max(m_minBytesPerCycle, proportionalHeapSize(currentHeapSize, m_growthMode, m_ramSize));
        m_maxEdenSize = m_maxHeapSize - currentHeapSize;
        if (m_isInOpportunisticTask) {
            // After an Opportunistic Full GC, we allow eden to occupy all the space we recovered.
            // In this case, m_maxHeapSize may be larger than currentHeapSize + m_maxEdenSize.
            // Note that m_maxEdenSize is still used when we increase m_maxHeapSize after an
            // Eden GC to ensure that eden can grow to at least m_maxHeapSize.
            m_maxHeapSize = std::max(m_maxHeapSize, lastMaxHeapSize);
        }
        dataLogLnIf(verbose, "Full: maxHeapSize = ", m_maxHeapSize);
        dataLogLnIf(verbose, "Full: maxEdenSize = ", m_maxEdenSize);
        m_sizeAfterLastFullCollect = currentHeapSize;
        dataLogLnIf(verbose, "Full: sizeAfterLastFullCollect = ", currentHeapSize);
        m_bytesAbandonedSinceLastFullCollect = 0;
        dataLogLnIf(verbose, "Full: bytesAbandonedSinceLastFullCollect = ", 0);
    } else {
        ASSERT(currentHeapSize >= m_sizeAfterLastCollect);
        // Theoretically, we shouldn't ever scan more memory than the heap size we planned to have.
        // But we are sloppy, so we have to defend against the overflow.
        size_t remainingHeapSize = currentHeapSize > m_maxHeapSize ? 0 : m_maxHeapSize - currentHeapSize;
        dataLogLnIf(verbose, "Eden: remainingHeapSize = ", remainingHeapSize);
        m_sizeAfterLastEdenCollect = currentHeapSize;
        dataLogLnIf(verbose, "Eden: sizeAfterLastEdenCollect = ", currentHeapSize);
        double edenToOldGenerationRatio = (double)remainingHeapSize / (double)m_maxHeapSize;
        double minEdenToOldGenerationRatio = 1.0 / 3.0;
        if (edenToOldGenerationRatio < minEdenToOldGenerationRatio)
            m_shouldDoFullCollection = true;
        m_maxHeapSize = std::max(m_maxHeapSize, currentHeapSize + m_maxEdenSize);
        dataLogLnIf(verbose, "Eden: maxHeapSize = ", m_maxHeapSize);
        dataLogLnIf(verbose, "Eden: maxEdenSize = ", m_maxEdenSize);
        if (m_fullActivityCallback) {
            ASSERT(currentHeapSize >= m_sizeAfterLastFullCollect);
            m_fullActivityCallback->didAllocate(*this, currentHeapSize - m_sizeAfterLastFullCollect);
        }
    }

#if USE(BMALLOC_MEMORY_FOOTPRINT_API)
    // Get critical memory threshold for next cycle.
    overCriticalMemoryThreshold(MemoryThresholdCallType::Direct);
#endif

    m_sizeAfterLastCollect = currentHeapSize;
    dataLogLnIf(verbose, "sizeAfterLastCollect = ", m_sizeAfterLastCollect);
    m_nonOversizedBytesAllocatedThisCycle = 0;
    m_oversizedBytesAllocatedThisCycle = 0;
    m_lastOversidedAllocationThisCycle = 0;

    dataLogIf(Options::logGC(), "=> ", currentHeapSize / 1024, "kb, ");
}

void Heap::didFinishCollection()
{
    m_afterGC = MonotonicTime::now();
    CollectionScope scope = *m_collectionScope;
    if (scope == CollectionScope::Full)
        m_lastFullGCLength = m_afterGC - m_beforeGC;
    else
        m_lastEdenGCLength = m_afterGC - m_beforeGC;

#if ENABLE(RESOURCE_USAGE)
    ASSERT(externalMemorySize() <= extraMemorySize());
#endif

    if (HeapProfiler* heapProfiler = vm().heapProfiler()) {
        gatherExtraHeapData(*heapProfiler);
        removeDeadHeapSnapshotNodes(*heapProfiler);
    }

    if (m_verifier) [[unlikely]]
        m_verifier->endGC();

    RELEASE_ASSERT(m_collectionScope);
    m_lastCollectionScope = m_collectionScope;
    m_collectionScope = std::nullopt;

    for (auto* observer : m_observers)
        observer->didGarbageCollect(scope);
}

void Heap::resumeCompilerThreads()
{
#if ENABLE(JIT)
    JITWorklist::ensureGlobalWorklist().resumeAllThreads();
#endif
}

GCActivityCallback* Heap::fullActivityCallback()
{
    return m_fullActivityCallback.get();
}

RefPtr<GCActivityCallback> Heap::protectedFullActivityCallback()
{
    return m_fullActivityCallback;
}

GCActivityCallback* Heap::edenActivityCallback()
{
    return m_edenActivityCallback.get();
}

RefPtr<GCActivityCallback> Heap::protectedEdenActivityCallback()
{
    return m_edenActivityCallback;
}

void Heap::setGarbageCollectionTimerEnabled(bool enable)
{
    if (m_fullActivityCallback)
        m_fullActivityCallback->setEnabled(enable);
    if (m_edenActivityCallback)
        m_edenActivityCallback->setEnabled(enable);
}

constexpr size_t oversizedAllocationThreshold = 64 * KB;
void Heap::didAllocate(size_t bytes)
{
    if (m_edenActivityCallback)
        m_edenActivityCallback->didAllocate(*this, totalBytesAllocatedThisCycle() + m_bytesAbandonedSinceLastFullCollect);
    if (bytes >= oversizedAllocationThreshold) {
        m_oversizedBytesAllocatedThisCycle += bytes;
        m_lastOversidedAllocationThisCycle = bytes;
    } else
        m_nonOversizedBytesAllocatedThisCycle += bytes;
    performIncrement(bytes);
}

void Heap::addFinalizer(JSCell* cell, CFinalizer finalizer)
{
    WeakSet::allocate(cell, &m_cFinalizerOwner, std::bit_cast<void*>(finalizer)); // Balanced by CFinalizerOwner::finalize().
}

void Heap::addFinalizer(JSCell* cell, LambdaFinalizer function)
{
    WeakSet::allocate(cell, &m_lambdaFinalizerOwner, function.leak()); // Balanced by LambdaFinalizerOwner::finalize().
}

void Heap::CFinalizerOwner::finalize(Handle<Unknown> handle, void* context)
{
    HandleSlot slot = handle.slot();
    CFinalizer finalizer = std::bit_cast<CFinalizer>(context);
    finalizer(slot->asCell());
    WeakSet::deallocate(WeakImpl::asWeakImpl(slot));
}

void Heap::LambdaFinalizerOwner::finalize(Handle<Unknown> handle, void* context)
{
    auto finalizer = WTF::adopt(static_cast<LambdaFinalizer::Impl*>(context));
    HandleSlot slot = handle.slot();
    finalizer(slot->asCell());
    WeakSet::deallocate(WeakImpl::asWeakImpl(slot));
}

void Heap::collectNowFullIfNotDoneRecently(Synchronousness synchronousness)
{
    if (!m_fullActivityCallback) {
        collectNow(synchronousness, CollectionScope::Full);
        return;
    }

    if (m_fullActivityCallback->didGCRecently()) {
        // A synchronous GC was already requested recently so we merely accelerate next collection.
        reportAbandonedObjectGraph();
        return;
    }

    m_fullActivityCallback->setDidGCRecently(true);
    collectNow(synchronousness, CollectionScope::Full);
}

void Heap::setFullActivityCallback(RefPtr<GCActivityCallback>&& callback)
{
    m_fullActivityCallback = WTFMove(callback);
}

void Heap::setEdenActivityCallback(RefPtr<GCActivityCallback>&& callback)
{
    m_edenActivityCallback = WTFMove(callback);
}

void Heap::disableStopIfNecessaryTimer()
{
    m_stopIfNecessaryTimer->disable();
}

bool Heap::useGenerationalGC()
{
    return Options::useGenerationalGC() && !VM::isInMiniMode();
}

bool Heap::shouldSweepSynchronously()
{
    return Options::sweepSynchronously() || VM::isInMiniMode();
}

bool Heap::shouldDoFullCollection()
{
    if (!useGenerationalGC())
        return true;

    if (!m_currentRequest.scope)
        return m_shouldDoFullCollection || overCriticalMemoryThreshold();
    return *m_currentRequest.scope == CollectionScope::Full;
}

void Heap::addLogicallyEmptyWeakBlock(WeakBlock* block)
{
    m_logicallyEmptyWeakBlocks.append(block);
}

void Heap::sweepAllLogicallyEmptyWeakBlocks()
{
    if (m_logicallyEmptyWeakBlocks.isEmpty())
        return;

    m_indexOfNextLogicallyEmptyWeakBlockToSweep = 0;
    while (sweepNextLogicallyEmptyWeakBlock()) { }
}

bool Heap::sweepNextLogicallyEmptyWeakBlock()
{
    if (m_indexOfNextLogicallyEmptyWeakBlockToSweep == WTF::notFound)
        return false;

    WeakBlock* block = m_logicallyEmptyWeakBlocks[m_indexOfNextLogicallyEmptyWeakBlockToSweep];

    block->sweep();
    if (block->isEmpty()) {
        std::swap(m_logicallyEmptyWeakBlocks[m_indexOfNextLogicallyEmptyWeakBlockToSweep], m_logicallyEmptyWeakBlocks.last());
        m_logicallyEmptyWeakBlocks.removeLast();
        WeakBlock::destroy(*this, block);
    } else
        m_indexOfNextLogicallyEmptyWeakBlockToSweep++;

    if (m_indexOfNextLogicallyEmptyWeakBlockToSweep >= m_logicallyEmptyWeakBlocks.size()) {
        m_indexOfNextLogicallyEmptyWeakBlockToSweep = WTF::notFound;
        return false;
    }

    return true;
}

size_t Heap::visitCount()
{
    size_t result = 0;
    forEachSlotVisitor(
        [&] (SlotVisitor& visitor) {
            result += visitor.visitCount();
        });
    return result;
}

size_t Heap::bytesVisited()
{
    size_t result = 0;
    forEachSlotVisitor(
        [&] (SlotVisitor& visitor) {
            result += visitor.bytesVisited();
        });
    return result;
}

void Heap::forEachCodeBlockImpl(const ScopedLambda<void(CodeBlock*)>& func)
{
    // We don't know the full set of CodeBlocks until compilation has terminated.
    completeAllJITPlans();

    return m_codeBlocks->iterate(func);
}

void Heap::forEachCodeBlockIgnoringJITPlansImpl(const AbstractLocker& locker, const ScopedLambda<void(CodeBlock*)>& func)
{
    return m_codeBlocks->iterate(locker, func);
}

void Heap::writeBarrierSlowPath(const JSCell* from)
{
    if (mutatorShouldBeFenced()) [[unlikely]] {
        // In this case, the barrierThreshold is the tautological threshold, so from could still be
        // not black. But we can't know for sure until we fire off a fence.
        WTF::storeLoadFence();
        if (from->cellState() != CellState::PossiblyBlack)
            return;
    }
    
    addToRememberedSet(from);
}

bool Heap::currentThreadIsDoingGCWork()
{
    return Thread::mayBeGCThread() || mutatorState() != MutatorState::Running;
}

void Heap::reportExtraMemoryVisited(size_t size)
{
    size_t* counter = &m_extraMemorySize;
    
    for (;;) {
        size_t oldSize = *counter;
        // FIXME: Change this to use SaturatedArithmetic when available.
        // https://bugs.webkit.org/show_bug.cgi?id=170411
        CheckedSize checkedNewSize = oldSize;
        checkedNewSize += size;
        size_t newSize = std::numeric_limits<size_t>::max();
        if (!checkedNewSize.hasOverflowed()) [[likely]]
            newSize = checkedNewSize.value();
        if (WTF::atomicCompareExchangeWeakRelaxed(counter, oldSize, newSize))
            return;
    }
}

#if ENABLE(RESOURCE_USAGE)
void Heap::reportExternalMemoryVisited(size_t size)
{
    size_t* counter = &m_externalMemorySize;

    for (;;) {
        size_t oldSize = *counter;
        if (WTF::atomicCompareExchangeWeakRelaxed(counter, oldSize, oldSize + size))
            return;
    }
}
#endif

void Heap::collectIfNecessaryOrDefer(GCDeferralContext* deferralContext)
{
    ASSERT(deferralContext || isDeferred() || !AssertNoGC::isInEffectOnCurrentThread());
    if constexpr (validateDFGDoesGC)
        vm().verifyCanGC();

    if (!m_isSafeToCollect)
        return;

    switch (mutatorState()) {
    case MutatorState::Running:
    case MutatorState::Allocating:
        break;
    case MutatorState::Sweeping:
    case MutatorState::Collecting:
        return;
    }
    if (!Options::useGC()) [[unlikely]]
        return;
    
    if (mayNeedToStop()) {
        if (deferralContext)
            deferralContext->m_shouldGC = true;
        else if (isDeferred())
            m_didDeferGCWork = true;
        else
            stopIfNecessary();
    }
    
    auto shouldRequestGC = [&] () -> bool {
        bool logRequestGC = false;
        // Don't log if we already have a request pending or if we have to come back later so we don't flood dataFile.
        if (Options::logGC()) [[unlikely]]
            logRequestGC = m_requests.isEmpty() && !deferralContext && !isDeferred();
        if (Options::gcMaxHeapSize()) [[unlikely]] {
            size_t bytesAllocatedThisCycle = totalBytesAllocatedThisCycle();
            if (bytesAllocatedThisCycle <= Options::gcMaxHeapSize())
                return false;
            dataLogLnIf(logRequestGC, "Requesting GC because bytes allocated this cycle: ", bytesAllocatedThisCycle, " exceed Options::gcMaxHeapSize(): ", Options::gcMaxHeapSize());
            return true;
        }

        ASSERT(m_maxHeapSize > m_sizeAfterLastCollect);
        size_t bytesAllowedThisCycle = m_maxHeapSize - m_sizeAfterLastCollect;

        bool isCritical = false;
#if USE(BMALLOC_MEMORY_FOOTPRINT_API)
        isCritical = overCriticalMemoryThreshold();
        if (isCritical)
            bytesAllowedThisCycle = std::min(m_maxEdenSizeWhenCritical, bytesAllowedThisCycle);
#endif

        size_t bytesAllocatedThisCycle = totalBytesAllocatedThisCycle();
        if (bytesAllocatedThisCycle <= bytesAllowedThisCycle)
            return false;
        if (bytesAllocatedThisCycle < m_maxEdenSizeForRateLimiting) {
            if (projectedGCRateLimitingValue(MonotonicTime::now()) > 1.0)
                return false;
        }

        // We don't want to GC if the last oversized allocation makes up too much of the memory allocated this cycle since it's likely
        //  that object is still live and doesn't give us much indication about how much memory we could actually reclaim. That said,
        // if the system is cricital or we have a small heap we want to be very agressive about reclaiming memory to reduce overall
        // pressure on the system.
        if (!isCritical && m_heapType == HeapType::Large) {
            if (static_cast<double>(m_lastOversidedAllocationThisCycle) / bytesAllocatedThisCycle > 1.0 / 3.0)
                return false;
        }

        dataLogLnIf(logRequestGC, "Requesting GC because bytes allocated this cycle: ", bytesAllocatedThisCycle, " exceed bytes allowed: ", bytesAllowedThisCycle, ConditionalDump(isCritical, " (critical)"), " normal bytes: ", m_nonOversizedBytesAllocatedThisCycle, " oversized bytes: ", m_oversizedBytesAllocatedThisCycle, " last oversized: ", m_lastOversidedAllocationThisCycle);
        return true;
    };
    if (!shouldRequestGC())
        return;

    if (deferralContext)
        deferralContext->m_shouldGC = true;
    else if (isDeferred())
        m_didDeferGCWork = true;
    else {
        collectAsync();
        stopIfNecessary(); // This will immediately start the collection if we have the conn.
    }
}

void Heap::decrementDeferralDepthAndGCIfNeededSlow()
{
    // Can't do anything if we're still deferred.
    if (m_deferralDepth)
        return;
    
    ASSERT(!isDeferred());
    
    m_didDeferGCWork = false;
    // FIXME: Bring back something like the DeferGCProbability mode.
    // https://bugs.webkit.org/show_bug.cgi?id=166627
    collectIfNecessaryOrDefer();
}

void Heap::registerWeakGCHashTable(WeakGCHashTable* weakGCHashTable)
{
    m_weakGCHashTables.add(weakGCHashTable);
}

void Heap::unregisterWeakGCHashTable(WeakGCHashTable* weakGCHashTable)
{
    m_weakGCHashTables.remove(weakGCHashTable);
}

void Heap::didAllocateBlock(size_t capacity)
{
#if ENABLE(RESOURCE_USAGE)
    m_blockBytesAllocated += capacity;
#else
    UNUSED_PARAM(capacity);
#endif
}

void Heap::didFreeBlock(size_t capacity)
{
#if ENABLE(RESOURCE_USAGE)
    m_blockBytesAllocated -= capacity;
#else
    UNUSED_PARAM(capacity);
#endif
}

#if ENABLE(SAMPLING_PROFILER)
constexpr bool samplingProfilerSupported = true;
template<typename Visitor>
static ALWAYS_INLINE void visitSamplingProfiler(VM& vm, Visitor& visitor)
{
    SamplingProfiler* samplingProfiler = vm.samplingProfiler();
    if (samplingProfiler) [[unlikely]] {
        Locker locker { samplingProfiler->getLock() };
        samplingProfiler->processUnverifiedStackTraces();
        samplingProfiler->visit(visitor);
        if (Options::logGC() == GCLogging::Verbose)
            dataLog("Sampling Profiler data:\n", visitor);
    }
};
#else
constexpr bool samplingProfilerSupported = false;
static UNUSED_FUNCTION void visitSamplingProfiler(VM&, AbstractSlotVisitor&) { };
#endif

void Heap::addCoreConstraints()
{
    m_constraintSet->add(
        "Cs", "Conservative Scan",
        MAKE_MARKING_CONSTRAINT_EXECUTOR_PAIR(([this, lastVersion = static_cast<uint64_t>(0)] (auto& visitor) mutable {
            bool shouldNotProduceWork = lastVersion == m_phaseVersion;
            SuperSamplerScope superSamplerScope(false);

            // For the GC Verfier, we would like to use the identical set of conservative roots
            // as the real GC. Otherwise, the GC verifier may report false negatives due to
            // variations in stack values. For this same reason, we will skip this constraint
            // when we're running the GC verification in the End phase.
            if (shouldNotProduceWork || m_isMarkingForGCVerifier)
                return;
            
            TimingScope preConvergenceTimingScope(*this, "Constraint: conservative scan"_s);
            m_objectSpace.prepareForConservativeScan();
            m_jitStubRoutines->prepareForConservativeScan();

            {

                // We only want to do this when the mutator has the conn because that means we're under a safepoint.
                // If we tried to scan while not under a safepoint we could stop a thread that's in the process of calling
                // one of the callees we are looking for.
                // FIXME: Should we have two constraints for this? One for concurrent and one under safepoint at the bitter end.
                // TODO: Verify this part only runs on one thread.
                ASSERT(worldIsStopped());
                ConservativeRoots conservativeRoots(*this);

                gatherStackRoots(conservativeRoots);
                gatherJSStackRoots(conservativeRoots);
                gatherScratchBufferRoots(conservativeRoots);

                SetRootMarkReasonScope rootScope(visitor, RootMarkReason::ConservativeScan);
                visitor.append(conservativeRoots);
                if (m_verifierSlotVisitor) [[unlikely]] {
                    SetRootMarkReasonScope rootScope(*m_verifierSlotVisitor, RootMarkReason::ConservativeScan);
                    m_verifierSlotVisitor->append(conservativeRoots);
                }
            }

            // JITStubRoutines must be visited after scanning ConservativeRoots since JITStubRoutines depend on the hook executed during gathering ConservativeRoots.
            SetRootMarkReasonScope rootScope(visitor, RootMarkReason::JITStubRoutines);
            m_jitStubRoutines->traceMarkedStubRoutines(visitor);
            if (m_verifierSlotVisitor) [[unlikely]] {
                // It's important to cast m_verifierSlotVisitor to an AbstractSlotVisitor here
                // so that we'll call the AbstractSlotVisitor version of traceMarkedStubRoutines().
                AbstractSlotVisitor& visitor = *m_verifierSlotVisitor;
                m_jitStubRoutines->traceMarkedStubRoutines(visitor);
            }
            lastVersion = m_phaseVersion;
        })),
        ConstraintVolatility::GreyedByExecution);
    
    m_constraintSet->add(
        "Msr", "Misc Small Roots",
        MAKE_MARKING_CONSTRAINT_EXECUTOR_PAIR(([this] (auto& visitor) {
            VM& vm = this->vm();
#if JSC_OBJC_API_ENABLED
            {
                SetRootMarkReasonScope rootScope(visitor, RootMarkReason::ExternalRememberedSet);
                scanExternalRememberedSet(vm, visitor);
            }
#endif

            {
                SetRootMarkReasonScope rootScope(visitor, RootMarkReason::StrongReferences);
                if (vm.smallStrings.needsToBeVisited(*m_collectionScope))
                    vm.smallStrings.visitStrongReferences(visitor);
            }
            
            {
                SetRootMarkReasonScope rootScope(visitor, RootMarkReason::ProtectedValues);
                for (auto& pair : m_protectedValues)
                    visitor.appendUnbarriered(pair.key);
            }
            
            if (!m_markListSet.isEmpty()) {
                SetRootMarkReasonScope rootScope(visitor, RootMarkReason::ConservativeScan);
                MarkedVectorBase::markLists(visitor, m_markListSet);
            }

            {
                SetRootMarkReasonScope rootScope(visitor, RootMarkReason::MarkedJSValueRefArray);
                m_markedJSValueRefArrays.forEach([&] (MarkedJSValueRefArray* array) {
                    array->visitAggregate(visitor);
                });
            }

            {
                SetRootMarkReasonScope rootScope(visitor, RootMarkReason::VMExceptions);
                visitor.appendUnbarriered(vm.exception());
                visitor.appendUnbarriered(vm.lastException());

                // We're going to m_terminationException directly instead of going through
                // the exception() getter because we want to assert in the getter that the
                // TerminationException has been reified. Here, we don't care if it is
                // reified or not.
                visitor.appendUnbarriered(vm.m_terminationException);
            }
        })),
        ConstraintVolatility::GreyedByExecution);
    
    m_constraintSet->add(
        "Sh", "Strong Handles",
        MAKE_MARKING_CONSTRAINT_EXECUTOR_PAIR(([this] (auto& visitor) {
            SetRootMarkReasonScope rootScope(visitor, RootMarkReason::StrongHandles);
            m_handleSet.visitStrongHandles(visitor);
            vm().visitAggregate(visitor);
        })),
        ConstraintVolatility::GreyedByExecution);
    
    m_constraintSet->add(
        "D", "Debugger",
        MAKE_MARKING_CONSTRAINT_EXECUTOR_PAIR(([this] (auto& visitor) {
            SetRootMarkReasonScope rootScope(visitor, RootMarkReason::Debugger);

            VM& vm = this->vm();
            if constexpr (samplingProfilerSupported)
                visitSamplingProfiler(vm, visitor);

            if (vm.typeProfiler())
                vm.typeProfilerLog()->visit(visitor);
            
            if (auto* shadowChicken = vm.shadowChicken())
                shadowChicken->visitChildren(visitor);
        })),
        ConstraintVolatility::GreyedByExecution);
    
    m_constraintSet->add(
        "Ws", "Weak Sets",
        MAKE_MARKING_CONSTRAINT_EXECUTOR_PAIR(([this] (auto& visitor) {
            SetRootMarkReasonScope rootScope(visitor, RootMarkReason::WeakSets);
            RefPtr<SharedTask<void(decltype(visitor)&)>> task = m_objectSpace.forEachWeakInParallel<decltype(visitor)>(visitor);
            visitor.addParallelConstraintTask(WTFMove(task));
        })),
        ConstraintVolatility::GreyedByMarking,
        ConstraintParallelism::Parallel);
    
    m_constraintSet->add(
        "O", "Output",
        MAKE_MARKING_CONSTRAINT_EXECUTOR_PAIR(([] (auto& visitor) {
            JSC::Heap* heap = visitor.heap();

            auto callOutputConstraint = [] (auto& visitor, HeapCell* heapCell, HeapCell::Kind) {
                SetRootMarkReasonScope rootScope(visitor, RootMarkReason::Output);
                JSCell* cell = static_cast<JSCell*>(heapCell);
                cell->methodTable()->visitOutputConstraints(cell, visitor);
            };
            
            auto add = [&] (auto& set) {
                RefPtr<SharedTask<void(decltype(visitor)&)>> task = set.template forEachMarkedCellInParallel<decltype(visitor)>(callOutputConstraint);
                visitor.addParallelConstraintTask(WTFMove(task));
            };

            {
                SetRootMarkReasonScope rootScope(visitor, RootMarkReason::ExecutableToCodeBlockEdges);
                add(heap->functionExecutableSpaceAndSet.outputConstraintsSet);
                add(heap->programExecutableSpaceAndSet.outputConstraintsSet);
                if (heap->m_evalExecutableSpace)
                    add(heap->m_evalExecutableSpace->outputConstraintsSet);
                if (heap->m_moduleProgramExecutableSpace)
                    add(heap->m_moduleProgramExecutableSpace->outputConstraintsSet);
            }
            if (heap->m_weakMapSpace) {
                SetRootMarkReasonScope rootScope(visitor, RootMarkReason::WeakMapSpace);
                add(*heap->m_weakMapSpace);
            }
        })),
        ConstraintVolatility::GreyedByMarking,
        ConstraintParallelism::Parallel);
    
#if ENABLE(JIT)
    if (Options::useJIT()) {
        m_constraintSet->add(
            "Jw", "JIT Worklist",
            MAKE_MARKING_CONSTRAINT_EXECUTOR_PAIR(([this] (auto& visitor) {
                SetRootMarkReasonScope rootScope(visitor, RootMarkReason::JITWorkList);

                JITWorklist::ensureGlobalWorklist().visitWeakReferences(visitor);
                
                // FIXME: This is almost certainly unnecessary.
                // https://bugs.webkit.org/show_bug.cgi?id=166829
                JITWorklist::ensureGlobalWorklist().iterateCodeBlocksForGC(visitor,
                    vm(),
                    [&] (CodeBlock* codeBlock) {
                        visitor.appendUnbarriered(codeBlock);
                    });
                
                if (Options::logGC() == GCLogging::Verbose)
                    dataLog("JIT Worklists:\n", visitor);
            })),
            ConstraintVolatility::GreyedByMarking);
    }
#endif
    
    m_constraintSet->add(
        "Cb", "CodeBlocks",
        MAKE_MARKING_CONSTRAINT_EXECUTOR_PAIR(([this] (auto& visitor) {
            SetRootMarkReasonScope rootScope(visitor, RootMarkReason::CodeBlocks);
            iterateExecutingAndCompilingCodeBlocksWithoutHoldingLocks(visitor,
                [&] (CodeBlock* codeBlock) {
                    // Visit the CodeBlock as a constraint only if it's black.
                    if (visitor.isMarked(codeBlock)
                        && codeBlock->cellState() == CellState::PossiblyBlack)
                        visitor.visitAsConstraint(codeBlock);
                });
        })),
        ConstraintVolatility::SeldomGreyed);
    
    m_constraintSet->add(makeUnique<MarkStackMergingConstraint>(*this));
}

void Heap::addMarkingConstraint(std::unique_ptr<MarkingConstraint> constraint)
{
    PreventCollectionScope preventCollectionScope(*this);
    m_constraintSet->add(WTFMove(constraint));
}

void Heap::notifyIsSafeToCollect()
{
    if (!Options::useGC()) [[unlikely]]
        return;

    MonotonicTime before;
    if (Options::logGC()) [[unlikely]] {
        before = MonotonicTime::now();
        dataLog("[GC<", RawPointer(this), ">: starting ");
    }
    
    addCoreConstraints();
    
    m_isSafeToCollect = true;
    
    if (Options::collectContinuously()) {
        m_collectContinuouslyThread = Thread::create(
            "JSC DEBUG Continuous GC"_s,
            [this] () {
                MonotonicTime initialTime = MonotonicTime::now();
                Seconds period = Seconds::fromMilliseconds(Options::collectContinuouslyPeriodMS());
                while (true) {
                    Locker locker { m_collectContinuouslyLock };
                    {
                        Locker locker { *m_threadLock };
                        if (m_requests.isEmpty()) {
                            m_requests.append(std::nullopt);
                            m_lastGrantedTicket++;
                            m_threadCondition->notifyOne(locker);
                        }
                    }
                    
                    Seconds elapsed = MonotonicTime::now() - initialTime;
                    Seconds elapsedInPeriod = elapsed % period;
                    MonotonicTime timeToWakeUp =
                        initialTime + elapsed - elapsedInPeriod + period;
                    while (!hasElapsed(timeToWakeUp) && !m_shouldStopCollectingContinuously) {
                        m_collectContinuouslyCondition.waitUntil(
                            m_collectContinuouslyLock, timeToWakeUp);
                    }
                    if (m_shouldStopCollectingContinuously)
                        break;
                }
            }, ThreadType::GarbageCollection);
    }
    
    dataLogIf(Options::logGC(), (MonotonicTime::now() - before).milliseconds(), "ms]\n");
}

// Use WTF_IGNORES_THREAD_SAFETY_ANALYSIS because this function conditionally locks m_collectContinuouslyLock,
// which is not supported by analysis.
void Heap::preventCollection() WTF_IGNORES_THREAD_SAFETY_ANALYSIS
{
    if (!m_isSafeToCollect)
        return;
    
    // This prevents the collectContinuously thread from starting a collection.
    m_collectContinuouslyLock.lock();
    
    // Wait for all collections to finish.
    waitForCollector(
        [&] (const AbstractLocker&) -> bool {
            ASSERT(m_lastServedTicket <= m_lastGrantedTicket);
            return m_lastServedTicket == m_lastGrantedTicket;
        });
    
    // Now a collection can only start if this thread starts it.
    RELEASE_ASSERT(!m_collectionScope);
}

// Use WTF_IGNORES_THREAD_SAFETY_ANALYSIS because this function conditionally unlocks m_collectContinuouslyLock,
// which is not supported by analysis.
void Heap::allowCollection() WTF_IGNORES_THREAD_SAFETY_ANALYSIS
{
    if (!m_isSafeToCollect)
        return;
    
    m_collectContinuouslyLock.unlock();
}

void Heap::setMutatorShouldBeFenced(bool value)
{
    m_mutatorShouldBeFenced = value;
    m_barrierThreshold = value ? tautologicalThreshold : blackThreshold;
}

void Heap::performIncrement(size_t bytes)
{
    if (!m_objectSpace.isMarking())
        return;

    if (isDeferred())
        return;

    m_incrementBalance += bytes * Options::gcIncrementScale();

    // Save ourselves from crazy. Since this is an optimization, it's OK to go back to any consistent
    // state when the double goes wild.
    if (std::isnan(m_incrementBalance) || std::isinf(m_incrementBalance))
        m_incrementBalance = 0;
    
    if (m_incrementBalance < static_cast<double>(Options::gcIncrementBytes()))
        return;

    double targetBytes = m_incrementBalance;
    if (targetBytes <= 0)
        return;
    targetBytes = std::min(targetBytes, Options::gcIncrementMaxBytes());

    SlotVisitor& visitor = *m_mutatorSlotVisitor;
    ParallelModeEnabler parallelModeEnabler(visitor);
    size_t bytesVisited = visitor.performIncrementOfDraining(static_cast<size_t>(targetBytes));
    // incrementBalance may go negative here because it'll remember how many bytes we overshot.
    m_incrementBalance -= bytesVisited;
}

void Heap::addHeapFinalizerCallback(const HeapFinalizerCallback& callback)
{
    m_heapFinalizerCallbacks.append(callback);
}

void Heap::removeHeapFinalizerCallback(const HeapFinalizerCallback& callback)
{
    m_heapFinalizerCallbacks.removeFirst(callback);
}

void Heap::setBonusVisitorTask(RefPtr<SharedTask<void(SlotVisitor&)>> task)
{
    Locker locker { m_markingMutex };
    m_bonusVisitorTask = task;
    m_markingConditionVariable.notifyAll();
}


void Heap::addMarkedJSValueRefArray(MarkedJSValueRefArray* array)
{
    m_markedJSValueRefArrays.append(array);
}

void Heap::runTaskInParallel(RefPtr<SharedTask<void(SlotVisitor&)>> task)
{
    unsigned initialRefCount = task->refCount();
    setBonusVisitorTask(task);
    task->run(*m_collectorSlotVisitor);
    setBonusVisitorTask(nullptr);
    // The constraint solver expects return of this function to imply termination of the task in all
    // threads. This ensures that property.
    {
        Locker locker { m_markingMutex };
        while (task->refCount() > initialRefCount)
            m_markingConditionVariable.wait(m_markingMutex);
    }
}

void Heap::verifierMark()
{
    RELEASE_ASSERT(!m_isMarkingForGCVerifier);

    SetForScope isMarkingForGCVerifierScope(m_isMarkingForGCVerifier, true);
    VerifierSlotVisitor& visitor = *m_verifierSlotVisitor;
    do {
        while (!visitor.isEmpty())
            visitor.drain();
        m_constraintSet->executeAllSynchronously(visitor);
        visitor.executeConstraintTasks();
    } while (!visitor.isEmpty());

    visitor.setDoneMarking();
}

void Heap::dumpVerifierMarkerData(HeapCell* cell)
{
    if (!Options::verifyGC())
        return;

    if (!Heap::isMarked(cell)) {
        dataLogLn("\n" "GC Verifier: cell ", RawPointer(cell), " was not marked by SlotVisitor");
        return;
    }

    // Use VerifierSlotVisitorScope to keep it live.
    RELEASE_ASSERT(m_verifierSlotVisitor && !m_isMarkingForGCVerifier);
    VerifierSlotVisitor& visitor = *m_verifierSlotVisitor;
    RELEASE_ASSERT(visitor.doneMarking());

    if (!visitor.isMarked(cell)) {
        dataLogLn("\n" "GC Verifier: ERROR cell ", RawPointer(cell), " was not marked by VerifierSlotVisitor");
        return;
    }

    dataLogLn("\n" "GC Verifier: Found marked cell ", RawPointer(cell), " with MarkerData:");
    visitor.dumpMarkerData(cell);
}

void Heap::verifyGC()
{
    RELEASE_ASSERT(m_verifierSlotVisitor);
    verifierMark();
    VerifierSlotVisitor& visitor = *m_verifierSlotVisitor;
    RELEASE_ASSERT(visitor.doneMarking() && !m_isMarkingForGCVerifier);

    visitor.forEachLiveCell([&] (HeapCell* cell) {
        if (Heap::isMarked(cell))
            return;

        dataLogLn("\n" "GC Verifier: ERROR cell ", RawPointer(cell), " was not marked");
        if (Options::verboseVerifyGC()) [[unlikely]]
            visitor.dumpMarkerData(cell);
        RELEASE_ASSERT(this->isMarked(cell));
    });

    if (!m_keepVerifierSlotVisitor)
        clearVerifierSlotVisitor();
}

void Heap::setKeepVerifierSlotVisitor() { m_keepVerifierSlotVisitor = true; }

void Heap::clearVerifierSlotVisitor()
{
    m_verifierSlotVisitor = nullptr;
    m_keepVerifierSlotVisitor = false;
}

void Heap::scheduleOpportunisticFullCollection()
{
    m_shouldDoOpportunisticFullCollection = true;
}

#define DEFINE_DYNAMIC_ISO_SUBSPACE_MEMBER_SLOW(name, heapCellType, type) \
    IsoSubspace* Heap::name##Slow() \
    { \
        ASSERT(!m_##name); \
        auto space = makeUnique<IsoSubspace> ISO_SUBSPACE_INIT(*this, heapCellType, type); \
        WTF::storeStoreFence(); \
        m_##name = WTFMove(space); \
        return m_##name.get(); \
    }

FOR_EACH_JSC_DYNAMIC_ISO_SUBSPACE(DEFINE_DYNAMIC_ISO_SUBSPACE_MEMBER_SLOW)

#undef DEFINE_DYNAMIC_ISO_SUBSPACE_MEMBER_SLOW

#define DEFINE_DYNAMIC_SPACE_AND_SET_MEMBER_SLOW(name, heapCellType, type, spaceType) \
    IsoSubspace* Heap::name##Slow() \
    { \
        ASSERT(!m_##name); \
        auto space = makeUnique<spaceType> ISO_SUBSPACE_INIT(*this, heapCellType, type); \
        WTF::storeStoreFence(); \
        m_##name = WTFMove(space); \
        return &m_##name->space; \
    }

DEFINE_DYNAMIC_SPACE_AND_SET_MEMBER_SLOW(evalExecutableSpace, destructibleCellHeapCellType, EvalExecutable, Heap::ScriptExecutableSpaceAndSets) // Hash:0x958e3e9d
DEFINE_DYNAMIC_SPACE_AND_SET_MEMBER_SLOW(moduleProgramExecutableSpace, destructibleCellHeapCellType, ModuleProgramExecutable, Heap::ScriptExecutableSpaceAndSets) // Hash:0x6506fa3c

#undef DEFINE_DYNAMIC_SPACE_AND_SET_MEMBER_SLOW

#define DEFINE_DYNAMIC_NON_ISO_SUBSPACE_MEMBER_SLOW(name, heapCellType, type, SubspaceType) \
    SubspaceType* Heap::name##Slow() \
    { \
        ASSERT(!m_##name); \
        auto space = makeUnique<SubspaceType>(ASCIILiteral(#SubspaceType " " #name), *this, heapCellType, fastMallocAllocator.get()); \
        WTF::storeStoreFence(); \
        m_##name = WTFMove(space); \
        return m_##name.get(); \
    }

FOR_EACH_JSC_WEBASSEMBLY_DYNAMIC_NON_ISO_SUBSPACE(DEFINE_DYNAMIC_NON_ISO_SUBSPACE_MEMBER_SLOW)
#undef DEFINE_DYNAMIC_NON_ISO_SUBSPACE_MEMBER_SLOW

#if ENABLE(WEBASSEMBLY)

void Heap::reportWasmCalleePendingDestruction(Ref<Wasm::Callee>&& callee)
{
    void* boxedCallee = CalleeBits::boxNativeCallee(callee.ptr());
    // This better be true or we won't find the callee in ConservativeRoots.
    ASSERT_UNUSED(boxedCallee, boxedCallee == removeArrayPtrTag(boxedCallee));

    Locker locker(m_wasmCalleesPendingDestructionLock);
    m_wasmCalleesPendingDestruction.add(WTFMove(callee));
}

bool Heap::isWasmCalleePendingDestruction(Wasm::Callee& callee)
{
    Locker locker(m_wasmCalleesPendingDestructionLock);
    return m_wasmCalleesPendingDestruction.contains(callee);
}

#endif

namespace GCClient {

#define INIT_CLIENT_ISO_SUBSPACE_FROM_SPACE_AND_SET(subspace) subspace(heap.subspace##AndSet.space)

#define INIT_CLIENT_ISO_SUBSPACE(name, heapCellType, type) \
    , name(heap.name)

Heap::Heap(JSC::Heap& heap)
    : m_server(heap)
    FOR_EACH_JSC_ISO_SUBSPACE(INIT_CLIENT_ISO_SUBSPACE)
    , INIT_CLIENT_ISO_SUBSPACE_FROM_SPACE_AND_SET(codeBlockSpace)
    , INIT_CLIENT_ISO_SUBSPACE_FROM_SPACE_AND_SET(functionExecutableSpace)
    , INIT_CLIENT_ISO_SUBSPACE_FROM_SPACE_AND_SET(programExecutableSpace)
    , INIT_CLIENT_ISO_SUBSPACE_FROM_SPACE_AND_SET(unlinkedFunctionExecutableSpace)
{
}

Heap::~Heap()
{
}

#undef INIT_CLIENT_ISO_SUBSPACE
#undef CLIENT_ISO_SUBSPACE_INIT_FROM_SPACE_AND_SET


#define DEFINE_DYNAMIC_ISO_SUBSPACE_MEMBER_SLOW_IMPL(name, heapCellType, type) \
    IsoSubspace* Heap::name##Slow() \
    { \
        ASSERT(!m_##name); \
        Locker locker { server().m_lock }; \
        JSC::IsoSubspace& serverSpace = *server().name<SubspaceAccess::OnMainThread>(); \
        auto space = makeUnique<IsoSubspace>(serverSpace); \
        WTF::storeStoreFence(); \
        m_##name = WTFMove(space); \
        return m_##name.get(); \
    }

#define DEFINE_DYNAMIC_ISO_SUBSPACE_MEMBER_SLOW(name) \
    DEFINE_DYNAMIC_ISO_SUBSPACE_MEMBER_SLOW_IMPL(name, unused, unused2) \

FOR_EACH_JSC_DYNAMIC_ISO_SUBSPACE(DEFINE_DYNAMIC_ISO_SUBSPACE_MEMBER_SLOW_IMPL)

DEFINE_DYNAMIC_ISO_SUBSPACE_MEMBER_SLOW(evalExecutableSpace)
DEFINE_DYNAMIC_ISO_SUBSPACE_MEMBER_SLOW(moduleProgramExecutableSpace)

#undef DEFINE_DYNAMIC_ISO_SUBSPACE_MEMBER_SLOW_IMPL
#undef DEFINE_DYNAMIC_ISO_SUBSPACE_MEMBER_SLOW

} // namespace GCClient

} // namespace JSC
