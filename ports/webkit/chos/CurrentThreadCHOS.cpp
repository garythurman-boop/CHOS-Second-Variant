/*
 * Copyright (C) 2007, 2009, 2015 Apple Inc. All rights reserved.
 * Copyright (C) 2007 Justin Haygood <jhaygood@reaktix.com>
 * Copyright (C) 2011 Research In Motion Limited. All rights reserved.
 * Copyright (C) 2017 Yusuke Suzuki <utatane.tea@gmail.com>
 * Copyright (C) 2026 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Native current-thread adoption, using the upstream ownership and two-pass TLS
// destruction algorithm. Creation/join and foreign GC suspension are separate
// services and are not provided by this current-thread backend.
#include "config.h"
#include <wtf/Threading.h>
#include <wtf/Locker.h>
#include <chos/user.h>

namespace WTF {
Thread::~Thread() = default;
void Thread::initializeTLSKey()
{
    threadSpecificKeyCreate(&s_key, destructTLS);
}
void Thread::establishPlatformSpecificHandle(PlatformThreadHandle handle)
{
    Locker locker { m_mutex };
    m_handle = handle;
}
Thread& Thread::initializeCurrentTLS()
{
    WTF::initialize();
    Ref thread = adoptRef(*new Thread(defaultQOS, SchedulingPolicy::Other));
    thread->establishPlatformSpecificHandle(pthread_self());
    thread->initializeInThread(); // Actual checked current-stack service.
    return initializeTLS(WTF::move(thread));
}
Thread& Thread::initializeTLS(Ref<Thread>&& thread)
{
    auto& retained = thread.leakRef();
    threadSpecificSet(s_key, &retained);
    return retained;
}
void Thread::destructTLS(void* data)
{
    auto* thread = static_cast<Thread*>(data);
    RELEASE_ASSERT(thread);
    if (thread->m_isDestroyedOnce) {
        thread->didExit();
        thread->deref();
        return;
    }
    thread->m_isDestroyedOnce = true;
    threadSpecificSet(s_key, thread);
    thread->m_clientData = nullptr;
}
void Thread::yield()
{
    RELEASE_ASSERT(!chos_yield());
}
}
