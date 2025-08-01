/*
 *  Copyright (C) 2008-2021 Apple Inc. All rights reserved.
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

#pragma once

#include "ButterflyInlines.h"
#include "GCDeferralContextInlines.h"
#include "JSArray.h"
#include "JSCInlines.h"
#include "JSGlobalObject.h"
#include "ObjectConstructor.h"
#include "RegExpInlines.h"
#include "RegExpObject.h"

namespace JSC {

static constexpr PropertyOffset RegExpMatchesArrayIndexPropertyOffset = firstOutOfLineOffset;
static constexpr PropertyOffset RegExpMatchesArrayInputPropertyOffset = firstOutOfLineOffset + 1;
static constexpr PropertyOffset RegExpMatchesArrayGroupsPropertyOffset = firstOutOfLineOffset + 2;
static constexpr PropertyOffset RegExpMatchesArrayIndicesPropertyOffset = firstOutOfLineOffset + 3;
static constexpr PropertyOffset RegExpMatchesIndicesGroupsPropertyOffset = firstOutOfLineOffset;

ALWAYS_INLINE JSArray* tryCreateUninitializedRegExpMatchesArray(ObjectInitializationScope& scope, GCDeferralContext* deferralContext, Structure* structure, unsigned initialLength)
{
    VM& vm = scope.vm();
    unsigned vectorLength = initialLength;
    if (vectorLength > MAX_STORAGE_VECTOR_LENGTH)
        return nullptr;

    const bool hasIndexingHeader = true;
    Butterfly* butterfly = Butterfly::tryCreateUninitialized(vm, nullptr, 0, structure->outOfLineCapacity(), hasIndexingHeader, vectorLength * sizeof(EncodedJSValue), deferralContext);
    if (!butterfly) [[unlikely]]
        return nullptr;

    butterfly->setVectorLength(vectorLength);
    butterfly->setPublicLength(initialLength);

    for (unsigned i = initialLength; i < vectorLength; ++i)
        butterfly->contiguous().atUnsafe(i).clear();

    JSArray* result = JSArray::createWithButterfly(vm, deferralContext, structure, butterfly);

    scope.notifyAllocated(result);
    return result;
}

ALWAYS_INLINE JSArray* createRegExpMatchesArray(
    VM& vm, JSGlobalObject* globalObject, JSString* input, StringView inputValue,
    RegExp* regExp, unsigned startOffset, MatchResult& result)
{
    if constexpr (validateDFGDoesGC)
        vm.verifyCanGC();

    Vector<int, 32> subpatternResults;
    int position = regExp->matchInline(globalObject, vm, inputValue, startOffset, subpatternResults);
    if (position == -1) {
        result = MatchResult::failed();
        return nullptr;
    }

    result.start = position;
    result.end = subpatternResults[1];
    
    JSArray* array;
    JSArray* indicesArray = nullptr;

    // FIXME: This should handle array allocation errors gracefully.
    // https://bugs.webkit.org/show_bug.cgi?id=155144
    
    unsigned numSubpatterns = regExp->numSubpatterns();
    bool hasNamedCaptures = regExp->hasNamedCaptures();
    bool createIndices = regExp->hasIndices();
    JSObject* groups = hasNamedCaptures ? constructEmptyObject(vm, globalObject->nullPrototypeObjectStructure()) : nullptr;
    Structure* matchStructure = createIndices ? globalObject->regExpMatchesArrayWithIndicesStructure() : globalObject->regExpMatchesArrayStructure();

    JSObject* indicesGroups = createIndices && hasNamedCaptures ? constructEmptyObject(vm, globalObject->nullPrototypeObjectStructure()) : nullptr;

    auto setProperties = [&] () {
        array->putDirectOffset(vm, RegExpMatchesArrayIndexPropertyOffset, jsNumber(result.start));
        array->putDirectOffset(vm, RegExpMatchesArrayInputPropertyOffset, input);
        array->putDirectOffset(vm, RegExpMatchesArrayGroupsPropertyOffset, hasNamedCaptures ? groups : jsUndefined());

        ASSERT(!array->butterfly()->indexingHeader()->preCapacity(matchStructure));
        auto capacity = matchStructure->outOfLineCapacity();
        auto size = matchStructure->outOfLineSize();
        gcSafeZeroMemory(static_cast<JSValue*>(array->butterfly()->base(0, capacity)), (capacity - size) * sizeof(JSValue));

        if (createIndices) {
            array->putDirectOffset(vm, RegExpMatchesArrayIndicesPropertyOffset, indicesArray);

            Structure* indicesStructure = globalObject->regExpMatchesIndicesArrayStructure();

            indicesArray->putDirectOffset(vm, RegExpMatchesIndicesGroupsPropertyOffset, indicesGroups ? indicesGroups : jsUndefined());

            ASSERT(!indicesArray->butterfly()->indexingHeader()->preCapacity(indicesStructure));
            auto indicesCapacity = indicesStructure->outOfLineCapacity();
            auto indicesSize = indicesStructure->outOfLineSize();
            gcSafeZeroMemory(static_cast<JSValue*>(indicesArray->butterfly()->base(0, indicesCapacity)), (indicesCapacity - indicesSize) * sizeof(JSValue));
        }
    };

    auto createIndexArray = [&] (GCDeferralContext& deferralContext, int start, int end) {
        ObjectInitializationScope scope(vm);

        JSArray* result = JSArray::tryCreateUninitializedRestricted(scope, &deferralContext, globalObject->arrayStructureForIndexingTypeDuringAllocation(ArrayWithContiguous), 2);
        result->initializeIndexWithoutBarrier(scope, 0, jsNumber(start));
        result->initializeIndexWithoutBarrier(scope, 1, jsNumber(end));
        
        return result;
    };

    if (globalObject->isHavingABadTime()) [[unlikely]] {
        GCDeferralContext deferralContext(vm);
        ObjectInitializationScope matchesArrayScope(vm);
        ObjectInitializationScope indicesArrayScope(vm);
        array = JSArray::tryCreateUninitializedRestricted(matchesArrayScope, &deferralContext, matchStructure, numSubpatterns + 1);

        if (createIndices)
            indicesArray = JSArray::tryCreateUninitializedRestricted(indicesArrayScope, &deferralContext, globalObject->regExpMatchesIndicesArrayStructure(), numSubpatterns + 1);

        // FIXME: we should probably throw an out of memory error here, but
        // when making this change we should check that all clients of this
        // function will correctly handle an exception being thrown from here.
        // https://bugs.webkit.org/show_bug.cgi?id=169786
        RELEASE_ASSERT(array);

        setProperties();
        
        array->initializeIndexWithoutBarrier(matchesArrayScope, 0, jsSubstringOfResolved(vm, &deferralContext, input, result.start, result.end - result.start));

        for (unsigned i = 1; i <= numSubpatterns; ++i) {
            int start = subpatternResults[2 * i];
            JSValue value;
            if (start >= 0)
                value = jsSubstringOfResolved(vm, &deferralContext, input, start, subpatternResults[2 * i + 1] - start);
            else
                value = jsUndefined();
            array->initializeIndexWithoutBarrier(matchesArrayScope, i, value);
        }

        if (createIndices) {
            for (unsigned i = 0; i <= numSubpatterns; ++i) {
                int start = subpatternResults[2 * i];
                JSValue value;
                if (start >= 0)
                    indicesArray->initializeIndexWithoutBarrier(indicesArrayScope, i, createIndexArray(deferralContext, start, subpatternResults[2 * i + 1]));
                else
                    indicesArray->initializeIndexWithoutBarrier(indicesArrayScope, i, jsUndefined());
            }
        }
    } else {
        GCDeferralContext deferralContext(vm);
        ObjectInitializationScope matchesArrayScope(vm);
        ObjectInitializationScope indicesArrayScope(vm);
        array = tryCreateUninitializedRegExpMatchesArray(matchesArrayScope, &deferralContext, matchStructure, numSubpatterns + 1);

        if (createIndices)
            indicesArray = tryCreateUninitializedRegExpMatchesArray(indicesArrayScope, &deferralContext, globalObject->regExpMatchesIndicesArrayStructure(), numSubpatterns + 1);

        // FIXME: we should probably throw an out of memory error here, but
        // when making this change we should check that all clients of this
        // function will correctly handle an exception being thrown from here.
        // https://bugs.webkit.org/show_bug.cgi?id=169786
        RELEASE_ASSERT(array);
        
        setProperties();
        
        array->initializeIndexWithoutBarrier(matchesArrayScope, 0, jsSubstringOfResolved(vm, &deferralContext, input, result.start, result.end - result.start), ArrayWithContiguous);
        
        for (unsigned i = 1; i <= numSubpatterns; ++i) {
            int start = subpatternResults[2 * i];
            JSValue value;
            if (start >= 0)
                value = jsSubstringOfResolved(vm, &deferralContext, input, start, subpatternResults[2 * i + 1] - start);
            else
                value = jsUndefined();
            array->initializeIndexWithoutBarrier(matchesArrayScope, i, value, ArrayWithContiguous);
        }

        if (createIndices) {
            for (unsigned i = 0; i <= numSubpatterns; ++i) {
                int start = subpatternResults[2 * i];
                if (start >= 0)
                    indicesArray->initializeIndexWithoutBarrier(indicesArrayScope, i, createIndexArray(deferralContext, start, subpatternResults[2 * i + 1]));
                else
                    indicesArray->initializeIndexWithoutBarrier(indicesArrayScope, i, jsUndefined());
            }
        }
    }

    // Now the object is safe to scan by GC.

    // We initialize the groups and indices objects late as they could allocate, which with the current API could cause
    // allocations.
    if (hasNamedCaptures) {
        for (unsigned i = 1; i <= numSubpatterns; ++i) {
            String groupName = regExp->getCaptureGroupNameForSubpatternId(i);
            if (!groupName.isEmpty()) {
                auto captureIndex = regExp->subpatternIdForGroupName(groupName, subpatternResults);

                JSValue value;
                if (captureIndex > 0)
                    value = array->getIndexQuickly(captureIndex);
                else
                    value = jsUndefined();
                groups->putDirect(vm, Identifier::fromString(vm, groupName), value);

                if (createIndices && captureIndex > 0)
                    indicesGroups->putDirect(vm, Identifier::fromString(vm, groupName), indicesArray->getIndexQuickly(captureIndex));
            }
        }
    }

    return array;
}

JSArray* createEmptyRegExpMatchesArray(JSGlobalObject*, JSString*, RegExp*);
Structure* createRegExpMatchesArrayStructure(VM&, JSGlobalObject*);
Structure* createRegExpMatchesArrayWithIndicesStructure(VM&, JSGlobalObject*);
Structure* createRegExpMatchesIndicesArrayStructure(VM&, JSGlobalObject*);
Structure* createRegExpMatchesArraySlowPutStructure(VM&, JSGlobalObject*);
Structure* createRegExpMatchesArrayWithIndicesSlowPutStructure(VM&, JSGlobalObject*);
Structure* createRegExpMatchesIndicesArraySlowPutStructure(VM&, JSGlobalObject*);

} // namespace JSC
