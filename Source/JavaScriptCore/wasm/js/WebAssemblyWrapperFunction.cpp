/*
 * Copyright (C) 2017-2021 Apple Inc. All rights reserved.
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
#include "WebAssemblyWrapperFunction.h"

#if ENABLE(WEBASSEMBLY)

#include "JSObjectInlines.h"
#include "JSWebAssemblyInstance.h"
#include "WasmTypeDefinitionInlines.h"

namespace JSC {

const ClassInfo WebAssemblyWrapperFunction::s_info = { "WebAssemblyWrapperFunction"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(WebAssemblyWrapperFunction) };

static JSC_DECLARE_HOST_FUNCTION(callWebAssemblyWrapperFunction);
static JSC_DECLARE_HOST_FUNCTION(callWebAssemblyWrapperFunctionIncludingInvalidValues);

WebAssemblyWrapperFunction::WebAssemblyWrapperFunction(VM& vm, NativeExecutable* executable, JSGlobalObject* globalObject, Structure* structure, JSWebAssemblyInstance* instance, JSObject* function, Wasm::WasmOrJSImportableFunction&& importableFunction, WasmOrJSImportableFunctionCallLinkInfo* callLinkInfo)
    : Base(vm, executable, globalObject, structure, instance, WTFMove(importableFunction), callLinkInfo)
    , m_function(function, WriteBarrierEarlyInit)
{ }

WebAssemblyWrapperFunction* WebAssemblyWrapperFunction::create(VM& vm, JSGlobalObject* globalObject, Structure* structure, JSObject* function, unsigned importIndex, JSWebAssemblyInstance* instance, Wasm::TypeIndex typeIndex, RefPtr<const Wasm::RTT>&& rtt)
{
    ASSERT_WITH_MESSAGE(!function->inherits<WebAssemblyWrapperFunction>(), "We should never double wrap a wrapper function.");

    String name = emptyString();
    const auto& signature = Wasm::TypeInformation::getFunctionSignature(typeIndex);
    NativeExecutable* executable = nullptr;
    if (signature.argumentsOrResultsIncludeV128() || signature.argumentsOrResultsIncludeExnref()) [[unlikely]]
        executable = vm.getHostFunction(callWebAssemblyWrapperFunctionIncludingInvalidValues, ImplementationVisibility::Public, NoIntrinsic, callHostFunctionAsConstructor, nullptr, name);
    else
        executable = vm.getHostFunction(callWebAssemblyWrapperFunction, ImplementationVisibility::Public, NoIntrinsic, callHostFunctionAsConstructor, nullptr, name);

    RELEASE_ASSERT(JSValue(function).isCallable());
    WebAssemblyWrapperFunction* result = new (NotNull, allocateCell<WebAssemblyWrapperFunction>(vm)) WebAssemblyWrapperFunction(vm, executable, globalObject, structure, instance, function,
        Wasm::WasmOrJSImportableFunction {
            {
                {
                    &Wasm::NullWasmCallee,
                    { },
                    &instance->importFunctionInfo(importIndex)->importFunctionStub
                },
                typeIndex,
                rtt.get()
            },
            { },
            { }
        },
        instance->importFunctionInfo(importIndex));
    result->m_importableFunction.importFunction.set(vm, globalObject, function);
    result->finishCreation(vm, executable, signature.argumentCount(), name);
    return result;
}

Structure* WebAssemblyWrapperFunction::createStructure(VM& vm, JSGlobalObject* globalObject, JSValue prototype)
{
    ASSERT(globalObject);
    return Structure::create(vm, globalObject, prototype, TypeInfo(JSFunctionType, StructureFlags), info());
}

template<typename Visitor>
void WebAssemblyWrapperFunction::visitChildrenImpl(JSCell* cell, Visitor& visitor)
{
    WebAssemblyWrapperFunction* thisObject = jsCast<WebAssemblyWrapperFunction*>(cell);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());
    Base::visitChildren(thisObject, visitor);

    visitor.append(thisObject->m_function);
}

DEFINE_VISIT_CHILDREN(WebAssemblyWrapperFunction);

JSC_DEFINE_HOST_FUNCTION(callWebAssemblyWrapperFunction, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    WebAssemblyWrapperFunction* wasmFunction = jsCast<WebAssemblyWrapperFunction*>(callFrame->jsCallee());
    JSObject* function = wasmFunction->function();
    auto callData = JSC::getCallData(function);
    RELEASE_ASSERT(callData.type != CallData::Type::None);
    RELEASE_AND_RETURN(scope, JSValue::encode(call(globalObject, function, callData, jsUndefined(), ArgList(callFrame))));
}

JSC_DEFINE_HOST_FUNCTION(callWebAssemblyWrapperFunctionIncludingInvalidValues, (JSGlobalObject* globalObject, CallFrame*))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    return throwVMTypeError(globalObject, scope, Wasm::errorMessageForExceptionType(Wasm::ExceptionType::TypeErrorInvalidValueUse));
}

} // namespace JSC

#endif // ENABLE(WEBASSEMBLY)
