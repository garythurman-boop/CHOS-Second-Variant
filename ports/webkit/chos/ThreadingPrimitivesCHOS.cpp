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

// Native private pthread bindings. Thread lifecycle and GC suspension are
// separate platform services; no signal-based POSIX implementation is selected.
#include "config.h"
#include <wtf/ThreadingPrimitives.h>
#include <errno.h>
#include <time.h>

#if !OS(CHOS) || !USE(PTHREADS)
#error "This backend requires the explicit CHOS pthread profile"
#endif

namespace WTF {
Mutex::~Mutex()
{
    RELEASE_ASSERT(!pthread_mutex_destroy(&m_mutex));
}
void Mutex::lock()
{
    RELEASE_ASSERT(!pthread_mutex_lock(&m_mutex));
}
bool Mutex::tryLock()
{
    int result = pthread_mutex_trylock(&m_mutex);
    RELEASE_ASSERT(!result || result == EBUSY);
    return !result;
}
void Mutex::unlock()
{
    RELEASE_ASSERT(!pthread_mutex_unlock(&m_mutex));
}
ThreadCondition::~ThreadCondition()
{
    RELEASE_ASSERT(!pthread_cond_destroy(&m_condition));
}
void ThreadCondition::wait(Mutex& mutex)
{
    RELEASE_ASSERT(!pthread_cond_wait(&m_condition, &mutex.impl()));
}
bool ThreadCondition::timedWait(Mutex& mutex, WallTime absoluteTime)
{
    // Subtraction uses the original inline time representation. Avoid a fake
    // WallTime::now implementation: the native condition checks CLOCK_REALTIME.
    double raw = (absoluteTime - WallTime()).value();
    if (absoluteTime.isNaN() || raw <= 0)
        return false;
    if (absoluteTime.isInfinity() || raw >= 0x1p63) {
        wait(mutex);
        return true;
    }
    // Validate before converting: double(INT64_MAX) rounds to 2^63.
    timespec deadline;
    deadline.tv_sec = static_cast<time_t>(raw);
    double nanoseconds = (raw - static_cast<double>(deadline.tv_sec)) * 1e9;
    deadline.tv_nsec = nanoseconds >= 1e9 ? 999999999L : static_cast<long>(nanoseconds);
    int result = pthread_cond_timedwait(&m_condition, &mutex.impl(), &deadline);
    RELEASE_ASSERT(!result || result == ETIMEDOUT);
    return !result;
}
void ThreadCondition::signal()
{
    RELEASE_ASSERT(!pthread_cond_signal(&m_condition));
}
void ThreadCondition::broadcast()
{
    RELEASE_ASSERT(!pthread_cond_broadcast(&m_condition));
}
} // namespace WTF
