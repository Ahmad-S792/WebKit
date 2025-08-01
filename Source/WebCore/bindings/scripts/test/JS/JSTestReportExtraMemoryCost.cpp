/*
    This file is part of the WebKit open source project.
    This file has been generated by generate-bindings.pl. DO NOT MODIFY!

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"
#include "JSTestReportExtraMemoryCost.h"

#include "ActiveDOMObject.h"
#include "ContextDestructionObserverInlines.h"
#include "ExtendedDOMClientIsoSubspaces.h"
#include "ExtendedDOMIsoSubspaces.h"
#include "JSDOMBinding.h"
#include "JSDOMConstructorNotConstructable.h"
#include "JSDOMExceptionHandling.h"
#include "JSDOMGlobalObjectInlines.h"
#include "JSDOMWrapperCache.h"
#include "ScriptExecutionContext.h"
#include "WebCoreJSClientData.h"
#include <JavaScriptCore/FunctionPrototype.h>
#include <JavaScriptCore/HeapAnalyzer.h>
#include <JavaScriptCore/JSCInlines.h>
#include <JavaScriptCore/JSDestructibleObjectHeapCellType.h>
#include <JavaScriptCore/SlotVisitorMacros.h>
#include <JavaScriptCore/SubspaceInlines.h>
#include <wtf/GetPtr.h>
#include <wtf/PointerPreparations.h>
#include <wtf/URL.h>
#include <wtf/text/MakeString.h>

namespace WebCore {
using namespace JSC;

// Attributes

static JSC_DECLARE_CUSTOM_GETTER(jsTestReportExtraMemoryCostConstructor);

class JSTestReportExtraMemoryCostPrototype final : public JSC::JSNonFinalObject {
public:
    using Base = JSC::JSNonFinalObject;
    static JSTestReportExtraMemoryCostPrototype* create(JSC::VM& vm, JSDOMGlobalObject* globalObject, JSC::Structure* structure)
    {
        JSTestReportExtraMemoryCostPrototype* ptr = new (NotNull, JSC::allocateCell<JSTestReportExtraMemoryCostPrototype>(vm)) JSTestReportExtraMemoryCostPrototype(vm, globalObject, structure);
        ptr->finishCreation(vm);
        return ptr;
    }

    DECLARE_INFO;
    template<typename CellType, JSC::SubspaceAccess>
    static JSC::GCClient::IsoSubspace* subspaceFor(JSC::VM& vm)
    {
        STATIC_ASSERT_ISO_SUBSPACE_SHARABLE(JSTestReportExtraMemoryCostPrototype, Base);
        return &vm.plainObjectSpace();
    }
    static JSC::Structure* createStructure(JSC::VM& vm, JSC::JSGlobalObject* globalObject, JSC::JSValue prototype)
    {
        return JSC::Structure::create(vm, globalObject, prototype, JSC::TypeInfo(JSC::ObjectType, StructureFlags), info());
    }

private:
    JSTestReportExtraMemoryCostPrototype(JSC::VM& vm, JSC::JSGlobalObject*, JSC::Structure* structure)
        : JSC::JSNonFinalObject(vm, structure)
    {
    }

    void finishCreation(JSC::VM&);
};
STATIC_ASSERT_ISO_SUBSPACE_SHARABLE(JSTestReportExtraMemoryCostPrototype, JSTestReportExtraMemoryCostPrototype::Base);

using JSTestReportExtraMemoryCostDOMConstructor = JSDOMConstructorNotConstructable<JSTestReportExtraMemoryCost>;

template<> const ClassInfo JSTestReportExtraMemoryCostDOMConstructor::s_info = { "TestReportExtraMemoryCost"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestReportExtraMemoryCostDOMConstructor) };

template<> JSValue JSTestReportExtraMemoryCostDOMConstructor::prototypeForStructure(JSC::VM& vm, const JSDOMGlobalObject& globalObject)
{
    UNUSED_PARAM(vm);
    return globalObject.functionPrototype();
}

template<> void JSTestReportExtraMemoryCostDOMConstructor::initializeProperties(VM& vm, JSDOMGlobalObject& globalObject)
{
    putDirect(vm, vm.propertyNames->length, jsNumber(0), JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum);
    JSString* nameString = jsNontrivialString(vm, "TestReportExtraMemoryCost"_s);
    m_originalName.set(vm, this, nameString);
    putDirect(vm, vm.propertyNames->name, nameString, JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum);
    putDirect(vm, vm.propertyNames->prototype, JSTestReportExtraMemoryCost::prototype(vm, globalObject), JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum | JSC::PropertyAttribute::DontDelete);
}

/* Hash table for prototype */

static const std::array<HashTableValue, 1> JSTestReportExtraMemoryCostPrototypeTableValues {
    HashTableValue { "constructor"_s, static_cast<unsigned>(PropertyAttribute::DontEnum), NoIntrinsic, { HashTableValue::GetterSetterType, jsTestReportExtraMemoryCostConstructor, 0 } },
};

const ClassInfo JSTestReportExtraMemoryCostPrototype::s_info = { "TestReportExtraMemoryCost"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestReportExtraMemoryCostPrototype) };

void JSTestReportExtraMemoryCostPrototype::finishCreation(VM& vm)
{
    Base::finishCreation(vm);
    reifyStaticProperties(vm, JSTestReportExtraMemoryCost::info(), JSTestReportExtraMemoryCostPrototypeTableValues, *this);
    JSC_TO_STRING_TAG_WITHOUT_TRANSITION();
}

const ClassInfo JSTestReportExtraMemoryCost::s_info = { "TestReportExtraMemoryCost"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestReportExtraMemoryCost) };

JSTestReportExtraMemoryCost::JSTestReportExtraMemoryCost(Structure* structure, JSDOMGlobalObject& globalObject, Ref<TestReportExtraMemoryCost>&& impl)
    : JSDOMWrapper<TestReportExtraMemoryCost>(structure, globalObject, WTFMove(impl))
{
}

static_assert(!std::is_base_of<ActiveDOMObject, TestReportExtraMemoryCost>::value, "Interface is not marked as [ActiveDOMObject] even though implementation class subclasses ActiveDOMObject.");

void JSTestReportExtraMemoryCost::finishCreation(VM& vm)
{
    Base::finishCreation(vm);
    ASSERT(inherits(info()));

    vm.heap.reportExtraMemoryAllocated(this, wrapped().memoryCost());
}

JSObject* JSTestReportExtraMemoryCost::createPrototype(VM& vm, JSDOMGlobalObject& globalObject)
{
    auto* structure = JSTestReportExtraMemoryCostPrototype::createStructure(vm, &globalObject, globalObject.objectPrototype());
    structure->setMayBePrototype(true);
    return JSTestReportExtraMemoryCostPrototype::create(vm, &globalObject, structure);
}

JSObject* JSTestReportExtraMemoryCost::prototype(VM& vm, JSDOMGlobalObject& globalObject)
{
    return getDOMPrototype<JSTestReportExtraMemoryCost>(vm, globalObject);
}

JSValue JSTestReportExtraMemoryCost::getConstructor(VM& vm, const JSGlobalObject* globalObject)
{
    return getDOMConstructor<JSTestReportExtraMemoryCostDOMConstructor, DOMConstructorID::TestReportExtraMemoryCost>(vm, *jsCast<const JSDOMGlobalObject*>(globalObject));
}

void JSTestReportExtraMemoryCost::destroy(JSC::JSCell* cell)
{
    JSTestReportExtraMemoryCost* thisObject = static_cast<JSTestReportExtraMemoryCost*>(cell);
    thisObject->JSTestReportExtraMemoryCost::~JSTestReportExtraMemoryCost();
}

JSC_DEFINE_CUSTOM_GETTER(jsTestReportExtraMemoryCostConstructor, (JSGlobalObject* lexicalGlobalObject, EncodedJSValue thisValue, PropertyName))
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    auto* prototype = jsDynamicCast<JSTestReportExtraMemoryCostPrototype*>(JSValue::decode(thisValue));
    if (!prototype) [[unlikely]]
        return throwVMTypeError(lexicalGlobalObject, throwScope);
    return JSValue::encode(JSTestReportExtraMemoryCost::getConstructor(vm, prototype->globalObject()));
}

JSC::GCClient::IsoSubspace* JSTestReportExtraMemoryCost::subspaceForImpl(JSC::VM& vm)
{
    return WebCore::subspaceForImpl<JSTestReportExtraMemoryCost, UseCustomHeapCellType::No>(vm, "JSTestReportExtraMemoryCost"_s,
        [] (auto& spaces) { return spaces.m_clientSubspaceForTestReportExtraMemoryCost.get(); },
        [] (auto& spaces, auto&& space) { spaces.m_clientSubspaceForTestReportExtraMemoryCost = std::forward<decltype(space)>(space); },
        [] (auto& spaces) { return spaces.m_subspaceForTestReportExtraMemoryCost.get(); },
        [] (auto& spaces, auto&& space) { spaces.m_subspaceForTestReportExtraMemoryCost = std::forward<decltype(space)>(space); }
    );
}

template<typename Visitor>
void JSTestReportExtraMemoryCost::visitChildrenImpl(JSCell* cell, Visitor& visitor)
{
    auto* thisObject = jsCast<JSTestReportExtraMemoryCost*>(cell);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());
    Base::visitChildren(thisObject, visitor);
    visitor.reportExtraMemoryVisited(thisObject->wrapped().memoryCost());
}

DEFINE_VISIT_CHILDREN(JSTestReportExtraMemoryCost);

size_t JSTestReportExtraMemoryCost::estimatedSize(JSCell* cell, VM& vm)
{
    auto* thisObject = jsCast<JSTestReportExtraMemoryCost*>(cell);
    return Base::estimatedSize(thisObject, vm) + thisObject->wrapped().memoryCost();
}

void JSTestReportExtraMemoryCost::analyzeHeap(JSCell* cell, HeapAnalyzer& analyzer)
{
    auto* thisObject = jsCast<JSTestReportExtraMemoryCost*>(cell);
    analyzer.setWrappedObjectForCell(cell, &thisObject->wrapped());
    if (RefPtr context = thisObject->scriptExecutionContext())
        analyzer.setLabelForCell(cell, makeString("url "_s, context->url().string()));
    Base::analyzeHeap(cell, analyzer);
}

bool JSTestReportExtraMemoryCostOwner::isReachableFromOpaqueRoots(JSC::Handle<JSC::Unknown> handle, void*, AbstractSlotVisitor& visitor, ASCIILiteral* reason)
{
    UNUSED_PARAM(handle);
    UNUSED_PARAM(visitor);
    UNUSED_PARAM(reason);
    return false;
}

void JSTestReportExtraMemoryCostOwner::finalize(JSC::Handle<JSC::Unknown> handle, void* context)
{
    auto* jsTestReportExtraMemoryCost = static_cast<JSTestReportExtraMemoryCost*>(handle.slot()->asCell());
    auto& world = *static_cast<DOMWrapperWorld*>(context);
    uncacheWrapper(world, jsTestReportExtraMemoryCost->protectedWrapped().ptr(), jsTestReportExtraMemoryCost);
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN
#if ENABLE(BINDING_INTEGRITY)
#if PLATFORM(WIN)
#pragma warning(disable: 4483)
extern "C" { extern void (*const __identifier("??_7TestReportExtraMemoryCost@WebCore@@6B@")[])(); }
#else
extern "C" { extern void* _ZTVN7WebCore25TestReportExtraMemoryCostE[]; }
#endif
template<std::same_as<TestReportExtraMemoryCost> T>
static inline void verifyVTable(TestReportExtraMemoryCost* ptr) 
{
    if constexpr (std::is_polymorphic_v<T>) {
        const void* actualVTablePointer = getVTablePointer<T>(ptr);
#if PLATFORM(WIN)
        void* expectedVTablePointer = __identifier("??_7TestReportExtraMemoryCost@WebCore@@6B@");
#else
        void* expectedVTablePointer = &_ZTVN7WebCore25TestReportExtraMemoryCostE[2];
#endif

        // If you hit this assertion you either have a use after free bug, or
        // TestReportExtraMemoryCost has subclasses. If TestReportExtraMemoryCost has subclasses that get passed
        // to toJS() we currently require TestReportExtraMemoryCost you to opt out of binding hardening
        // by adding the SkipVTableValidation attribute to the interface IDL definition
        RELEASE_ASSERT(actualVTablePointer == expectedVTablePointer);
    }
}
#endif
WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

JSC::JSValue toJSNewlyCreated(JSC::JSGlobalObject*, JSDOMGlobalObject* globalObject, Ref<TestReportExtraMemoryCost>&& impl)
{
#if ENABLE(BINDING_INTEGRITY)
    verifyVTable<TestReportExtraMemoryCost>(impl.ptr());
#endif
    return createWrapper<TestReportExtraMemoryCost>(globalObject, WTFMove(impl));
}

JSC::JSValue toJS(JSC::JSGlobalObject* lexicalGlobalObject, JSDOMGlobalObject* globalObject, TestReportExtraMemoryCost& impl)
{
    return wrap(lexicalGlobalObject, globalObject, impl);
}

TestReportExtraMemoryCost* JSTestReportExtraMemoryCost::toWrapped(JSC::VM&, JSC::JSValue value)
{
    if (auto* wrapper = jsDynamicCast<JSTestReportExtraMemoryCost*>(value))
        return &wrapper->wrapped();
    return nullptr;
}

}
