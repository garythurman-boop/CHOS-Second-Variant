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

// Native worker creation and current-thread adoption, using the upstream
// context handshake and two-pass TLS destruction. Foreign GC suspension needs
// a separate kernel coordination service and is deliberately not provided here.
#include "config.h"
#include <wtf/Threading.h>
#include <wtf/Locker.h>
#include <chos/user.h>
#include <errno.h>

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
static void* nativeThreadEntryPoint(void* context)
{
    Thread::entryPoint(static_cast<Thread::NewThreadContext*>(context));
    return nullptr;
}
bool Thread::establishHandle(NewThreadContext& context, StackAllocationSpecification stackSpec, QOS qos, SchedulingPolicy schedulingPolicy)
{
    // CHOS currently admits ordinary scheduler workers, with owned anonymous
    // stacks only. Reject unsupported policies/locations before creating one.
    if (schedulingPolicy != SchedulingPolicy::Other || stackSpec.kind() == StackAllocationSpecification::Kind::SizeAndLocation)
        return false;
    UNUSED_PARAM(qos); // Advisory hint; no native priority/affinity contract.
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes))
        return false;
    if (stackSpec.kind() == StackAllocationSpecification::Kind::SizeOnly && pthread_attr_setstacksize(&attributes, stackSpec.sizeBytes())) {
        RELEASE_ASSERT(!pthread_attr_destroy(&attributes));
        return false;
    }
    pthread_t handle;
    int result = pthread_create(&handle, &attributes, nativeThreadEntryPoint, &context);
    RELEASE_ASSERT(!pthread_attr_destroy(&attributes));
    if (result)
        return false;
    establishPlatformSpecificHandle(handle);
    return true;
}
void Thread::initializeCurrentThreadInternal(const char* name)
{
    UNUSED_PARAM(name); // No kernel thread-name service; no success is fabricated.
    chos_thread_context_t context { };
    RELEASE_ASSERT(!chos_thread_context(&context));
    constexpr unsigned required = CHOS_THREAD_CONTEXT_CURRENT | CHOS_THREAD_CONTEXT_REGISTERS_VALID | CHOS_THREAD_CONTEXT_STACK_VALID;
    RELEASE_ASSERT(context.version == CHOS_THREAD_CONTEXT_VERSION && context.size_bytes == sizeof(context));
    RELEASE_ASSERT((context.flags & required) == required && context.tid && context.stack_bytes);
    RELEASE_ASSERT(context.tid == static_cast<uint32_t>(chos_thread_id()));
    RELEASE_ASSERT(context.sp >= context.stack_base && context.sp <= context.stack_base + context.stack_bytes);
}
void Thread::initializeSchedulingAttributes()
{
    RELEASE_ASSERT(m_schedulingPolicy == SchedulingPolicy::Other);
}
void Thread::updateSchedulingAttributes(SchedulingState state) const
{
    // QOS/demotion are hints on this platform. Do not claim priority changes.
    UNUSED_PARAM(state);
    RELEASE_ASSERT(m_schedulingPolicy == SchedulingPolicy::Other);
}
int Thread::waitForCompletion()
{
    PlatformThreadHandle handle;
    {
        Locker locker { m_mutex };
        if (joinableState() != Joinable)
            return EINVAL;
        // Claim before blocking: didExit() needs m_mutex during key cleanup.
        // On CHOS Joined also prevents a competing operation from touching a
        // pthread_t whose TCB is freed by successful join. didExit is independent
        // of this state. Restore it if the native join refuses (e.g. self join).
        didJoin();
        handle = m_handle;
    }
    int result = pthread_join(handle, nullptr);
    if (result) {
        Locker locker { m_mutex };
        m_joinableState = Joinable;
    }
    return result;
}
void Thread::detach()
{
    Locker locker { m_mutex };
    RELEASE_ASSERT(joinableState() == Joinable);
    RELEASE_ASSERT(!pthread_detach(m_handle));
    // Even an already-exited native TCB is released by detach. Retain the
    // guard after exit so repeated WTF operations never access a freed handle.
    didBecomeDetached();
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
