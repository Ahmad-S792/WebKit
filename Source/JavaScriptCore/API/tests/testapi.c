/*
 * Copyright (C) 2006-2022 Apple Inc. All rights reserved.
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

#undef ASSERT_ENABLED
#define ASSERT_ENABLED 1
#include "config.h"

#if USE(CF)
#include "JavaScriptCore.h"
#else
#include "JavaScript.h"
#endif

#include "JSBasePrivate.h"
#include "JSContextRefPrivate.h"
#include "JSHeapFinalizerPrivate.h"
#include "JSMarkingConstraintPrivate.h"
#include "JSObjectRefPrivate.h"
#include "JSScriptRefPrivate.h"
#include "JSStringRefPrivate.h"
#include "JSWeakPrivate.h"
#if !OS(WINDOWS)
#include <libgen.h>
#endif
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if !OS(WINDOWS)
#include <unistd.h>
#endif
#include <wtf/Assertions.h>

#if OS(WINDOWS)
#include <windows.h>
#endif

#include "CompareAndSwapTest.h"
#include "CustomGlobalObjectClassTest.h"
#include "ExecutionTimeLimitTest.h"
#include "FunctionOverridesTest.h"
#include "FunctionToStringTests.h"
#include "GlobalContextWithFinalizerTest.h"
#include "JSONParseTest.h"
#include "JSObjectGetProxyTargetTest.h"
#include "MultithreadedMultiVMExecutionTest.h"
#include "PingPongStackOverflowTest.h"
#include "TypedArrayCTest.h"

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

#if JSC_OBJC_API_ENABLED
void testObjectiveCAPI(const char*);
#endif

void configureJSCForTesting(void);
int testLaunchJSCFromNonMainThread(const char* filter);
int testCAPIViaCpp(const char* filter);

bool assertTrue(bool value, const char* message);

static JSGlobalContextRef context;
int failed;
static void assertEqualsAsBoolean(JSValueRef value, bool expectedValue)
{
    if (JSValueToBoolean(context, value) != expectedValue) {
        fprintf(stderr, "assertEqualsAsBoolean failed: %p, %d\n", value, expectedValue);
        failed = 1;
    }
}

static void assertEqualsAsNumber(JSValueRef value, double expectedValue)
{
    double number = JSValueToNumber(context, value, NULL);

    // FIXME <rdar://4668451> - On i386 the isnan(double) macro tries to map to the isnan(float) function,
    // causing a build break with -Wshorten-64-to-32 enabled.  The issue is known by the appropriate team.
    // After that's resolved, we can remove these casts
    if (number != expectedValue && !(isnan((float)number) && isnan((float)expectedValue))) {
        fprintf(stderr, "assertEqualsAsNumber failed: %p, %lf\n", value, expectedValue);
        failed = 1;
    }
}

static void assertEqualsAsUTF8String(JSValueRef value, const char* expectedValue)
{
    JSStringRef valueAsString = JSValueToStringCopy(context, value, NULL);

    size_t jsSize = JSStringGetMaximumUTF8CStringSize(valueAsString);
    char* jsBuffer = (char*)malloc(jsSize);
    JSStringGetUTF8CString(valueAsString, jsBuffer, jsSize);

    unsigned i;
    for (i = 0; jsBuffer[i]; i++) {
        if (jsBuffer[i] != expectedValue[i]) {
            fprintf(stderr, "assertEqualsAsUTF8String failed at character %d: %c(%d) != %c(%d)\n", i, jsBuffer[i], jsBuffer[i], expectedValue[i], expectedValue[i]);
            fprintf(stderr, "value: %s\n", jsBuffer);
            fprintf(stderr, "expectedValue: %s\n", expectedValue);
            failed = 1;
        }
    }

    if (jsSize < strlen(jsBuffer) + 1) {
        fprintf(stderr, "assertEqualsAsUTF8String failed: jsSize was too small\n");
        failed = 1;
    }

    free(jsBuffer);
    JSStringRelease(valueAsString);
}

static void assertEqualsAsCharactersPtr(JSValueRef value, const char* expectedValue)
{
    JSStringRef valueAsString = JSValueToStringCopy(context, value, NULL);

#if USE(CF)
    size_t jsLength = JSStringGetLength(valueAsString);
    const JSChar* jsBuffer = JSStringGetCharactersPtr(valueAsString);

    CFStringRef expectedValueAsCFString = CFStringCreateWithCString(kCFAllocatorDefault, 
                                                                    expectedValue,
                                                                    kCFStringEncodingUTF8);    
    CFIndex cfLength = CFStringGetLength(expectedValueAsCFString);
    UniChar* cfBuffer = (UniChar*)malloc(cfLength * sizeof(UniChar));
    CFStringGetCharacters(expectedValueAsCFString, CFRangeMake(0, cfLength), cfBuffer);
    CFRelease(expectedValueAsCFString);

    if (memcmp(jsBuffer, cfBuffer, cfLength * sizeof(UniChar)) != 0) {
        fprintf(stderr, "assertEqualsAsCharactersPtr failed: jsBuffer != cfBuffer\n");
        failed = 1;
    }
    
    if (jsLength != (size_t)cfLength) {
        fprintf(stderr, "assertEqualsAsCharactersPtr failed: jsLength(%llu) != cfLength(%llu)\n", (unsigned long long)jsLength, (unsigned long long)cfLength);
        failed = 1;
    }

    free(cfBuffer);
#else
    size_t bufferSize = JSStringGetMaximumUTF8CStringSize(valueAsString);
    char* buffer = (char*)malloc(bufferSize);
    JSStringGetUTF8CString(valueAsString, buffer, bufferSize);

    if (strcmp(buffer, expectedValue)) {
        fprintf(stderr, "assertEqualsAsCharactersPtr failed: jsBuffer != cfBuffer\n");
        failed = 1;
    }

    free(buffer);
#endif
    JSStringRelease(valueAsString);
}

static bool timeZoneIsPST(void)
{
    char timeZoneName[70];
    struct tm gtm;
    memset(&gtm, 0, sizeof(gtm));
    strftime(timeZoneName, sizeof(timeZoneName), "%Z", &gtm);

    return 0 == strcmp("PST", timeZoneName);
}

static JSValueRef jsGlobalValue; // non-stack value for testing JSValueProtect()

/* MyObject pseudo-class */

static bool MyObject_hasProperty(JSContextRef context, JSObjectRef object, JSStringRef propertyName)
{
    UNUSED_PARAM(context);
    UNUSED_PARAM(object);

    if (JSStringIsEqualToUTF8CString(propertyName, "alwaysOne")
        || JSStringIsEqualToUTF8CString(propertyName, "cantFind")
        || JSStringIsEqualToUTF8CString(propertyName, "throwOnGet")
        || JSStringIsEqualToUTF8CString(propertyName, "myPropertyName")
        || JSStringIsEqualToUTF8CString(propertyName, "hasPropertyLie")
        || JSStringIsEqualToUTF8CString(propertyName, "0")) {
        return true;
    }
    
    return false;
}

static JSValueRef throwException(JSContextRef context, JSObjectRef object, JSValueRef* exception)
{
    JSStringRef script = JSStringCreateWithUTF8CString("throw 'an exception'");
    JSStringRef sourceURL = JSStringCreateWithUTF8CString("test script");
    JSValueRef result = JSEvaluateScript(context, script, object, sourceURL, 1, exception);
    JSStringRelease(script);
    JSStringRelease(sourceURL);
    return result;
}

static JSValueRef MyObject_getProperty(JSContextRef context, JSObjectRef object, JSStringRef propertyName, JSValueRef* exception)
{
    UNUSED_PARAM(context);
    UNUSED_PARAM(object);
    
    if (JSStringIsEqualToUTF8CString(propertyName, "alwaysOne")) {
        return JSValueMakeNumber(context, 1);
    }
    
    if (JSStringIsEqualToUTF8CString(propertyName, "myPropertyName")) {
        return JSValueMakeNumber(context, 1);
    }

    if (JSStringIsEqualToUTF8CString(propertyName, "cantFind")) {
        return JSValueMakeUndefined(context);
    }
    
    if (JSStringIsEqualToUTF8CString(propertyName, "hasPropertyLie")) {
        return 0;
    }

    if (JSStringIsEqualToUTF8CString(propertyName, "throwOnGet")) {
        return throwException(context, object, exception);
    }

    if (JSStringIsEqualToUTF8CString(propertyName, "0")) {
        *exception = JSValueMakeNumber(context, 1);
        return JSValueMakeNumber(context, 1);
    }
    
    return JSValueMakeNull(context);
}

static bool MyObject_setProperty(JSContextRef context, JSObjectRef object, JSStringRef propertyName, JSValueRef value, JSValueRef* exception)
{
    UNUSED_PARAM(context);
    UNUSED_PARAM(object);
    UNUSED_PARAM(value);
    UNUSED_PARAM(exception);

    if (JSStringIsEqualToUTF8CString(propertyName, "cantSet"))
        return true; // pretend we set the property in order to swallow it
    
    if (JSStringIsEqualToUTF8CString(propertyName, "throwOnSet")) {
        throwException(context, object, exception);
    }
    
    return false;
}

static bool MyObject_deleteProperty(JSContextRef context, JSObjectRef object, JSStringRef propertyName, JSValueRef* exception)
{
    UNUSED_PARAM(context);
    UNUSED_PARAM(object);
    
    if (JSStringIsEqualToUTF8CString(propertyName, "cantDelete"))
        return true;
    
    if (JSStringIsEqualToUTF8CString(propertyName, "throwOnDelete")) {
        throwException(context, object, exception);
        return false;
    }

    return false;
}

static void MyObject_getPropertyNames(JSContextRef context, JSObjectRef object, JSPropertyNameAccumulatorRef propertyNames)
{
    UNUSED_PARAM(context);
    UNUSED_PARAM(object);
    
    JSStringRef propertyName;
    
    propertyName = JSStringCreateWithUTF8CString("alwaysOne");
    JSPropertyNameAccumulatorAddName(propertyNames, propertyName);
    JSStringRelease(propertyName);
    
    propertyName = JSStringCreateWithUTF8CString("myPropertyName");
    JSPropertyNameAccumulatorAddName(propertyNames, propertyName);
    JSStringRelease(propertyName);
}

static bool isValueEqualToString(JSContextRef context, JSValueRef value, const char* string)
{
    if (!JSValueIsString(context, value))
        return false;
    JSStringRef valueString = JSValueToStringCopy(context, value, NULL);
    if (!valueString)
        return false;
    bool isEqual = JSStringIsEqualToUTF8CString(valueString, string);
    JSStringRelease(valueString);
    return isEqual;
}

static JSValueRef MyObject_callAsFunction(JSContextRef context, JSObjectRef object, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception)
{
    UNUSED_PARAM(context);
    UNUSED_PARAM(object);
    UNUSED_PARAM(thisObject);
    UNUSED_PARAM(exception);

    if (argumentCount > 0 && isValueEqualToString(context, arguments[0], "throwOnCall")) {
        throwException(context, object, exception);
        return JSValueMakeUndefined(context);
    }

    if (argumentCount > 0 && JSValueIsStrictEqual(context, arguments[0], JSValueMakeNumber(context, 0)))
        return JSValueMakeNumber(context, 1);
    
    return JSValueMakeUndefined(context);
}

static JSObjectRef MyObject_callAsConstructor(JSContextRef context, JSObjectRef object, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception)
{
    UNUSED_PARAM(context);
    UNUSED_PARAM(object);

    if (argumentCount > 0 && isValueEqualToString(context, arguments[0], "throwOnConstruct")) {
        throwException(context, object, exception);
        return object;
    }

    if (argumentCount > 0 && JSValueIsStrictEqual(context, arguments[0], JSValueMakeNumber(context, 0)))
        return JSValueToObject(context, JSValueMakeNumber(context, 1), exception);
    
    return JSValueToObject(context, JSValueMakeNumber(context, 0), exception);
}

static bool MyObject_hasInstance(JSContextRef context, JSObjectRef constructor, JSValueRef possibleValue, JSValueRef* exception)
{
    UNUSED_PARAM(context);
    UNUSED_PARAM(constructor);

    if (isValueEqualToString(context, possibleValue, "throwOnHasInstance")) {
        throwException(context, constructor, exception);
        return false;
    }

    JSStringRef numberString = JSStringCreateWithUTF8CString("Number");
    JSObjectRef numberConstructor = JSValueToObject(context, JSObjectGetProperty(context, JSContextGetGlobalObject(context), numberString, exception), exception);
    JSStringRelease(numberString);

    return JSValueIsInstanceOfConstructor(context, possibleValue, numberConstructor, exception);
}

static JSValueRef MyObject_convertToType(JSContextRef context, JSObjectRef object, JSType type, JSValueRef* exception)
{
    UNUSED_PARAM(object);
    UNUSED_PARAM(exception);
    
    switch (type) {
    case kJSTypeNumber:
        return JSValueMakeNumber(context, 1);
    case kJSTypeString:
        {
            JSStringRef string = JSStringCreateWithUTF8CString("MyObjectAsString");
            JSValueRef result = JSValueMakeString(context, string);
            JSStringRelease(string);
            return result;
        }
    default:
        break;
    }

    // string conversion -- forward to default object class
    return JSValueMakeNull(context);
}

static JSValueRef MyObject_convertToTypeWrapper(JSContextRef context, JSObjectRef object, JSType type, JSValueRef* exception)
{
    UNUSED_PARAM(context);
    UNUSED_PARAM(object);
    UNUSED_PARAM(type);
    UNUSED_PARAM(exception);
    // Forward to default object class
    return 0;
}

static bool MyObject_set_nullGetForwardSet(JSContextRef ctx, JSObjectRef object, JSStringRef propertyName, JSValueRef value, JSValueRef* exception)
{
    UNUSED_PARAM(ctx);
    UNUSED_PARAM(object);
    UNUSED_PARAM(propertyName);
    UNUSED_PARAM(value);
    UNUSED_PARAM(exception);
    return false; // Forward to parent class.
}

static const JSStaticValue evilStaticValues[] = {
    { "nullGetSet", 0, 0, kJSPropertyAttributeNone },
    { "nullGetForwardSet", 0, MyObject_set_nullGetForwardSet, kJSPropertyAttributeNone },
    { 0, 0, 0, 0 }
};

static const JSStaticFunction evilStaticFunctions[] = {
    { "nullCall", 0, kJSPropertyAttributeNone },
    { 0, 0, 0 }
};

static const JSClassDefinition MyObject_definition = {
    0,
    kJSClassAttributeNone,
    
    "MyObject",
    NULL,
    
    evilStaticValues,
    evilStaticFunctions,
    
    NULL,
    NULL,
    MyObject_hasProperty,
    MyObject_getProperty,
    MyObject_setProperty,
    MyObject_deleteProperty,
    MyObject_getPropertyNames,
    MyObject_callAsFunction,
    MyObject_callAsConstructor,
    MyObject_hasInstance,
    MyObject_convertToType,
};

static const JSClassDefinition MyObject_convertToTypeWrapperDefinition = {
    0,
    kJSClassAttributeNone,
    
    "MyObject",
    NULL,
    
    NULL,
    NULL,
    
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    MyObject_convertToTypeWrapper,
};

static const JSClassDefinition MyObject_nullWrapperDefinition = {
    0,
    kJSClassAttributeNone,
    
    "MyObject",
    NULL,
    
    NULL,
    NULL,
    
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

static JSClassRef MyObject_class(JSContextRef context)
{
    UNUSED_PARAM(context);

    static JSClassRef jsClass;
    if (!jsClass) {
        JSClassDefinition classDefinition = MyObject_convertToTypeWrapperDefinition;
        JSClassDefinition nullClassDefinition = MyObject_nullWrapperDefinition;
        JSClassRef baseClass = JSClassCreate(&MyObject_definition);
        classDefinition.parentClass = baseClass;
        JSClassRef wrapperClass = JSClassCreate(&classDefinition);
        nullClassDefinition.parentClass = wrapperClass;
        jsClass = JSClassCreate(&nullClassDefinition);
    }

    return jsClass;
}

static JSValueRef PropertyCatchalls_getProperty(JSContextRef context, JSObjectRef object, JSStringRef propertyName, JSValueRef* exception)
{
    UNUSED_PARAM(context);
    UNUSED_PARAM(object);
    UNUSED_PARAM(propertyName);
    UNUSED_PARAM(exception);

    if (JSStringIsEqualToUTF8CString(propertyName, "x")) {
        static size_t count;
        if (count++ < 5)
            return NULL;

        // Swallow all .x gets after 5, returning null.
        return JSValueMakeNull(context);
    }

    if (JSStringIsEqualToUTF8CString(propertyName, "y")) {
        static size_t count;
        if (count++ < 5)
            return NULL;

        // Swallow all .y gets after 5, returning null.
        return JSValueMakeNull(context);
    }
    
    if (JSStringIsEqualToUTF8CString(propertyName, "z")) {
        static size_t count;
        if (count++ < 5)
            return NULL;

        // Swallow all .y gets after 5, returning null.
        return JSValueMakeNull(context);
    }

    return NULL;
}

static bool PropertyCatchalls_setProperty(JSContextRef context, JSObjectRef object, JSStringRef propertyName, JSValueRef value, JSValueRef* exception)
{
    UNUSED_PARAM(context);
    UNUSED_PARAM(object);
    UNUSED_PARAM(propertyName);
    UNUSED_PARAM(value);
    UNUSED_PARAM(exception);

    if (JSStringIsEqualToUTF8CString(propertyName, "x")) {
        static size_t count;

        // Swallow all .x sets after 4.
        return count++ > 4;
    }

    if (JSStringIsEqualToUTF8CString(propertyName, "make_throw") || JSStringIsEqualToUTF8CString(propertyName, "0")) {
        *exception = JSValueMakeNumber(context, 5);
        return true;
    }

    return false;
}

static void PropertyCatchalls_getPropertyNames(JSContextRef context, JSObjectRef object, JSPropertyNameAccumulatorRef propertyNames)
{
    UNUSED_PARAM(context);
    UNUSED_PARAM(object);

    static size_t count;
    static const char* numbers[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
    
    // Provide a property of a different name every time.
    JSStringRef propertyName = JSStringCreateWithUTF8CString(numbers[count++ % 10]);
    JSPropertyNameAccumulatorAddName(propertyNames, propertyName);
    JSStringRelease(propertyName);
}

static const JSClassDefinition PropertyCatchalls_definition = {
    0,
    kJSClassAttributeNone,
    
    "PropertyCatchalls",
    NULL,
    
    NULL,
    NULL,
    
    NULL,
    NULL,
    NULL,
    PropertyCatchalls_getProperty,
    PropertyCatchalls_setProperty,
    NULL,
    PropertyCatchalls_getPropertyNames,
    NULL,
    NULL,
    NULL,
    NULL,
};

static JSClassRef PropertyCatchalls_class(JSContextRef context)
{
    UNUSED_PARAM(context);

    static JSClassRef jsClass;
    if (!jsClass)
        jsClass = JSClassCreate(&PropertyCatchalls_definition);
    
    return jsClass;
}

static bool EvilExceptionObject_hasInstance(JSContextRef context, JSObjectRef constructor, JSValueRef possibleValue, JSValueRef* exception)
{
    UNUSED_PARAM(context);
    UNUSED_PARAM(constructor);
    
    JSStringRef hasInstanceName = JSStringCreateWithUTF8CString("hasInstance");
    JSValueRef hasInstance = JSObjectGetProperty(context, constructor, hasInstanceName, exception);
    JSStringRelease(hasInstanceName);
    if (!hasInstance)
        return false;
    JSObjectRef function = JSValueToObject(context, hasInstance, exception);
    JSValueRef result = JSObjectCallAsFunction(context, function, constructor, 1, &possibleValue, exception);
    return result && JSValueToBoolean(context, result);
}

static JSValueRef EvilExceptionObject_convertToType(JSContextRef context, JSObjectRef object, JSType type, JSValueRef* exception)
{
    UNUSED_PARAM(object);
    UNUSED_PARAM(exception);
    JSStringRef funcName;
    switch (type) {
    case kJSTypeNumber:
        funcName = JSStringCreateWithUTF8CString("toNumber");
        break;
    case kJSTypeString:
        funcName = JSStringCreateWithUTF8CString("toStringExplicit");
        break;
    default:
        return JSValueMakeNull(context);
    }
    
    JSValueRef func = JSObjectGetProperty(context, object, funcName, exception);
    JSStringRelease(funcName);    
    JSObjectRef function = JSValueToObject(context, func, exception);
    if (!function)
        return JSValueMakeNull(context);
    JSValueRef value = JSObjectCallAsFunction(context, function, object, 0, NULL, exception);
    if (!value) {
        JSStringRef errorString = JSStringCreateWithUTF8CString("convertToType failed"); 
        JSValueRef errorStringRef = JSValueMakeString(context, errorString);
        JSStringRelease(errorString);
        return errorStringRef;
    }
    return value;
}

static const JSClassDefinition EvilExceptionObject_definition = {
    0,
    kJSClassAttributeNone,

    "EvilExceptionObject",
    NULL,

    NULL,
    NULL,

    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    EvilExceptionObject_hasInstance,
    EvilExceptionObject_convertToType,
};

static JSClassRef EvilExceptionObject_class(JSContextRef context)
{
    UNUSED_PARAM(context);
    
    static JSClassRef jsClass;
    if (!jsClass)
        jsClass = JSClassCreate(&EvilExceptionObject_definition);
    
    return jsClass;
}

static const JSClassDefinition EmptyObject_definition = {
    0,
    kJSClassAttributeNone,
    
    NULL,
    NULL,
    
    NULL,
    NULL,
    
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

static JSClassRef EmptyObject_class(JSContextRef context)
{
    UNUSED_PARAM(context);
    
    static JSClassRef jsClass;
    if (!jsClass)
        jsClass = JSClassCreate(&EmptyObject_definition);
    
    return jsClass;
}


static JSValueRef Base_get(JSContextRef ctx, JSObjectRef object, JSStringRef propertyName, JSValueRef* exception)
{
    UNUSED_PARAM(object);
    UNUSED_PARAM(propertyName);
    UNUSED_PARAM(exception);

    return JSValueMakeNumber(ctx, 1); // distinguish base get form derived get
}

static bool Base_set(JSContextRef ctx, JSObjectRef object, JSStringRef propertyName, JSValueRef value, JSValueRef* exception)
{
    UNUSED_PARAM(object);
    UNUSED_PARAM(propertyName);
    UNUSED_PARAM(value);

    *exception = JSValueMakeNumber(ctx, 1); // distinguish base set from derived set
    return true;
}

static JSValueRef Base_callAsFunction(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception)
{
    UNUSED_PARAM(function);
    UNUSED_PARAM(thisObject);
    UNUSED_PARAM(argumentCount);
    UNUSED_PARAM(arguments);
    UNUSED_PARAM(exception);
    
    return JSValueMakeNumber(ctx, 1); // distinguish base call from derived call
}

static JSValueRef Base_returnHardNull(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception)
{
    UNUSED_PARAM(ctx);
    UNUSED_PARAM(function);
    UNUSED_PARAM(thisObject);
    UNUSED_PARAM(argumentCount);
    UNUSED_PARAM(arguments);
    UNUSED_PARAM(exception);
    
    return 0; // should convert to undefined!
}

static const JSStaticFunction Base_staticFunctions[] = {
    { "baseProtoDup", NULL, kJSPropertyAttributeNone },
    { "baseProto", Base_callAsFunction, kJSPropertyAttributeNone },
    { "baseHardNull", Base_returnHardNull, kJSPropertyAttributeNone },
    { 0, 0, 0 }
};

static const JSStaticValue Base_staticValues[] = {
    { "baseDup", Base_get, Base_set, kJSPropertyAttributeNone },
    { "baseOnly", Base_get, Base_set, kJSPropertyAttributeNone },
    { 0, 0, 0, 0 }
};

static bool TestInitializeFinalize;
static void Base_initialize(JSContextRef context, JSObjectRef object)
{
    UNUSED_PARAM(context);

    if (TestInitializeFinalize) {
        ASSERT((void*)1 == JSObjectGetPrivate(object));
        JSObjectSetPrivate(object, (void*)2);
    }
}

static unsigned Base_didFinalize;
static void Base_finalize(JSObjectRef object)
{
    UNUSED_PARAM(object);
    if (TestInitializeFinalize) {
        ASSERT((void*)4 == JSObjectGetPrivate(object));
        Base_didFinalize = true;
    }
}

static JSClassRef Base_class(JSContextRef context)
{
    UNUSED_PARAM(context);

    static JSClassRef jsClass;
    if (!jsClass) {
        JSClassDefinition definition = kJSClassDefinitionEmpty;
        definition.staticValues = Base_staticValues;
        definition.staticFunctions = Base_staticFunctions;
        definition.initialize = Base_initialize;
        definition.finalize = Base_finalize;
        jsClass = JSClassCreate(&definition);
    }
    return jsClass;
}

static JSValueRef Derived_get(JSContextRef ctx, JSObjectRef object, JSStringRef propertyName, JSValueRef* exception)
{
    UNUSED_PARAM(object);
    UNUSED_PARAM(propertyName);
    UNUSED_PARAM(exception);

    return JSValueMakeNumber(ctx, 2); // distinguish base get form derived get
}

static bool Derived_set(JSContextRef ctx, JSObjectRef object, JSStringRef propertyName, JSValueRef value, JSValueRef* exception)
{
    UNUSED_PARAM(ctx);
    UNUSED_PARAM(object);
    UNUSED_PARAM(propertyName);
    UNUSED_PARAM(value);

    *exception = JSValueMakeNumber(ctx, 2); // distinguish base set from derived set
    return true;
}

static JSValueRef Derived_callAsFunction(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception)
{
    UNUSED_PARAM(function);
    UNUSED_PARAM(thisObject);
    UNUSED_PARAM(argumentCount);
    UNUSED_PARAM(arguments);
    UNUSED_PARAM(exception);
    
    return JSValueMakeNumber(ctx, 2); // distinguish base call from derived call
}

static const JSStaticFunction Derived_staticFunctions[] = {
    { "protoOnly", Derived_callAsFunction, kJSPropertyAttributeNone },
    { "protoDup", NULL, kJSPropertyAttributeNone },
    { "baseProtoDup", Derived_callAsFunction, kJSPropertyAttributeNone },
    { 0, 0, 0 }
};

static const JSStaticValue Derived_staticValues[] = {
    { "derivedOnly", Derived_get, Derived_set, kJSPropertyAttributeNone },
    { "protoDup", Derived_get, Derived_set, kJSPropertyAttributeNone },
    { "baseDup", Derived_get, Derived_set, kJSPropertyAttributeNone },
    { 0, 0, 0, 0 }
};

static void Derived_initialize(JSContextRef context, JSObjectRef object)
{
    UNUSED_PARAM(context);

    if (TestInitializeFinalize) {
        ASSERT((void*)2 == JSObjectGetPrivate(object));
        JSObjectSetPrivate(object, (void*)3);
    }
}

static void Derived_finalize(JSObjectRef object)
{
    if (TestInitializeFinalize) {
        ASSERT((void*)3 == JSObjectGetPrivate(object));
        JSObjectSetPrivate(object, (void*)4);
    }
}

static JSClassRef Derived_class(JSContextRef context)
{
    static JSClassRef jsClass;
    if (!jsClass) {
        JSClassDefinition definition = kJSClassDefinitionEmpty;
        definition.parentClass = Base_class(context);
        definition.staticValues = Derived_staticValues;
        definition.staticFunctions = Derived_staticFunctions;
        definition.initialize = Derived_initialize;
        definition.finalize = Derived_finalize;
        jsClass = JSClassCreate(&definition);
    }
    return jsClass;
}

static JSClassRef Derived2_class(JSContextRef context)
{
    static JSClassRef jsClass;
    if (!jsClass) {
        JSClassDefinition definition = kJSClassDefinitionEmpty;
        definition.parentClass = Derived_class(context);
        jsClass = JSClassCreate(&definition);
    }
    return jsClass;
}

static JSValueRef print_callAsFunction(JSContextRef ctx, JSObjectRef functionObject, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception)
{
    UNUSED_PARAM(functionObject);
    UNUSED_PARAM(thisObject);
    UNUSED_PARAM(exception);

    ASSERT(JSContextGetGlobalContext(ctx) == context);
    
    if (argumentCount > 0) {
        JSStringRef string = JSValueToStringCopy(ctx, arguments[0], NULL);
        size_t sizeUTF8 = JSStringGetMaximumUTF8CStringSize(string);
        char* stringUTF8 = (char*)malloc(sizeUTF8);
        JSStringGetUTF8CString(string, stringUTF8, sizeUTF8);
        printf("%s\n", stringUTF8);
        free(stringUTF8);
        JSStringRelease(string);
    }
    
    return JSValueMakeUndefined(ctx);
}

static JSObjectRef myConstructor_callAsConstructor(JSContextRef context, JSObjectRef constructorObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception)
{
    UNUSED_PARAM(constructorObject);
    UNUSED_PARAM(exception);
    
    JSObjectRef result = JSObjectMake(context, NULL, NULL);
    if (argumentCount > 0) {
        JSStringRef value = JSStringCreateWithUTF8CString("value");
        JSObjectSetProperty(context, result, value, arguments[0], kJSPropertyAttributeNone, NULL);
        JSStringRelease(value);
    }
    
    return result;
}

static JSObjectRef myBadConstructor_callAsConstructor(JSContextRef context, JSObjectRef constructorObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception)
{
    UNUSED_PARAM(context);
    UNUSED_PARAM(constructorObject);
    UNUSED_PARAM(argumentCount);
    UNUSED_PARAM(arguments);
    UNUSED_PARAM(exception);
    
    return 0;
}


static void globalObject_initialize(JSContextRef context, JSObjectRef object)
{
    UNUSED_PARAM(object);
    // Ensure that an execution context is passed in
    ASSERT(context);

    JSObjectRef globalObject = JSContextGetGlobalObject(context);
    ASSERT(globalObject);

    // Ensure that the standard global properties have been set on the global object
    JSStringRef array = JSStringCreateWithUTF8CString("Array");
    JSObjectRef arrayConstructor = JSValueToObject(context, JSObjectGetProperty(context, globalObject, array, NULL), NULL);
    JSStringRelease(array);

    UNUSED_PARAM(arrayConstructor);
    ASSERT(arrayConstructor);
}

static JSValueRef globalObject_get(JSContextRef ctx, JSObjectRef object, JSStringRef propertyName, JSValueRef* exception)
{
    UNUSED_PARAM(object);
    UNUSED_PARAM(propertyName);
    UNUSED_PARAM(exception);

    return JSValueMakeNumber(ctx, 3);
}

static bool globalObject_set(JSContextRef ctx, JSObjectRef object, JSStringRef propertyName, JSValueRef value, JSValueRef* exception)
{
    UNUSED_PARAM(object);
    UNUSED_PARAM(propertyName);
    UNUSED_PARAM(value);

    *exception = JSValueMakeNumber(ctx, 3);
    return true;
}

static JSValueRef globalObject_call(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception)
{
    UNUSED_PARAM(function);
    UNUSED_PARAM(thisObject);
    UNUSED_PARAM(argumentCount);
    UNUSED_PARAM(arguments);
    UNUSED_PARAM(exception);

    return JSValueMakeNumber(ctx, 3);
}

static JSValueRef functionGC(JSContextRef context, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception)
{
    UNUSED_PARAM(function);
    UNUSED_PARAM(thisObject);
    UNUSED_PARAM(argumentCount);
    UNUSED_PARAM(arguments);
    UNUSED_PARAM(exception);
    JSGarbageCollect(context);
    return JSValueMakeUndefined(context);
}

static const JSStaticValue globalObject_staticValues[] = {
    { "globalStaticValue", globalObject_get, globalObject_set, kJSPropertyAttributeNone },
    { "globalStaticValue2", globalObject_get, 0, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontEnum },
    { 0, 0, 0, 0 }
};

static const JSStaticFunction globalObject_staticFunctions[] = {
    { "globalStaticFunction", globalObject_call, kJSPropertyAttributeNone },
    { "globalStaticFunction2", globalObject_call, kJSPropertyAttributeNone },
    { "globalStaticFunction3", globalObject_call, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontEnum },
    { "gc", functionGC, kJSPropertyAttributeNone },
    { 0, 0, 0 }
};

static char* createStringWithContentsOfFile(const char* fileName);

static void testInitializeFinalize(void)
{
    JSObjectRef o = JSObjectMake(context, Derived_class(context), (void*)1);
    UNUSED_PARAM(o);
    ASSERT(JSObjectGetPrivate(o) == (void*)3);
}

static JSValueRef jsNumberValue =  NULL;

static JSObjectRef aHeapRef = NULL;

static void makeGlobalNumberValue(JSContextRef context) {
    JSValueRef v = JSValueMakeNumber(context, 420);
    JSValueProtect(context, v);
    jsNumberValue = v;
    v = NULL;
}

bool assertTrue(bool value, const char* message)
{
    if (!value) {
        if (message)
            fprintf(stderr, "assertTrue failed: '%s'\n", message);
        else
            fprintf(stderr, "assertTrue failed.\n");
        failed = 1;
    }
    return value;
}

static bool checkForCycleInPrototypeChain(void)
{
    bool result = true;
    JSGlobalContextRef context = JSGlobalContextCreate(0);
    JSObjectRef object1 = JSObjectMake(context, /* jsClass */ 0, /* data */ 0);
    JSObjectRef object2 = JSObjectMake(context, /* jsClass */ 0, /* data */ 0);
    JSObjectRef object3 = JSObjectMake(context, /* jsClass */ 0, /* data */ 0);

    JSObjectSetPrototype(context, object1, JSValueMakeNull(context));
    ASSERT(JSValueIsNull(context, JSObjectGetPrototype(context, object1)));

    // object1 -> object1
    JSObjectSetPrototype(context, object1, object1);
    result &= assertTrue(JSValueIsNull(context, JSObjectGetPrototype(context, object1)), "It is possible to assign self as a prototype");

    // object1 -> object2 -> object1
    JSObjectSetPrototype(context, object2, object1);
    ASSERT(JSValueIsStrictEqual(context, JSObjectGetPrototype(context, object2), object1));
    JSObjectSetPrototype(context, object1, object2);
    result &= assertTrue(JSValueIsNull(context, JSObjectGetPrototype(context, object1)), "It is possible to close a prototype chain cycle");

    // object1 -> object2 -> object3 -> object1
    JSObjectSetPrototype(context, object2, object3);
    ASSERT(JSValueIsStrictEqual(context, JSObjectGetPrototype(context, object2), object3));
    JSObjectSetPrototype(context, object1, object2);
    ASSERT(JSValueIsStrictEqual(context, JSObjectGetPrototype(context, object1), object2));
    JSObjectSetPrototype(context, object3, object1);
    result &= assertTrue(!JSValueIsStrictEqual(context, JSObjectGetPrototype(context, object3), object1), "It is possible to close a prototype chain cycle");

    JSValueRef exception;
    JSStringRef code = JSStringCreateWithUTF8CString("o = { }; p = { }; o.__proto__ = p; p.__proto__ = o");
    JSStringRef file = JSStringCreateWithUTF8CString("");
    result &= assertTrue(!JSEvaluateScript(context, code, /* thisObject*/ 0, file, 1, &exception)
                         , "An exception should be thrown");

    JSStringRelease(code);
    JSStringRelease(file);
    JSGlobalContextRelease(context);
    return result;
}

static JSValueRef valueToObjectExceptionCallAsFunction(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception)
{
    UNUSED_PARAM(function);
    UNUSED_PARAM(thisObject);
    UNUSED_PARAM(argumentCount);
    UNUSED_PARAM(arguments);
    JSValueRef jsUndefined = JSValueMakeUndefined(JSContextGetGlobalContext(ctx));
    JSValueToObject(JSContextGetGlobalContext(ctx), jsUndefined, exception);
    
    return JSValueMakeUndefined(ctx);
}
static bool valueToObjectExceptionTest(void)
{
    JSGlobalContextRef testContext;
    JSClassDefinition globalObjectClassDefinition = kJSClassDefinitionEmpty;
    globalObjectClassDefinition.initialize = globalObject_initialize;
    globalObjectClassDefinition.staticValues = globalObject_staticValues;
    globalObjectClassDefinition.staticFunctions = globalObject_staticFunctions;
    globalObjectClassDefinition.attributes = kJSClassAttributeNoAutomaticPrototype;
    JSClassRef globalObjectClass = JSClassCreate(&globalObjectClassDefinition);
    testContext = JSGlobalContextCreateInGroup(NULL, globalObjectClass);
    JSObjectRef globalObject = JSContextGetGlobalObject(testContext);

    JSStringRef valueToObject = JSStringCreateWithUTF8CString("valueToObject");
    JSObjectRef valueToObjectFunction = JSObjectMakeFunctionWithCallback(testContext, valueToObject, valueToObjectExceptionCallAsFunction);
    JSObjectSetProperty(testContext, globalObject, valueToObject, valueToObjectFunction, kJSPropertyAttributeNone, NULL);
    JSStringRelease(valueToObject);

    JSStringRef test = JSStringCreateWithUTF8CString("valueToObject();");
    JSEvaluateScript(testContext, test, NULL, NULL, 1, NULL);
    
    JSStringRelease(test);
    JSClassRelease(globalObjectClass);
    JSGlobalContextRelease(testContext);
    
    return true;
}

static bool globalContextNameTest(void)
{
    bool result = true;
    JSGlobalContextRef context = JSGlobalContextCreate(0);

    JSStringRef str = JSGlobalContextCopyName(context);
    result &= assertTrue(!str, "Default context name is NULL");

    JSStringRef name1 = JSStringCreateWithUTF8CString("name1");
    JSStringRef name2 = JSStringCreateWithUTF8CString("name2");

    JSGlobalContextSetName(context, name1);
    JSStringRef fetchName1 = JSGlobalContextCopyName(context);
    JSGlobalContextSetName(context, name2);
    JSStringRef fetchName2 = JSGlobalContextCopyName(context);
    JSGlobalContextSetName(context, NULL);
    JSStringRef fetchName3 = JSGlobalContextCopyName(context);

    result &= assertTrue(JSStringIsEqual(name1, fetchName1), "Unexpected Context name");
    result &= assertTrue(JSStringIsEqual(name2, fetchName2), "Unexpected Context name");
    result &= assertTrue(!JSStringIsEqual(fetchName1, fetchName2), "Unexpected Context name");
    result &= assertTrue(!fetchName3, "Unexpected Context name");

    JSStringRelease(name1);
    JSStringRelease(name2);
    JSStringRelease(fetchName1);
    JSStringRelease(fetchName2);

    JSGlobalContextRelease(context);

    return result;
}

IGNORE_GCC_WARNINGS_BEGIN("unused-but-set-variable")
static void checkConstnessInJSObjectNames(void)
{
    JSStaticFunction fun;
    fun.name = "something";
    JSStaticValue val;
    val.name = "something";
}
IGNORE_GCC_WARNINGS_END

#ifdef __cplusplus
extern "C" {
#endif
void JSSynchronousGarbageCollectForDebugging(JSContextRef);
#ifdef __cplusplus
}
#endif

static void checkJSStringOOBUTF8(void)
{
    const size_t sourceCStringSize = 200;
    const size_t cStringSize = 10;
    const size_t outCStringSize = cStringSize + sourceCStringSize;

IGNORE_WARNINGS_BEGIN("vla")
    char sourceCString[sourceCStringSize];
IGNORE_WARNINGS_END
    memset(sourceCString, 0, sizeof(sourceCString));
    for (size_t i = 0; i < sourceCStringSize - 1; ++i)
        sourceCString[i] = '0' + (i%10);

IGNORE_WARNINGS_BEGIN("vla")
    char outCString[outCStringSize];
IGNORE_WARNINGS_END
    memset(outCString, 0x13, sizeof(outCString));

    JSStringRef str = JSStringCreateWithUTF8CString(sourceCString);
    size_t bytesWritten = JSStringGetUTF8CString(str, outCString, cStringSize);

    assertTrue(bytesWritten == 10, "we report 10 bytes written precisely");

    for (size_t i = 0; i < sizeof(outCString); ++i) {
        if (i == cStringSize - 1)
            assertTrue(outCString[i] == '\0', "string terminated");
        else if (i < cStringSize - 1)
            assertTrue(outCString[i] == sourceCString[i], "string copied");
        else
            assertTrue(outCString[i] == 0x13, "did not write past the end");
    }

    JSStringRelease(str);
}

static void checkJSStringOOBUTF16(void)
{
    const size_t sourceCStringSize = 22;
    const size_t cStringSize = 20;
    const size_t outCStringSize = cStringSize + sourceCStringSize;

IGNORE_WARNINGS_BEGIN("vla")
    char sourceCString[sourceCStringSize];
IGNORE_WARNINGS_END
    memset(sourceCString, 0, sizeof(sourceCString));
    for (size_t i = 0; i < sourceCStringSize - 1; ++i)
        sourceCString[i] = '0' + (i%10);

    sourceCString[3] = '\xF0';
    sourceCString[4] = '\x9F';
    sourceCString[5] = '\x98';
    sourceCString[6] = '\x81';

IGNORE_WARNINGS_BEGIN("vla")
    char outCString[outCStringSize];
IGNORE_WARNINGS_END
    memset(outCString, 0x13, sizeof(outCString));

    JSStringRef str = JSStringCreateWithUTF8CString(sourceCString);
    size_t bytesWritten = JSStringGetUTF8CString(str, outCString, cStringSize);

    assertTrue(bytesWritten == 20, "we report 20 bytes written precisely");

    for (size_t i = 0; i < sizeof(outCString); ++i) {
        if (i == cStringSize - 1)
            assertTrue(outCString[i] == '\0', "string terminated");
        else if (i < cStringSize - 1)
            assertTrue(outCString[i] == sourceCString[i], "string copied");
        else
            assertTrue(outCString[i] == 0x13, "did not write past the end");
    }

    JSStringRelease(str);
}

static void checkJSStringOOBUTF16AtEnd(void)
{
    const size_t sourceCStringSize = 22;
    const size_t cStringSize = 20;
    const size_t outCStringSize = cStringSize + sourceCStringSize;

IGNORE_WARNINGS_BEGIN("vla")
    char sourceCString[sourceCStringSize];
IGNORE_WARNINGS_END
    memset(sourceCString, 0, sizeof(sourceCString));
    for (size_t i = 0; i < sourceCStringSize - 1; ++i)
        sourceCString[i] = '0' + (i%10);

    sourceCString[17] = '\xF0';
    sourceCString[18] = '\x9F';
    sourceCString[19] = '\x98';
    sourceCString[20] = '\x81';

IGNORE_WARNINGS_BEGIN("vla")
    char outCString[outCStringSize];
IGNORE_WARNINGS_END
    memset(outCString, 0x13, sizeof(outCString));

    JSStringRef str = JSStringCreateWithUTF8CString(sourceCString);
    size_t bytesWritten = JSStringGetUTF8CString(str, outCString, cStringSize);

    assertTrue(bytesWritten == 18, "we report 18 bytes written precisely");

    for (size_t i = 0; i < sizeof(outCString); ++i) {
        if (i == 17)
            assertTrue(outCString[i] == '\0', "string terminated");
        else if (i < 17)
            assertTrue(outCString[i] == sourceCString[i], "string copied");
        else
            assertTrue(outCString[i] == 0x13, "did not write past the end");
    }

    JSStringRelease(str);
}

static void checkJSStringOOB(void)
{
    printf("Test: checkJSStringOOB\n");
    checkJSStringOOBUTF8();
    printf(".\n");
    checkJSStringOOBUTF16();
    printf(".\n");
    checkJSStringOOBUTF16AtEnd();
    printf("PASS: checkJSStringOOB\n");
}

static void checkJSStringInvalid(void)
{
    printf("Test: checkJSStringInvalid\n");
    JSChar* source = (JSChar*)malloc(sizeof(JSChar) * 4);
    source[0] = 'a';
    source[1] = 'b';
    source[2] = 'c';
    source[3] = 0xD800;
    JSStringRef string = JSStringCreateWithCharacters(source, 4);

    char* out = (char*)malloc(sizeof(char) * 32);
    memset(out, 1, sizeof(char) * 32);
    size_t bytesWritten = JSStringGetUTF8CString(string, out, sizeof(char) * 32);

    assertTrue(bytesWritten == 4, "we report 4 bytes written precisely");
    assertTrue(out[0] == 'a', "a");
    assertTrue(out[1] == 'b', "b");
    assertTrue(out[2] == 'c', "c");
    assertTrue(out[3] == '\0', "string terminated");

    JSStringRelease(string);
    free(out);
    free(source);
}

static const unsigned numWeakRefs = 10000;

static void markingConstraint(JSMarkerRef marker, void *userData)
{
    JSWeakRef *weakRefs;
    unsigned i;
    
    weakRefs = (JSWeakRef*)userData;
    
    for (i = 0; i < numWeakRefs; i += 2) {
        JSWeakRef weakRef = weakRefs[i];
        if (weakRef) {
            JSObjectRef object = JSWeakGetObject(weakRefs[i]);
            marker->Mark(marker, object);
            assertTrue(marker->IsMarked(marker, object), "A marked object is marked");
        }
    }
}

static bool didRunHeapFinalizer;
static JSContextGroupRef expectedContextGroup;

static void heapFinalizer(JSContextGroupRef group, void *userData)
{
    assertTrue((uintptr_t)userData == (uintptr_t)42, "Correct userData was passed");
    assertTrue(group == expectedContextGroup, "Correct context group");
    
    didRunHeapFinalizer = true;
}

static void testMarkingConstraintsAndHeapFinalizers(void)
{
    JSContextGroupRef group;
    JSWeakRef *weakRefs;
    unsigned i;
    unsigned deadCount;
    
    printf("Testing Marking Constraints.\n");
    
    group = JSContextGroupCreate();
    expectedContextGroup = group;
    
    JSGlobalContextRef context = JSGlobalContextCreateInGroup(group, NULL);

    weakRefs = (JSWeakRef*)calloc(numWeakRefs, sizeof(JSWeakRef));

    JSContextGroupAddMarkingConstraint(group, markingConstraint, (void*)weakRefs);
    JSContextGroupAddHeapFinalizer(group, heapFinalizer, (void*)(uintptr_t)42);
    
    for (i = numWeakRefs; i--;)
        weakRefs[i] = JSWeakCreate(group, JSObjectMakeArray(context, 0, NULL, NULL));
    
    JSSynchronousGarbageCollectForDebugging(context);
    assertTrue(didRunHeapFinalizer, "Did run heap finalizer");
    
    deadCount = 0;
    for (i = 0; i < numWeakRefs; i += 2) {
        assertTrue((bool)JSWeakGetObject(weakRefs[i]), "Marked objects stayed alive");
        if (!JSWeakGetObject(weakRefs[i + 1]))
            deadCount++;
    }
    
    assertTrue(deadCount != 0, "At least some objects died");
    
    for (i = numWeakRefs; i--;) {
        JSWeakRef weakRef = weakRefs[i];
        weakRefs[i] = NULL;
        JSWeakRelease(group, weakRef);
    }
    
    didRunHeapFinalizer = false;
    JSSynchronousGarbageCollectForDebugging(context);
    assertTrue(didRunHeapFinalizer, "Did run heap finalizer");

    JSContextGroupRemoveHeapFinalizer(group, heapFinalizer, (void*)(uintptr_t)42);

    didRunHeapFinalizer = false;
    JSSynchronousGarbageCollectForDebugging(context);
    assertTrue(!didRunHeapFinalizer, "Did not run heap finalizer");

    JSGlobalContextRelease(context);
    JSContextGroupRelease(group);

    printf("PASS: Marking Constraints and Heap Finalizers.\n");
}

#if USE(CF)
static void testCFStrings(void)
{
    /* The assertion utility functions we use below expects to get the JSGlobalContextRef
       from the global context variable. */
    JSGlobalContextRef oldContext = context;
    context = JSGlobalContextCreate(0);

    UniChar singleUniChar = 65; // Capital A
    CFMutableStringRef cfString = CFStringCreateMutableWithExternalCharactersNoCopy(kCFAllocatorDefault, &singleUniChar, 1, 1, kCFAllocatorNull);

    JSStringRef jsCFIString = JSStringCreateWithCFString(cfString);
    JSValueRef jsCFString = JSValueMakeString(context, jsCFIString);

    CFStringRef cfEmptyString = CFSTR("");

    JSStringRef jsCFEmptyIString = JSStringCreateWithCFString(cfEmptyString);
    JSValueRef jsCFEmptyString = JSValueMakeString(context, jsCFEmptyIString);

    CFIndex cfStringLength = CFStringGetLength(cfString);
    UniChar* buffer = (UniChar*)malloc(cfStringLength * sizeof(UniChar));
    CFStringGetCharacters(cfString, CFRangeMake(0, cfStringLength), buffer);
    JSStringRef jsCFIStringWithCharacters = JSStringCreateWithCharacters((JSChar*)buffer, cfStringLength);
    JSValueRef jsCFStringWithCharacters = JSValueMakeString(context, jsCFIStringWithCharacters);

    JSStringRef jsCFEmptyIStringWithCharacters = JSStringCreateWithCharacters((JSChar*)buffer, CFStringGetLength(cfEmptyString));
    free(buffer);
    JSValueRef jsCFEmptyStringWithCharacters = JSValueMakeString(context, jsCFEmptyIStringWithCharacters);

    ASSERT(JSValueGetType(context, jsCFString) == kJSTypeString);
    ASSERT(JSValueGetType(context, jsCFStringWithCharacters) == kJSTypeString);
    ASSERT(JSValueGetType(context, jsCFEmptyString) == kJSTypeString);
    ASSERT(JSValueGetType(context, jsCFEmptyStringWithCharacters) == kJSTypeString);

    JSStringRef emptyString = JSStringCreateWithCFString(CFSTR(""));
    const JSChar* characters = JSStringGetCharactersPtr(emptyString);
    if (!characters) {
        printf("FAIL: Returned null when accessing character pointer of an empty String.\n");
        failed = 1;
    } else
        printf("PASS: returned empty when accessing character pointer of an empty String.\n");

    size_t length = JSStringGetLength(emptyString);
    if (length) {
        printf("FAIL: Didn't return 0 length for empty String.\n");
        failed = 1;
    } else
        printf("PASS: returned 0 length for empty String.\n");
    JSStringRelease(emptyString);

    assertEqualsAsBoolean(jsCFString, true);
    assertEqualsAsBoolean(jsCFStringWithCharacters, true);
    assertEqualsAsBoolean(jsCFEmptyString, false);
    assertEqualsAsBoolean(jsCFEmptyStringWithCharacters, false);

    assertEqualsAsNumber(jsCFString, nan(""));
    assertEqualsAsNumber(jsCFStringWithCharacters, nan(""));
    assertEqualsAsNumber(jsCFEmptyString, 0);
    assertEqualsAsNumber(jsCFEmptyStringWithCharacters, 0);
    ASSERT(sizeof(JSChar) == sizeof(UniChar));

    assertEqualsAsCharactersPtr(jsCFString, "A");
    assertEqualsAsCharactersPtr(jsCFStringWithCharacters, "A");
    assertEqualsAsCharactersPtr(jsCFEmptyString, "");
    assertEqualsAsCharactersPtr(jsCFEmptyStringWithCharacters, "");

    assertEqualsAsUTF8String(jsCFString, "A");
    assertEqualsAsUTF8String(jsCFStringWithCharacters, "A");
    assertEqualsAsUTF8String(jsCFEmptyString, "");
    assertEqualsAsUTF8String(jsCFEmptyStringWithCharacters, "");

    CFStringRef cfJSString = JSStringCopyCFString(kCFAllocatorDefault, jsCFIString);
    CFStringRef cfJSEmptyString = JSStringCopyCFString(kCFAllocatorDefault, jsCFEmptyIString);
    ASSERT(CFEqual(cfJSString, cfString));
    ASSERT(CFEqual(cfJSEmptyString, cfEmptyString));
    CFRelease(cfJSString);
    CFRelease(cfJSEmptyString);

    JSObjectRef o = JSObjectMake(context, NULL, NULL);
    JSStringRef jsOneIString = JSStringCreateWithUTF8CString("1");
    JSObjectSetProperty(context, o, jsOneIString, JSValueMakeNumber(context, 1), kJSPropertyAttributeNone, NULL);
    JSObjectSetProperty(context, o, jsCFIString,  JSValueMakeNumber(context, 1), kJSPropertyAttributeDontEnum, NULL);
    JSPropertyNameArrayRef nameArray = JSObjectCopyPropertyNames(context, o);
    size_t expectedCount = JSPropertyNameArrayGetCount(nameArray);
    size_t count;
    for (count = 0; count < expectedCount; ++count)
        JSPropertyNameArrayGetNameAtIndex(nameArray, count);
    JSPropertyNameArrayRelease(nameArray);
    ASSERT(count == 1); // jsCFString should not be enumerated

    JSStringRelease(jsOneIString);
    JSStringRelease(jsCFIString);
    JSStringRelease(jsCFEmptyIString);
    JSStringRelease(jsCFIStringWithCharacters);
    JSStringRelease(jsCFEmptyIStringWithCharacters);
    CFRelease(cfString);

    JSGlobalContextRelease(context);
    context = oldContext;
}
#endif

static bool samplingProfilerTest(void)
{
#if ENABLE(SAMPLING_PROFILER)
    JSContextGroupRef contextGroup = JSContextGroupCreate();
    JSGlobalContextRef context = JSGlobalContextCreateInGroup(contextGroup, NULL);
    {
        bool result = JSContextGroupEnableSamplingProfiler(contextGroup);
        if (result)
            printf("PASS: Enabled sampling profiler.\n");
        else {
            printf("FAIL: Failed to enable sampling profiler.\n");
            return true;
        }
        JSStringRef script = JSStringCreateWithUTF8CString("var start = Date.now(); while ((start + 200) > Date.now()) { new Error().stack; }");
        JSEvaluateScript(context, script, NULL, NULL, 1, NULL);
        JSStringRelease(script);
        JSContextGroupDisableSamplingProfiler(contextGroup);
    }

    {
        JSStringRef json = JSContextGroupTakeSamplesFromSamplingProfiler(contextGroup);
        if (json)
            printf("PASS: Taking JSON from sampling profiler.\n");
        else {
            printf("FAIL: Failed to enable sampling profiler.\n");
            return true;
        }

        size_t sizeUTF8 = JSStringGetMaximumUTF8CStringSize(json);
        char* stringUTF8 = (char*)malloc(sizeUTF8);
        JSStringGetUTF8CString(json, stringUTF8, sizeUTF8);
        if (sizeUTF8)
            printf("PASS: Some JSON data is generated.\n");
        else {
            printf("FAIL: Failed to take JSON data.\n");
            return true;
        }
        free(stringUTF8);

        JSStringRelease(json);
    }

    JSGlobalContextRelease(context);
    JSContextGroupRelease(contextGroup);
#endif
    return false;
}

int main(int argc, char* argv[])
{
#if OS(WINDOWS)
    // Cygwin calls SetErrorMode(SEM_FAILCRITICALERRORS), which we will inherit. This is bad for
    // testing/debugging, as it causes the post-mortem debugger not to be invoked. We reset the
    // error mode here to work around Cygwin's behavior. See <http://webkit.org/b/55222>.
    SetErrorMode(0);
#endif

    configureJSCForTesting();

#if !OS(WINDOWS)
    char *resolvedPath = realpath(argv[0], NULL);
    if (!resolvedPath)
        fprintf(stderr, "Could not get the absolute pathname for: %s\n", argv[0]);
    else {
        char *newCWD = dirname(resolvedPath);
        if (chdir(newCWD))
            fprintf(stderr, "Could not chdir to: %s\n", newCWD);
        free(resolvedPath);
    }
#endif

    const char* filter = argc > 1 ? argv[1] : NULL;
    // This test needs to run before anything else.
    failed += testLaunchJSCFromNonMainThread(filter);

#if JSC_OBJC_API_ENABLED
    testObjectiveCAPI(filter);
#endif

    RELEASE_ASSERT(!testCAPIViaCpp(filter));
    if (filter)
        return failed;

    testCompareAndSwap();
    startMultithreadedMultiVMExecutionTest();
    
    // Test garbage collection with a fresh context
    context = JSGlobalContextCreateInGroup(NULL, NULL);
    TestInitializeFinalize = true;
    testInitializeFinalize();
    JSGlobalContextRelease(context);
    TestInitializeFinalize = false;

    ASSERT(Base_didFinalize);

    testMarkingConstraintsAndHeapFinalizers();

#if USE(CF)
    testCFStrings();
#endif

    JSClassDefinition globalObjectClassDefinition = kJSClassDefinitionEmpty;
    globalObjectClassDefinition.initialize = globalObject_initialize;
    globalObjectClassDefinition.staticValues = globalObject_staticValues;
    globalObjectClassDefinition.staticFunctions = globalObject_staticFunctions;
    globalObjectClassDefinition.attributes = kJSClassAttributeNoAutomaticPrototype;
    JSClassRef globalObjectClass = JSClassCreate(&globalObjectClassDefinition);
    context = JSGlobalContextCreateInGroup(NULL, globalObjectClass);

    JSContextGroupRef contextGroup = JSContextGetGroup(context);
    
    JSGlobalContextRetain(context);
    JSGlobalContextRelease(context);
    ASSERT(JSContextGetGlobalContext(context) == context);
    
    JSReportExtraMemoryCost(context, 0);
    JSReportExtraMemoryCost(context, 1);
    JSReportExtraMemoryCost(context, 1024);

    JSObjectRef globalObject = JSContextGetGlobalObject(context);
    ASSERT(JSValueIsObject(context, globalObject));
    
    JSValueRef jsUndefined = JSValueMakeUndefined(context);
    JSValueRef jsNull = JSValueMakeNull(context);
    JSValueRef jsTrue = JSValueMakeBoolean(context, true);
    JSValueRef jsFalse = JSValueMakeBoolean(context, false);
    JSValueRef jsZero = JSValueMakeNumber(context, 0);
    JSValueRef jsOne = JSValueMakeNumber(context, 1);
    JSValueRef jsOneThird = JSValueMakeNumber(context, 1.0 / 3.0);
    JSObjectRef jsObjectNoProto = JSObjectMake(context, NULL, NULL);
    JSObjectSetPrototype(context, jsObjectNoProto, JSValueMakeNull(context));

    JSObjectSetPrivate(globalObject, (void*)123);
    if (JSObjectGetPrivate(globalObject) != (void*)123) {
        printf("FAIL: Didn't return private data when set by JSObjectSetPrivate().\n");
        failed = 1;
    } else
        printf("PASS: returned private data when set by JSObjectSetPrivate().\n");

    // FIXME: test funny utf8 characters
    JSStringRef jsEmptyIString = JSStringCreateWithUTF8CString("");
    JSValueRef jsEmptyString = JSValueMakeString(context, jsEmptyIString);
    
    JSStringRef jsOneIString = JSStringCreateWithUTF8CString("1");
    JSValueRef jsOneString = JSValueMakeString(context, jsOneIString);

    JSChar constantString[] = { 'H', 'e', 'l', 'l', 'o', };
    JSStringRef constantStringRef = JSStringCreateWithCharactersNoCopy(constantString, sizeof(constantString) / sizeof(constantString[0]));
    ASSERT(JSStringGetCharactersPtr(constantStringRef) == constantString);
    JSStringRelease(constantStringRef);

    ASSERT(JSValueGetType(context, NULL) == kJSTypeNull);
    ASSERT(JSValueGetType(context, jsUndefined) == kJSTypeUndefined);
    ASSERT(JSValueGetType(context, jsNull) == kJSTypeNull);
    ASSERT(JSValueGetType(context, jsTrue) == kJSTypeBoolean);
    ASSERT(JSValueGetType(context, jsFalse) == kJSTypeBoolean);
    ASSERT(JSValueGetType(context, jsZero) == kJSTypeNumber);
    ASSERT(JSValueGetType(context, jsOne) == kJSTypeNumber);
    ASSERT(JSValueGetType(context, jsOneThird) == kJSTypeNumber);
    ASSERT(JSValueGetType(context, jsEmptyString) == kJSTypeString);
    ASSERT(JSValueGetType(context, jsOneString) == kJSTypeString);

    ASSERT(!JSValueIsBoolean(context, NULL));
    ASSERT(!JSValueIsObject(context, NULL));
    ASSERT(!JSValueIsArray(context, NULL));
    ASSERT(!JSValueIsDate(context, NULL));
    ASSERT(!JSValueIsString(context, NULL));
    ASSERT(!JSValueIsNumber(context, NULL));
    ASSERT(!JSValueIsUndefined(context, NULL));
    ASSERT(JSValueIsNull(context, NULL));
    ASSERT(!JSObjectCallAsFunction(context, NULL, NULL, 0, NULL, NULL));
    ASSERT(!JSObjectCallAsConstructor(context, NULL, 0, NULL, NULL));
    ASSERT(!JSObjectIsConstructor(context, NULL));
    ASSERT(!JSObjectIsFunction(context, NULL));

    JSStringRef nullString = JSStringCreateWithUTF8CString(0);
    const JSChar* characters = JSStringGetCharactersPtr(nullString);
    if (characters) {
        printf("FAIL: Didn't return null when accessing character pointer of a null String.\n");
        failed = 1;
    } else
        printf("PASS: returned null when accessing character pointer of a null String.\n");

    size_t length = JSStringGetLength(nullString);
    if (length) {
        printf("FAIL: Didn't return 0 length for null String.\n");
        failed = 1;
    } else
        printf("PASS: returned 0 length for null String.\n");
    JSStringRelease(nullString);

    JSObjectRef propertyCatchalls = JSObjectMake(context, PropertyCatchalls_class(context), NULL);
    JSStringRef propertyCatchallsString = JSStringCreateWithUTF8CString("PropertyCatchalls");
    JSObjectSetProperty(context, globalObject, propertyCatchallsString, propertyCatchalls, kJSPropertyAttributeNone, NULL);
    JSStringRelease(propertyCatchallsString);

    JSObjectRef myObject = JSObjectMake(context, MyObject_class(context), NULL);
    JSStringRef myObjectIString = JSStringCreateWithUTF8CString("MyObject");
    JSObjectSetProperty(context, globalObject, myObjectIString, myObject, kJSPropertyAttributeNone, NULL);
    JSStringRelease(myObjectIString);
    
    JSObjectRef EvilExceptionObject = JSObjectMake(context, EvilExceptionObject_class(context), NULL);
    JSStringRef EvilExceptionObjectIString = JSStringCreateWithUTF8CString("EvilExceptionObject");
    JSObjectSetProperty(context, globalObject, EvilExceptionObjectIString, EvilExceptionObject, kJSPropertyAttributeNone, NULL);
    JSStringRelease(EvilExceptionObjectIString);
    
    JSObjectRef EmptyObject = JSObjectMake(context, EmptyObject_class(context), NULL);
    JSStringRef EmptyObjectIString = JSStringCreateWithUTF8CString("EmptyObject");
    JSObjectSetProperty(context, globalObject, EmptyObjectIString, EmptyObject, kJSPropertyAttributeNone, NULL);
    JSStringRelease(EmptyObjectIString);
    
    JSStringRef lengthStr = JSStringCreateWithUTF8CString("length");
    JSObjectRef aStackRef = JSObjectMakeArray(context, 0, 0, 0);
    aHeapRef = aStackRef;
    JSObjectSetProperty(context, aHeapRef, lengthStr, JSValueMakeNumber(context, 10), 0, 0);
    JSStringRef privatePropertyName = JSStringCreateWithUTF8CString("privateProperty");
    if (!JSObjectSetPrivateProperty(context, myObject, privatePropertyName, aHeapRef)) {
        printf("FAIL: Could not set private property.\n");
        failed = 1;
    } else
        printf("PASS: Set private property.\n");
    aStackRef = 0;
    if (JSObjectSetPrivateProperty(context, aHeapRef, privatePropertyName, aHeapRef)) {
        printf("FAIL: JSObjectSetPrivateProperty should fail on non-API objects.\n");
        failed = 1;
    } else
        printf("PASS: Did not allow JSObjectSetPrivateProperty on a non-API object.\n");
    if (JSObjectGetPrivateProperty(context, myObject, privatePropertyName) != aHeapRef) {
        printf("FAIL: Could not retrieve private property.\n");
        failed = 1;
    } else
        printf("PASS: Retrieved private property.\n");
    if (JSObjectGetPrivateProperty(context, aHeapRef, privatePropertyName)) {
        printf("FAIL: JSObjectGetPrivateProperty should return NULL when called on a non-API object.\n");
        failed = 1;
    } else
        printf("PASS: JSObjectGetPrivateProperty return NULL.\n");

    if (JSObjectGetProperty(context, myObject, privatePropertyName, 0) == aHeapRef) {
        printf("FAIL: Accessed private property through ordinary property lookup.\n");
        failed = 1;
    } else
        printf("PASS: Cannot access private property through ordinary property lookup.\n");

    JSGarbageCollect(context);

    int i;
    for (i = 0; i < 10000; i++)
        JSObjectMake(context, 0, 0);

    aHeapRef = JSValueToObject(context, JSObjectGetPrivateProperty(context, myObject, privatePropertyName), 0);
    if (JSValueToNumber(context, JSObjectGetProperty(context, aHeapRef, lengthStr, 0), 0) != 10) {
        printf("FAIL: Private property has been collected.\n");
        failed = 1;
    } else
        printf("PASS: Private property does not appear to have been collected.\n");
    JSStringRelease(lengthStr);

    if (!JSObjectSetPrivateProperty(context, myObject, privatePropertyName, 0)) {
        printf("FAIL: Could not set private property to NULL.\n");
        failed = 1;
    } else
        printf("PASS: Set private property to NULL.\n");
    if (JSObjectGetPrivateProperty(context, myObject, privatePropertyName)) {
        printf("FAIL: Could not retrieve private property.\n");
        failed = 1;
    } else
        printf("PASS: Retrieved private property.\n");

    JSStringRef nullJSON = JSStringCreateWithUTF8CString(0);
    JSValueRef nullJSONObject = JSValueMakeFromJSONString(context, nullJSON);
    if (nullJSONObject) {
        printf("FAIL: Did not parse null String as JSON correctly\n");
        failed = 1;
    } else
        printf("PASS: Parsed null String as JSON correctly.\n");
    JSStringRelease(nullJSON);

    JSStringRef validJSON = JSStringCreateWithUTF8CString("{\"aProperty\":true}");
    JSValueRef jsonObject = JSValueMakeFromJSONString(context, validJSON);
    JSStringRelease(validJSON);
    if (!JSValueIsObject(context, jsonObject)) {
        printf("FAIL: Did not parse valid JSON correctly\n");
        failed = 1;
    } else
        printf("PASS: Parsed valid JSON string.\n");
    JSStringRef propertyName = JSStringCreateWithUTF8CString("aProperty");
    assertEqualsAsBoolean(JSObjectGetProperty(context, JSValueToObject(context, jsonObject, 0), propertyName, 0), true);
    JSStringRelease(propertyName);
    JSStringRef invalidJSON = JSStringCreateWithUTF8CString("fail!");
    if (JSValueMakeFromJSONString(context, invalidJSON)) {
        printf("FAIL: Should return null for invalid JSON data\n");
        failed = 1;
    } else
        printf("PASS: Correctly returned null for invalid JSON data.\n");
    JSValueRef exception = NULL;
    JSStringRef str = JSValueCreateJSONString(context, jsonObject, 0, 0);
    if (!JSStringIsEqualToUTF8CString(str, "{\"aProperty\":true}")) {
        printf("FAIL: Did not correctly serialise with indent of 0.\n");
        failed = 1;
    } else
        printf("PASS: Correctly serialised with indent of 0.\n");
    JSStringRelease(str);

    str = JSValueCreateJSONString(context, jsonObject, 4, 0);
    if (!JSStringIsEqualToUTF8CString(str, "{\n    \"aProperty\": true\n}")) {
        printf("FAIL: Did not correctly serialise with indent of 4.\n");
        failed = 1;
    } else
        printf("PASS: Correctly serialised with indent of 4.\n");
    JSStringRelease(str);

    str = JSStringCreateWithUTF8CString("({get a(){ throw '';}})");
    JSValueRef unstringifiableObj = JSEvaluateScript(context, str, NULL, NULL, 1, NULL);
    JSStringRelease(str);
    
    str = JSValueCreateJSONString(context, unstringifiableObj, 4, 0);
    if (str) {
        printf("FAIL: Didn't return null when attempting to serialize unserializable value.\n");
        JSStringRelease(str);
        failed = 1;
    } else
        printf("PASS: returned null when attempting to serialize unserializable value.\n");
    
    str = JSValueCreateJSONString(context, unstringifiableObj, 4, &exception);
    if (str) {
        printf("FAIL: Didn't return null when attempting to serialize unserializable value.\n");
        JSStringRelease(str);
        failed = 1;
    } else
        printf("PASS: returned null when attempting to serialize unserializable value.\n");
    if (!exception) {
        printf("FAIL: Did not set exception on serialisation error\n");
        failed = 1;
    } else
        printf("PASS: set exception on serialisation error\n");
    // Conversions that throw exceptions
    exception = NULL;
    ASSERT(NULL == JSValueToObject(context, jsNull, &exception));
    ASSERT(exception);
    
    exception = NULL;
    // FIXME <rdar://4668451> - On i386 the isnan(double) macro tries to map to the isnan(float) function,
    // causing a build break with -Wshorten-64-to-32 enabled.  The issue is known by the appropriate team.
    // After that's resolved, we can remove these casts
    ASSERT(isnan((float)JSValueToNumber(context, jsObjectNoProto, &exception)));
    ASSERT(exception);

    exception = NULL;
    ASSERT(!JSValueToStringCopy(context, jsObjectNoProto, &exception));
    ASSERT(exception);
    
    ASSERT(JSValueToBoolean(context, myObject));
    
    exception = NULL;
    ASSERT(!JSValueIsEqual(context, jsObjectNoProto, JSValueMakeNumber(context, 1), &exception));
    ASSERT(exception);
    
    exception = NULL;
    JSObjectGetPropertyAtIndex(context, myObject, 0, &exception);
    ASSERT(1 == JSValueToNumber(context, exception, NULL));

    assertEqualsAsBoolean(jsUndefined, false);
    assertEqualsAsBoolean(jsNull, false);
    assertEqualsAsBoolean(jsTrue, true);
    assertEqualsAsBoolean(jsFalse, false);
    assertEqualsAsBoolean(jsZero, false);
    assertEqualsAsBoolean(jsOne, true);
    assertEqualsAsBoolean(jsOneThird, true);
    assertEqualsAsBoolean(jsEmptyString, false);
    assertEqualsAsBoolean(jsOneString, true);
    
    assertEqualsAsNumber(jsUndefined, nan(""));
    assertEqualsAsNumber(jsNull, 0);
    assertEqualsAsNumber(jsTrue, 1);
    assertEqualsAsNumber(jsFalse, 0);
    assertEqualsAsNumber(jsZero, 0);
    assertEqualsAsNumber(jsOne, 1);
    assertEqualsAsNumber(jsOneThird, 1.0 / 3.0);
    assertEqualsAsNumber(jsEmptyString, 0);
    assertEqualsAsNumber(jsOneString, 1);
    
    assertEqualsAsCharactersPtr(jsUndefined, "undefined");
    assertEqualsAsCharactersPtr(jsNull, "null");
    assertEqualsAsCharactersPtr(jsTrue, "true");
    assertEqualsAsCharactersPtr(jsFalse, "false");
    assertEqualsAsCharactersPtr(jsZero, "0");
    assertEqualsAsCharactersPtr(jsOne, "1");
    assertEqualsAsCharactersPtr(jsOneThird, "0.3333333333333333");
    assertEqualsAsCharactersPtr(jsEmptyString, "");
    assertEqualsAsCharactersPtr(jsOneString, "1");

    assertEqualsAsUTF8String(jsUndefined, "undefined");
    assertEqualsAsUTF8String(jsNull, "null");
    assertEqualsAsUTF8String(jsTrue, "true");
    assertEqualsAsUTF8String(jsFalse, "false");
    assertEqualsAsUTF8String(jsZero, "0");
    assertEqualsAsUTF8String(jsOne, "1");
    assertEqualsAsUTF8String(jsOneThird, "0.3333333333333333");
    assertEqualsAsUTF8String(jsEmptyString, "");
    assertEqualsAsUTF8String(jsOneString, "1");

    checkJSStringOOB();
    checkJSStringInvalid();

    checkConstnessInJSObjectNames();

    ASSERT(JSValueIsStrictEqual(context, jsTrue, jsTrue));
    ASSERT(!JSValueIsStrictEqual(context, jsOne, jsOneString));

    ASSERT(JSValueIsEqual(context, jsOne, jsOneString, NULL));
    ASSERT(!JSValueIsEqual(context, jsTrue, jsFalse, NULL));
    
    jsGlobalValue = JSObjectMake(context, NULL, NULL);
    makeGlobalNumberValue(context);
    JSValueProtect(context, jsGlobalValue);
    JSGarbageCollect(context);
    ASSERT(JSValueIsObject(context, jsGlobalValue));
    JSValueUnprotect(context, jsGlobalValue);
    JSValueUnprotect(context, jsNumberValue);

    JSStringRef goodSyntax = JSStringCreateWithUTF8CString("x = 1;");
    const char* badSyntaxConstant = "x := 1;";
    JSStringRef badSyntax = JSStringCreateWithUTF8CString(badSyntaxConstant);
    ASSERT(JSCheckScriptSyntax(context, goodSyntax, NULL, 0, NULL));
    ASSERT(!JSCheckScriptSyntax(context, badSyntax, NULL, 0, NULL));
    ASSERT(!JSScriptCreateFromString(contextGroup, 0, 0, badSyntax, 0, 0));
    ASSERT(!JSScriptCreateReferencingImmortalASCIIText(contextGroup, 0, 0, badSyntaxConstant, strlen(badSyntaxConstant), 0, 0));

    JSValueRef result;
    JSValueRef v;
    JSObjectRef o;
    JSStringRef string;

    result = JSEvaluateScript(context, goodSyntax, NULL, NULL, 1, NULL);
    ASSERT(result);
    ASSERT(JSValueIsEqual(context, result, jsOne, NULL));

    exception = NULL;
    result = JSEvaluateScript(context, badSyntax, NULL, NULL, 1, &exception);
    ASSERT(!result);
    ASSERT(JSValueIsObject(context, exception));
    
    JSStringRef array = JSStringCreateWithUTF8CString("Array");
    JSObjectRef arrayConstructor = JSValueToObject(context, JSObjectGetProperty(context, globalObject, array, NULL), NULL);
    JSStringRelease(array);
    result = JSObjectCallAsConstructor(context, arrayConstructor, 0, NULL, NULL);
    ASSERT(result);
    ASSERT(JSValueIsObject(context, result));
    ASSERT(JSValueIsInstanceOfConstructor(context, result, arrayConstructor, NULL));
    ASSERT(!JSValueIsInstanceOfConstructor(context, JSValueMakeNull(context), arrayConstructor, NULL));

    o = JSValueToObject(context, result, NULL);
    exception = NULL;
    ASSERT(JSValueIsUndefined(context, JSObjectGetPropertyAtIndex(context, o, 0, &exception)));
    ASSERT(!exception);
    
    JSObjectSetPropertyAtIndex(context, o, 0, JSValueMakeNumber(context, 1), &exception);
    ASSERT(!exception);
    
    exception = NULL;
    ASSERT(1 == JSValueToNumber(context, JSObjectGetPropertyAtIndex(context, o, 0, &exception), &exception));
    ASSERT(!exception);

    JSStringRef functionBody;
    JSObjectRef function;
    
    exception = NULL;
    functionBody = JSStringCreateWithUTF8CString("rreturn Array;");
    JSStringRef line = JSStringCreateWithUTF8CString("line");
    ASSERT(!JSObjectMakeFunction(context, NULL, 0, NULL, functionBody, NULL, 1, &exception));
    ASSERT(JSValueIsObject(context, exception));
    v = JSObjectGetProperty(context, JSValueToObject(context, exception, NULL), line, NULL);
    assertEqualsAsNumber(v, 3);
    JSStringRelease(functionBody);
    JSStringRelease(line);

    exception = NULL;
    functionBody = JSStringCreateWithUTF8CString("rreturn Array;");
    line = JSStringCreateWithUTF8CString("line");
    ASSERT(!JSObjectMakeFunction(context, NULL, 0, NULL, functionBody, NULL, -42, &exception));
    ASSERT(JSValueIsObject(context, exception));
    v = JSObjectGetProperty(context, JSValueToObject(context, exception, NULL), line, NULL);
    assertEqualsAsNumber(v, 3);
    JSStringRelease(functionBody);
    JSStringRelease(line);

    exception = NULL;
    functionBody = JSStringCreateWithUTF8CString("// Line one.\nrreturn Array;");
    line = JSStringCreateWithUTF8CString("line");
    ASSERT(!JSObjectMakeFunction(context, NULL, 0, NULL, functionBody, NULL, 1, &exception));
    ASSERT(JSValueIsObject(context, exception));
    v = JSObjectGetProperty(context, JSValueToObject(context, exception, NULL), line, NULL);
    assertEqualsAsNumber(v, 4);
    JSStringRelease(functionBody);
    JSStringRelease(line);

    exception = NULL;
    functionBody = JSStringCreateWithUTF8CString("return Array;");
    function = JSObjectMakeFunction(context, NULL, 0, NULL, functionBody, NULL, 1, &exception);
    JSStringRelease(functionBody);
    ASSERT(!exception);
    ASSERT(JSObjectIsFunction(context, function));
    v = JSObjectCallAsFunction(context, function, NULL, 0, NULL, NULL);
    ASSERT(v);
    ASSERT(JSValueIsEqual(context, v, arrayConstructor, NULL));
    
    exception = NULL;
    function = JSObjectMakeFunction(context, NULL, 0, NULL, jsEmptyIString, NULL, 0, &exception);
    ASSERT(!exception);
    v = JSObjectCallAsFunction(context, function, NULL, 0, NULL, &exception);
    ASSERT(v && !exception);
    ASSERT(JSValueIsUndefined(context, v));
    
    exception = NULL;
    v = NULL;
    JSStringRef foo = JSStringCreateWithUTF8CString("foo");
    JSStringRef argumentNames[] = { foo };
    functionBody = JSStringCreateWithUTF8CString("return foo;");
    function = JSObjectMakeFunction(context, foo, 1, argumentNames, functionBody, NULL, 1, &exception);
    ASSERT(function && !exception);
    JSValueRef arguments[] = { JSValueMakeNumber(context, 2) };
    JSObjectCallAsFunction(context, function, NULL, 1, arguments, &exception);
    JSStringRelease(foo);
    JSStringRelease(functionBody);
    
    string = JSValueToStringCopy(context, function, NULL);
    assertEqualsAsUTF8String(JSValueMakeString(context, string), "function foo(foo\n) {\nreturn foo;\n}");
    JSStringRelease(string);

    JSStringRef print = JSStringCreateWithUTF8CString("print");
    JSObjectRef printFunction = JSObjectMakeFunctionWithCallback(context, print, print_callAsFunction);
    JSObjectSetProperty(context, globalObject, print, printFunction, kJSPropertyAttributeNone, NULL); 
    JSStringRelease(print);
    
    ASSERT(!JSObjectSetPrivate(printFunction, (void*)1));
    ASSERT(!JSObjectGetPrivate(printFunction));

    JSStringRef myConstructorIString = JSStringCreateWithUTF8CString("MyConstructor");
    JSObjectRef myConstructor = JSObjectMakeConstructor(context, NULL, myConstructor_callAsConstructor);
    JSObjectSetProperty(context, globalObject, myConstructorIString, myConstructor, kJSPropertyAttributeNone, NULL);
    JSStringRelease(myConstructorIString);
    
    JSStringRef myBadConstructorIString = JSStringCreateWithUTF8CString("MyBadConstructor");
    JSObjectRef myBadConstructor = JSObjectMakeConstructor(context, NULL, myBadConstructor_callAsConstructor);
    JSObjectSetProperty(context, globalObject, myBadConstructorIString, myBadConstructor, kJSPropertyAttributeNone, NULL);
    JSStringRelease(myBadConstructorIString);
    
    ASSERT(!JSObjectSetPrivate(myConstructor, (void*)1));
    ASSERT(!JSObjectGetPrivate(myConstructor));
    
    string = JSStringCreateWithUTF8CString("Base");
    JSObjectRef baseConstructor = JSObjectMakeConstructor(context, Base_class(context), NULL);
    JSObjectSetProperty(context, globalObject, string, baseConstructor, kJSPropertyAttributeNone, NULL);
    JSStringRelease(string);
    
    string = JSStringCreateWithUTF8CString("Derived");
    JSObjectRef derivedConstructor = JSObjectMakeConstructor(context, Derived_class(context), NULL);
    JSObjectSetProperty(context, globalObject, string, derivedConstructor, kJSPropertyAttributeNone, NULL);
    JSStringRelease(string);
    
    string = JSStringCreateWithUTF8CString("Derived2");
    JSObjectRef derived2Constructor = JSObjectMakeConstructor(context, Derived2_class(context), NULL);
    JSObjectSetProperty(context, globalObject, string, derived2Constructor, kJSPropertyAttributeNone, NULL);
    JSStringRelease(string);

    JSValueRef argumentsArrayValues[] = { JSValueMakeNumber(context, 10), JSValueMakeNumber(context, 20) };
    o = JSObjectMakeArray(context, sizeof(argumentsArrayValues) / sizeof(JSValueRef), argumentsArrayValues, NULL);
    string = JSStringCreateWithUTF8CString("length");
    v = JSObjectGetProperty(context, o, string, NULL);
    assertEqualsAsNumber(v, 2);
    v = JSObjectGetPropertyAtIndex(context, o, 0, NULL);
    assertEqualsAsNumber(v, 10);
    v = JSObjectGetPropertyAtIndex(context, o, 1, NULL);
    assertEqualsAsNumber(v, 20);

    o = JSObjectMakeArray(context, 0, NULL, NULL);
    v = JSObjectGetProperty(context, o, string, NULL);
    assertEqualsAsNumber(v, 0);
    JSStringRelease(string);

    JSValueRef argumentsDateValues[] = { JSValueMakeNumber(context, 0) };
    o = JSObjectMakeDate(context, 1, argumentsDateValues, NULL);
    if (timeZoneIsPST())
        assertEqualsAsUTF8String(o, "Wed Dec 31 1969 16:00:00 GMT-0800 (Pacific Standard Time)");

    string = JSStringCreateWithUTF8CString("an error message");
    JSValueRef argumentsErrorValues[] = { JSValueMakeString(context, string) };
    o = JSObjectMakeError(context, 1, argumentsErrorValues, NULL);
    assertEqualsAsUTF8String(o, "Error: an error message");
    JSStringRelease(string);

    string = JSStringCreateWithUTF8CString("foo");
    JSStringRef string2 = JSStringCreateWithUTF8CString("gi");
    JSValueRef argumentsRegExpValues[] = { JSValueMakeString(context, string), JSValueMakeString(context, string2) };
    o = JSObjectMakeRegExp(context, 2, argumentsRegExpValues, NULL);
    assertEqualsAsUTF8String(o, "/foo/gi");
    JSStringRelease(string);
    JSStringRelease(string2);

    JSClassDefinition nullDefinition = kJSClassDefinitionEmpty;
    nullDefinition.attributes = kJSClassAttributeNoAutomaticPrototype;
    JSClassRef nullClass = JSClassCreate(&nullDefinition);
    JSClassRelease(nullClass);
    
    nullDefinition = kJSClassDefinitionEmpty;
    nullClass = JSClassCreate(&nullDefinition);
    JSClassRelease(nullClass);

    functionBody = JSStringCreateWithUTF8CString("return this;");
    function = JSObjectMakeFunction(context, NULL, 0, NULL, functionBody, NULL, 1, NULL);
    JSStringRelease(functionBody);
    v = JSObjectCallAsFunction(context, function, NULL, 0, NULL, NULL);
    ASSERT(JSValueIsEqual(context, v, globalObject, NULL));
    v = JSObjectCallAsFunction(context, function, o, 0, NULL, NULL);
    ASSERT(JSValueIsEqual(context, v, o, NULL));

    functionBody = JSStringCreateWithUTF8CString("return eval(\"this\");");
    function = JSObjectMakeFunction(context, NULL, 0, NULL, functionBody, NULL, 1, NULL);
    JSStringRelease(functionBody);
    v = JSObjectCallAsFunction(context, function, NULL, 0, NULL, NULL);
    ASSERT(JSValueIsEqual(context, v, globalObject, NULL));
    v = JSObjectCallAsFunction(context, function, o, 0, NULL, NULL);
    ASSERT(JSValueIsEqual(context, v, o, NULL));

    const char* thisScript = "this;";
    JSStringRef script = JSStringCreateWithUTF8CString(thisScript);
    v = JSEvaluateScript(context, script, NULL, NULL, 1, NULL);
    ASSERT(JSValueIsEqual(context, v, globalObject, NULL));
    v = JSEvaluateScript(context, script, o, NULL, 1, NULL);
    ASSERT(JSValueIsEqual(context, v, o, NULL));
    JSStringRelease(script);

    JSScriptRef scriptObject = JSScriptCreateReferencingImmortalASCIIText(contextGroup, 0, 0, thisScript, strlen(thisScript), 0, 0);
    v = JSScriptEvaluate(context, scriptObject, NULL, NULL);
    ASSERT(JSValueIsEqual(context, v, globalObject, NULL));
    v = JSScriptEvaluate(context, scriptObject, o, NULL);
    ASSERT(JSValueIsEqual(context, v, o, NULL));
    JSScriptRelease(scriptObject);

    script = JSStringCreateWithUTF8CString("eval(this);");
    v = JSEvaluateScript(context, script, NULL, NULL, 1, NULL);
    ASSERT(JSValueIsEqual(context, v, globalObject, NULL));
    v = JSEvaluateScript(context, script, o, NULL, 1, NULL);
    ASSERT(JSValueIsEqual(context, v, o, NULL));
    JSStringRelease(script);

    script = JSStringCreateWithUTF8CString("[ ]");
    v = JSEvaluateScript(context, script, NULL, NULL, 1, NULL);
    ASSERT(JSValueIsArray(context, v));
    JSStringRelease(script);

    script = JSStringCreateWithUTF8CString("new Date");
    v = JSEvaluateScript(context, script, NULL, NULL, 1, NULL);
    ASSERT(JSValueIsDate(context, v));
    JSStringRelease(script);

    exception = NULL;
    script = JSStringCreateWithUTF8CString("rreturn Array;");
    JSStringRef sourceURL = JSStringCreateWithUTF8CString("file:///foo/bar.js");
    JSStringRef sourceURLKey = JSStringCreateWithUTF8CString("sourceURL");
    JSEvaluateScript(context, script, NULL, sourceURL, 1, &exception);
    ASSERT(exception);
    v = JSObjectGetProperty(context, JSValueToObject(context, exception, NULL), sourceURLKey, NULL);
    assertEqualsAsUTF8String(v, "file:///foo/bar.js");
    JSStringRelease(script);
    JSStringRelease(sourceURL);
    JSStringRelease(sourceURLKey);

    JSGlobalContextSetEvalEnabled(context, false, jsOneIString);
    exception = NULL;
    script = JSStringCreateWithUTF8CString("eval(\"3\");");
    JSEvaluateScript(context, script, NULL, NULL, 1, &exception);
    ASSERT(exception);
    JSStringRelease(script);
    exception = NULL;
    script = JSStringCreateWithUTF8CString("Function(\"return 3;\");");
    JSEvaluateScript(context, script, NULL, NULL, 1, &exception);
    ASSERT(exception);
    JSStringRelease(script);
    JSGlobalContextSetEvalEnabled(context, true, NULL);

    // Verify that creating a constructor for a class with no static functions does not trigger
    // an assert inside putDirect or lead to a crash during GC. <https://bugs.webkit.org/show_bug.cgi?id=25785>
    nullDefinition = kJSClassDefinitionEmpty;
    nullClass = JSClassCreate(&nullDefinition);
    JSObjectMakeConstructor(context, nullClass, 0);
    JSClassRelease(nullClass);

    const char* scriptPath = "./testapiScripts/testapi.js";
    char* scriptUTF8 = createStringWithContentsOfFile(scriptPath);
    if (!scriptUTF8) {
        printf("FAIL: Test script could not be loaded.\n");
        failed = 1;
    } else {
        JSStringRef url = JSStringCreateWithUTF8CString(scriptPath);
        JSStringRef script = JSStringCreateWithUTF8CString(scriptUTF8);
        JSStringRef errorMessage = 0;
        int errorLine = 0;
        JSScriptRef scriptObject = JSScriptCreateFromString(contextGroup, url, 1, script, &errorMessage, &errorLine);
        ASSERT((!scriptObject) != (!errorMessage));
        if (!scriptObject) {
            printf("FAIL: Test script did not parse\n\t%s:%d\n\t", scriptPath, errorLine);
#if USE(CF)
            CFStringRef errorCF = JSStringCopyCFString(kCFAllocatorDefault, errorMessage);
            CFShow(errorCF);
            CFRelease(errorCF);
#endif
            JSStringRelease(errorMessage);
            failed = 1;
        }

        JSStringRelease(script);
        exception = NULL;
        result = scriptObject ? JSScriptEvaluate(context, scriptObject, 0, &exception) : 0;
        if (result && JSValueIsUndefined(context, result))
            printf("PASS: Test script executed successfully.\n");
        else {
            printf("FAIL: Test script returned unexpected value:\n");
            JSStringRef exceptionIString = JSValueToStringCopy(context, exception, NULL);
#if USE(CF)
            CFStringRef exceptionCF = JSStringCopyCFString(kCFAllocatorDefault, exceptionIString);
            CFShow(exceptionCF);
            CFRelease(exceptionCF);
#endif
            JSStringRelease(exceptionIString);
            failed = 1;
        }
        JSScriptRelease(scriptObject);
        free(scriptUTF8);
    }

    // Check Promise is not exposed.
    {
        JSObjectRef globalObject = JSContextGetGlobalObject(context);
        {
            JSStringRef promiseProperty = JSStringCreateWithUTF8CString("Promise");
            ASSERT(JSObjectHasProperty(context, globalObject, promiseProperty));
            JSStringRelease(promiseProperty);
        }
        {
            JSStringRef script = JSStringCreateWithUTF8CString("typeof Promise");
            JSStringRef function = JSStringCreateWithUTF8CString("function");
            JSValueRef value = JSEvaluateScript(context, script, NULL, NULL, 1, NULL);
            ASSERT(JSValueIsString(context, value));
            JSStringRef valueAsString = JSValueToStringCopy(context, value, NULL);
            ASSERT(JSStringIsEqual(valueAsString, function));
            JSStringRelease(valueAsString);
            JSStringRelease(function);
            JSStringRelease(script);
        }
        printf("PASS: Promise is exposed under JSContext API.\n");
    }

    // Check microtasks.
    {
        JSGlobalContextRef context = JSGlobalContextCreateInGroup(NULL, NULL);
        {
            JSObjectRef globalObject = JSContextGetGlobalObject(context);
            JSValueRef exception;
            JSStringRef code = JSStringCreateWithUTF8CString("result = 0; Promise.resolve(42).then(function (value) { result = value; });");
            JSStringRef file = JSStringCreateWithUTF8CString("");
            assertTrue((bool)JSEvaluateScript(context, code, globalObject, file, 1, &exception), "An exception should not be thrown");
            JSStringRelease(code);
            JSStringRelease(file);

            JSStringRef resultProperty = JSStringCreateWithUTF8CString("result");
            ASSERT(JSObjectHasProperty(context, globalObject, resultProperty));

            JSValueRef resultValue = JSObjectGetProperty(context, globalObject, resultProperty, &exception);
            assertEqualsAsNumber(resultValue, 42);
            JSStringRelease(resultProperty);
        }
        JSGlobalContextRelease(context);
    }

    // Check JSObjectGetGlobalContext
    {
        JSGlobalContextRef context = JSGlobalContextCreateInGroup(NULL, NULL);
        {
            JSObjectRef globalObject = JSContextGetGlobalObject(context);
            assertTrue(JSObjectGetGlobalContext(globalObject) == context, "global object context is correct");
            JSObjectRef object = JSObjectMake(context, NULL, NULL);
            assertTrue(JSObjectGetGlobalContext(object) == context, "regular object context is correct");
            JSStringRef returnFunctionSource = JSStringCreateWithUTF8CString("return this;");
            JSObjectRef theFunction = JSObjectMakeFunction(context, NULL, 0, NULL, returnFunctionSource, NULL, 1, NULL);
            assertTrue(JSObjectGetGlobalContext(theFunction) == context, "function object context is correct");
            assertTrue(JSObjectGetGlobalContext(NULL) == NULL, "NULL object context is NULL");
            JSStringRelease(returnFunctionSource);
        }
        JSGlobalContextRelease(context);
    }
    failed |= testTypedArrayCAPI();
    failed |= testFunctionOverrides();
    failed |= testFunctionToString();
    failed |= testGlobalContextWithFinalizer();
    failed |= testJSONParse();
    failed |= testJSObjectGetProxyTarget();

    // Clear out local variables pointing at JSObjectRefs to allow their values to be collected
    function = NULL;
    v = NULL;
    o = NULL;
    globalObject = NULL;
    myConstructor = NULL;

    JSStringRelease(jsEmptyIString);
    JSStringRelease(jsOneIString);
    JSStringRelease(goodSyntax);
    JSStringRelease(badSyntax);

    JSGlobalContextRelease(context);
    JSClassRelease(globalObjectClass);

    // Test for an infinite prototype chain that used to be created. This test
    // passes if the call to JSObjectHasProperty() does not hang.

    JSClassDefinition prototypeLoopClassDefinition = kJSClassDefinitionEmpty;
    prototypeLoopClassDefinition.staticFunctions = globalObject_staticFunctions;
    JSClassRef prototypeLoopClass = JSClassCreate(&prototypeLoopClassDefinition);
    JSGlobalContextRef prototypeLoopContext = JSGlobalContextCreateInGroup(NULL, prototypeLoopClass);

    JSStringRef nameProperty = JSStringCreateWithUTF8CString("name");
    JSObjectHasProperty(prototypeLoopContext, JSContextGetGlobalObject(prototypeLoopContext), nameProperty);

    JSGlobalContextRelease(prototypeLoopContext);
    JSClassRelease(prototypeLoopClass);

    printf("PASS: Infinite prototype chain does not occur.\n");

    if (checkForCycleInPrototypeChain())
        printf("PASS: A cycle in a prototype chain can't be created.\n");
    else {
        printf("FAIL: A cycle in a prototype chain can be created.\n");
        failed = true;
    }
    if (valueToObjectExceptionTest())
        printf("PASS: throwException did not crash when handling an error with appendMessageToError set and no codeBlock available.\n");

    if (globalContextNameTest())
        printf("PASS: global context name behaves as expected.\n");

    customGlobalObjectClassTest();
    globalObjectSetPrototypeTest();
    globalObjectPrivatePropertyTest();
    failed |= samplingProfilerTest();

    failed |= finalizeMultithreadedMultiVMExecutionTest();

    // Don't run these tests till after the MultithreadedMultiVMExecutionTest has finished.
    // 1. testPingPongStackOverflow() changes stack size per thread configuration at runtime to very small value,
    // which can cause stack-overflow on MultithreadedMultiVMExecutionTest test.
    // 2. testExecutionTimeLimit() modifies JIT options at runtime
    // as part of its testing. This can wreak havoc on the rest of the system that
    // expects the options to be frozen. Ideally, we'll find a way for testExecutionTimeLimit()
    // to do its work without changing JIT options, but that is not easy to do.
    //
    // For now, we'll just run them here at the end as a workaround.
    failed |= testPingPongStackOverflow();
    failed |= testExecutionTimeLimit();

    if (failed) {
        printf("FAIL: Some tests failed.\n");
        return failed;
    }

    printf("PASS: Program exited normally.\n");
    return 0;
}

static char* createStringWithContentsOfFile(const char* fileName)
{
    char* buffer;
    
    size_t buffer_size = 0;
    size_t buffer_capacity = 1024;
    buffer = (char*)malloc(buffer_capacity);
    
    FILE* f = fopen(fileName, "r");
    if (!f) {
        fprintf(stderr, "Could not open file: %s\n", fileName);
        free(buffer);
        return 0;
    }
    
    while (!feof(f) && !ferror(f)) {
        buffer_size += fread(buffer + buffer_size, 1, buffer_capacity - buffer_size, f);
        if (buffer_size == buffer_capacity) { // guarantees space for trailing '\0'
            buffer_capacity *= 2;
            buffer = (char*)realloc(buffer, buffer_capacity);
            ASSERT(buffer);
        }
        
        ASSERT(buffer_size < buffer_capacity);
    }
    fclose(f);
    buffer[buffer_size] = '\0';
    
    return buffer;
}

#if OS(WINDOWS)
__declspec(dllexport) int WINAPI dllLauncherEntryPoint(int argc, char* argv[])
{
    return main(argc, argv);
}
#endif

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
