/*
 *  Copyright (C) 1999-2000 Harri Porten (porten@kde.org)
 *  Copyright (C) 2004-2024 Apple Inc. All rights reserved.
 *  Copyright (C) 2006 Bjoern Graf (bjoern.graf@gmail.com)
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"

#include "APICast.h"
#include "ArrayBuffer.h"
#include "AtomicsObject.h"
#include "BigIntConstructor.h"
#include "BytecodeCacheError.h"
#include "CatchScope.h"
#include "CodeBlock.h"
#include "CodeCache.h"
#include "CompilerTimingScope.h"
#include "Completion.h"
#include "ConfigFile.h"
#include "DeferTermination.h"
#include "DeferredWorkTimer.h"
#include "Disassembler.h"
#include "Exception.h"
#include "ExceptionHelpers.h"
#include "Fuzzilli.h"
#include "GlobalObjectMethodTable.h"
#include "HeapSnapshotBuilder.h"
#include "InitializeThreading.h"
#include "Interpreter.h"
#include "JIT.h"
#include "JITOperationList.h"
#include "JITSizeStatistics.h"
#include "JSArray.h"
#include "JSArrayBuffer.h"
#include "JSBasePrivate.h"
#include "JSBigInt.h"
#include "JSFinalizationRegistry.h"
#include "JSFunction.h"
#include "JSFunctionInlines.h"
#include "JSInternalPromise.h"
#include "JSLock.h"
#include "JSNativeStdFunction.h"
#include "JSONObject.h"
#include "JSObjectInlines.h"
#include "JSScriptFetchParameters.h"
#include "JSSourceCode.h"
#include "JSString.h"
#include "JSTypedArrays.h"
#include "JSWebAssemblyInstance.h"
#include "JSWebAssemblyMemory.h"
#include "LLIntThunks.h"
#include "LinkBuffer.h"
#include "NativeCallee.h"
#include "ObjectConstructor.h"
#include "ParserError.h"
#include "ProfilerDatabase.h"
#include "ReleaseHeapAccessScope.h"
#include "SamplingProfiler.h"
#include "SideDataRepository.h"
#include "SimpleTypedArrayController.h"
#include "StackVisitor.h"
#include "StructureInlines.h"
#include "SuperSampler.h"
#include "TestRunnerUtils.h"
#include "TypedArrayInlines.h"
#include "VMInlines.h"
#include "VMInspector.h"
#include "VMTrapsInlines.h"
#include "WasmCapabilities.h"
#include "WasmFaultSignalHandler.h"
#include "WebAssemblyMemoryConstructor.h"
#include <span>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <type_traits>
#include <wtf/CPUTime.h>
#include <wtf/CommaPrinter.h>
#include <wtf/FastMalloc.h>
#include <wtf/FileHandle.h>
#include <wtf/FileSystem.h>
#include <wtf/MainThread.h>
#include <wtf/MemoryPressureHandler.h>
#include <wtf/MonotonicTime.h>
#include <wtf/SafeStrerror.h>
#include <wtf/Scope.h>
#include <wtf/StringPrintStream.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/URL.h>
#include <wtf/WTFProcess.h>
#include <wtf/WallTime.h>
#include <wtf/text/Base64.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/threads/BinarySemaphore.h>
#include <wtf/threads/Signals.h>

#if OS(WINDOWS)
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#if PLATFORM(COCOA)
#include <crt_externs.h>
#include <wtf/OSObjectPtr.h>
#include <wtf/cocoa/CrashReporter.h>
#include <wtf/cocoa/RuntimeApplicationChecksCocoa.h>
#endif

#if PLATFORM(GTK)
#include <locale.h>
#endif

#if HAVE(READLINE)
// readline/history.h has a Function typedef which conflicts with the WTF::Function template from WTF/Forward.h
// We #define it to something else to avoid this conflict.
#define Function ReadlineFunction
#include <readline/history.h>
#include <readline/readline.h>
#undef Function
#endif

#if OS(WINDOWS)
#include <mmsystem.h>
#include <windows.h>
#include <wtf/win/WTFCRTDebug.h>
#endif

#if OS(DARWIN) && CPU(ARM_THUMB2)
#include <fenv.h>
#include <arm/arch.h>
#endif

#if OS(DARWIN)
#if __has_include(<libproc.h>)
#include <libproc.h>
#endif
#endif

#if OS(DARWIN)
#include <wtf/spi/darwin/ProcessMemoryFootprint.h>
#elif OS(LINUX)
#include <wtf/linux/ProcessMemoryFootprint.h>
#endif

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

#if OS(DARWIN) || OS(LINUX)
struct MemoryFootprint : ProcessMemoryFootprint {
    MemoryFootprint(const ProcessMemoryFootprint& src)
        : ProcessMemoryFootprint(src)
    {
    }
};
#else
struct MemoryFootprint {
    uint64_t current;
    uint64_t peak;

    static MemoryFootprint now()
    {
        return { 0L, 0L };
    }
    
    static void resetPeak()
    {
    }
};
#endif

#if !defined(PATH_MAX)
#define PATH_MAX 4096
#endif

using namespace JSC;

namespace {

#define EXIT_EXCEPTION 3

[[noreturn]] static void jscExit(int status)
{
    waitForAsynchronousDisassembly();
    
#if ENABLE(DFG_JIT)
    if (DFG::isCrashing()) {
        for (;;) {
#if OS(WINDOWS)
            Sleep(1000);
#else
            pause();
#endif
        }
    }
#endif // ENABLE(DFG_JIT)
    exitProcess(status);
}

static unsigned asyncTestPasses { 0 };
static unsigned asyncTestExpectedPasses { 0 };

}

template<typename Vector>
static bool fillBufferWithContentsOfFile(const String& fileName, Vector& buffer);
static RefPtr<Uint8Array> fillBufferWithContentsOfFile(const String& fileName);

class CommandLine;
class GlobalObject;
class Workers;

template<typename Func>
int runJSC(const CommandLine&, bool isWorker, const Func&);
static void checkException(GlobalObject*, bool isLastFile, bool hasException, JSValue, const CommandLine&, bool& success);

class Message : public ThreadSafeRefCounted<Message> {
public:
#if ENABLE(WEBASSEMBLY)
    using Content = Variant<ArrayBufferContents, RefPtr<SharedArrayBufferContents>>;
#else
    using Content = Variant<ArrayBufferContents>;
#endif
    Message(Content&&, int32_t);
    ~Message();
    
    Content&& releaseContents() { return WTFMove(m_contents); }
    int32_t index() const { return m_index; }

private:
    Content m_contents;
    int32_t m_index { 0 };
};

class Worker : public BasicRawSentinelNode<Worker> {
public:
    Worker(Workers&, bool isMain);
    ~Worker();
    
    void enqueue(const AbstractLocker&, RefPtr<Message>);
    RefPtr<Message> dequeue();
    bool isMain() const { return m_isMain; }

    static Worker& current();

private:
    static ThreadSpecific<Worker*>& currentWorker();

    Workers& m_workers;
    Deque<RefPtr<Message>> m_messages;
    const bool m_isMain;
};

class Workers {
    WTF_MAKE_TZONE_ALLOCATED(Workers);
    WTF_MAKE_NONCOPYABLE(Workers);
public:
    Workers();
    ~Workers();
    
    template<typename Func>
    void broadcast(const Func&);
    
    void report(const String&);
    String tryGetReport();
    String getReport();
    
    static Workers& singleton();
    
private:
    friend class Worker;
    
    Lock m_lock;
    Condition m_condition;
    SentinelLinkedList<Worker, BasicRawSentinelNode<Worker>> m_workers;
    Deque<String> m_reports;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(Workers);


static JSC_DECLARE_HOST_FUNCTION(functionAtob);
static JSC_DECLARE_HOST_FUNCTION(functionBtoa);

static JSC_DECLARE_HOST_FUNCTION(functionDisassembleBase64);

static JSC_DECLARE_HOST_FUNCTION(functionCreateGlobalObject);
static JSC_DECLARE_HOST_FUNCTION(functionCreateHeapBigInt);
#if USE(BIGINT32)
static JSC_DECLARE_HOST_FUNCTION(functionCreateBigInt32);
#endif
static JSC_DECLARE_HOST_FUNCTION(functionUseBigInt32);
static JSC_DECLARE_HOST_FUNCTION(functionIsBigInt32);
static JSC_DECLARE_HOST_FUNCTION(functionIsHeapBigInt);
static JSC_DECLARE_HOST_FUNCTION(functionCreateNonRopeNonAtomString);

static JSC_DECLARE_HOST_FUNCTION(functionPrintStdOut);
static JSC_DECLARE_HOST_FUNCTION(functionPrintStdErr);
static JSC_DECLARE_HOST_FUNCTION(functionPrettyPrint);
static JSC_DECLARE_HOST_FUNCTION(functionDebug);
static JSC_DECLARE_HOST_FUNCTION(functionDescribe);
static JSC_DECLARE_HOST_FUNCTION(functionDescribeArray);
static JSC_DECLARE_HOST_FUNCTION(functionSleepSeconds);
static JSC_DECLARE_HOST_FUNCTION(functionJSCStack);
static JSC_DECLARE_HOST_FUNCTION(functionGCAndSweep);
static JSC_DECLARE_HOST_FUNCTION(functionFullGC);
static JSC_DECLARE_HOST_FUNCTION(functionEdenGC);
static JSC_DECLARE_HOST_FUNCTION(functionHeapSize);
static JSC_DECLARE_HOST_FUNCTION(functionMemoryUsageStatistics);
static JSC_DECLARE_HOST_FUNCTION(functionCreateMemoryFootprint);
static JSC_DECLARE_HOST_FUNCTION(functionResetMemoryPeak);
static JSC_DECLARE_HOST_FUNCTION(functionAddressOf);
static JSC_DECLARE_HOST_FUNCTION(functionVersion);
static JSC_DECLARE_HOST_FUNCTION(functionRun);
static JSC_DECLARE_HOST_FUNCTION(functionRunString);
static JSC_DECLARE_HOST_FUNCTION(functionLoad);
static JSC_DECLARE_HOST_FUNCTION(functionLoadString);
static JSC_DECLARE_HOST_FUNCTION(functionReadFile);
static JSC_DECLARE_HOST_FUNCTION(functionWriteFile);
static JSC_DECLARE_HOST_FUNCTION(functionCheckSyntax);
static JSC_DECLARE_HOST_FUNCTION(functionOpenFile);
static JSC_DECLARE_HOST_FUNCTION(functionReadline);
static JSC_DECLARE_HOST_FUNCTION(functionPreciseTime);
static JSC_DECLARE_HOST_FUNCTION(functionNeverInlineFunction);
static JSC_DECLARE_HOST_FUNCTION(functionNoDFG);
static JSC_DECLARE_HOST_FUNCTION(functionNoFTL);
static JSC_DECLARE_HOST_FUNCTION(functionNoOSRExitFuzzing);
static JSC_DECLARE_HOST_FUNCTION(functionOptimizeNextInvocation);
static JSC_DECLARE_HOST_FUNCTION(functionNumberOfDFGCompiles);
static JSC_DECLARE_HOST_FUNCTION(functionCallerIsBBQOrOMGCompiled);
static JSC_DECLARE_HOST_FUNCTION(functionJSCOptions);
static JSC_DECLARE_HOST_FUNCTION(functionReoptimizationRetryCount);
static JSC_DECLARE_HOST_FUNCTION(functionTransferArrayBuffer);
static JSC_DECLARE_HOST_FUNCTION(functionFailNextNewCodeBlock);
[[noreturn]] static JSC_DECLARE_HOST_FUNCTION(functionQuit);
static JSC_DECLARE_HOST_FUNCTION(functionFalse);
static JSC_DECLARE_HOST_FUNCTION(functionUndefined1);
static JSC_DECLARE_HOST_FUNCTION(functionUndefined2);
static JSC_DECLARE_HOST_FUNCTION(functionIsInt32);
static JSC_DECLARE_HOST_FUNCTION(functionIsPureNaN);
static JSC_DECLARE_HOST_FUNCTION(functionEffectful42);
static JSC_DECLARE_HOST_FUNCTION(functionIdentity);
static JSC_DECLARE_HOST_FUNCTION(functionMakeMasquerader);
static JSC_DECLARE_HOST_FUNCTION(functionCallMasquerader);
static JSC_DECLARE_HOST_FUNCTION(functionHasCustomProperties);
static JSC_DECLARE_HOST_FUNCTION(functionDumpTypesForAllVariables);
static JSC_DECLARE_HOST_FUNCTION(functionDrainMicrotasks);
static JSC_DECLARE_HOST_FUNCTION(functionSetTimeout);
static JSC_DECLARE_HOST_FUNCTION(functionReleaseWeakRefs);
static JSC_DECLARE_HOST_FUNCTION(functionFinalizationRegistryLiveCount);
static JSC_DECLARE_HOST_FUNCTION(functionFinalizationRegistryDeadCount);
static JSC_DECLARE_HOST_FUNCTION(functionIs32BitPlatform);
static JSC_DECLARE_HOST_FUNCTION(functionCheckModuleSyntax);
static JSC_DECLARE_HOST_FUNCTION(functionCheckScriptSyntax);
static JSC_DECLARE_HOST_FUNCTION(functionPlatformSupportsSamplingProfiler);
static JSC_DECLARE_HOST_FUNCTION(functionGenerateHeapSnapshot);
static JSC_DECLARE_HOST_FUNCTION(functionGenerateHeapSnapshotForGCDebugging);
static JSC_DECLARE_HOST_FUNCTION(functionResetSuperSamplerState);
static JSC_DECLARE_HOST_FUNCTION(functionEnsureArrayStorage);
#if ENABLE(SAMPLING_PROFILER)
static JSC_DECLARE_HOST_FUNCTION(functionStartSamplingProfiler);
static JSC_DECLARE_HOST_FUNCTION(functionSamplingProfilerStackTraces);
#endif

static JSC_DECLARE_HOST_FUNCTION(functionMaxArguments);
static JSC_DECLARE_HOST_FUNCTION(functionAsyncTestStart);
static JSC_DECLARE_HOST_FUNCTION(functionAsyncTestPassed);

#if ENABLE(WEBASSEMBLY)
static JSC_DECLARE_HOST_FUNCTION(functionWebAssemblyMemoryMode);
static JSC_DECLARE_HOST_FUNCTION(functionCreateWebAssemblyMemoryWithMode);
#endif

#if ENABLE(SAMPLING_FLAGS)
static JSC_DECLARE_HOST_FUNCTION(functionSetSamplingFlags);
static JSC_DECLARE_HOST_FUNCTION(functionClearSamplingFlags);
#endif

static JSC_DECLARE_HOST_FUNCTION(functionGetRandomSeed);
static JSC_DECLARE_HOST_FUNCTION(functionSetRandomSeed);
static JSC_DECLARE_HOST_FUNCTION(functionIsRope);
static JSC_DECLARE_HOST_FUNCTION(functionCallerSourceOrigin);
static JSC_DECLARE_HOST_FUNCTION(functionDollarCreateRealm);
static JSC_DECLARE_HOST_FUNCTION(functionDollarEvalScript);
static JSC_DECLARE_HOST_FUNCTION(functionDollarGC);
static JSC_DECLARE_HOST_FUNCTION(functionDollarClearKeptObjects);
static JSC_DECLARE_HOST_FUNCTION(functionDollarGlobalObjectFor);
static JSC_DECLARE_HOST_FUNCTION(functionDollarIsRemoteFunction);
static JSC_DECLARE_HOST_FUNCTION(functionDollarAgentStart);
static JSC_DECLARE_HOST_FUNCTION(functionDollarAgentReceiveBroadcast);
static JSC_DECLARE_HOST_FUNCTION(functionDollarAgentReport);
static JSC_DECLARE_HOST_FUNCTION(functionDollarAgentSleep);
static JSC_DECLARE_HOST_FUNCTION(functionDollarAgentBroadcast);
static JSC_DECLARE_HOST_FUNCTION(functionDollarAgentGetReport);
static JSC_DECLARE_HOST_FUNCTION(functionDollarAgentLeaving);
static JSC_DECLARE_HOST_FUNCTION(functionDollarAgentMonotonicNow);
static JSC_DECLARE_HOST_FUNCTION(functionWaiterListSize);
static JSC_DECLARE_HOST_FUNCTION(functionWaitForReport);
static JSC_DECLARE_HOST_FUNCTION(functionHeapCapacity);
static JSC_DECLARE_HOST_FUNCTION(functionFlashHeapAccess);
static JSC_DECLARE_HOST_FUNCTION(functionDisableRichSourceInfo);
static JSC_DECLARE_HOST_FUNCTION(functionMallocInALoop);
static JSC_DECLARE_HOST_FUNCTION(functionTotalCompileTime);

static JSC_DECLARE_HOST_FUNCTION(functionSetUnhandledRejectionCallback);
static JSC_DECLARE_HOST_FUNCTION(functionAsDoubleNumber);

static JSC_DECLARE_HOST_FUNCTION(functionDropAllLocks);

static JSC_DECLARE_HOST_FUNCTION(functionPerformanceNow);
static JSC_DECLARE_HOST_FUNCTION(functionPerformanceMark);
static JSC_DECLARE_HOST_FUNCTION(functionPerformanceMeasure);

#if ENABLE(FUZZILLI)
static JSC_DECLARE_HOST_FUNCTION(functionFuzzilli);
#endif

struct Script {
    enum class StrictMode {
        Strict,
        Sloppy
    };

    enum class ScriptType {
        Script,
        Module
    };

    enum class CodeSource {
        File,
        CommandLine,
#if ENABLE(FUZZILLI)
        Reprl,
#endif
    };

    StrictMode strictMode;
    CodeSource codeSource;
    ScriptType scriptType;
    char* argument;

    Script(StrictMode strictMode, CodeSource codeSource, ScriptType scriptType, char *argument)
        : strictMode(strictMode)
        , codeSource(codeSource)
        , scriptType(scriptType)
        , argument(argument)
    {
        if (strictMode == StrictMode::Strict)
            ASSERT(codeSource == CodeSource::File);
    }
};

class CommandLine {
public:
    CommandLine(int argc, char** argv)
    {
        parseArguments(argc, argv);
    }

    enum CommandLineForWorkersTag { CommandLineForWorkers };
    explicit CommandLine(CommandLineForWorkersTag);

    Vector<Script> m_scripts;
    Vector<String> m_arguments;
    String m_profilerOutput;
    String m_uncaughtExceptionName;
    bool m_interactive { false };
    bool m_dump { false };
    bool m_module { false };
    bool m_exitCode { false };
    bool m_destroyVM { false };
    bool m_treatWatchdogExceptionAsSuccess { false };
    bool m_ignoreUncaughtExceptions { false };
    bool m_alwaysDumpUncaughtException { false };
    bool m_dumpMemoryFootprint { false };
    bool m_dumpLinkBufferStats { false };
    bool m_dumpSamplingProfilerData { false };
    bool m_inspectable { false };
    bool m_canBlockIsFalse { false };
    bool m_reprl { false }; // Set to true to use Fuzzilli.

    void parseArguments(int, char**);
};
static LazyNeverDestroyed<CommandLine> mainCommandLine;

static const char interactivePrompt[] = ">>> ";

class StopWatch {
public:
    void start();
    void stop();
    long getElapsedMS(); // call stop() first

private:
    MonotonicTime m_startTime;
    MonotonicTime m_stopTime;
};

void StopWatch::start()
{
    m_startTime = MonotonicTime::now();
}

void StopWatch::stop()
{
    m_stopTime = MonotonicTime::now();
}

long StopWatch::getElapsedMS()
{
    return (m_stopTime - m_startTime).millisecondsAs<long>();
}

template<typename Vector>
static inline String stringFromUTF(const Vector& utf8)
{
    return String::fromUTF8WithLatin1Fallback(utf8.span());
}

static JSC_DECLARE_CUSTOM_GETTER(accessorMakeMasquerader);
static JSC_DECLARE_CUSTOM_SETTER(testCustomAccessorSetter);
static JSC_DECLARE_CUSTOM_SETTER(testCustomValueSetter);

JSC_DEFINE_CUSTOM_GETTER(accessorMakeMasquerader, (JSGlobalObject* globalObject, EncodedJSValue, PropertyName))
{
    VM& vm = globalObject->vm();
    return JSValue::encode(InternalFunction::createFunctionThatMasqueradesAsUndefined(vm, globalObject, 0, "IsHTMLDDA"_s, functionCallMasquerader));
}

int propertyFilterSideDataKey;

class GlobalObject final : public JSGlobalObject {
public:
    using Base = JSGlobalObject;
    static constexpr unsigned StructureFlags = Base::StructureFlags | OverridesGetOwnPropertyNames;

    static GlobalObject* create(VM& vm, Structure* structure, const Vector<String>& arguments)
    {
        GlobalObject* object = new (NotNull, allocateCell<GlobalObject>(vm)) GlobalObject(vm, structure);
        object->finishCreation(vm, arguments);
        return object;
    }

    DECLARE_INFO;
    static const GlobalObjectMethodTable s_globalObjectMethodTable;

    static Structure* createStructure(VM& vm, JSValue prototype)
    {
        return Structure::create(vm, nullptr, prototype, TypeInfo(GlobalObjectType, StructureFlags), info());
    }

    static RuntimeFlags javaScriptRuntimeFlags(const JSGlobalObject*) { return RuntimeFlags::createAllEnabled(); }

    static void getOwnPropertyNames(JSObject* object, JSGlobalObject* globalObject, PropertyNameArray& propertyNames, DontEnumPropertiesMode mode)
    {
        VM& vm = globalObject->vm();
        PropertyNameArray ownPropertyNames(vm, propertyNames.propertyNameMode(), propertyNames.privateSymbolMode());
        Base::getOwnPropertyNames(object, globalObject, ownPropertyNames, mode);
        auto* thisObject = jsCast<GlobalObject*>(object);
        auto& filter = thisObject->ensurePropertyFilter();
        for (auto& propertyName : ownPropertyNames) {
            if (!filter.contains(propertyName.impl()))
                propertyNames.add(propertyName);
        }
    }

private:
    GlobalObject(VM&, Structure*);

    static constexpr unsigned DontEnum = 0 | PropertyAttribute::DontEnum;

    class PropertyFilter : public SideDataRepository::SideData {
        WTF_DEPRECATED_MAKE_FAST_ALLOCATED(PropertyFilter);
    public:
        void add(UniquedStringImpl* uid)
        {
            auto result = m_names.add(uid);
            if (result.isNewEntry)
                m_strings.append(uid);
        }

        bool contains(UniquedStringImpl* name) const { return m_names.contains(name); }
        const UncheckedKeyHashSet<UniquedStringImpl*>& names() const { return m_names; }

    private:
        Vector<AtomString> m_strings; // To keep the UniqueStringImpls alive.
        UncheckedKeyHashSet<UniquedStringImpl*> m_names;
    };

    void finishCreation(VM& vm, const Vector<String>& arguments)
    {
        auto& filter = ensurePropertyFilter();

        auto addFunction = [&] (VM& vm, ASCIILiteral name, NativeFunction function, unsigned arguments, unsigned attributes = static_cast<unsigned>(PropertyAttribute::DontEnum)) {
            addFunctionImpl(filter, vm, name, function, arguments, attributes);
        };

        auto addFunctionToObject = [&] (VM& vm, JSObject* owner, ASCIILiteral name, NativeFunction function, unsigned arguments, unsigned attributes = static_cast<unsigned>(PropertyAttribute::DontEnum)) {
            addFunctionToObjectImpl(filter, vm, owner, name, function, arguments, attributes);
        };

        auto putDirect = [&] (VM& vm, PropertyName propertyName, JSValue value, unsigned attributes = 0) -> bool {
            return putDirectImpl(filter, vm, propertyName, value, attributes);
        };

        auto putDirectWithoutTransition = [&] (VM& vm, PropertyName propertyName, JSValue value, unsigned attributes) {
            putDirectWithoutTransitionImpl(filter, vm, propertyName, value, attributes);
        };

        auto putDirectNativeFunction = [&] (VM& vm, JSGlobalObject* globalObject, const PropertyName& propertyName, unsigned functionLength, NativeFunction nativeFunction, ImplementationVisibility implementationVisibility, Intrinsic intrinsic, unsigned attributes) -> bool {
            return putDirectNativeFunctionImpl(filter, vm, globalObject, propertyName, functionLength, nativeFunction, implementationVisibility, intrinsic, attributes);
        };

        Base::finishCreation(vm);
        JSC_TO_STRING_TAG_WITHOUT_TRANSITION();

        // Set loop counts based on enabled engine tiers.
        unsigned testLoopCount = 10000;
        if (!Options::useDFGJIT())
            testLoopCount = 1000;
        if (!Options::useBaselineJIT())
            testLoopCount = 100;

        unsigned wasmTestLoopCount = 10000;
        if (!Options::useOMGJIT())
            wasmTestLoopCount = 1000;
        if (!Options::useBBQJIT())
            wasmTestLoopCount = 100;

        putDirect(vm, Identifier::fromString(vm, "testLoopCount"_s), jsNumber(testLoopCount), DontEnum);
        putDirect(vm, Identifier::fromString(vm, "wasmTestLoopCount"_s), jsNumber(wasmTestLoopCount), DontEnum);

        addFunction(vm, "atob"_s, functionAtob, 1);
        addFunction(vm, "btoa"_s, functionBtoa, 1);
        addFunction(vm, "disassembleBase64"_s, functionDisassembleBase64, 1);
        addFunction(vm, "debug"_s, functionDebug, 1);
        addFunction(vm, "describe"_s, functionDescribe, 1);
        addFunction(vm, "describeArray"_s, functionDescribeArray, 1);
        addFunction(vm, "print"_s, functionPrintStdOut, 1);
        addFunction(vm, "printErr"_s, functionPrintStdErr, 1);
        addFunction(vm, "prettyPrint"_s, functionPrettyPrint, 1);
        addFunction(vm, "quit"_s, functionQuit, 0);
        addFunction(vm, "gc"_s, functionGCAndSweep, 0);
        addFunction(vm, "fullGC"_s, functionFullGC, 0);
        addFunction(vm, "edenGC"_s, functionEdenGC, 0);
        addFunction(vm, "gcHeapSize"_s, functionHeapSize, 0);
        addFunction(vm, "memoryUsageStatistics"_s, functionMemoryUsageStatistics, 0);
        addFunction(vm, "MemoryFootprint"_s, functionCreateMemoryFootprint, 0);
        addFunction(vm, "resetMemoryPeak"_s, functionResetMemoryPeak, 0);
        addFunction(vm, "addressOf"_s, functionAddressOf, 1);
        addFunction(vm, "version"_s, functionVersion, 1);
        addFunction(vm, "run"_s, functionRun, 1);
        addFunction(vm, "runString"_s, functionRunString, 1);
        addFunction(vm, "load"_s, functionLoad, 1);
        addFunction(vm, "loadString"_s, functionLoadString, 1);
        addFunction(vm, "readFile"_s, functionReadFile, 2);
        addFunction(vm, "read"_s, functionReadFile, 2);
        addFunction(vm, "writeFile"_s, functionWriteFile, 2);
        addFunction(vm, "write"_s, functionWriteFile, 2);
        addFunction(vm, "checkSyntax"_s, functionCheckSyntax, 1);
        addFunction(vm, "sleepSeconds"_s, functionSleepSeconds, 1);
        addFunction(vm, "jscStack"_s, functionJSCStack, 1);
        addFunction(vm, "openFile"_s, functionOpenFile, 1);
        addFunction(vm, "readline"_s, functionReadline, 0);
        addFunction(vm, "preciseTime"_s, functionPreciseTime, 0);
        addFunction(vm, "neverInlineFunction"_s, functionNeverInlineFunction, 1);
        addFunction(vm, "noInline"_s, functionNeverInlineFunction, 1);
        addFunction(vm, "noDFG"_s, functionNoDFG, 1);
        addFunction(vm, "noFTL"_s, functionNoFTL, 1);
        addFunction(vm, "noOSRExitFuzzing"_s, functionNoOSRExitFuzzing, 1);
        addFunction(vm, "numberOfDFGCompiles"_s, functionNumberOfDFGCompiles, 1);
        addFunction(vm, "callerIsBBQOrOMGCompiled"_s, functionCallerIsBBQOrOMGCompiled, 0);
        addFunction(vm, "jscOptions"_s, functionJSCOptions, 0);
        addFunction(vm, "optimizeNextInvocation"_s, functionOptimizeNextInvocation, 1);
        addFunction(vm, "reoptimizationRetryCount"_s, functionReoptimizationRetryCount, 1);
        addFunction(vm, "transferArrayBuffer"_s, functionTransferArrayBuffer, 1);
        addFunction(vm, "failNextNewCodeBlock"_s, functionFailNextNewCodeBlock, 1);
#if ENABLE(SAMPLING_FLAGS)
        addFunction(vm, "setSamplingFlags"_s, functionSetSamplingFlags, 1);
        addFunction(vm, "clearSamplingFlags"_s, functionClearSamplingFlags, 1);
#endif

        putDirectNativeFunction(vm, this, Identifier::fromString(vm, "OSRExit"_s), 0, functionUndefined1, ImplementationVisibility::Public, OSRExitIntrinsic, DontEnum);
        putDirectNativeFunction(vm, this, Identifier::fromString(vm, "isFinalTier"_s), 0, functionFalse, ImplementationVisibility::Public, IsFinalTierIntrinsic, DontEnum);
        putDirectNativeFunction(vm, this, Identifier::fromString(vm, "predictInt32"_s), 0, functionUndefined2, ImplementationVisibility::Public, SetInt32HeapPredictionIntrinsic, DontEnum);
        putDirectNativeFunction(vm, this, Identifier::fromString(vm, "isInt32"_s), 0, functionIsInt32, ImplementationVisibility::Public, CheckInt32Intrinsic, DontEnum);
        putDirectNativeFunction(vm, this, Identifier::fromString(vm, "isPureNaN"_s), 0, functionIsPureNaN, ImplementationVisibility::Public, CheckInt32Intrinsic, DontEnum);
        putDirectNativeFunction(vm, this, Identifier::fromString(vm, "fiatInt52"_s), 0, functionIdentity, ImplementationVisibility::Public, FiatInt52Intrinsic, DontEnum);
        
        addFunction(vm, "effectful42"_s, functionEffectful42, 0);
        addFunction(vm, "makeMasquerader"_s, functionMakeMasquerader, 0);
        addFunction(vm, "hasCustomProperties"_s, functionHasCustomProperties, 0);

        addFunction(vm, "createGlobalObject"_s, functionCreateGlobalObject, 0);
        addFunction(vm, "createHeapBigInt"_s, functionCreateHeapBigInt, 1);
#if USE(BIGINT32)
        addFunction(vm, "createBigInt32"_s, functionCreateBigInt32, 1);
#endif
        addFunction(vm, "useBigInt32"_s, functionUseBigInt32, 0);
        addFunction(vm, "isBigInt32"_s, functionIsBigInt32, 1);
        addFunction(vm, "isHeapBigInt"_s, functionIsHeapBigInt, 1);

        addFunction(vm, "createNonRopeNonAtomString"_s, functionCreateNonRopeNonAtomString, 1);

        addFunction(vm, "dumpTypesForAllVariables"_s, functionDumpTypesForAllVariables , 0);

        addFunction(vm, "drainMicrotasks"_s, functionDrainMicrotasks, 0);
        addFunction(vm, "setTimeout"_s, functionSetTimeout, 2);

        addFunction(vm, "releaseWeakRefs"_s, functionReleaseWeakRefs, 0);
        addFunction(vm, "finalizationRegistryLiveCount"_s, functionFinalizationRegistryLiveCount, 0);
        addFunction(vm, "finalizationRegistryDeadCount"_s, functionFinalizationRegistryDeadCount, 0);
        
        addFunction(vm, "getRandomSeed"_s, functionGetRandomSeed, 0);
        addFunction(vm, "setRandomSeed"_s, functionSetRandomSeed, 1);
        addFunction(vm, "isRope"_s, functionIsRope, 1);
        addFunction(vm, "callerSourceOrigin"_s, functionCallerSourceOrigin, 0);

        addFunction(vm, "is32BitPlatform"_s, functionIs32BitPlatform, 0);

        addFunction(vm, "checkModuleSyntax"_s, functionCheckModuleSyntax, 1);
        addFunction(vm, "checkScriptSyntax"_s, functionCheckScriptSyntax, 1);

        addFunction(vm, "platformSupportsSamplingProfiler"_s, functionPlatformSupportsSamplingProfiler, 0);
        addFunction(vm, "generateHeapSnapshot"_s, functionGenerateHeapSnapshot, 0);
        addFunction(vm, "generateHeapSnapshotForGCDebugging"_s, functionGenerateHeapSnapshotForGCDebugging, 0);
        addFunction(vm, "resetSuperSamplerState"_s, functionResetSuperSamplerState, 0);
        addFunction(vm, "ensureArrayStorage"_s, functionEnsureArrayStorage, 0);
#if ENABLE(SAMPLING_PROFILER)
        addFunction(vm, "startSamplingProfiler"_s, functionStartSamplingProfiler, 0);
        addFunction(vm, "samplingProfilerStackTraces"_s, functionSamplingProfilerStackTraces, 0);
#endif

        addFunction(vm, "maxArguments"_s, functionMaxArguments, 0);

        addFunction(vm, "asyncTestStart"_s, functionAsyncTestStart, 1);
        addFunction(vm, "asyncTestPassed"_s, functionAsyncTestPassed, 1);

#if ENABLE(WEBASSEMBLY)
        addFunction(vm, "WebAssemblyMemoryMode"_s, functionWebAssemblyMemoryMode, 1);
        addFunction(vm, "createWebAssemblyMemoryWithMode"_s, functionCreateWebAssemblyMemoryWithMode, 2);
#endif

        if (!arguments.isEmpty()) {
            JSArray* array = constructEmptyArray(this, nullptr);
            for (size_t i = 0; i < arguments.size(); ++i)
                array->putDirectIndex(this, i, jsString(vm, arguments[i]));
            putDirect(vm, Identifier::fromString(vm, "arguments"_s), array, DontEnum);
        }

        putDirect(vm, Identifier::fromString(vm, "console"_s), jsUndefined(), DontEnum);
        
        Structure* plainObjectStructure = JSFinalObject::createStructure(vm, this, objectPrototype(), 0);
        
        JSObject* dollar = JSFinalObject::create(vm, plainObjectStructure);
        putDirect(vm, Identifier::fromString(vm, "$"_s), dollar, DontEnum);
        putDirect(vm, Identifier::fromString(vm, "$262"_s), dollar, DontEnum);
        
        addFunctionToObject(vm, dollar, "createRealm"_s, functionDollarCreateRealm, 0, static_cast<unsigned>(PropertyAttribute::None));
        addFunctionToObject(vm, dollar, "detachArrayBuffer"_s, functionTransferArrayBuffer, 1, static_cast<unsigned>(PropertyAttribute::None));
        addFunctionToObject(vm, dollar, "evalScript"_s, functionDollarEvalScript, 1, static_cast<unsigned>(PropertyAttribute::None));
        addFunctionToObject(vm, dollar, "gc"_s, functionDollarGC, 0, static_cast<unsigned>(PropertyAttribute::None));
        addFunctionToObject(vm, dollar, "clearKeptObjects"_s, functionDollarClearKeptObjects, 0, static_cast<unsigned>(PropertyAttribute::None));
        addFunctionToObject(vm, dollar, "globalObjectFor"_s, functionDollarGlobalObjectFor, 1, static_cast<unsigned>(PropertyAttribute::None));
        addFunctionToObject(vm, dollar, "isRemoteFunction"_s, functionDollarIsRemoteFunction, 1, static_cast<unsigned>(PropertyAttribute::None));
        
        dollar->putDirect(vm, Identifier::fromString(vm, "global"_s), globalThis());
        dollar->putDirectCustomAccessor(vm, Identifier::fromString(vm, "IsHTMLDDA"_s),
            CustomGetterSetter::create(vm, accessorMakeMasquerader, nullptr),
            static_cast<unsigned>(PropertyAttribute::CustomValue)
        );

        JSObject* agent = JSFinalObject::create(vm, plainObjectStructure);
        dollar->putDirect(vm, Identifier::fromString(vm, "agent"_s), agent);
        
        // The test262 INTERPRETING.md document says that some of these functions are just in the main
        // thread and some are in the other threads. We just put them in all threads.
        addFunctionToObject(vm, agent, "start"_s, functionDollarAgentStart, 1);
        addFunctionToObject(vm, agent, "receiveBroadcast"_s, functionDollarAgentReceiveBroadcast, 1);
        addFunctionToObject(vm, agent, "report"_s, functionDollarAgentReport, 1);
        addFunctionToObject(vm, agent, "sleep"_s, functionDollarAgentSleep, 1);
        addFunctionToObject(vm, agent, "broadcast"_s, functionDollarAgentBroadcast, 1);
        addFunctionToObject(vm, agent, "getReport"_s, functionDollarAgentGetReport, 0);
        addFunctionToObject(vm, agent, "leaving"_s, functionDollarAgentLeaving, 0);
        addFunctionToObject(vm, agent, "monotonicNow"_s, functionDollarAgentMonotonicNow, 0);

        addFunction(vm, "waiterListSize"_s, functionWaiterListSize, 2);

        addFunction(vm, "waitForReport"_s, functionWaitForReport, 0);

        addFunction(vm, "heapCapacity"_s, functionHeapCapacity, 0);
        addFunction(vm, "flashHeapAccess"_s, functionFlashHeapAccess, 0);

        addFunction(vm, "disableRichSourceInfo"_s, functionDisableRichSourceInfo, 0);
        addFunction(vm, "mallocInALoop"_s, functionMallocInALoop, 0);
        addFunction(vm, "totalCompileTime"_s, functionTotalCompileTime, 0);

        addFunction(vm, "setUnhandledRejectionCallback"_s, functionSetUnhandledRejectionCallback, 1);

        addFunction(vm, "asDoubleNumber"_s, functionAsDoubleNumber, 1);

        addFunction(vm, "dropAllLocks"_s, functionDropAllLocks, 1);

        JSObject* performance = JSFinalObject::create(vm, plainObjectStructure);
        putDirect(vm, Identifier::fromString(vm, "performance"_s), performance, DontEnum);
        addFunctionToObject(vm, performance, "now"_s, functionPerformanceNow, 0);
        addFunctionToObject(vm, performance, "mark"_s, functionPerformanceMark, 1);
        addFunctionToObject(vm, performance, "measure"_s, functionPerformanceMeasure, 2);

#if ENABLE(FUZZILLI)
        addFunction(vm, "fuzzilli"_s, functionFuzzilli, 2);
#endif

        if (Options::exposeCustomSettersOnGlobalObjectForTesting()) {
            auto putDirectCustomAccessor = [&] (VM& vm, PropertyName propertyName, JSValue value, unsigned attributes) -> bool {
                return putDirectCustomAccessorImpl(filter, vm, propertyName, value, attributes);
            };

            {
                CustomGetterSetter* custom = CustomGetterSetter::create(vm, nullptr, testCustomAccessorSetter);
                Identifier identifier = Identifier::fromString(vm, "testCustomAccessorSetter"_s);
                putDirectCustomAccessor(vm, identifier, custom, PropertyAttribute::DontEnum | PropertyAttribute::DontDelete | PropertyAttribute::CustomAccessor);
            }

            {
                CustomGetterSetter* custom = CustomGetterSetter::create(vm, nullptr, testCustomValueSetter);
                Identifier identifier = Identifier::fromString(vm, "testCustomValueSetter"_s);
                putDirectCustomAccessor(vm, identifier, custom, PropertyAttribute::DontEnum | PropertyAttribute::DontDelete | PropertyAttribute::CustomValue);
            }
        }

        if (Options::useDollarVM()) {
            // $vm is added in JSGlobalObject but we also want it filtered out. Just add it to the filter here.
            Identifier dollarVMIdentifier = Identifier::fromString(vm, "$vm"_s);
            rememberDontEnumProperty(filter, dollarVMIdentifier.impl(), 0 | PropertyAttribute::DontEnum);
        }

#if PLATFORM(COCOA)
        enableAllSDKAlignedBehaviors();
#endif
    }

public:
    static bool testCustomSetterImpl(JSGlobalObject* lexicalGlobalObject, GlobalObject* thisObject, EncodedJSValue encodedValue, ASCIILiteral propertyName)
    {
        VM& vm = lexicalGlobalObject->vm();

        Identifier identifier = Identifier::fromString(vm, propertyName);
        thisObject->putDirect(vm, identifier, JSValue::decode(encodedValue), DontEnum);

        return true;
    }

    bool putDirect(VM& vm, PropertyName propertyName, JSValue value, unsigned attributes = 0)
    {
        auto& filter = ensurePropertyFilter();
        return putDirectImpl(filter, vm, propertyName, value, attributes);
    }

private:
    PropertyFilter& ensurePropertyFilter()
    {
        return vm().ensureSideData<PropertyFilter>(&propertyFilterSideDataKey, [] () -> std::unique_ptr<PropertyFilter> {
            return makeUnique<PropertyFilter>();
        });
    }

    void rememberDontEnumProperty(PropertyFilter& filter, UniquedStringImpl* uid, unsigned attributes)
    {
        static constexpr unsigned DontEnum = static_cast<unsigned>(PropertyAttribute::DontEnum);
        if (attributes & DontEnum)
            filter.add(uid);
    }

    void addFunctionToObjectImpl(PropertyFilter& filter, VM& vm, JSObject* owner, ASCIILiteral name, NativeFunction function, unsigned arguments, unsigned attributes = static_cast<unsigned>(PropertyAttribute::DontEnum))
    {
        Identifier identifier = Identifier::fromString(vm, name);
        owner->putDirect(vm, identifier, JSFunction::create(vm, this, arguments, identifier.string(), function, ImplementationVisibility::Public), attributes);

        // addFunctionToObjectImpl is also used as a utility function for adding to other objects.
        // We only need to call rememberDontEnumProperty on GlobalObject properties.
        if (owner == this)
            rememberDontEnumProperty(filter, identifier.impl(), attributes);
    }

    void addFunctionImpl(PropertyFilter& filter, VM& vm, ASCIILiteral name, NativeFunction function, unsigned arguments, unsigned attributes = static_cast<unsigned>(PropertyAttribute::DontEnum))
    {
        addFunctionToObjectImpl(filter, vm, this, name, function, arguments, attributes);
    }

    bool putDirectImpl(PropertyFilter& filter, VM& vm, PropertyName propertyName, JSValue value, unsigned attributes = 0)
    {
        rememberDontEnumProperty(filter, propertyName.uid(), attributes);
        return Base::putDirect(vm, propertyName, value, attributes);
    }

    void putDirectWithoutTransition(VM&, PropertyName, JSValue, unsigned) = delete;
    void putDirectWithoutTransitionImpl(PropertyFilter& filter, VM& vm, PropertyName propertyName, JSValue value, unsigned attributes)
    {
        rememberDontEnumProperty(filter, propertyName.uid(), attributes);
        Base::putDirectWithoutTransition(vm, propertyName, value, attributes);
    }

    bool putDirectNativeFunction(VM&, JSGlobalObject*, const PropertyName&, unsigned, NativeFunction, ImplementationVisibility, Intrinsic, unsigned) = delete;
    bool putDirectNativeFunctionImpl(PropertyFilter& filter, VM& vm, JSGlobalObject* globalObject, const PropertyName& propertyName, unsigned functionLength, NativeFunction nativeFunction, ImplementationVisibility implementationVisibility, Intrinsic intrinsic, unsigned attributes)
    {
        rememberDontEnumProperty(filter, propertyName.uid(), attributes);
        return Base::putDirectNativeFunction(vm, globalObject, propertyName, functionLength, nativeFunction, implementationVisibility, intrinsic, attributes);
    }

    bool putDirectCustomAccessorImpl(PropertyFilter& filter, VM& vm, PropertyName propertyName, JSValue value, unsigned attributes)
    {
        rememberDontEnumProperty(filter, propertyName.uid(), attributes);
        return Base::putDirectCustomAccessor(vm, propertyName, value, attributes);
    }

    static JSInternalPromise* moduleLoaderImportModule(JSGlobalObject*, JSModuleLoader*, JSString*, JSValue, const SourceOrigin&);
    static Identifier moduleLoaderResolve(JSGlobalObject*, JSModuleLoader*, JSValue, JSValue, JSValue);
    static JSInternalPromise* moduleLoaderFetch(JSGlobalObject*, JSModuleLoader*, JSValue, JSValue, JSValue);
    static JSObject* moduleLoaderCreateImportMetaProperties(JSGlobalObject*, JSModuleLoader*, JSValue, JSModuleRecord*, JSValue);

#if ENABLE(FUZZILLI)
    static void promiseRejectionTracker(JSGlobalObject*, JSPromise*, JSPromiseRejectionOperation);
#endif

    static void reportUncaughtExceptionAtEventLoop(JSGlobalObject*, Exception*);
};
STATIC_ASSERT_ISO_SUBSPACE_SHARABLE(GlobalObject, JSGlobalObject);

static bool supportsRichSourceInfo = true;
static bool shellSupportsRichSourceInfo(const JSGlobalObject*)
{
    return supportsRichSourceInfo;
}

const ClassInfo GlobalObject::s_info = { "global"_s, &JSGlobalObject::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(GlobalObject) };
const GlobalObjectMethodTable GlobalObject::s_globalObjectMethodTable = {
    &shellSupportsRichSourceInfo,
    &shouldInterruptScript,
    &javaScriptRuntimeFlags,
    nullptr, // queueMicrotaskToEventLoop
    &shouldInterruptScriptBeforeTimeout,
    &moduleLoaderImportModule,
    &moduleLoaderResolve,
    &moduleLoaderFetch,
    &moduleLoaderCreateImportMetaProperties,
    nullptr, // moduleLoaderEvaluate
#if ENABLE(FUZZILLI)
    &promiseRejectionTracker,
#else
    nullptr,
#endif
    &reportUncaughtExceptionAtEventLoop,
    &currentScriptExecutionOwner,
    &scriptExecutionStatus,
    &reportViolationForUnsafeEval,
    nullptr, // defaultLanguage
    nullptr, // compileStreaming
    nullptr, // instantinateStreaming
    &deriveShadowRealmGlobalObject,
    &codeForEval,
    &canCompileStrings,
    &trustedScriptStructure,
};

GlobalObject::GlobalObject(VM& vm, Structure* structure)
    : JSGlobalObject(vm, structure, &s_globalObjectMethodTable)
{
}

JSC_DEFINE_CUSTOM_SETTER(testCustomAccessorSetter, (JSGlobalObject* lexicalGlobalObject, EncodedJSValue thisValue, EncodedJSValue encodedValue, PropertyName))
{
    RELEASE_ASSERT(JSValue::decode(thisValue).isCell());
    JSCell* thisCell = JSValue::decode(thisValue).asCell();
    RELEASE_ASSERT(thisCell->type() == GlobalProxyType);
    GlobalObject* thisObject = jsDynamicCast<GlobalObject*>(jsCast<JSGlobalProxy*>(thisCell)->target());
    RELEASE_ASSERT(thisObject);
    return GlobalObject::testCustomSetterImpl(lexicalGlobalObject, thisObject, encodedValue, "_testCustomAccessorSetter"_s);
}

JSC_DEFINE_CUSTOM_SETTER(testCustomValueSetter, (JSGlobalObject* lexicalGlobalObject, EncodedJSValue thisValue, EncodedJSValue encodedValue, PropertyName))
{
    RELEASE_ASSERT(JSValue::decode(thisValue).isCell());
    JSCell* thisCell = JSValue::decode(thisValue).asCell();
    GlobalObject* thisObject = jsDynamicCast<GlobalObject*>(thisCell);
    RELEASE_ASSERT(thisObject);
    return GlobalObject::testCustomSetterImpl(lexicalGlobalObject, thisObject, encodedValue, "_testCustomValueSetter"_s);
}

static char16_t pathSeparator()
{
#if OS(WINDOWS)
    return '\\';
#else
    return '/';
#endif
}

static URL currentWorkingDirectory()
{
#if OS(WINDOWS)
    // https://msdn.microsoft.com/en-us/library/windows/desktop/aa364934.aspx
    // https://msdn.microsoft.com/en-us/library/windows/desktop/aa365247.aspx#maxpath
    // The _MAX_PATH in Windows is 260. If the path of the current working directory is longer than that, _getcwd truncates the result.
    // And other I/O functions taking a path name also truncate it. To avoid this situation,
    //
    // (1). When opening the file in Windows for modules, we always use the abosolute path and add "\\?\" prefix to the path name.
    // (2). When retrieving the current working directory, use GetCurrentDirectory instead of _getcwd.
    //
    // In the path utility functions inside the JSC shell, we does not handle the UNC and UNCW including the network host name.
    DWORD bufferLength = ::GetCurrentDirectoryW(0, nullptr);
    if (!bufferLength)
        return { };
    // In Windows, wchar_t is the UTF-16LE.
    // https://msdn.microsoft.com/en-us/library/dd374081.aspx
    // https://msdn.microsoft.com/en-us/library/windows/desktop/ff381407.aspx
    Vector<wchar_t> buffer(bufferLength);
    DWORD lengthNotIncludingNull = ::GetCurrentDirectoryW(bufferLength, buffer.mutableSpan().data());
    String directoryString(buffer.span().data(), lengthNotIncludingNull);
    // We don't support network path like \\host\share\<path name>.
    if (directoryString.startsWith("\\\\"_s))
        return { };

#else
    Vector<char> buffer(PATH_MAX);
    if (!getcwd(buffer.mutableSpan().data(), PATH_MAX))
        return { };
    String directoryString = String::fromUTF8(buffer.span().data());
#endif
    if (directoryString.isEmpty())
        return { };

    // Add a trailing slash if needed so the URL resolves to a directory and not a file.
    if (directoryString[directoryString.length() - 1] != pathSeparator())
        directoryString = makeString(directoryString, pathSeparator());

    return URL::fileURLWithFileSystemPath(directoryString);
}

// FIXME: We may wish to support module specifiers beginning with a (back)slash on Windows. We could either:
// - align with V8 and SM:  treat '/foo' as './foo'
// - align with PowerShell: treat '/foo' as 'C:/foo'
static bool isAbsolutePath(StringView path)
{
#if OS(WINDOWS)
    // Just look for local drives like C:\.
    return path.length() > 2 && isASCIIAlpha(path[0]) && path[1] == ':' && (path[2] == '\\' || path[2] == '/');
#else
    return path.startsWith('/');
#endif
}

static bool isDottedRelativePath(StringView path)
{
#if OS(WINDOWS)
    auto length = path.length();
    if (length < 2 || path[0] != '.')
        return false;

    if (path[1] == '/' || path[1] == '\\')
        return true;

    return length > 2 && path[1] == '.' && (path[2] == '/' || path[2] == '\\');
#else
    return path.startsWith("./"_s) || path.startsWith("../"_s);
#endif
}

static URL absoluteFileURL(const String& fileName)
{
    if (isAbsolutePath(fileName))
        return URL::fileURLWithFileSystemPath(fileName);

    auto directoryName = currentWorkingDirectory();
    if (!directoryName.isValid())
        return URL::fileURLWithFileSystemPath(fileName);

    return URL(directoryName, fileName);
}

JSInternalPromise* GlobalObject::moduleLoaderImportModule(JSGlobalObject* globalObject, JSModuleLoader*, JSString* moduleNameValue, JSValue parameters, const SourceOrigin& sourceOrigin)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    auto* promise = JSInternalPromise::create(vm, globalObject->internalPromiseStructure());

    auto rejectWithError = [&](JSValue error) {
        promise->reject(globalObject, error);
        return promise;
    };

    auto referrer = sourceOrigin.url();
    auto specifier = moduleNameValue->value(globalObject);
    RETURN_IF_EXCEPTION(scope, promise->rejectWithCaughtException(globalObject, scope));

    if (!referrer.protocolIsFile())
        RELEASE_AND_RETURN(scope, rejectWithError(createError(globalObject, makeString("Could not resolve the referrer's path '"_s, referrer.string(), "', while trying to resolve module '"_s, specifier.data, "'."_s))));

    auto attributes = JSC::retrieveImportAttributesFromDynamicImportOptions(globalObject, parameters, { vm.propertyNames->type.impl() });
    RETURN_IF_EXCEPTION(scope, promise->rejectWithCaughtException(globalObject, scope));

    auto type = JSC::retrieveTypeImportAttribute(globalObject, attributes);
    RETURN_IF_EXCEPTION(scope, promise->rejectWithCaughtException(globalObject, scope));

    parameters = jsUndefined();
    if (type)
        parameters = JSScriptFetchParameters::create(vm, ScriptFetchParameters::create(type.value()));

    auto result = JSC::importModule(globalObject, Identifier::fromString(vm, specifier), jsString(vm, referrer.string()), parameters, jsUndefined());
    RETURN_IF_EXCEPTION(scope, promise->rejectWithCaughtException(globalObject, scope));

    return result;
}

Identifier GlobalObject::moduleLoaderResolve(JSGlobalObject* globalObject, JSModuleLoader*, JSValue keyValue, JSValue referrerValue, JSValue)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    scope.releaseAssertNoException();
    const Identifier key = keyValue.toPropertyKey(globalObject);
    RETURN_IF_EXCEPTION(scope, { });

    if (key.isSymbol())
        return key;

    auto resolvePath = [&] (const URL& directoryURL) -> Identifier {
        String specifier = key.impl();
        bool specifierIsAbsolute = isAbsolutePath(specifier);
        if (!specifierIsAbsolute && !isDottedRelativePath(specifier)) {
            throwTypeError(globalObject, scope, makeString("Module specifier, '"_s, specifier, "' is not absolute and does not start with \"./\" or \"../\". Referenced from: "_s, directoryURL.fileSystemPath()));
            return { };
        }

        if (!directoryURL.protocolIsFile()) {
            throwException(globalObject, scope, createError(globalObject, makeString("Could not resolve the referrer's path: "_s, directoryURL.string())));
            return { };
        }

        auto resolvedURL = specifierIsAbsolute ? URL::fileURLWithFileSystemPath(specifier) : URL(directoryURL, specifier);
        if (!resolvedURL.isValid()) {
            throwException(globalObject, scope, createError(globalObject, makeString("Resolved module url is not valid: "_s, resolvedURL.string())));
            return { };
        }
        ASSERT(resolvedURL.protocolIsFile());

        return Identifier::fromString(vm, resolvedURL.string());
    };

    if (referrerValue.isUndefined())
        return resolvePath(currentWorkingDirectory());

    const Identifier referrer = referrerValue.toPropertyKey(globalObject);
    RETURN_IF_EXCEPTION(scope, { });

    if (referrer.isSymbol())
        return resolvePath(currentWorkingDirectory());

    // If the referrer exists, we assume that the referrer is the correct file url.
    URL url = URL({ }, referrer.impl());
    ASSERT(url.protocolIsFile());
    return resolvePath(url);
}

template<typename Vector>
static void convertShebangToJSComment(Vector& buffer)
{
    if (buffer.size() >= 2) {
        if (buffer[0] == '#' && buffer[1] == '!')
            buffer[0] = buffer[1] = '/';
    }
}

static RefPtr<Uint8Array> fillBufferWithContentsOfFile(FILE* file)
{
    struct stat statBuf;
    if (fstat(fileno(file), &statBuf) == -1)
        return nullptr;
    if ((statBuf.st_mode & S_IFMT) != S_IFREG)
        return nullptr;
    if (fseek(file, 0, SEEK_END) == -1)
        return nullptr;
    long bufferCapacity = ftell(file);
    if (bufferCapacity == -1)
        return nullptr;
    if (fseek(file, 0, SEEK_SET) == -1)
        return nullptr;
    auto result = Uint8Array::tryCreate(bufferCapacity);
    if (!result)
        return nullptr;
    size_t readSize = fread(result->data(), 1, bufferCapacity, file);
    if (readSize != static_cast<size_t>(bufferCapacity))
        return nullptr;
    return result;
}

static RefPtr<Uint8Array> fillBufferWithContentsOfFile(const String& fileName)
{
    FILE* f = fopen(fileName.utf8().data(), "rb");
    if (!f) {
        fprintf(stderr, "Could not open file: %s\n", fileName.utf8().data());
        return nullptr;
    }

    RefPtr<Uint8Array> result = fillBufferWithContentsOfFile(f);
    fclose(f);

    return result;
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

template<typename Vector>
static bool fillBufferWithContentsOfFile(FILE* file, Vector& buffer)
{
    // We might have injected "use strict"; at the top.
    size_t initialSize = buffer.size();
    if (fseek(file, 0, SEEK_END) == -1)
        return false;
    long bufferCapacity = ftell(file);
    if (bufferCapacity == -1)
        return false;
    if (fseek(file, 0, SEEK_SET) == -1)
        return false;
    buffer.grow(bufferCapacity + initialSize);
    auto span = buffer.mutableSpan().subspan(initialSize);
    size_t readSize = fread(span.data(), 1, span.size(), file);
    return readSize == buffer.size() - initialSize;
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

static bool fillBufferWithContentsOfFile(const String& fileName, Vector<char>& buffer)
{
    struct stat statBuf;
    auto fileNameUTF = fileName.tryGetUTF8();
    if (!fileNameUTF.has_value()) {
        fprintf(stderr, "Error when parsing file name: %s\n", fileName.ascii().data());
        return false;
    }
    if (stat(fileNameUTF->data(), &statBuf) == -1) {
        fprintf(stderr, "Could not open file: %s\n", fileNameUTF->data());
        return false;
    }

    if ((statBuf.st_mode & S_IFMT) != S_IFREG) {
        fprintf(stderr, "Trying to open a non-file: %s\n", fileNameUTF->data());
        return false;
    }
    auto* f = fopen(fileNameUTF->data(), "rb");
    if (!f) {
        fprintf(stderr, "Could not open file: %s\n", fileNameUTF->data());
        return false;
    }

    bool result = fillBufferWithContentsOfFile(f, buffer);
    fclose(f);

    return result;
}

static bool fetchScriptFromLocalFileSystem(const String& fileName, Vector<char>& buffer)
{
    if (!fillBufferWithContentsOfFile(fileName, buffer))
        return false;
    convertShebangToJSComment(buffer);
    return true;
}

class ShellSourceProvider final : public StringSourceProvider {
public:
    static Ref<ShellSourceProvider> create(const String& source, const SourceOrigin& sourceOrigin, String&& sourceURL, const TextPosition& startPosition, SourceProviderSourceType sourceType)
    {
        return adoptRef(*new ShellSourceProvider(source, sourceOrigin, WTFMove(sourceURL), startPosition, sourceType));
    }

    ~ShellSourceProvider() final
    {
        commitCachedBytecode();
    }

    RefPtr<CachedBytecode> cachedBytecode() const final
    {
        if (!m_cachedBytecode)
            loadBytecode();
        return m_cachedBytecode.copyRef();
    }

    void updateCache(const UnlinkedFunctionExecutable* executable, const SourceCode&, CodeSpecializationKind kind, const UnlinkedFunctionCodeBlock* codeBlock) const final
    {
        if (!cacheEnabled() || !m_cachedBytecode)
            return;
        BytecodeCacheError error;
        RefPtr<CachedBytecode> cachedBytecode = encodeFunctionCodeBlock(executable->vm(), codeBlock, error);
        if (cachedBytecode && !error.isValid())
            m_cachedBytecode->addFunctionUpdate(executable, kind, *cachedBytecode);
    }

    void cacheBytecode(const BytecodeCacheGenerator& generator) const final
    {
        if (!cacheEnabled())
            return;
        if (!m_cachedBytecode)
            m_cachedBytecode = CachedBytecode::create();
        auto update = generator();
        if (update)
            m_cachedBytecode->addGlobalUpdate(*update);
    }

    void commitCachedBytecode() const final
    {
        if (!cacheEnabled() || !m_cachedBytecode || !m_cachedBytecode->hasUpdates())
            return;

        auto clearBytecode = makeScopeExit([&] {
            m_cachedBytecode = nullptr;
        });

        String filename = cachePath();
        auto handle = FileSystem::openFile(filename, FileSystem::FileOpenMode::ReadWrite, FileSystem::FileAccessPermission::All, { FileSystem::FileLockMode::Exclusive, FileSystem::FileLockMode::Nonblocking });
        if (!handle)
            return;

        auto fileSize = handle.size();
        if (!fileSize)
            return;

        size_t cacheFileSize;
        if (!WTF::convertSafely(*fileSize, cacheFileSize) || cacheFileSize != m_cachedBytecode->size()) {
            // The bytecode cache has already been updated
            return;
        }

        if (!handle.truncate(m_cachedBytecode->sizeForUpdate()))
            return;

        m_cachedBytecode->commitUpdates([&] (off_t offset, std::span<const uint8_t> data) {
            auto result = handle.seek(offset, FileSystem::FileSeekOrigin::Beginning);
            ASSERT_UNUSED(result, !!result);
            auto bytesWritten = handle.write(data);
            ASSERT_UNUSED(bytesWritten, bytesWritten == data.size());
        });
    }

private:
    String cachePath() const
    {
        if (!cacheEnabled())
            return { };
        const char* cachePath = Options::diskCachePath();
        String filename = FileSystem::encodeForFileName(FileSystem::lastComponentOfPathIgnoringTrailingSlash(sourceOrigin().url().fileSystemPath()));
        return FileSystem::pathByAppendingComponent(StringView::fromLatin1(cachePath), makeString(source().hash(), '-', filename, ".bytecode-cache"_s));
    }

    void loadBytecode() const
    {
        if (!cacheEnabled())
            return;

        String filename = cachePath();
        if (filename.isNull())
            return;

        auto handle = FileSystem::openFile(filename, FileSystem::FileOpenMode::Read, FileSystem::FileAccessPermission::All, { FileSystem::FileLockMode::Shared, FileSystem::FileLockMode::Nonblocking });
        if (!handle)
            return;

        auto mappedFileData = handle.map(FileSystem::MappedFileMode::Private);
        if (!mappedFileData)
            return;

        m_cachedBytecode = CachedBytecode::create(WTFMove(*mappedFileData));
    }

    ShellSourceProvider(const String& source, const SourceOrigin& sourceOrigin, String&& sourceURL, const TextPosition& startPosition, SourceProviderSourceType sourceType)
        : StringSourceProvider(source, sourceOrigin, SourceTaintedOrigin::Untainted, WTFMove(sourceURL), startPosition, sourceType)
        // Workers started via $.agent.start are not shut down in a synchronous manner, and it
        // is possible the main thread terminates the process while a worker is writing its
        // bytecode cache, which results in intermittent test failures. As $.agent.start is only
        // a rarely used testing facility, we simply do not cache bytecode on these threads.
        , m_cacheEnabled(Worker::current().isMain() && !!Options::diskCachePath())
    {
    }

    bool cacheEnabled() const { return m_cacheEnabled; }

    mutable RefPtr<CachedBytecode> m_cachedBytecode;
    const bool m_cacheEnabled;
};

static inline SourceCode jscSource(const String& source, const SourceOrigin& sourceOrigin, String sourceURL = String(), const TextPosition& startPosition = TextPosition(), SourceProviderSourceType sourceType = SourceProviderSourceType::Program)
{
    return SourceCode(ShellSourceProvider::create(source, sourceOrigin, WTFMove(sourceURL), startPosition, sourceType), startPosition.m_line.oneBasedInt(), startPosition.m_column.oneBasedInt());
}

template<typename Vector>
static inline SourceCode jscSource(const Vector& utf8, const SourceOrigin& sourceOrigin, const String& filename)
{
    // FIXME: This should use an absolute file URL https://bugs.webkit.org/show_bug.cgi?id=193077
    String str = stringFromUTF(utf8);
    return jscSource(str, sourceOrigin, filename);
}

template<typename Vector>
static bool fetchModuleFromLocalFileSystem(const URL& fileURL, Vector& buffer)
{
    String fileName = fileURL.fileSystemPath();
#if OS(WINDOWS)
    // https://msdn.microsoft.com/en-us/library/windows/desktop/aa365247.aspx#maxpath
    // Use long UNC to pass the long path name to the Windows APIs.
    // These also appear to turn off handling forward slashes as
    // directory separators as it disables all string parsing on names.
    fileName = makeStringByReplacingAll(fileName, '/', '\\');
    auto pathName = makeString("\\\\?\\"_s, fileName).wideCharacters();
    struct _stat status { };
    if (_wstat(pathName.span().data(), &status))
        return false;
    if ((status.st_mode & S_IFMT) != S_IFREG)
        return false;

    FILE* f = _wfopen(pathName.span().data(), L"rb");
#else
    auto pathName = fileName.utf8();
    struct stat status { };
    if (stat(pathName.data(), &status))
        return false;
    if ((status.st_mode & S_IFMT) != S_IFREG)
        return false;

    FILE* f = fopen(pathName.data(), "r");
#endif
    if (!f) {
        fprintf(stderr, "Could not open file: %s\n", fileName.utf8().data());
        return false;
    }

    bool result = fillBufferWithContentsOfFile(f, buffer);
    if (result)
        convertShebangToJSComment(buffer);
    fclose(f);

    return result;
}

JSInternalPromise* GlobalObject::moduleLoaderFetch(JSGlobalObject* globalObject, JSModuleLoader*, JSValue key, JSValue attributesValue, JSValue)
{
    VM& vm = globalObject->vm();
    JSInternalPromise* promise = JSInternalPromise::create(vm, globalObject->internalPromiseStructure());

    auto scope = DECLARE_THROW_SCOPE(vm);

    auto rejectWithError = [&](JSValue error) {
        promise->reject(globalObject, error);
        return promise;
    };

    String moduleKey = key.toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, promise->rejectWithCaughtException(globalObject, scope));

    URL moduleURL({ }, moduleKey);
    ASSERT(moduleURL.protocolIsFile());
    // Strip the URI from our key so Errors print canonical system paths.
    moduleKey = moduleURL.fileSystemPath();

    RefPtr<ScriptFetchParameters> attributes;
    if (auto* value = jsDynamicCast<JSScriptFetchParameters*>(attributesValue))
        attributes = &value->parameters();

    Vector<uint8_t> buffer;
    if (!fetchModuleFromLocalFileSystem(moduleURL, buffer))
        RELEASE_AND_RETURN(scope, rejectWithError(createError(globalObject, makeString("Could not open file '"_s, moduleKey, "'."_s))));

#if ENABLE(WEBASSEMBLY)
    // FileSystem does not have mime-type header. The JSC shell recognizes WebAssembly's magic header.
    if ((buffer.size() >= 4 && buffer[0] == '\0' && buffer[1] == 'a' && buffer[2] == 's' && buffer[3] == 'm') || (attributes && attributes->type() == ScriptFetchParameters::Type::WebAssembly)) {
        auto source = SourceCode(WebAssemblySourceProvider::create(WTFMove(buffer), SourceOrigin { moduleURL }, WTFMove(moduleKey)));
        auto sourceCode = JSSourceCode::create(vm, WTFMove(source));
        scope.release();
        promise->resolve(globalObject, sourceCode);
        return promise;
    }
#endif

    if (attributes && attributes->type() == ScriptFetchParameters::Type::JSON) {
        auto source = SourceCode(StringSourceProvider::create(stringFromUTF(buffer), SourceOrigin { moduleURL }, WTFMove(moduleKey), SourceTaintedOrigin::Untainted, TextPosition(), SourceProviderSourceType::JSON));
        auto sourceCode = JSSourceCode::create(vm, WTFMove(source));
        scope.release();
        promise->resolve(globalObject, sourceCode);
        return promise;
    }

    auto sourceCode = JSSourceCode::create(vm, jscSource(stringFromUTF(buffer), SourceOrigin { moduleURL }, WTFMove(moduleKey), TextPosition(), SourceProviderSourceType::Module));
    scope.release();
    promise->resolve(globalObject, sourceCode);
    return promise;
}

JSObject* GlobalObject::moduleLoaderCreateImportMetaProperties(JSGlobalObject* globalObject, JSModuleLoader*, JSValue key, JSModuleRecord*, JSValue)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSObject* metaProperties = constructEmptyObject(vm, globalObject->nullPrototypeObjectStructure());
    RETURN_IF_EXCEPTION(scope, nullptr);

    metaProperties->putDirect(vm, Identifier::fromString(vm, "filename"_s), key);
    RETURN_IF_EXCEPTION(scope, nullptr);

    metaProperties->putDirect(vm, JSC::Identifier::fromString(vm, "url"_s), key);
    RETURN_IF_EXCEPTION(scope, nullptr);

    return metaProperties;
}

#if ENABLE(FUZZILLI)

void GlobalObject::promiseRejectionTracker(JSGlobalObject*, JSPromise*, JSPromiseRejectionOperation operation)
{
    switch (operation) {
    case JSPromiseRejectionOperation::Reject:
        Fuzzilli::numPendingRejectedPromises += 1;
        break;
    case JSPromiseRejectionOperation::Handle:
        Fuzzilli::numPendingRejectedPromises -= 1;
        break;
    }
}

#endif // ENABLE(FUZZILLI)

static CString toCString(JSGlobalObject* globalObject, ThrowScope& scope, Expected<CString, UTF8ConversionError> expectedString)
{
    if (expectedString)
        return expectedString.value();
    switch (expectedString.error()) {
    case UTF8ConversionError::OutOfMemory:
        throwOutOfMemoryError(globalObject, scope);
        return { };
    case UTF8ConversionError::Invalid:
        scope.throwException(globalObject, createError(globalObject, "Illegal source encountered during UTF8 conversion"_s));
        return { };
    }
    RELEASE_ASSERT_NOT_REACHED();
    return { };
}

template<typename T> static CString toCString(JSGlobalObject* globalObject, ThrowScope& scope, T& string)
{
    return toCString(globalObject, scope, string.tryGetUTF8());
}

static EncodedJSValue printInternal(JSGlobalObject* globalObject, CallFrame* callFrame, FILE* out, bool pretty)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (asyncTestExpectedPasses) {
        JSValue value = callFrame->argument(0);
        if (value.isString() && WTF::equal(asString(value)->value(globalObject).data.impl(), "Test262:AsyncTestComplete"_s)) {
            asyncTestPasses++;
            return JSValue::encode(jsUndefined());
        }
    }

    for (unsigned i = 0; i < callFrame->argumentCount(); ++i) {
        if (i)
            if (EOF == fputc(' ', out))
                goto fail;

        String string = pretty ? callFrame->uncheckedArgument(i).toWTFStringForConsole(globalObject) : callFrame->uncheckedArgument(i).toWTFString(globalObject);
        RETURN_IF_EXCEPTION(scope, { });
        auto cString = toCString(globalObject, scope, string);
        RETURN_IF_EXCEPTION(scope, { });
        fwrite(cString.data(), sizeof(char), cString.length(), out);
        if (ferror(out))
            goto fail;
    }

    fputc('\n', out);
fail:
    fflush(out);
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionPrintStdOut, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    return printInternal(globalObject, callFrame, stdout, false);
}

JSC_DEFINE_HOST_FUNCTION(functionPrintStdErr, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    return printInternal(globalObject, callFrame, stderr, false);
}

JSC_DEFINE_HOST_FUNCTION(functionPrettyPrint, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    return printInternal(globalObject, callFrame, stdout, true);
}

JSC_DEFINE_HOST_FUNCTION(functionAtob, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!callFrame->argumentCount())
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Missing input for atob."_s)));

    JSValue jsValue = callFrame->argument(0);
    if (jsValue.isUndefined())
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Invalid character in argument for atob."_s)));

    String encodedString = jsValue.toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, { });

    if (encodedString.isNull())
        return JSValue::encode(jsEmptyString(vm));

    auto decodedString = base64DecodeToString(encodedString, { Base64DecodeOption::ValidatePadding, Base64DecodeOption::IgnoreWhitespace });
    if (decodedString.isNull())
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Invalid character in argument for atob."_s)));

    return JSValue::encode(jsString(vm, decodedString));
}

JSC_DEFINE_HOST_FUNCTION(functionBtoa, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!callFrame->argumentCount())
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Missing input for btoa."_s)));

    String stringToEncode = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, { });

    if (stringToEncode.isNull())
        return JSValue::encode(jsEmptyString(vm));

    if (!stringToEncode.containsOnlyLatin1())
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Invalid character in argument for btoa."_s)));

    String encodedString = base64EncodeToStringReturnNullIfOverflow(stringToEncode.latin1());
    if (!encodedString)
        return JSValue::encode(throwException(globalObject, scope, createOutOfMemoryError(globalObject)));

    return JSValue::encode(jsString(vm, encodedString));
}

JSC_DEFINE_HOST_FUNCTION(functionDisassembleBase64, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!callFrame->argumentCount())
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Missing input for disassembleBase64."_s)));

    String encodedString = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, { });

    if (encodedString.isNull())
        return JSValue::encode(jsEmptyString(vm));

    std::optional<Vector<uint8_t>> decodedVector = base64Decode(encodedString, { Base64DecodeOption::ValidatePadding, Base64DecodeOption::IgnoreWhitespace });
    if (!decodedVector)
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Invalid character in base64 string argument."_s)));

    auto code = CodePtr<DisassemblyPtrTag>::fromUntaggedPtr(decodedVector->mutableSpan().data());
    StringPrintStream out;
    // prefix with "\n" so the first line has the same whitespace as the rest when displayed from jsc.
    out.print("\n");
    if (tryToDisassemble(code, decodedVector->sizeInBytes(), "", out))
        return JSValue::encode(jsString(vm, out.toString()));

    return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Couldn't disassemble."_s)));
}

JSC_DEFINE_HOST_FUNCTION(functionDebug, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    auto* jsString = callFrame->argument(0).toString(globalObject);
    RETURN_IF_EXCEPTION(scope, { });
    auto view = jsString->view(globalObject);
    RETURN_IF_EXCEPTION(scope, { });
    auto string = toCString(globalObject, scope, view.data);
    RETURN_IF_EXCEPTION(scope, { });
    fputs("--> ", stderr);
    fwrite(string.data(), sizeof(char), string.length(), stderr);
    fputc('\n', stderr);
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionDescribe, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    if (callFrame->argumentCount() < 1)
        return JSValue::encode(jsUndefined());
    return JSValue::encode(jsString(vm, toString(callFrame->argument(0))));
}

JSC_DEFINE_HOST_FUNCTION(functionDescribeArray, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    if (callFrame->argumentCount() < 1)
        return JSValue::encode(jsUndefined());
    VM& vm = globalObject->vm();
    JSObject* object = jsDynamicCast<JSObject*>(callFrame->argument(0));
    if (!object)
        return JSValue::encode(jsNontrivialString(vm, "<not object>"_s));
    return JSValue::encode(jsNontrivialString(vm, toString("<Butterfly: ", RawPointer(object->butterfly()), "; public length: ", object->getArrayLength(), "; vector length: ", object->getVectorLength(), ">")));
}

JSC_DEFINE_HOST_FUNCTION(functionSleepSeconds, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (callFrame->argumentCount() >= 1) {
        Seconds seconds = Seconds(callFrame->argument(0).toNumber(globalObject));
        RETURN_IF_EXCEPTION(scope, encodedJSValue());
        sleep(seconds);
    }
    
    return JSValue::encode(jsUndefined());
}

class FunctionJSCStackFunctor {
public:
    FunctionJSCStackFunctor(StringBuilder& trace)
        : m_trace(trace)
    {
    }

    IterationStatus operator()(StackVisitor& visitor) const
    {
        m_trace.append(makeString("    "_s, visitor->index(), "   "_s, visitor->toString(), '\n'));
        return IterationStatus::Continue;
    }

private:
    StringBuilder& m_trace;
};

JSC_DEFINE_HOST_FUNCTION(functionJSCStack, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    StringBuilder trace;
    trace.append("--> Stack trace:\n"_s);

    FunctionJSCStackFunctor functor(trace);
    StackVisitor::visit(callFrame, vm, functor);
    fprintf(stderr, "%s", trace.toString().utf8().data());
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionGCAndSweep, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    JSLockHolder lock(vm);
    vm.heap.collectNow(Sync, CollectionScope::Full);
    return JSValue::encode(jsNumber(vm.heap.sizeAfterLastFullCollection()));
}

JSC_DEFINE_HOST_FUNCTION(functionFullGC, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    JSLockHolder lock(vm);
    vm.heap.collectSync(CollectionScope::Full);
    return JSValue::encode(jsNumber(vm.heap.sizeAfterLastFullCollection()));
}

JSC_DEFINE_HOST_FUNCTION(functionEdenGC, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    JSLockHolder lock(vm);
    vm.heap.collectSync(CollectionScope::Eden);
    return JSValue::encode(jsNumber(vm.heap.sizeAfterLastEdenCollection()));
}

JSC_DEFINE_HOST_FUNCTION(functionHeapSize, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    JSLockHolder lock(vm);
    return JSValue::encode(jsNumber(vm.heap.size()));
}

class JSCMemoryFootprint : public JSDestructibleObject {
    using Base = JSDestructibleObject;
public:
    template<typename CellType, SubspaceAccess>
    static CompleteSubspace* subspaceFor(VM& vm)
    {
        return &vm.destructibleObjectSpace();
    }

    JSCMemoryFootprint(VM& vm, Structure* structure)
        : Base(vm, structure)
    { }

    static Structure* createStructure(VM& vm, JSGlobalObject* globalObject, JSValue prototype)
    {
        return Structure::create(vm, globalObject, prototype, TypeInfo(ObjectType, StructureFlags), info());
    }

    static JSCMemoryFootprint* create(VM& vm, JSGlobalObject* globalObject)
    {
        Structure* structure = createStructure(vm, globalObject, jsNull());
        JSCMemoryFootprint* footprint = new (NotNull, allocateCell<JSCMemoryFootprint>(vm)) JSCMemoryFootprint(vm, structure);
        footprint->finishCreation(vm);
        return footprint;
    }

    void finishCreation(VM& vm)
    {
        Base::finishCreation(vm);

        auto addProperty = [&] (VM& vm, ASCIILiteral name, JSValue value) {
            JSCMemoryFootprint::addProperty(vm, name, value);
        };

        MemoryFootprint footprint = MemoryFootprint::now();

        addProperty(vm, "current"_s, jsNumber(footprint.current));
        addProperty(vm, "peak"_s, jsNumber(footprint.peak));
    }

    DECLARE_INFO;

private:
    void addProperty(VM& vm, ASCIILiteral name, JSValue value)
    {
        Identifier identifier = Identifier::fromString(vm, name);
        putDirect(vm, identifier, value);
    }
};

const ClassInfo JSCMemoryFootprint::s_info = { "MemoryFootprint"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSCMemoryFootprint) };

JSC_DEFINE_HOST_FUNCTION(functionMemoryUsageStatistics, (JSGlobalObject* globalObject, CallFrame*))
{
    auto contextRef = toRef(globalObject);
    return JSValue::encode(toJS(JSGetMemoryUsageStatistics(contextRef)));
}

JSC_DEFINE_HOST_FUNCTION(functionCreateMemoryFootprint, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    JSLockHolder lock(vm);
    return JSValue::encode(JSCMemoryFootprint::create(vm, globalObject));
}

JSC_DEFINE_HOST_FUNCTION(functionResetMemoryPeak, (JSGlobalObject*, CallFrame*))
{
    MemoryFootprint::resetPeak();
    return JSValue::encode(jsUndefined());
}

// This function is not generally very helpful in 64-bit code as the tag and payload
// share a register. But in 32-bit JITed code the tag may not be checked if an
// optimization removes type checking requirements, such as in ===.
JSC_DEFINE_HOST_FUNCTION(functionAddressOf, (JSGlobalObject*, CallFrame* callFrame))
{
    JSValue value = callFrame->argument(0);
    if (!value.isCell())
        return JSValue::encode(jsUndefined());
    // Need to cast to uint64_t so std::bit_cast will play along.
    uint64_t asNumber = std::bit_cast<uintptr_t>(value.asCell());
    EncodedJSValue returnValue = JSValue::encode(jsNumber(std::bit_cast<double>(asNumber)));
    return returnValue;
}

JSC_DEFINE_HOST_FUNCTION(functionVersion, (JSGlobalObject*, CallFrame*))
{
    // We need this function for compatibility with the Mozilla JS tests but for now
    // we don't actually do any version-specific handling
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionRun, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    String fileName = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    Vector<char> script;
    if (!fetchScriptFromLocalFileSystem(fileName, script))
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Could not open file."_s)));

    GlobalObject* realm = GlobalObject::create(vm, GlobalObject::createStructure(vm, jsNull()), Vector<String>());

    JSArray* array = constructEmptyArray(realm, nullptr);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    for (unsigned i = 1; i < callFrame->argumentCount(); ++i) {
        array->putDirectIndex(realm, i - 1, callFrame->uncheckedArgument(i));
        RETURN_IF_EXCEPTION(scope, encodedJSValue());
    }
    realm->putDirect(vm, Identifier::fromString(vm, "arguments"_s), array);

    NakedPtr<Exception> exception;
    StopWatch stopWatch;
    stopWatch.start();
    evaluate(realm, jscSource(script, SourceOrigin { absoluteFileURL(fileName) }, fileName), JSValue(), exception);
    stopWatch.stop();

    if (exception) {
        if (vm.isTerminationException(exception.get()))
            vm.setExecutionForbidden();
        throwException(realm, scope, exception);
        return JSValue::encode(jsUndefined());
    }
    
    return JSValue::encode(jsNumber(stopWatch.getElapsedMS()));
}

JSC_DEFINE_HOST_FUNCTION(functionRunString, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    String source = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());

    GlobalObject* realm = GlobalObject::create(vm, GlobalObject::createStructure(vm, jsNull()), Vector<String>());

    JSArray* array = constructEmptyArray(realm, nullptr);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    for (unsigned i = 1; i < callFrame->argumentCount(); ++i) {
        array->putDirectIndex(realm, i - 1, callFrame->uncheckedArgument(i));
        RETURN_IF_EXCEPTION(scope, encodedJSValue());
    }
    realm->putDirect(vm, Identifier::fromString(vm, "arguments"_s), array);

    NakedPtr<Exception> exception;
    {
        SourceCode code = jscSource(source, callFrame->callerSourceOrigin(vm), String(), TextPosition(), SourceProviderSourceType::Program);
        evaluate(realm, code, JSValue(), exception);
    }

    if (exception) {
        if (vm.isTerminationException(exception.get()))
            vm.setExecutionForbidden();
        scope.throwException(realm, exception);
        return JSValue::encode(jsUndefined());
    }
    
    return JSValue::encode(realm->globalThis());
}

static URL computeFilePath(VM& vm, JSGlobalObject* globalObject, CallFrame* callFrame)
{
    auto scope = DECLARE_THROW_SCOPE(vm);

    bool callerRelative = callFrame->argument(1).getString(globalObject) == "caller relative"_s;
    RETURN_IF_EXCEPTION(scope, URL());

    String fileName = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, URL());

    URL path;
    if (callerRelative) {
        path = URL(callFrame->callerSourceOrigin(vm).url(), fileName);
        if (!path.protocolIsFile()) {
            throwException(globalObject, scope, createURIError(globalObject, makeString("caller relative URL path is not a local file: "_s, path.string())));
            return URL();
        }
    } else
        path = absoluteFileURL(fileName);
    return path;
}

JSC_DEFINE_HOST_FUNCTION(functionLoad, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    URL path = computeFilePath(vm, globalObject, callFrame);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());

    Vector<char> script;
    if (!fetchScriptFromLocalFileSystem(path.fileSystemPath(), script))
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Could not open file."_s)));

    NakedPtr<Exception> evaluationException;
    JSValue result = evaluate(globalObject, jscSource(script, SourceOrigin { path }, path.fileSystemPath()), JSValue(), evaluationException);
    if (evaluationException) {
        if (vm.isTerminationException(evaluationException.get()))
            vm.setExecutionForbidden();
        throwException(globalObject, scope, evaluationException);
    }
    return JSValue::encode(result);
}

JSC_DEFINE_HOST_FUNCTION(functionLoadString, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    String sourceCode = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());

    NakedPtr<Exception> evaluationException;
    JSValue result = evaluate(globalObject, jscSource(sourceCode, callFrame->callerSourceOrigin(vm)), JSValue(), evaluationException);
    if (evaluationException) {
        if (vm.isTerminationException(evaluationException.get()))
            vm.setExecutionForbidden();
        throwException(globalObject, scope, evaluationException);
    }
    return JSValue::encode(result);
}

JSC_DEFINE_HOST_FUNCTION(functionReadFile, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    bool isBinary = false;
    if (callFrame->argumentCount() > 1) {
        String type = callFrame->argument(1).toWTFString(globalObject);
        RETURN_IF_EXCEPTION(scope, encodedJSValue());
        if (type == "binary"_s)
            isBinary = true;
        else if (type != "caller relative"_s)
            return throwVMError(globalObject, scope, "Expected 'binary' or 'caller relative' as second argument."_s);
    }

    URL filePath = computeFilePath(vm, globalObject, callFrame);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());

    RefPtr<Uint8Array> content = fillBufferWithContentsOfFile(filePath.fileSystemPath());
    if (!content)
        return throwVMError(globalObject, scope, makeString("Could not open file: "_s, filePath.fileSystemPath()));

    if (!isBinary)
        return JSValue::encode(jsString(vm, String::fromUTF8WithLatin1Fallback(content->span())));

    Structure* structure = globalObject->typedArrayStructure(TypeUint8, content->isResizableOrGrowableShared());
    JSObject* result = JSUint8Array::create(vm, structure, WTFMove(content));
    RETURN_IF_EXCEPTION(scope, encodedJSValue());

    return JSValue::encode(result);
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

JSC_DEFINE_HOST_FUNCTION(functionWriteFile, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    String fileName = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, { });

    Variant<String, std::span<const uint8_t>> data;
    JSValue dataValue = callFrame->argument(1);

    if (dataValue.isString()) {
        data = asString(dataValue)->value(globalObject);
        RETURN_IF_EXCEPTION(scope, { });
    } else if (dataValue.inherits<JSArrayBuffer>()) {
        auto* arrayBuffer = jsCast<JSArrayBuffer*>(dataValue);
        if (arrayBuffer->impl()->isDetached())
            data = emptyString();
        else
            data = std::span(static_cast<const uint8_t*>(arrayBuffer->impl()->data()), arrayBuffer->impl()->byteLength());
    } else if (dataValue.inherits<JSArrayBufferView>()) {
        auto* view = jsCast<JSArrayBufferView*>(dataValue);
        if (view->isDetached())
            data = emptyString();
        else
            data = std::span(static_cast<const uint8_t*>(view->vector()), view->byteLength());
    } else {
        data = dataValue.toWTFString(globalObject);
        RETURN_IF_EXCEPTION(scope, { });
    }

    auto handle = FileSystem::openFile(fileName, FileSystem::FileOpenMode::Truncate);
    if (!handle)
        return throwVMError(globalObject, scope, "Could not open file."_s);

    auto size = WTF::visit(WTF::makeVisitor([&](const String& string) {
        CString utf8 = string.utf8();
        return handle.write(byteCast<uint8_t>(utf8.span()));
    }, [&] (const std::span<const uint8_t>& data) {
        return handle.write(data);
    }), data);

    if (!size)
        return JSValue::encode(jsNumber(-1));
    return JSValue::encode(jsNumber(*size));
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

JSC_DEFINE_HOST_FUNCTION(functionCheckSyntax, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    String fileName = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    Vector<char> script;
    if (!fetchScriptFromLocalFileSystem(fileName, script))
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Could not open file."_s)));

    StopWatch stopWatch;
    stopWatch.start();

    JSValue syntaxException;
    bool validSyntax = checkSyntax(globalObject, jscSource(script, SourceOrigin { absoluteFileURL(fileName) }, fileName), &syntaxException);
    stopWatch.stop();

    if (!validSyntax)
        throwException(globalObject, scope, syntaxException);
    return JSValue::encode(jsNumber(stopWatch.getElapsedMS()));
}

#if ENABLE(SAMPLING_FLAGS)
JSC_DEFINE_HOST_FUNCTION(functionSetSamplingFlags, (JSGlobalObject*, CallFrame* callFrame))
{
    for (unsigned i = 0; i < callFrame->argumentCount(); ++i) {
        unsigned flag = static_cast<unsigned>(callFrame->uncheckedArgument(i).toNumber(globalObject));
        if ((flag >= 1) && (flag <= 32))
            SamplingFlags::setFlag(flag);
    }
    return JSValue::encode(jsNull());
}

JSC_DEFINE_HOST_FUNCTION(functionClearSamplingFlags, (JSGlobalObject*, CallFrame* callFrame))
{
    for (unsigned i = 0; i < callFrame->argumentCount(); ++i) {
        unsigned flag = static_cast<unsigned>(callFrame->uncheckedArgument(i).toNumber(globalObject));
        if ((flag >= 1) && (flag <= 32))
            SamplingFlags::clearFlag(flag);
    }
    return JSValue::encode(jsNull());
}
#endif

JSC_DEFINE_HOST_FUNCTION(functionGetRandomSeed, (JSGlobalObject* globalObject, CallFrame*))
{
    return JSValue::encode(jsNumber(globalObject->weakRandom().seed()));
}

JSC_DEFINE_HOST_FUNCTION(functionSetRandomSeed, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    unsigned seed = callFrame->argument(0).toUInt32(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    globalObject->weakRandom().setSeed(seed);
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionIsRope, (JSGlobalObject*, CallFrame* callFrame))
{
    JSValue argument = callFrame->argument(0);
    if (!argument.isString())
        return JSValue::encode(jsBoolean(false));
    const StringImpl* impl = asString(argument)->tryGetValueImpl();
    return JSValue::encode(jsBoolean(!impl));
}

JSC_DEFINE_HOST_FUNCTION(functionCallerSourceOrigin, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    SourceOrigin sourceOrigin = callFrame->callerSourceOrigin(vm);
    if (sourceOrigin.url().isNull())
        return JSValue::encode(jsNull());
    return JSValue::encode(jsString(vm, sourceOrigin.string()));
}

// This class doesn't use WTF::File because we don't want to map the entire file into dirty memory
class JSFileDescriptor : public JSDestructibleObject {
    using Base = JSDestructibleObject;
public:
    template<typename CellType, SubspaceAccess>
    static CompleteSubspace* subspaceFor(VM& vm)
    {
        return &vm.destructibleObjectSpace();
    }

    static Structure* createStructure(VM& vm, JSGlobalObject* globalObject, JSValue prototype)
    {
        return Structure::create(vm, globalObject, prototype, TypeInfo(ObjectType, StructureFlags), info());
    }

    static JSFileDescriptor* create(VM& vm, JSGlobalObject* globalObject, FILE*&& descriptor)
    {
        Structure* structure = createStructure(vm, globalObject, jsNull());
        auto* file = new (NotNull, allocateCell<JSFileDescriptor>(vm)) JSFileDescriptor(vm, structure);
        file->finishCreation(vm, descriptor);
        return file;
    }

    void finishCreation(VM& vm, FILE* descriptor)
    {
        ASSERT(descriptor);
        m_descriptor = descriptor;

        Base::finishCreation(vm);
    }

    static void destroy(JSCell* thisObject)
    {
        static_cast<JSFileDescriptor*>(thisObject)->~JSFileDescriptor();
    }

    FILE* descriptor() const { return m_descriptor; }

    DECLARE_INFO;

private:
    JSFileDescriptor(VM& vm, Structure* structure)
        : Base(vm, structure)
    { }

    ~JSFileDescriptor()
    {
        fclose(m_descriptor);
    }

    FILE* m_descriptor;
};

const ClassInfo JSFileDescriptor::s_info = { "FileDescriptor"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSFileDescriptor) };

JSC_DEFINE_HOST_FUNCTION(functionOpenFile, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    URL filePath = computeFilePath(vm, globalObject, callFrame);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());

    FILE* descriptor = fopen(filePath.fileSystemPath().ascii().data(), "r");
    if (!descriptor)
        return throwVMException(globalObject, scope, createURIError(globalObject, makeString("Could not open file at "_s, filePath.string(), " fopen had error: "_s, safeStrerror(errno).span())));

    RELEASE_AND_RETURN(scope, JSValue::encode(JSFileDescriptor::create(vm, globalObject, WTFMove(descriptor))));
}

JSC_DEFINE_HOST_FUNCTION(functionReadline, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    Vector<char, 256> line;
    int c;
    FILE* descriptor = stdin;

    if (auto* file = jsDynamicCast<JSFileDescriptor*>(callFrame->argument(0)))
        descriptor = file->descriptor();

    while ((c = getc(descriptor)) != EOF) {
        // FIXME: Should we also break on \r? 
        if (c == '\n')
            break;
        line.append(c);
    }
    return JSValue::encode(jsString(globalObject->vm(), String(line.span())));
}

JSC_DEFINE_HOST_FUNCTION(functionPreciseTime, (JSGlobalObject*, CallFrame*))
{
    return JSValue::encode(jsNumber(WallTime::now().secondsSinceEpoch().value()));
}

JSC_DEFINE_HOST_FUNCTION(functionNeverInlineFunction, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    return JSValue::encode(setNeverInline(globalObject, callFrame));
}

JSC_DEFINE_HOST_FUNCTION(functionNoDFG, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    return JSValue::encode(setNeverOptimize(globalObject, callFrame));
}

JSC_DEFINE_HOST_FUNCTION(functionNoFTL, (JSGlobalObject*, CallFrame* callFrame))
{
    if (callFrame->argumentCount()) {
        FunctionExecutable* executable = getExecutableForFunction(callFrame->argument(0));
        if (executable)
            executable->setNeverFTLOptimize(true);
    }
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionNoOSRExitFuzzing, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    return JSValue::encode(setCannotUseOSRExitFuzzing(globalObject, callFrame));
}

JSC_DEFINE_HOST_FUNCTION(functionOptimizeNextInvocation, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    return JSValue::encode(optimizeNextInvocation(globalObject, callFrame));
}

JSC_DEFINE_HOST_FUNCTION(functionNumberOfDFGCompiles, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    return JSValue::encode(numberOfDFGCompiles(globalObject, callFrame));
}

JSC_DEFINE_HOST_FUNCTION(functionCallerIsBBQOrOMGCompiled, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!Options::useBBQTierUpChecks())
        return JSValue::encode(jsBoolean(true));

    CallerFunctor wasmToJSFrame;
    StackVisitor::visit(callFrame, vm, wasmToJSFrame);
    if (!wasmToJSFrame.callerFrame() || !wasmToJSFrame.callerFrame()->isNativeCalleeFrame() || wasmToJSFrame.callerFrame()->callee().asNativeCallee()->category() != NativeCallee::Category::Wasm)
        return throwVMError(globalObject, scope, "caller is not a wasm->js import function"_s);

    // We have a wrapper frame that we generate for imports. If we ever can direct call from wasm we would need to change this.
    ASSERT(wasmToJSFrame.callerFrame()->callee().isNativeCallee());
    CallerFunctor wasmFrame;
    StackVisitor::visit(wasmToJSFrame.callerFrame(), vm, wasmFrame);
    if (!wasmFrame.callerFrame()->callee().isNativeCallee()) {
        // This can happen if FTL directly calls wasm which tail calls this function, which fuzzers can produce, just bail out.
        ASSERT(wasmFrame.callerFrame()->codeBlock()->jitType() == JITType::FTLJIT);
        return throwVMError(globalObject, scope, "caller isn't a wasm function"_s);
    }
    ASSERT(wasmFrame.callerFrame()->callee().isNativeCallee());
    ASSERT(wasmFrame.callerFrame()->callee().asNativeCallee()->category() == NativeCallee::Category::Wasm);
#if ENABLE(WEBASSEMBLY)
    auto mode = static_cast<Wasm::Callee*>(wasmFrame.callerFrame()->callee().asNativeCallee())->compilationMode();
    return JSValue::encode(jsBoolean(isAnyBBQ(mode) || isAnyOMG(mode)));
#endif
    RELEASE_ASSERT_NOT_REACHED();
}

Message::Message(Content&& contents, int32_t index)
    : m_contents(WTFMove(contents))
    , m_index(index)
{
}

Message::~Message() = default;

Worker::Worker(Workers& workers, bool isMain)
    : m_workers(workers)
    , m_isMain(isMain)
{
    Locker locker { m_workers.m_lock };
    m_workers.m_workers.append(this);
    
    *currentWorker() = this;
}

Worker::~Worker()
{
    Locker locker { m_workers.m_lock };
    RELEASE_ASSERT(isOnList());
    remove();
}

void Worker::enqueue(const AbstractLocker&, RefPtr<Message> message)
{
    m_messages.append(message);
}

RefPtr<Message> Worker::dequeue()
{
    Locker locker { m_workers.m_lock };
    while (m_messages.isEmpty())
        m_workers.m_condition.wait(m_workers.m_lock);
    return m_messages.takeFirst();
}

Worker& Worker::current()
{
    return **currentWorker();
}

ThreadSpecific<Worker*>& Worker::currentWorker()
{
    static ThreadSpecific<Worker*>* result;
    static std::once_flag flag;
    std::call_once(
        flag,
        [] () {
            result = new ThreadSpecific<Worker*>();
        });
    return *result;
}

Workers::Workers() = default;

Workers::~Workers()
{
    UNREACHABLE_FOR_PLATFORM();
}

template<typename Func>
void Workers::broadcast(const Func& func)
{
    Locker locker { m_lock };
    for (Worker& worker : m_workers) {
        if (&worker != &Worker::current())
            func(locker, worker);
    }
    m_condition.notifyAll();
}

void Workers::report(const String& string)
{
    Locker locker { m_lock };
    m_reports.append(string.isolatedCopy());
    m_condition.notifyAll();
}

String Workers::tryGetReport()
{
    Locker locker { m_lock };
    if (m_reports.isEmpty())
        return String();
    return m_reports.takeFirst();
}

String Workers::getReport()
{
    Locker locker { m_lock };
    while (m_reports.isEmpty())
        m_condition.wait(m_lock);
    return m_reports.takeFirst();
}

Workers& Workers::singleton()
{
    static Workers* result;
    static std::once_flag flag;
    std::call_once(
        flag,
        [] {
            result = new Workers();
        });
    return *result;
}

JSC_DEFINE_HOST_FUNCTION(functionDollarCreateRealm, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    GlobalObject* result = GlobalObject::create(vm, GlobalObject::createStructure(vm, jsNull()), Vector<String>());
    return JSValue::encode(result->getDirect(vm, Identifier::fromString(vm, "$"_s)));
}

JSC_DEFINE_HOST_FUNCTION(functionDollarEvalScript, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    String sourceCode = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    
    JSValue global = callFrame->thisValue().get(globalObject, Identifier::fromString(vm, "global"_s));
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    while (global.inherits<JSGlobalProxy>())
        global = jsCast<JSGlobalProxy*>(global)->target();
    GlobalObject* realm = jsDynamicCast<GlobalObject*>(global);
    if (!realm)
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Expected global to point to a global object"_s)));

    NakedPtr<Exception> evaluationException;
    JSValue result = evaluate(realm, jscSource(sourceCode, callFrame->callerSourceOrigin(vm)), JSValue(), evaluationException);
    if (evaluationException) {
        if (vm.isTerminationException(evaluationException.get()))
            vm.setExecutionForbidden();
        throwException(globalObject, scope, evaluationException);
    }
    return JSValue::encode(result);
}

JSC_DEFINE_HOST_FUNCTION(functionDollarGC, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    vm.heap.collectNow(Sync, CollectionScope::Full);
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionDollarClearKeptObjects, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    vm.finalizeSynchronousJSExecution();
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionDollarGlobalObjectFor, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (callFrame->argumentCount() < 1)
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Not enough arguments"_s)));
    JSValue arg = callFrame->argument(0);
    if (arg.isObject())
        return JSValue::encode(asObject(arg)->globalObject()->globalThis());

    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionDollarIsRemoteFunction, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (callFrame->argumentCount() < 1)
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Not enough arguments"_s)));

    JSValue arg = callFrame->argument(0);
    return JSValue::encode(jsBoolean(isRemoteFunction(arg)));
}

JSC_DEFINE_HOST_FUNCTION(functionDollarAgentStart, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    String sourceCode = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    
    Lock didStartLock;
    Condition didStartCondition;
    bool didStart = false;

    auto isGigacageMemoryExhausted = [&](Gigacage::Kind kind) {
        if (!Gigacage::isEnabled(kind))
            return false;
        if (Gigacage::footprint(kind) < Gigacage::size(kind) * 0.8)
            return false;
        return true;
    };

    if (isGigacageMemoryExhausted(Gigacage::Primitive))
        return JSValue::encode(throwOutOfMemoryError(globalObject, scope, "Gigacage is exhausted"_s));

    String workerPath = "worker"_s;
    if (!callFrame->argument(1).isUndefined()) {
        workerPath = callFrame->argument(1).toWTFString(globalObject);
        RETURN_IF_EXCEPTION(scope, encodedJSValue());
    }
    
    Thread::create(
        "JSC Agent"_s,
        [sourceCode = WTFMove(sourceCode).isolatedCopy(), workerPath = WTFMove(workerPath).isolatedCopy(), &didStartLock, &didStartCondition, &didStart] () {
            CommandLine commandLine(CommandLine::CommandLineForWorkers);
            commandLine.m_interactive = false;
            runJSC(
                commandLine, true,
                [&] (VM&, GlobalObject* globalObject, bool& success) {
                    // Notify the thread that started us that we have registered a worker.
                    {
                        Locker locker { didStartLock };
                        didStart = true;
                        didStartCondition.notifyOne();
                    }
                    
                    NakedPtr<Exception> evaluationException;
                    JSValue result;
                    result = evaluate(globalObject, jscSource(sourceCode, SourceOrigin(URL({ }, workerPath))), JSValue(), evaluationException);
                    if (evaluationException)
                        result = evaluationException->value();
                    checkException(globalObject, true, evaluationException, result, commandLine, success);
                    if (!success)
                        exitProcess(EXIT_FAILURE);
                });
        })->detach();
    
    {
        Locker locker { didStartLock };
        while (!didStart)
            didStartCondition.wait(didStartLock);
    }
    
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionDollarAgentReceiveBroadcast, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSValue callback = callFrame->argument(0);
    auto callData = JSC::getCallData(callback);
    if (callData.type == CallData::Type::None)
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Expected callback"_s)));
    
    RefPtr<Message> message;
    {
        ReleaseHeapAccessScope releaseAccess(vm.heap);
        message = Worker::current().dequeue();
    }

    auto content = message->releaseContents();
    JSValue result = ([&]() -> JSValue {
        if (std::holds_alternative<ArrayBufferContents>(content)) {
            auto nativeBuffer = ArrayBuffer::create(std::get<ArrayBufferContents>(WTFMove(content)));
            ArrayBufferSharingMode sharingMode = nativeBuffer->sharingMode();
            return JSArrayBuffer::create(vm, globalObject->arrayBufferStructure(sharingMode), WTFMove(nativeBuffer));
        }
#if ENABLE(WEBASSEMBLY)
        if (std::holds_alternative<RefPtr<SharedArrayBufferContents>>(content)) {
            JSWebAssemblyMemory* jsMemory = JSC::JSWebAssemblyMemory::create(vm, globalObject->webAssemblyMemoryStructure());
            auto handler = [&vm, jsMemory](Wasm::Memory::GrowSuccess, PageCount oldPageCount, PageCount newPageCount) { jsMemory->growSuccessCallback(vm, oldPageCount, newPageCount); };
            RefPtr<Wasm::Memory> memory;
            if (auto shared = std::get<RefPtr<SharedArrayBufferContents>>(WTFMove(content)))
                memory = Wasm::Memory::create(vm, shared.releaseNonNull(), WTFMove(handler));
            else
                memory = Wasm::Memory::createZeroSized(vm, MemorySharingMode::Shared, WTFMove(handler));
            jsMemory->adopt(memory.releaseNonNull());
            return jsMemory;
        }
#endif
        return jsUndefined();
    })();

    MarkedArgumentBuffer args;
    args.append(result);
    args.append(jsNumber(message->index()));
    if (args.hasOverflowed()) [[unlikely]]
        return JSValue::encode(throwOutOfMemoryError(globalObject, scope));
    RELEASE_AND_RETURN(scope, JSValue::encode(call(globalObject, callback, callData, jsNull(), args)));
}

JSC_DEFINE_HOST_FUNCTION(functionDollarAgentReport, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    String report = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    
    Workers::singleton().report(report);
    
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionDollarAgentSleep, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (callFrame->argumentCount() >= 1) {
        Seconds seconds = Seconds::fromMilliseconds(callFrame->argument(0).toNumber(globalObject));
        RETURN_IF_EXCEPTION(scope, encodedJSValue());
        sleep(seconds);
    }
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionDollarAgentBroadcast, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    int32_t index = callFrame->argument(1).toInt32(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());

    JSArrayBuffer* jsBuffer = jsDynamicCast<JSArrayBuffer*>(callFrame->argument(0));
    if (jsBuffer && jsBuffer->isShared()) {
        Workers::singleton().broadcast(
            [&] (const AbstractLocker& locker, Worker& worker) {
                ArrayBuffer* nativeBuffer = jsBuffer->impl();
                ArrayBufferContents contents;
                nativeBuffer->transferTo(vm, contents); // "transferTo" means "share" if the buffer is shared.
                RefPtr<Message> message = adoptRef(new Message(WTFMove(contents), index));
                worker.enqueue(locker, message);
            });
        return JSValue::encode(jsUndefined());
    }

#if ENABLE(WEBASSEMBLY)
    JSWebAssemblyMemory* memory = jsDynamicCast<JSWebAssemblyMemory*>(callFrame->argument(0));
    if (memory && memory->memory().sharingMode() == MemorySharingMode::Shared) {
        Workers::singleton().broadcast(
            [&] (const AbstractLocker& locker, Worker& worker) {
                RefPtr<SharedArrayBufferContents> contents { memory->memory().shared() };
                RefPtr<Message> message = adoptRef(new Message(WTFMove(contents), index));
                worker.enqueue(locker, message);
            });
        return JSValue::encode(jsUndefined());
    }
#endif

    return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Not supported object"_s)));
}

JSC_DEFINE_HOST_FUNCTION(functionDollarAgentGetReport, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();

    String string = Workers::singleton().tryGetReport();
    if (!string)
        return JSValue::encode(jsNull());
    
    return JSValue::encode(jsString(vm, WTFMove(string)));
}

JSC_DEFINE_HOST_FUNCTION(functionDollarAgentLeaving, (JSGlobalObject*, CallFrame*))
{
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionDollarAgentMonotonicNow, (JSGlobalObject*, CallFrame*))
{
    return JSValue::encode(jsNumber(MonotonicTime::now().secondsSinceEpoch().milliseconds()));
}

JSC_DEFINE_HOST_FUNCTION(functionWaiterListSize, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    return getWaiterListSize(globalObject, callFrame);
}

JSC_DEFINE_HOST_FUNCTION(functionWaitForReport, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();

    String string;
    {
        ReleaseHeapAccessScope releaseAccess(vm.heap);
        string = Workers::singleton().getReport();
    }
    if (!string)
        return JSValue::encode(jsNull());
    
    return JSValue::encode(jsString(vm, WTFMove(string)));
}

JSC_DEFINE_HOST_FUNCTION(functionHeapCapacity, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    return JSValue::encode(jsNumber(vm.heap.capacity()));
}

JSC_DEFINE_HOST_FUNCTION(functionFlashHeapAccess, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    
    double sleepTimeMs = 0;
    if (callFrame->argumentCount() >= 1) {
        sleepTimeMs = callFrame->argument(0).toNumber(globalObject);
        RETURN_IF_EXCEPTION(scope, encodedJSValue());
    }

    vm.heap.releaseAccess();
    if (sleepTimeMs)
        sleep(Seconds::fromMilliseconds(sleepTimeMs));
    vm.heap.acquireAccess();
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionDisableRichSourceInfo, (JSGlobalObject*, CallFrame*))
{
    supportsRichSourceInfo = false;
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionMallocInALoop, (JSGlobalObject*, CallFrame*))
{
    Vector<void*> ptrs;
    for (unsigned i = 0; i < 5000; ++i)
        ptrs.append(fastMalloc(1024 * 2));
    for (void* ptr : ptrs)
        fastFree(ptr);
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionTotalCompileTime, (JSGlobalObject*, CallFrame*))
{
#if ENABLE(JIT)
    return JSValue::encode(jsNumber(JIT::totalCompileTime().milliseconds()));
#else
    return JSValue::encode(jsNumber(0));
#endif
}

IGNORE_GCC_WARNINGS_BEGIN("unused-but-set-parameter")
template<typename ValueType>
void addOption(VM& vm, JSObject* optionsObject, const Identifier& identifier, ValueType value)
{
    if constexpr (std::is_fundamental_v<ValueType>)
        optionsObject->putDirect(vm, identifier, JSValue(value));
}
IGNORE_GCC_WARNINGS_END

JSC_DEFINE_HOST_FUNCTION(functionJSCOptions, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    JSObject* optionsObject = constructEmptyObject(globalObject);
#define READ_OPTION(type_, name_, defaultValue_, availability_, description_) \
    addOption(vm, optionsObject, Identifier::fromString(vm, #name_ ""_s), Options::name_());
    FOR_EACH_JSC_OPTION(READ_OPTION)
#undef READ_OPTION
    return JSValue::encode(optionsObject);
}

JSC_DEFINE_HOST_FUNCTION(functionReoptimizationRetryCount, (JSGlobalObject*, CallFrame* callFrame))
{
    if (callFrame->argumentCount() < 1)
        return JSValue::encode(jsUndefined());
    
    CodeBlock* block = getSomeBaselineCodeBlockForFunction(callFrame->argument(0));
    if (!block)
        return JSValue::encode(jsNumber(0));
    
    return JSValue::encode(jsNumber(block->reoptimizationRetryCounter()));
}

JSC_DEFINE_HOST_FUNCTION(functionTransferArrayBuffer, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (callFrame->argumentCount() < 1)
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Not enough arguments"_s)));
    
    JSArrayBuffer* buffer = jsDynamicCast<JSArrayBuffer*>(callFrame->argument(0));
    if (!buffer)
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Expected an array buffer"_s)));
    
    ArrayBufferContents dummyContents;
    buffer->impl()->transferTo(vm, dummyContents);
    
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionFailNextNewCodeBlock, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    vm.setFailNextNewCodeBlock();
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionQuit, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    vm.codeCache()->write();

    jscExit(EXIT_SUCCESS);
}

JSC_DEFINE_HOST_FUNCTION(functionFalse, (JSGlobalObject*, CallFrame*))
{
    return JSValue::encode(jsBoolean(false));
}

JSC_DEFINE_HOST_FUNCTION(functionUndefined1, (JSGlobalObject*, CallFrame*))
{
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionUndefined2, (JSGlobalObject*, CallFrame*))
{
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionIsInt32, (JSGlobalObject*, CallFrame* callFrame))
{
    for (size_t i = 0; i < callFrame->argumentCount(); ++i) {
        if (!callFrame->argument(i).isInt32())
            return JSValue::encode(jsBoolean(false));
    }
    return JSValue::encode(jsBoolean(true));
}

JSC_DEFINE_HOST_FUNCTION(functionIsPureNaN, (JSGlobalObject*, CallFrame* callFrame))
{
    for (size_t i = 0; i < callFrame->argumentCount(); ++i) {
        JSValue value = callFrame->argument(i);
        if (!value.isNumber())
            return JSValue::encode(jsBoolean(false));
        double number = value.asNumber();
        if (!std::isnan(number))
            return JSValue::encode(jsBoolean(false));
        if (isImpureNaN(number))
            return JSValue::encode(jsBoolean(false));
    }
    return JSValue::encode(jsBoolean(true));
}

JSC_DEFINE_HOST_FUNCTION(functionIdentity, (JSGlobalObject*, CallFrame* callFrame))
{
    return JSValue::encode(callFrame->argument(0));
}

JSC_DEFINE_HOST_FUNCTION(functionEffectful42, (JSGlobalObject*, CallFrame*))
{
    return JSValue::encode(jsNumber(42));
}

JSC_DEFINE_HOST_FUNCTION(functionMakeMasquerader, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    return JSValue::encode(InternalFunction::createFunctionThatMasqueradesAsUndefined(vm, globalObject, 0, "IsHTMLDDA"_s, functionCallMasquerader));
}

JSC_DEFINE_HOST_FUNCTION(functionCallMasquerader, (JSGlobalObject*, CallFrame*))
{
    return JSValue::encode(jsNull());
}

JSC_DEFINE_HOST_FUNCTION(functionHasCustomProperties, (JSGlobalObject*, CallFrame* callFrame))
{
    JSValue value = callFrame->argument(0);
    if (value.isObject())
        return JSValue::encode(jsBoolean(asObject(value)->hasCustomProperties()));
    return JSValue::encode(jsBoolean(false));
}

JSC_DEFINE_HOST_FUNCTION(functionDumpTypesForAllVariables, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    vm.dumpTypeProfilerData();
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionDrainMicrotasks, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    vm.drainMicrotasks();
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionSetTimeout, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    // FIXME: This means we can't pass any internal function but I don't think that's common for testing.
    auto callback = jsDynamicCast<JSFunction*>(callFrame->argument(0));
    if (!callback)
        return throwVMTypeError(globalObject, scope, "First argument is not a JS function"_s);

    auto ticket = vm.deferredWorkTimer->addPendingWork(DeferredWorkTimer::WorkType::AtSomePoint, vm, callback, { });
    auto dispatch = [callback, ticket] {
        callback->vm().deferredWorkTimer->scheduleWorkSoon(ticket, [callback](DeferredWorkTimer::Ticket) {
            JSGlobalObject* globalObject = callback->globalObject();
            MarkedArgumentBuffer args;
            call(globalObject, callback, jsUndefined(), args, "You shouldn't see this..."_s);
        });
    };

    // We need to add the dispatch callback to the run loop even the delay is 0 secs, otherwise 
    // it will cause setTimeout starvation problem (see stress test settimeout-starvation.js).
    JSValue timeout = callFrame->argument(1);
    Seconds delay = timeout.isNumber() ? Seconds::fromMilliseconds(timeout.asNumber()) : Seconds(0);
    RunLoop::currentSingleton().dispatchAfter(delay, WTFMove(dispatch));

    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionReleaseWeakRefs, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    vm.finalizeSynchronousJSExecution();
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionFinalizationRegistryLiveCount, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    auto* finalizationRegistry = jsDynamicCast<JSFinalizationRegistry*>(callFrame->argument(0));
    if (!finalizationRegistry)
        return throwVMTypeError(globalObject, scope, "first argument is not a finalizationRegistry"_s);

    Locker locker { finalizationRegistry->cellLock() };
    return JSValue::encode(jsNumber(finalizationRegistry->liveCount(locker)));
}

JSC_DEFINE_HOST_FUNCTION(functionFinalizationRegistryDeadCount, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    auto* finalizationRegistry = jsDynamicCast<JSFinalizationRegistry*>(callFrame->argument(0));
    if (!finalizationRegistry)
        return throwVMTypeError(globalObject, scope, "first argument is not a finalizationRegistry"_s);

    Locker locker { finalizationRegistry->cellLock() };
    return JSValue::encode(jsNumber(finalizationRegistry->deadCount(locker)));
}

JSC_DEFINE_HOST_FUNCTION(functionIs32BitPlatform, (JSGlobalObject*, CallFrame*))
{
#if USE(JSVALUE64)
    return JSValue::encode(JSValue(JSC::JSValue::JSFalse));
#else
    return JSValue::encode(JSValue(JSC::JSValue::JSTrue));
#endif
}

JSC_DEFINE_HOST_FUNCTION(functionCreateGlobalObject, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    auto* newGlobalObject = GlobalObject::create(vm, GlobalObject::createStructure(vm, jsNull()), Vector<String>());
    return JSValue::encode(newGlobalObject->globalThis());
}

JSC_DEFINE_HOST_FUNCTION(functionCreateHeapBigInt, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    JSValue argument = callFrame->argument(0);
    JSValue bigInt = argument.toBigInt(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
#if USE(BIGINT32)
    if (bigInt.isHeapBigInt())
        return JSValue::encode(bigInt);
    ASSERT(bigInt.isBigInt32());
    int32_t value = bigInt.bigInt32AsInt32();
    RELEASE_AND_RETURN(scope, JSValue::encode(JSBigInt::createFrom(globalObject, value)));
#else
    return JSValue::encode(bigInt);
#endif
}

#if USE(BIGINT32)
JSC_DEFINE_HOST_FUNCTION(functionCreateBigInt32, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    JSValue argument = callFrame->argument(0);
    JSValue bigIntValue = argument.toBigInt(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    if (bigIntValue.isBigInt32())
        return JSValue::encode(bigIntValue);
    ASSERT(bigIntValue.isHeapBigInt());
    JSBigInt* bigInt = jsCast<JSBigInt*>(bigIntValue);
    if (!bigInt->length())
        return JSValue::encode(jsBigInt32(0));
    if (bigInt->length() == 1) {
        JSBigInt::Digit digit = bigInt->digit(0);
        if (bigInt->sign()) {
            if (digit <= static_cast<uint64_t>(-static_cast<int64_t>(INT32_MIN)))
                return JSValue::encode(jsBigInt32(static_cast<int32_t>(-static_cast<int64_t>(digit))));
        } else {
            if (digit <= INT32_MAX)
                return JSValue::encode(jsBigInt32(static_cast<int32_t>(digit)));
        }
    }
    throwTypeError(globalObject, scope, "Out of range of BigInt32"_s);
    return { };
}
#endif

JSC_DEFINE_HOST_FUNCTION(functionUseBigInt32, (JSGlobalObject*, CallFrame*))
{
#if USE(BIGINT32)
    return JSValue::encode(jsBoolean(true));
#else
    return JSValue::encode(jsBoolean(false));
#endif
}

JSC_DEFINE_HOST_FUNCTION(functionIsBigInt32, (JSGlobalObject*, CallFrame* callFrame))
{
#if USE(BIGINT32)
    return JSValue::encode(jsBoolean(callFrame->argument(0).isBigInt32()));
#else
    UNUSED_PARAM(callFrame);
    return JSValue::encode(jsBoolean(false));
#endif
}

JSC_DEFINE_HOST_FUNCTION(functionIsHeapBigInt, (JSGlobalObject*, CallFrame* callFrame))
{
    return JSValue::encode(jsBoolean(callFrame->argument(0).isHeapBigInt()));
}

JSC_DEFINE_HOST_FUNCTION(functionCreateNonRopeNonAtomString, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    String source = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, { });

    if (source.isEmpty() || source.length() == 1)
        return throwVMTypeError(globalObject, scope, "empty / one-length string can be always atom string"_s);

    if (source.impl()->isAtom()) {
        if (source.is8Bit())
            source = StringImpl::create(source.span8());
        else
            source = StringImpl::create(source.span16());
    }

    RELEASE_ASSERT(!source.impl()->isAtom());

    return JSValue::encode(jsString(vm, WTFMove(source)));
}

JSC_DEFINE_HOST_FUNCTION(functionCheckModuleSyntax, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    String source = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());

    StopWatch stopWatch;
    stopWatch.start();

    ParserError error;
    bool validSyntax = checkModuleSyntax(globalObject, jscSource(source, { }, String(), TextPosition(), SourceProviderSourceType::Module), error);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    stopWatch.stop();

    if (!validSyntax)
        throwException(globalObject, scope, jsNontrivialString(vm, toString("SyntaxError: ", error.message(), ":", error.line())));
    return JSValue::encode(jsNumber(stopWatch.getElapsedMS()));
}

JSC_DEFINE_HOST_FUNCTION(functionCheckScriptSyntax, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    String source = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());

    StopWatch stopWatch;
    stopWatch.start();

    ParserError error;

    bool validSyntax = checkSyntax(vm, jscSource(source, { }), error);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    stopWatch.stop();

    if (!validSyntax)
        throwException(globalObject, scope, jsNontrivialString(vm, toString("SyntaxError: ", error.message(), ":", error.line())));
    return JSValue::encode(jsNumber(stopWatch.getElapsedMS()));
}

JSC_DEFINE_HOST_FUNCTION(functionPlatformSupportsSamplingProfiler, (JSGlobalObject*, CallFrame*))
{
#if ENABLE(SAMPLING_PROFILER)
    return JSValue::encode(JSValue(JSC::JSValue::JSTrue));
#else
    return JSValue::encode(JSValue(JSC::JSValue::JSFalse));
#endif
}

JSC_DEFINE_HOST_FUNCTION(functionGenerateHeapSnapshot, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    JSLockHolder lock(vm);
    DeferTermination deferScope(vm);
    auto scope = DECLARE_THROW_SCOPE(vm);

    HeapSnapshotBuilder snapshotBuilder(vm.ensureHeapProfiler(), HeapSnapshotBuilder::SnapshotType::InspectorSnapshot, OverflowPolicy::RecordOverflow);
    snapshotBuilder.buildSnapshot();

    String jsonString = snapshotBuilder.json();
    if (snapshotBuilder.hasOverflowed()) {
        throwOutOfMemoryError(globalObject, scope);
        return encodedJSUndefined();
    }

    RELEASE_AND_RETURN(scope, JSValue::encode(JSONParse(globalObject, jsonString)));
}

JSC_DEFINE_HOST_FUNCTION(functionGenerateHeapSnapshotForGCDebugging, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    JSLockHolder lock(vm);
    DeferTermination deferScope(vm);
    auto scope = DECLARE_THROW_SCOPE(vm);
    String jsonString;
    {
        DeferGCForAWhile deferGC(vm); // Prevent concurrent GC from interfering with the full GC that the snapshot does.

        HeapSnapshotBuilder snapshotBuilder(vm.ensureHeapProfiler(), HeapSnapshotBuilder::SnapshotType::GCDebuggingSnapshot, OverflowPolicy::RecordOverflow);
        snapshotBuilder.buildSnapshot();

        jsonString = snapshotBuilder.json();
        if (snapshotBuilder.hasOverflowed()) {
            throwOutOfMemoryError(globalObject, scope);
            return encodedJSUndefined();
        }
    }
    scope.releaseAssertNoException();
    return JSValue::encode(jsString(vm, WTFMove(jsonString)));
}

JSC_DEFINE_HOST_FUNCTION(functionResetSuperSamplerState, (JSGlobalObject*, CallFrame*))
{
    resetSuperSamplerState();
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionEnsureArrayStorage, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    for (unsigned i = 0; i < callFrame->argumentCount(); ++i) {
        if (JSObject* object = jsDynamicCast<JSObject*>(callFrame->argument(i)))
            object->ensureArrayStorage(vm);
    }
    return JSValue::encode(jsUndefined());
}

#if ENABLE(SAMPLING_PROFILER)
JSC_DEFINE_HOST_FUNCTION(functionStartSamplingProfiler, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    SamplingProfiler& samplingProfiler = vm.ensureSamplingProfiler(WTF::Stopwatch::create());
    samplingProfiler.noticeCurrentThreadAsJSCExecutionThread();
    samplingProfiler.start();
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionSamplingProfilerStackTraces, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    DeferTermination deferScope(vm);
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!vm.samplingProfiler())
        return JSValue::encode(throwException(globalObject, scope, createError(globalObject, "Sampling profiler was never started"_s)));

    auto json = vm.samplingProfiler()->stackTracesAsJSON();
    auto jsonString = json->toJSONString();
    EncodedJSValue result = JSValue::encode(JSONParse(globalObject, WTFMove(jsonString)));
    scope.releaseAssertNoException();
    return result;
}
#endif // ENABLE(SAMPLING_PROFILER)

JSC_DEFINE_HOST_FUNCTION(functionMaxArguments, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    unsigned result = std::min<unsigned>(JSC::maxArguments, static_cast<unsigned>((std::bit_cast<uint8_t*>(Thread::currentSingleton().stack().origin()) - std::bit_cast<uint8_t*>(vm.softStackLimit())) / sizeof(EncodedJSValue)));
    return JSValue::encode(jsNumber(result));
}

JSC_DEFINE_HOST_FUNCTION(functionAsyncTestStart, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSValue numberOfAsyncPasses = callFrame->argument(0);
    if (!numberOfAsyncPasses.isUInt32())
        return throwVMError(globalObject, scope, "Expected first argument to be a uint32"_s);

    asyncTestExpectedPasses += numberOfAsyncPasses.asUInt32();
    return encodedJSUndefined();
}

JSC_DEFINE_HOST_FUNCTION(functionAsyncTestPassed, (JSGlobalObject*, CallFrame*))
{
    asyncTestPasses++;
    return encodedJSUndefined();
}

#if ENABLE(WEBASSEMBLY)

JSC_DEFINE_HOST_FUNCTION(functionWebAssemblyMemoryMode, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    
    if (!Wasm::isSupported())
        return throwVMTypeError(globalObject, scope, "WebAssemblyMemoryMode should only be called if the useWebAssembly option is set"_s);

    auto createString = [&](MemoryMode mode) {
        StringPrintStream out;
        out.print(mode);
        return out.toString();
    };

    if (JSObject* object = callFrame->argument(0).getObject()) {
        if (auto* memory = jsDynamicCast<JSWebAssemblyMemory*>(object))
            return JSValue::encode(jsString(vm, createString(memory->memory().mode())));
        if (auto* instance = jsDynamicCast<JSWebAssemblyInstance*>(object))
            return JSValue::encode(jsString(vm, createString(instance->memoryMode())));
    }

    return throwVMTypeError(globalObject, scope, "WebAssemblyMemoryMode expects either a WebAssembly.Memory or WebAssembly.Instance"_s);
}

JSC_DEFINE_HOST_FUNCTION(functionCreateWebAssemblyMemoryWithMode, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!Wasm::isSupported())
        return throwVMTypeError(globalObject, scope, "createWebAssemblyMemoryWithMode should only be called if the useWebAssembly option is set"_s);

    JSObject* memoryDescriptor = jsDynamicCast<JSObject*>(callFrame->argument(0));
    if (!memoryDescriptor)
        return throwVMTypeError(globalObject, scope, "createWebAssemblyMemoryWithMode expects the first argument to be an object"_s);

    auto desiredMode = callFrame->argument(1).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    MemoryMode mode = MemoryMode::Signaling;
    if (desiredMode == "BoundsChecking"_s)
        mode = MemoryMode::BoundsChecking;
    else if (desiredMode != "Signaling"_s)
        return throwVMError(globalObject, scope, "createWebAssemblyMemoryWithMode expects either 'BoundsChecking' or 'Signaling' as the second argument."_s);

    RELEASE_AND_RETURN(scope, JSValue::encode(WebAssemblyMemoryConstructor::createMemoryFromDescriptor(globalObject, globalObject->webAssemblyMemoryStructure(), memoryDescriptor, mode)));
}

#endif // ENABLE(WEBASSEMBLY)

JSC_DEFINE_HOST_FUNCTION(functionSetUnhandledRejectionCallback, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    JSObject* object = callFrame->argument(0).getObject();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!object || !object->isCallable())
        return throwVMTypeError(globalObject, scope);

    globalObject->setUnhandledRejectionCallback(vm, object);
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionAsDoubleNumber, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    double num = callFrame->argument(0).toNumber(globalObject);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    return JSValue::encode(jsDoubleNumber(num));
}

JSC_DEFINE_HOST_FUNCTION(functionDropAllLocks, (JSGlobalObject* globalObject, CallFrame*))
{
    JSLock::DropAllLocks dropAllLocks(globalObject);
    return JSValue::encode(jsUndefined());
}

// This is intended to match the resolution of WebCore's Performance::now when we're using a high resolution time.
JSC_DEFINE_HOST_FUNCTION(functionPerformanceNow, (JSGlobalObject*, CallFrame*))
{
    static const MonotonicTime timeOrigin = MonotonicTime::now();
    return JSValue::encode(jsNumber((MonotonicTime::now() - timeOrigin).reduceTimeResolution(Seconds::highTimePrecision()).milliseconds()));
}

static String asSignpostString(JSGlobalObject* globalObject, JSValue v)
{
    if (v.isUndefined())
        return emptyString();
    return v.toWTFString(globalObject);
}

JSC_DEFINE_HOST_FUNCTION(functionPerformanceMark, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    auto message = asSignpostString(globalObject, callFrame->argument(0));
    RETURN_IF_EXCEPTION(scope, EncodedJSValue());

    globalObject->startSignpost(WTFMove(message));
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(functionPerformanceMeasure, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    auto message = asSignpostString(globalObject, callFrame->argument(0));
    RETURN_IF_EXCEPTION(scope, EncodedJSValue());

    globalObject->stopSignpost(WTFMove(message));
    return JSValue::encode(jsUndefined());
}

#if ENABLE(FUZZILLI)

// We have to assume that the fuzzer will be able to call this function e.g. by
// enumerating the properties of the global object and eval'ing them. As such
// this function is implemented in a way that requires passing some magic value
// as first argument (with the idea being that the fuzzer won't be able to
// generate this value) which then also acts as a selector for the operation
// to perform.
JSC_DEFINE_HOST_FUNCTION(functionFuzzilli, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!callFrame->argument(0).isString()) {
        // We directly require a string as argument for simplicity.
        return JSValue::encode(jsUndefined());
    }
    auto operation = callFrame->argument(0).toWTFString(globalObject);
    RETURN_IF_EXCEPTION(scope, { });

    if (operation == "FUZZILLI_CRASH"_s) {
        int32_t command = callFrame->argument(1).toInt32(globalObject);
        RETURN_IF_EXCEPTION(scope, { });

        switch (command) {
        case 0:
            *reinterpret_cast<int32_t*>(0x41414141) = 0x1337;
            break;
        case 1:
            RELEASE_ASSERT(0);
            break;
        case 2:
            ASSERT(0);
            break;
        }

    } else if (operation == "FUZZILLI_PRINT"_s) {
        String string = callFrame->argument(1).toWTFString(globalObject);
        RETURN_IF_EXCEPTION(scope, { });

        Fuzzilli::logFile().println(string);
        Fuzzilli::logFile().flush();
    }

    return JSValue::encode(jsUndefined());
}

#endif // ENABLE(FUZZILLI)

// Use SEH for Release builds only to get rid of the crash report dialog
// (luckily the same tests fail in Release and Debug builds so far). Need to
// be in a separate main function because the jscmain function requires object
// unwinding.

#if COMPILER(MSVC) && !defined(_DEBUG)
#define TRY       __try {
#define EXCEPT(x) } __except (EXCEPTION_EXECUTE_HANDLER) { x; }
#else
#define TRY
#define EXCEPT(x)
#endif

#if OS(UNIX)
static BinarySemaphore waitToExit;
#endif

int jscmain(int argc, char** argv);

#if OS(DARWIN) || OS(LINUX)
static size_t memoryLimit;

static void crashIfExceedingMemoryLimit()
{
    if (!memoryLimit)
        return;
    MemoryFootprint footprint = MemoryFootprint::now();
    if (footprint.current > memoryLimit) {
        dataLogLn("Crashing because current footprint: ", footprint.current, " exceeds limit: ", memoryLimit);
        CRASH();
    }
}

static void startMemoryMonitoringThreadIfNeeded()
{
    char* memoryLimitString = getenv("JSCTEST_memoryLimit");
    if (!memoryLimitString)
        return;

    if (sscanf(memoryLimitString, "%zu", &memoryLimit) != 1) {
        dataLogLn("WARNING: malformed JSCTEST_memoryLimit environment variable");
        return;
    }

    if (!memoryLimit)
        return;

    Thread::create("jsc Memory Monitor"_s, [=] {
        while (true) {
            sleep(Seconds::fromMilliseconds(5));
            crashIfExceedingMemoryLimit();
        }
    });
}
#endif // OS(DARWIN) || OS(LINUX)

static double s_desiredTimeout;
static double s_timeoutMultiplier = 1.0;
static Seconds s_timeoutDuration;
static Seconds s_maxAllowedCPUTime;
static VM* s_vm;

static void startTimeoutTimer(Seconds duration)
{
    Thread::create("jsc Timeout Thread"_s, [=] () {
        sleep(duration);
        VMInspector::forEachVM([&] (VM& vm) -> IterationStatus {
            if (&vm != s_vm)
                return IterationStatus::Continue;
            vm.notifyNeedShellTimeoutCheck();
            return IterationStatus::Done;
        });

        if (const char* timeoutString = getenv("JSCTEST_hardTimeout")) {
            double hardTimeoutInDouble = 0;
            if (sscanf(timeoutString, "%lf", &hardTimeoutInDouble) != 1)
                dataLog("WARNING: hardTimeout string is malformed, got ", timeoutString, " but expected a number. Not using a timeout.\n");
            else {
                Seconds hardTimeout { hardTimeoutInDouble };
                sleep(hardTimeout);
                dataLogLn("HARD TIMEOUT after ", hardTimeout);
                exitProcess(EXIT_FAILURE);
            }
        }
    });
}

// The purpose of crashDueToJSCShellTimeout is solely to make it easier to distinquish
// jsc shell test timeouts (i.e. crashDueToJSCShellTimeout is present in crash stack
// traces) from other crashes.
NO_RETURN_DUE_TO_CRASH NEVER_INLINE void crashDueToJSCShellTimeout();
void crashDueToJSCShellTimeout()
{
    CRASH();
}

static void timeoutCheckCallback(VM& vm)
{
    RELEASE_ASSERT(&vm == s_vm);
    auto cpuTime = CPUTime::forCurrentThread();
    if (cpuTime >= s_maxAllowedCPUTime) {
        dataLog("Timed out after ", s_timeoutDuration, " seconds!\n");
        crashDueToJSCShellTimeout();
    }
    auto remainingTime = s_maxAllowedCPUTime - cpuTime;
    startTimeoutTimer(remainingTime);
}

static void initializeTimeoutIfNeeded()
{
    if (char* timeoutString = getenv("JSCTEST_timeout")) {
        if (sscanf(timeoutString, "%lf", &s_desiredTimeout) != 1) {
            dataLog("WARNING: timeout string is malformed, got ", timeoutString,
                " but expected a number. Not using a timeout.\n");
        } else
            g_jscConfig.shellTimeoutCheckCallback = timeoutCheckCallback;
    }
}

static void startTimeoutThreadIfNeeded(VM& vm)
{
    if (!g_jscConfig.shellTimeoutCheckCallback)
        return;

    s_vm = &vm;
    s_timeoutDuration = Seconds(s_desiredTimeout * s_timeoutMultiplier);
    s_maxAllowedCPUTime = CPUTime::forCurrentThread() + s_timeoutDuration;
    Seconds timeoutDuration(s_desiredTimeout * s_timeoutMultiplier);
    startTimeoutTimer(timeoutDuration);
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

int main(int argc, char** argv)
{
#if OS(DARWIN)
#if __has_include(<libproc.h>)
    // Let the kernel kill us when OOM
    {
        int retval = proc_setpcontrol(PROC_SETPC_TERMINATE);
        ASSERT_UNUSED(retval, !retval);
    }
#endif
#endif

#if OS(WINDOWS)
    // Cygwin calls ::SetErrorMode(SEM_FAILCRITICALERRORS), which we will inherit. This is bad for
    // testing/debugging, as it causes the post-mortem debugger not to be invoked. We reset the
    // error mode here to work around Cygwin's behavior. See <http://webkit.org/b/55222>.
    ::SetErrorMode(0);

    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);

    WTF::disableCRTDebugAssertDialog();

    timeBeginPeriod(1);
#endif

#if PLATFORM(GTK)
    if (!setlocale(LC_ALL, ""))
        WTFLogAlways("Locale not supported by C library.\n\tUsing the fallback 'C' locale.");
#endif

    // Need to initialize WTF before we start any threads. Cannot initialize JSC
    // yet, since that would do somethings that we'd like to defer until after we
    // have a chance to parse options.
    WTF::initialize();
#if PLATFORM(COCOA) || OS(ANDROID)
    WTF::disableForwardingVPrintfStdErrToOSLog();
#endif

#if PLATFORM(COCOA)
    if (getenv("JSCTEST_CrashReportArgV")) {
        StringPrintStream out;
        CommaPrinter space(" "_s);
        for (int i = 0; i < argc; ++i)
            out.print(space, argv[i]);
        WTF::setCrashLogMessage(out.toCString().data());
    }
#endif

#if OS(UNIX)
    if (getenv("JS_SHELL_WAIT_FOR_SIGUSR2_TO_EXIT")) {
        addSignalHandler(Signal::Usr, SignalHandler([&] (Signal, SigInfo&, PlatformRegisters&) {
            dataLogLn("Signal handler hit, we can exit now.");
            waitToExit.signal();
            return SignalAction::Handled;
        }));
        activateSignalHandlersFor(Signal::Usr);
    }
#endif

    // We can't use destructors in the following code because it uses Windows
    // Structured Exception Handling
    int res = EXIT_SUCCESS;
    TRY
        res = jscmain(argc, argv);
    EXCEPT(res = EXIT_EXCEPTION)
    finalizeStatsAtEndOfTesting();
    if (getenv("JS_SHELL_WAIT_FOR_INPUT_TO_EXIT")) {
        WTF::fastDisableScavenger();
        fprintf(stdout, "\njs shell waiting for input to exit\n");
        fflush(stdout);
        getc(stdin);
    }

#if OS(UNIX)
    if (getenv("JS_SHELL_WAIT_FOR_SIGUSR2_TO_EXIT")) {
        WTF::fastDisableScavenger();
        fprintf(stdout, "\njs shell waiting for `kill -USR2 [pid]` to exit\n");
        fflush(stdout);

        waitToExit.wait();

        fprintf(stdout, "\njs shell exiting\n");
        fflush(stdout);
    }
#endif

    jscExit(res);
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

static void dumpException(GlobalObject* globalObject, JSValue exception)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_CATCH_SCOPE(vm);

#define CHECK_EXCEPTION() do { \
        if (scope.exception()) { \
            scope.clearException(); \
            return; \
        } \
    } while (false)

    auto exceptionString = exception.toWTFString(globalObject);
    CHECK_EXCEPTION();
    Expected<CString, UTF8ConversionError> expectedCString = exceptionString.tryGetUTF8();
    if (expectedCString)
        printf("Exception: %s\n", expectedCString.value().data());
    else
        printf("Exception: <out of memory while extracting exception string>\n");

    Identifier nameID = Identifier::fromString(vm, "name"_s);
    CHECK_EXCEPTION();
    Identifier fileNameID = Identifier::fromString(vm, "sourceURL"_s);
    CHECK_EXCEPTION();
    Identifier lineNumberID = Identifier::fromString(vm, "line"_s);
    CHECK_EXCEPTION();
    Identifier stackID = Identifier::fromString(vm, "stack"_s);
    CHECK_EXCEPTION();

    JSValue nameValue = exception.get(globalObject, nameID);
    CHECK_EXCEPTION();
    JSValue fileNameValue = exception.get(globalObject, fileNameID);
    CHECK_EXCEPTION();
    JSValue lineNumberValue = exception.get(globalObject, lineNumberID);
    CHECK_EXCEPTION();
    JSValue stackValue = exception.get(globalObject, stackID);
    CHECK_EXCEPTION();
    
    auto nameString = nameValue.toWTFString(globalObject);
    CHECK_EXCEPTION();

    if (nameString == "SyntaxError"_s && (!fileNameValue.isUndefinedOrNull() || !lineNumberValue.isUndefinedOrNull())) {
        auto fileNameString = fileNameValue.toWTFString(globalObject);
        CHECK_EXCEPTION();
        auto lineNumberString = lineNumberValue.toWTFString(globalObject);
        CHECK_EXCEPTION();
        printf("at %s:%s\n", fileNameString.utf8().data(), lineNumberString.utf8().data());
    }
    
    if (!stackValue.isUndefinedOrNull()) {
        auto stackString = stackValue.toWTFString(globalObject);
        CHECK_EXCEPTION();
        if (stackString.length()) {
            auto expectedUtf8 = stackString.tryGetUTF8();
            if (expectedUtf8)
                printf("%s\n", expectedUtf8.value().data());
        }
    }

    fflush(stdout);
#undef CHECK_EXCEPTION
}

static bool checkUncaughtException(VM& vm, GlobalObject* globalObject, JSValue exception, const CommandLine& options)
{
    const String& expectedExceptionName = options.m_uncaughtExceptionName;
    auto scope = DECLARE_CATCH_SCOPE(vm);
    scope.clearException();
    if (!exception) {
        printf("Expected uncaught exception with name '%s' but none was thrown\n", expectedExceptionName.utf8().data());
        return false;
    }

    JSValue exceptionClass = globalObject->get(globalObject, Identifier::fromString(vm, expectedExceptionName));
    if (!exceptionClass.isObject() || scope.exception()) {
        printf("Expected uncaught exception with name '%s' but given exception class is not defined\n", expectedExceptionName.utf8().data());
        return false;
    }

    bool isInstanceOfExpectedException = jsCast<JSObject*>(exceptionClass)->hasInstance(globalObject, exception);
    if (scope.exception()) {
        printf("Expected uncaught exception with name '%s' but given exception class fails performing hasInstance\n", expectedExceptionName.utf8().data());
        return false;
    }
    if (isInstanceOfExpectedException) {
        if (options.m_alwaysDumpUncaughtException)
            dumpException(globalObject, exception);
        return true;
    }

    printf("Expected uncaught exception with name '%s' but exception value is not instance of this exception class\n", expectedExceptionName.utf8().data());
    dumpException(globalObject, exception);
    return false;
}

static void checkException(GlobalObject* globalObject, bool isLastFile, bool hasException, JSValue value, const CommandLine& options, bool& success)
{
    VM& vm = globalObject->vm();

    auto isTerminationException = [&] (JSValue value) {
        if (!value.isCell())
            return false;

        JSCell* valueCell = value.asCell();
        vm.ensureTerminationException();
        Exception* terminationException = vm.terminationException();
        Exception* exception = jsDynamicCast<Exception*>(valueCell);
        if (exception)
            return vm.isTerminationException(exception);
        JSCell* terminationError = terminationException->value().asCell();
        return valueCell == terminationError;
    };

    if (isTerminationException(value)) {
        vm.setExecutionForbidden();
        if (options.m_treatWatchdogExceptionAsSuccess) {
            ASSERT(hasException);
            return;
        }
    }

    if (!options.m_uncaughtExceptionName || !isLastFile) {
        success = success && (!hasException || options.m_ignoreUncaughtExceptions);
        if (options.m_dump && !hasException)
            printf("End: %s\n", value.toWTFString(globalObject).utf8().data());
        if (hasException && !options.m_ignoreUncaughtExceptions)
            dumpException(globalObject, value);
    } else
        success = success && (checkUncaughtException(vm, globalObject, (hasException) ? value : JSValue(), options) || options.m_ignoreUncaughtExceptions);
}

void GlobalObject::reportUncaughtExceptionAtEventLoop(JSGlobalObject* globalObject, Exception* exception)
{
    auto* global = jsCast<GlobalObject*>(globalObject);
    dumpException(global, exception->value());
    bool hideNoReturn = true;
    if (hideNoReturn)
        jscExit(EXIT_EXCEPTION);
}

static void runWithOptions(GlobalObject* globalObject, CommandLine& options, bool& success)
{
    Vector<Script>& scripts = options.m_scripts;
    String fileName;
    Vector<char> scriptBuffer;

    VM& vm = globalObject->vm();
    auto scope = DECLARE_CATCH_SCOPE(vm);

#if ENABLE(SAMPLING_FLAGS)
    SamplingFlags::start();
#endif

    for (size_t i = 0; i < scripts.size(); i++) {
        JSInternalPromise* promise = nullptr;
        bool isModule = options.m_module || scripts[i].scriptType == Script::ScriptType::Module;

        switch (scripts[i].codeSource) {
        case Script::CodeSource::File: {
            fileName = String::fromLatin1(scripts[i].argument);
            if (scripts[i].strictMode == Script::StrictMode::Strict)
                scriptBuffer.append("\"use strict\";\n"_span);

            if (isModule) {
                // If necessary, prepend "./" so the module loader doesn't think this is a bare-name specifier.
                fileName = isAbsolutePath(fileName) || isDottedRelativePath(fileName) ? fileName : makeString('.', pathSeparator(), fileName);
                promise = loadAndEvaluateModule(globalObject, fileName, jsUndefined(), jsUndefined());
                RETURN_IF_EXCEPTION(scope, void());
            } else {
                if (!fetchScriptFromLocalFileSystem(fileName, scriptBuffer)) {
                    success = false; // fail early so we can catch missing files
                    return;
                }
            }
            break;
        }
        case Script::CodeSource::CommandLine: {
            size_t commandLineLength = strlen(scripts[i].argument);
            scriptBuffer.resize(commandLineLength);
            std::copy_n(scripts[i].argument, commandLineLength, scriptBuffer.begin());
            fileName = "[Command Line]"_s;
            break;
        }
#if ENABLE(FUZZILLI)
        case Script::CodeSource::Reprl: {
            Fuzzilli::readInput(&scriptBuffer);
            fileName = "[REPRL]"_s;
            break;
        }
#endif // ENABLE(FUZZILLI)
        }

        bool isLastFile = i == scripts.size() - 1;
        SourceOrigin sourceOrigin { absoluteFileURL(fileName) };
        if (isModule) {
            if (!promise) {
                // FIXME: This should use an absolute file URL https://bugs.webkit.org/show_bug.cgi?id=193077
                promise = loadAndEvaluateModule(globalObject, jscSource(stringFromUTF(scriptBuffer), sourceOrigin, fileName, TextPosition(), SourceProviderSourceType::Module), jsUndefined());
                RETURN_IF_EXCEPTION(scope, void());
            }

            JSFunction* fulfillHandler = JSNativeStdFunction::create(vm, globalObject, 1, String(), [&success, &options, isLastFile](JSGlobalObject* globalObject, CallFrame* callFrame) {
                checkException(jsCast<GlobalObject*>(globalObject), isLastFile, false, callFrame->argument(0), options, success);
                return JSValue::encode(jsUndefined());
            });

            JSFunction* rejectHandler = JSNativeStdFunction::create(vm, globalObject, 1, String(), [&success, &options, isLastFile](JSGlobalObject* globalObject, CallFrame* callFrame) {
                checkException(jsCast<GlobalObject*>(globalObject), isLastFile, true, callFrame->argument(0), options, success);
                return JSValue::encode(jsUndefined());
            });

            promise->then(globalObject, fulfillHandler, rejectHandler);
            scope.releaseAssertNoExceptionExceptTermination();
            vm.drainMicrotasks();
        } else {
            NakedPtr<Exception> evaluationException;
            JSValue returnValue = evaluate(globalObject, jscSource(scriptBuffer, sourceOrigin , fileName), JSValue(), evaluationException);
            scope.assertNoException();
            if (evaluationException)
                returnValue = evaluationException->value();
            checkException(globalObject, isLastFile, evaluationException, returnValue, options, success);
        }

        scriptBuffer.clear();
        scope.clearException();
    }

#if ENABLE(REGEXP_TRACING)
    vm.dumpRegExpTrace();
#endif
}

#define RUNNING_FROM_XCODE 0

static void runInteractive(GlobalObject* globalObject)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_CATCH_SCOPE(vm);

    URL directoryName = currentWorkingDirectory();
    if (!directoryName.isValid())
        return;
    SourceOrigin sourceOrigin(URL(directoryName, "./interpreter"_s));
    
    bool shouldQuit = false;
    while (!shouldQuit) {
#if HAVE(READLINE) && !RUNNING_FROM_XCODE
        ParserError error;
        String source;
        do {
            error = ParserError();
            char* line = readline(source.isEmpty() ? interactivePrompt : "... ");
            shouldQuit = !line;
            if (!line)
                break;
            source = makeString(source, String::fromUTF8(line), '\n');
            checkSyntax(vm, jscSource(source, sourceOrigin), error);
            if (!line[0]) {
                free(line);
                break;
            }
            add_history(line);
            free(line);
        } while (error.syntaxErrorType() == ParserError::SyntaxErrorRecoverable);
        
        if (error.isValid()) {
            printf("%s:%d\n", error.message().utf8().data(), error.line());
            continue;
        }
        
        
        NakedPtr<Exception> evaluationException;
        JSValue returnValue = evaluate(globalObject, jscSource(source, sourceOrigin), JSValue(), evaluationException);
#else
        printf("%s", interactivePrompt);
        Vector<char, 256> line;
        int c;
        while ((c = getchar()) != EOF) {
            // FIXME: Should we also break on \r? 
            if (c == '\n')
                break;
            line.append(c);
        }
        if (line.isEmpty())
            break;

        NakedPtr<Exception> evaluationException;
        JSValue returnValue = evaluate(globalObject, jscSource(line, sourceOrigin, sourceOrigin.string()), JSValue(), evaluationException);
#endif
        if (evaluationException && vm.isTerminationException(evaluationException.get()))
            vm.setExecutionForbidden();

        Expected<CString, UTF8ConversionError> utf8;
        if (evaluationException) {
            fputs("Exception: ", stdout);
            utf8 = evaluationException->value().toWTFString(globalObject).tryGetUTF8();
        } else
            utf8 = returnValue.toWTFStringForConsole(globalObject).tryGetUTF8();

        CString result;
        if (utf8)
            result = utf8.value();
        else if (utf8.error() == UTF8ConversionError::OutOfMemory)
            result = "OutOfMemory while processing string";
        else
            result = "Error while processing string";
        fwrite(result.data(), sizeof(char), result.length(), stdout);
        putchar('\n');

        scope.clearException();
        vm.drainMicrotasks();
    }
    printf("\n");
}

[[noreturn]] static void printUsageStatement(bool help = false)
{
    fprintf(stderr, "Usage: jsc [options] [files] [-- arguments]\n");
    fprintf(stderr, "  -d         Dumps bytecode\n");
    fprintf(stderr, "  -s         Synchronous compilation (equivalent to `--useConcurrentJIT=0`)\n");
    fprintf(stderr, "  -e         Evaluate argument as script code\n");
    fprintf(stderr, "  -f         Specifies a source file (deprecated)\n");
    fprintf(stderr, "  -h|--help  Prints this help message\n");
#if ENABLE(FUZZILLI)
    fprintf(stderr, "  --reprl    Enables REPRL mode (used by the Fuzzilli fuzzer)\n");
#endif
    fprintf(stderr, "  -i         Enables interactive mode (default if no files are specified)\n");
    fprintf(stderr, "  -m         Execute as a module\n");
    fprintf(stderr, "  -p <file>  Outputs profiling data to a file\n");
    fprintf(stderr, "  -x         Output exit code before terminating\n");
    fprintf(stderr, "\n");
#if OS(UNIX)
    fprintf(stderr, "  --signal-expected          Installs signal handlers that exit on a crash (Unix platforms only, lldb will not work with this option) \n");
#endif
    fprintf(stderr, "  --sample                   Collects and outputs sampling profiler data\n");
    fprintf(stderr, "  --test262-async            Check that some script calls the print function with the string 'Test262:AsyncTestComplete'\n");
    fprintf(stderr, "  --strict-file=<file>       Parse the given file as if it were in strict mode (this option may be passed more than once)\n");
    fprintf(stderr, "  --module-file=<file>       Parse and evaluate the given file as module (this option may be passed more than once)\n");
    fprintf(stderr, "  --exception=<name>         Check the last script exits with an uncaught exception with the specified name\n");
    fprintf(stderr, "  --ignoreUncaughtExceptions Do not error out if an uncaught exception is observed\n");
    fprintf(stderr, "  --watchdog-exception-ok    Uncaught watchdog exceptions exit with success\n");
    fprintf(stderr, "  --dumpException            Dump uncaught exception text\n");
    fprintf(stderr, "  --footprint                Dump memory footprint after done executing\n");
    fprintf(stderr, "  --options                  Dumps all JSC VM options and exits\n");
    fprintf(stderr, "  --dumpOptions              Dumps all non-default JSC VM options before continuing\n");
    fprintf(stderr, "  --<jsc VM option>=<value>  Sets the specified JSC VM option\n");
#if USE(LIBPAS)
    fprintf(stderr, "  --crash-vm=<value>         Crash VM on startup due to PGM failure. Options PGMOOBLowerGuardPage, PGMOOBUpperGuardPage, or PGMUAF (For Testing Purposes).\n");
#endif
    fprintf(stderr, "  --destroy-vm               Destroy VM before exiting\n");
    fprintf(stderr, "  --can-block-is-false       Make main thread's Atomics.wait throw\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Files with a .mjs extension will always be evaluated as modules.\n");
    fprintf(stderr, "\n");

    jscExit(help ? EXIT_SUCCESS : EXIT_FAILURE);
}

static bool isMJSFile(char *filename)
{
    filename = strrchr(filename, '.');

    if (filename)
        return !strcmp(filename, ".mjs");

    return false;
}

#if USE(LIBPAS)
WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

static NEVER_INLINE void crashPGMUAF()
{
    WTF::forceEnablePGM(1);
    size_t allocSize = WTF::pageSize() * 10000;
    char* result = static_cast<char*>(fastMalloc(allocSize));
    fastFree(result);
    *result = 'a';
}

static NEVER_INLINE void crashPGMUpperGuardPage()
{
    WTF::forceEnablePGM(1);
    size_t allocSize = WTF::pageSize() * 10000;
    char* result = static_cast<char*>(fastMalloc(allocSize));
    result = result + allocSize;
    *result = 'a';
}

static NEVER_INLINE void crashPGMLowerGuardPage()
{
    WTF::forceEnablePGM(1);
    size_t allocSize = WTF::pageSize() * 10000;
    char* result = static_cast<char*>(fastMalloc(allocSize));
    result = result - 1;
    *result = 'a';
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
#endif

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

void CommandLine::parseArguments(int argc, char** argv)
{
    Options::AllowUnfinalizedAccessScope scope;
    Options::initialize();
    Options::useSharedArrayBuffer() = true;

#if PLATFORM(IOS_FAMILY) && !PLATFORM(APPLETV) && !PLATFORM(WATCHOS)
    Options::crashIfCantAllocateJITMemory() = true;
#endif

    if (Options::dumpOptions()) {
        printf("Command line:");
#if PLATFORM(COCOA)
        for (char** envp = *_NSGetEnviron(); *envp; envp++) {
            const char* env = *envp;
            if (!strncmp("JSC_", env, 4))
                printf(" %s", env);
        }
#endif // PLATFORM(COCOA)
        for (int i = 0; i < argc; ++i)
            printf(" %s", argv[i]);
        printf("\n");
    }

    int i = 1;
    bool optionsDumpRequested = false;

    bool hasBadJSCOptions = false;
    for (; i < argc; ++i) {
        const char* arg = argv[i];
        if (!strcmp(arg, "-f")) {
            if (++i == argc)
                printUsageStatement();
            m_scripts.append(Script(Script::StrictMode::Sloppy, Script::CodeSource::File, Script::ScriptType::Script, argv[i]));
            continue;
        }
        if (!strcmp(arg, "-e")) {
            if (++i == argc)
                printUsageStatement();
            m_scripts.append(Script(Script::StrictMode::Sloppy, Script::CodeSource::CommandLine, Script::ScriptType::Script, argv[i]));
            continue;
        }
#if ENABLE(FUZZILLI)
        if (!strcmp(arg, "--reprl") && !m_reprl) {
            m_reprl = true;
            m_scripts.append(Script(Script::StrictMode::Sloppy, Script::CodeSource::Reprl, Script::ScriptType::Script, nullptr));
            continue;
        }
#endif // ENABLE(FUZZILLI)
        if (!strcmp(arg, "-i")) {
            m_interactive = true;
            continue;
        }
        if (!strcmp(arg, "-d")) {
            m_dump = true;
            continue;
        }
        if (!strcmp(arg, "-s")) {
            Options::useConcurrentJIT() = false;
            continue;
        }
        if (!strcmp(arg, "-p")) {
            if (++i == argc)
                printUsageStatement();
            Options::setOption("useProfiler=1");
            m_profilerOutput = String::fromLatin1(argv[i]);
            continue;
        }
        if (!strcmp(arg, "-m")) {
            m_module = true;
            continue;
        }
        if (!strcmp(arg, "--signal-expected")) {
#if OS(UNIX)
            SignalAction (*exit)(Signal, SigInfo&, PlatformRegisters&) = [] (Signal signal, SigInfo&, PlatformRegisters&) {
                dataLogLn("Signal handler for ", ScopedEnumDump(signal), " hit. Exiting with status 137");
                // Deliberate exit with a SIGKILL code greater than 130.
                terminateProcess(137);
                return SignalAction::ForceDefault;
            };

            addSignalHandler(Signal::IllegalInstruction, SignalHandler(exit));
            addSignalHandler(Signal::AccessFault, SignalHandler(exit));
            addSignalHandler(Signal::FloatingPoint, SignalHandler(exit));
            // once we do this lldb won't work anymore because we will exit on any breakpoints it sets.
            addSignalHandler(Signal::Breakpoint, SignalHandler(exit));

            activateSignalHandlersFor(Signal::IllegalInstruction);
            activateSignalHandlersFor(Signal::AccessFault);
            activateSignalHandlersFor(Signal::FloatingPoint);
            activateSignalHandlersFor(Signal::Breakpoint);

#if !OS(DARWIN)
            addSignalHandler(Signal::Abort, SignalHandler(exit));
            activateSignalHandlersFor(Signal::Abort);
#endif
#endif
            continue;
        }
        if (!strcmp(arg, "-x")) {
            m_exitCode = true;
            continue;
        }
        if (!strcmp(arg, "--")) {
            ++i;
            break;
        }
        if (!strcmp(arg, "-h") || !strcmp(arg, "--help"))
            printUsageStatement(true);

        if (!strcmp(arg, "--options")) {
            JSC::Options::dumpOptions() = static_cast<unsigned>(JSC::Options::DumpLevel::Verbose);
            optionsDumpRequested = true;
            continue;
        }
        if (!strcmp(arg, "--dumpOptions")) {
            JSC::Options::dumpOptions() = static_cast<unsigned>(JSC::Options::DumpLevel::Overridden);
            continue;
        }
        if (!strcmp(arg, "--sample")) {
            JSC::Options::useSamplingProfiler() = true;
            JSC::Options::collectExtraSamplingProfilerData() = true;
            m_dumpSamplingProfilerData = true;
            continue;
        }
#if USE(LIBPAS)
        if (!strcmp(arg, "--crash-vm=PGMOOBLowerGuardPage"))
            crashPGMLowerGuardPage();
        if (!strcmp(arg, "--crash-vm=PGMOOBUpperGuardPage"))
            crashPGMUpperGuardPage();
        if (!strcmp(arg, "--crash-vm=PGMUAF"))
            crashPGMUAF();
#endif
        if (!strcmp(arg, "--destroy-vm")) {
            m_destroyVM = true;
            continue;
        }
        if (!strcmp(arg, "--can-block-is-false")) {
            m_canBlockIsFalse = true;
            continue;
        }
        if (!strcmp(arg, "--disableOptionsFreezingForTesting")) {
            JSC::Config::disableFreezingForTesting();
            continue;
        }

        static const char* timeoutMultiplierOptStr = "--timeoutMultiplier=";
        static const unsigned timeoutMultiplierOptStrLength = strlen(timeoutMultiplierOptStr);
        if (!strncmp(arg, timeoutMultiplierOptStr, timeoutMultiplierOptStrLength)) {
            const char* valueStr = &arg[timeoutMultiplierOptStrLength];
            if (sscanf(valueStr, "%lf", &s_timeoutMultiplier) != 1)
                dataLog("WARNING: --timeoutMultiplier=", valueStr, " is invalid. Expects a numeric ratio.\n");
            continue;
        }

        if (!strcmp(arg, "--test262-async")) {
            asyncTestExpectedPasses++;
            continue;
        }

        if (!strcmp(arg, "--inspectable") || !strcmp(arg, "--remote-debug")) {
            m_inspectable = true;
            continue;
        }

        static const unsigned strictFileStrLength = strlen("--strict-file=");
        if (!strncmp(arg, "--strict-file=", strictFileStrLength)) {
            m_scripts.append(Script(Script::StrictMode::Strict, Script::CodeSource::File, Script::ScriptType::Script, argv[i] + strictFileStrLength));
            continue;
        }

        static const unsigned moduleFileStrLength = strlen("--module-file=");
        if (!strncmp(arg, "--module-file=", moduleFileStrLength)) {
            m_scripts.append(Script(Script::StrictMode::Sloppy, Script::CodeSource::File, Script::ScriptType::Module, argv[i] + moduleFileStrLength));
            continue;
        }

        if (!strcmp(arg, "--dumpException")) {
            m_alwaysDumpUncaughtException = true;
            continue;
        }

        if (!strcmp(arg, "--ignoreUncaughtExceptions")) {
            m_ignoreUncaughtExceptions = true;
            continue;
        }

        if (!strcmp(arg, "--footprint")) {
            m_dumpMemoryFootprint = true;
            continue;
        }

        if (!strcmp(arg, "--dumpLinkBufferStats")) {
            m_dumpLinkBufferStats = true;
            continue;
        }

        static const unsigned exceptionStrLength = strlen("--exception=");
        if (!strncmp(arg, "--exception=", exceptionStrLength)) {
            m_uncaughtExceptionName = String::fromLatin1(arg + exceptionStrLength);
            continue;
        }

        if (!strcmp(arg, "--watchdog-exception-ok")) {
            m_treatWatchdogExceptionAsSuccess = true;
            continue;
        }

        static const unsigned useJITCodeValidationsStrLength = strlen("--useJITCodeValidations=");
        if (!strncmp(arg, "--useJITCodeValidations=", useJITCodeValidationsStrLength)) {
            const char* valueStr = argv[i] + useJITCodeValidationsStrLength;
            bool success = Options::setAllJITCodeValidations(valueStr);
            if (!success) {
                hasBadJSCOptions = true;
                dataLogLn("ERROR: invalid value for --useJITCodeValidations: ", valueStr);
            }
            continue;
        }

        // See if the -- option is a JSC VM option.
        if (strstr(arg, "--") == arg) {
            if (!JSC::Options::setOption(&arg[2], /* verify = */ false)) {
                hasBadJSCOptions = true;
                dataLog("ERROR: invalid option: ", arg, "\n");
            }
            continue;
        }

        // This arg is not recognized by the VM nor by jsc. Pass it on to the
        // script.
        Script::ScriptType scriptType = isMJSFile(argv[i]) ? Script::ScriptType::Module : Script::ScriptType::Script;
        m_scripts.append(Script(Script::StrictMode::Sloppy, Script::CodeSource::File, scriptType, argv[i]));
    }

    if (hasBadJSCOptions && JSC::Options::validateOptions())
        CRASH();

    JSC::Options::notifyOptionsChanged();

    if (m_scripts.isEmpty())
        m_interactive = true;

    for (; i < argc; ++i)
        m_arguments.append(String::fromLatin1(argv[i]));

    JSC::Options::assertOptionsAreCoherent();
    if (optionsDumpRequested) {
        JSC::Options::executeDumpOptions();
        jscExit(EXIT_SUCCESS);
    }
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

CommandLine::CommandLine(CommandLineForWorkersTag)
    : m_treatWatchdogExceptionAsSuccess(mainCommandLine->m_treatWatchdogExceptionAsSuccess)
    , m_ignoreUncaughtExceptions(mainCommandLine->m_ignoreUncaughtExceptions)
{
}

template<typename Func>
int runJSC(const CommandLine& options, bool isWorker, const Func& func)
{
    Worker worker(Workers::singleton(), !isWorker);
    
    VM& vm = VM::create(HeapType::Large).leakRef();
    if (!isWorker && options.m_canBlockIsFalse)
        vm.m_typedArrayController = adoptRef(new JSC::SimpleTypedArrayController(false));

    int result;
    bool success;

#if ENABLE(FUZZILLI)
    // Let parent know we are ready.
    if (options.m_reprl) {
        Fuzzilli::initializeReprl();
    }
#endif // ENABLE(FUZZILLI)

    do {
#if ENABLE(FUZZILLI)
        if (options.m_reprl)
            Fuzzilli::waitForCommand();
#endif // ENABLE(FUZZILLI)

        success = true;
        GlobalObject* globalObject = nullptr;
        {
            JSLockHolder locker(vm);

            startTimeoutThreadIfNeeded(vm);
            globalObject = GlobalObject::create(vm, GlobalObject::createStructure(vm, jsNull()), options.m_arguments);
            globalObject->setInspectable(options.m_inspectable);
            func(vm, globalObject, success);
            vm.drainMicrotasks();
        }
        vm.deferredWorkTimer->runRunLoop();
        {
            JSLockHolder locker(vm);

            if (!options.m_reprl && options.m_interactive && success)
                runInteractive(globalObject);
        }

        result = success && (asyncTestExpectedPasses == asyncTestPasses) ? 0 : 3;

        if (options.m_exitCode) {
            printf("jsc exiting %d", result);
            if (asyncTestExpectedPasses != asyncTestPasses)
                printf(" because expected: %d async test passes but got: %d async test passes", asyncTestExpectedPasses, asyncTestPasses);
            printf("\n");
        }

        if (Options::useProfiler()) {
            JSLockHolder locker(vm);
            if (!vm.m_perBytecodeProfiler->save(options.m_profilerOutput.utf8().data()))
                fprintf(stderr, "could not save profiler output.\n");
        }


#if ENABLE(REGEXP_TRACING)
        {
            JSLockHolder locker(vm);
            vm.dumpRegExpTrace();
        }
#endif

#if ENABLE(JIT)
        {
            JSLockHolder locker(vm);
            if (Options::useExceptionFuzz())
                printf("JSC EXCEPTION FUZZ: encountered %u checks.\n", numberOfExceptionFuzzChecks());
            bool fireAtEnabled = Options::fireExecutableAllocationFuzzAt() || Options::fireExecutableAllocationFuzzAtOrAfter();
            if (Options::verboseExecutableAllocationFuzz() && Options::useExecutableAllocationFuzz() && !fireAtEnabled)
                printf("JSC EXECUTABLE ALLOCATION FUZZ: encountered %u checks.\n", numberOfExecutableAllocationFuzzChecks());
            if (Options::useOSRExitFuzz() && Options::verboseOSRExitFuzz()) {
                printf("JSC OSR EXIT FUZZ: encountered %u static checks.\n", numberOfStaticOSRExitFuzzChecks());
                printf("JSC OSR EXIT FUZZ: encountered %u dynamic checks.\n", numberOfOSRExitFuzzChecks());
            }

            auto compileTimeStats = JIT::compileTimeStats();
            Vector<CString> compileTimeKeys;
            for (auto& entry : compileTimeStats)
                compileTimeKeys.append(entry.key);
            std::sort(compileTimeKeys.begin(), compileTimeKeys.end());
            for (const CString& key : compileTimeKeys) {
                if (key.data())
                    printf("%40s: %.3lf ms\n", key.data(), compileTimeStats.get(key).milliseconds());
            }

            if (Options::reportTotalPhaseTimes())
                logTotalPhaseTimes();
        }
#endif

        if (Options::gcAtEnd()) {
            // We need to hold the API lock to do a GC.
            JSLockHolder locker(&vm);
            vm.heap.collectNow(Sync, CollectionScope::Full);
        }

        if (options.m_dumpSamplingProfilerData) {
#if ENABLE(SAMPLING_PROFILER)
            JSLockHolder locker(&vm);
            vm.samplingProfiler()->reportTopFunctions();
            vm.samplingProfiler()->reportTopBytecodes();
#else
            dataLog("Sampling profiler is not enabled on this platform\n");
#endif
        }

#if ENABLE(JIT)
        if (vm.jitSizeStatistics)
            dataLogLn(*vm.jitSizeStatistics);
#endif

#if ENABLE(FUZZILLI)
        if (options.m_reprl) {
            Fuzzilli::flushReprl(result);
            continue;
        }
#endif // ENABLE(FUZZILLI)
    } while (options.m_reprl);

    vm.codeCache()->write();

    if (options.m_destroyVM || isWorker) {
        JSLockHolder locker(vm);
        // This is needed because we don't want the worker's main
        // thread to die before its compilation threads finish.
        vm.derefSuppressingSaferCPPChecking();
    }

    return result;
}

#if ENABLE(JIT_OPERATION_VALIDATION) || ENABLE(JIT_OPERATION_DISASSEMBLY)
extern const JITOperationAnnotation startOfJITOperationsInShell __asm("section$start$__DATA_CONST$__jsc_ops");
extern const JITOperationAnnotation endOfJITOperationsInShell __asm("section$end$__DATA_CONST$__jsc_ops");
#endif

int jscmain(int argc, char** argv)
{
    // Need to override and enable restricted options before we start parsing options below.
    JSC::Config::enableRestrictedOptions();
    JSC::Options::machExceptionHandlerSandboxPolicy = JSC::Options::SandboxPolicy::Allow;

    WTF::initializeMainThread();

    // Note that the options parsing can affect VM creation, and thus
    // comes first.
    mainCommandLine.construct(argc, argv);

#if OS(WINDOWS)
    // Needed for complex.yaml tests.
    if (char* tz = getenv("TZ"))
        setTimeZoneOverride(StringView::fromLatin1(tz));
#endif

    {
        Options::AllowUnfinalizedAccessScope scope;
        processConfigFile(Options::configFile(), "jsc");
        if (mainCommandLine->m_dump)
            Options::dumpGeneratedBytecodes() = true;
    }

    JSC::initialize();
#if ENABLE(JIT_OPERATION_VALIDATION)
    JSC::JITOperationList::populatePointersInEmbedder(&startOfJITOperationsInShell, &endOfJITOperationsInShell);
#endif
#if ENABLE(JIT_OPERATION_DISASSEMBLY)
    if (Options::needDisassemblySupport()) [[unlikely]]
        JSC::JITOperationList::populateDisassemblyLabelsInEmbedder(&startOfJITOperationsInShell, &endOfJITOperationsInShell);
#endif

    if (!Options::maxHeapSizeAsRAMSizeMultiple())
        Options::maxHeapSizeAsRAMSizeMultiple() = 2;

    initializeTimeoutIfNeeded();

#if OS(DARWIN) || OS(LINUX)
    startMemoryMonitoringThreadIfNeeded();
#endif

    if (Options::useSuperSampler())
        enableSuperSampler();

    bool gigacageDisableRequested = false;
#if GIGACAGE_ENABLED && !OS(WINDOWS)
    if (char* gigacageEnabled = getenv("GIGACAGE_ENABLED")) {
        if (!strcasecmp(gigacageEnabled, "no") || !strcasecmp(gigacageEnabled, "false") || !strcasecmp(gigacageEnabled, "0"))
            gigacageDisableRequested = true;
    }
#endif
    if (!gigacageDisableRequested)
        Gigacage::forbidDisablingPrimitiveGigacage();

#if PLATFORM(COCOA)
    auto& memoryPressureHandler = MemoryPressureHandler::singleton();
    {
        auto queue = adoptOSObject(dispatch_queue_create("jsc shell memory pressure handler", DISPATCH_QUEUE_SERIAL));
        memoryPressureHandler.setDispatchQueue(WTFMove(queue));
    }
    Box<Critical> memoryPressureCriticalState = Box<Critical>::create(Critical::No);
    Box<Synchronous> memoryPressureSynchronousState = Box<Synchronous>::create(Synchronous::No);
    memoryPressureHandler.setLowMemoryHandler([=] (Critical critical, Synchronous synchronous) {
        crashIfExceedingMemoryLimit();

        // We set these racily with respect to reading them from the JS execution thread.
        *memoryPressureCriticalState = critical;
        *memoryPressureSynchronousState = synchronous;
    });
    memoryPressureHandler.setShouldLogMemoryMemoryPressureEvents(false);
    memoryPressureHandler.install();

    auto onEachMicrotaskTick = [&] (VM& vm) {
        if (*memoryPressureCriticalState == Critical::No)
            return;

        *memoryPressureCriticalState = Critical::No;
        bool isSynchronous = *memoryPressureSynchronousState == Synchronous::Yes;

        WTF::releaseFastMallocFreeMemory();
        vm.deleteAllCode(DeleteAllCodeIfNotCollecting);

        if (!vm.heap.currentThreadIsDoingGCWork()) {
            if (isSynchronous) {
                vm.heap.collectNow(Sync, CollectionScope::Full);
                WTF::releaseFastMallocFreeMemory();
            } else
                vm.heap.collectNowFullIfNotDoneRecently(Async);
        }
    };
#endif

    int result = runJSC(
        mainCommandLine.get(), false,
        [&] (VM& vm, GlobalObject* globalObject, bool& success) {
            UNUSED_PARAM(vm);
#if PLATFORM(COCOA)
            vm.setOnEachMicrotaskTick(WTFMove(onEachMicrotaskTick));
#endif
            runWithOptions(globalObject, mainCommandLine.get(), success);
        });

    printSuperSamplerState();

    if (mainCommandLine->m_dumpMemoryFootprint) {
        MemoryFootprint footprint = MemoryFootprint::now();

        printf("Memory Footprint:\n    Current Footprint: %" PRIu64 "\n    Peak Footprint: %" PRIu64 "\n", footprint.current, footprint.peak);
    }

#if ENABLE(ASSEMBLER)
    if (mainCommandLine->m_dumpLinkBufferStats)
        LinkBuffer::dumpProfileStatistics();
#endif

    return result;
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
