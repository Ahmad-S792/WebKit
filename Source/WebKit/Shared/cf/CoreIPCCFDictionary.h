/*
 * Copyright (C) 2024 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if USE(CF)

#include <wtf/RetainPtr.h>
#include <wtf/Vector.h>

namespace WebKit {

class CoreIPCCFType;
class CoreIPCCFArray;
class CoreIPCBoolean;
class CoreIPCCFCharacterSet;
class CoreIPCData;
class CoreIPCDate;
class CoreIPCCFDictionary;
class CoreIPCNull;
class CoreIPCNumber;
class CoreIPCString;
class CoreIPCCFURL;

class CoreIPCCFDictionary {
public:
    using KeyType = Variant<CoreIPCCFArray, CoreIPCBoolean, CoreIPCCFCharacterSet, CoreIPCData, CoreIPCDate, CoreIPCCFDictionary, CoreIPCNull, CoreIPCNumber, CoreIPCString, CoreIPCCFURL>;
    using KeyValueVector = Vector<KeyValuePair<KeyType, CoreIPCCFType>>;

    CoreIPCCFDictionary(CFDictionaryRef);
    CoreIPCCFDictionary(CoreIPCCFDictionary&&);
    CoreIPCCFDictionary(std::unique_ptr<KeyValueVector>&&);
    ~CoreIPCCFDictionary();
    RetainPtr<CFDictionaryRef> createCFDictionary() const;
    const std::unique_ptr<KeyValueVector>& vector() const { return m_vector; }
private:
    std::unique_ptr<KeyValueVector> m_vector;
};

} // namespace WebKit

#endif // USE(CF)
