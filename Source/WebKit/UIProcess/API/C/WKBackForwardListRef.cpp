/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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

#include "config.h"
#include "WKBackForwardListRef.h"

#include "APIArray.h"
#include "WKAPICast.h"
#include "WebBackForwardList.h"
#include "WebFrameProxy.h"

using namespace WebKit;

WKTypeID WKBackForwardListGetTypeID()
{
    return toAPI(WebBackForwardList::APIType);
}

WKBackForwardListItemRef WKBackForwardListGetCurrentItem(WKBackForwardListRef listRef)
{
    return toAPI(toProtectedImpl(listRef)->protectedCurrentItem().get());
}

WKBackForwardListItemRef WKBackForwardListGetBackItem(WKBackForwardListRef listRef)
{
    return toAPI(toProtectedImpl(listRef)->protectedBackItem().get());
}

WKBackForwardListItemRef WKBackForwardListGetForwardItem(WKBackForwardListRef listRef)
{
    return toAPI(toProtectedImpl(listRef)->protectedForwardItem().get());
}

WKBackForwardListItemRef WKBackForwardListGetItemAtIndex(WKBackForwardListRef listRef, int index)
{
    return toAPI(toProtectedImpl(listRef)->protectedItemAtIndex(index).get());
}

void WKBackForwardListClear(WKBackForwardListRef listRef)
{
    toProtectedImpl(listRef)->clear();
}

unsigned WKBackForwardListGetBackListCount(WKBackForwardListRef listRef)
{
    return toProtectedImpl(listRef)->backListCount();
}

unsigned WKBackForwardListGetForwardListCount(WKBackForwardListRef listRef)
{
    return toProtectedImpl(listRef)->forwardListCount();
}

WKArrayRef WKBackForwardListCopyBackListWithLimit(WKBackForwardListRef listRef, unsigned limit)
{
    return toAPILeakingRef(toProtectedImpl(listRef)->backListAsAPIArrayWithLimit(limit));
}

WKArrayRef WKBackForwardListCopyForwardListWithLimit(WKBackForwardListRef listRef, unsigned limit)
{
    return toAPILeakingRef(toProtectedImpl(listRef)->forwardListAsAPIArrayWithLimit(limit));
}
