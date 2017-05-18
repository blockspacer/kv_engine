/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2017 Couchbase, Inc
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#pragma once

#include <memcached/allocator_hooks.h>
#include <atomic>
#include <mutex>

/*
 * A singleton which tracks memory usage for use in benchmarks.
 *
 * This class provides hooks for new and delete which are registered when the
 * singleton is created.
 *
 * Tracks the current allocation along with the maximum total allocation size
 * it has seen.
 */
class BenchmarkMemoryTracker {
public:
    ~BenchmarkMemoryTracker();

    static BenchmarkMemoryTracker* getInstance(
            const ALLOCATOR_HOOKS_API& hooks_api_);

    static void destroyInstance();

    void reset();

    size_t getMaxAlloc();
    size_t getCurrentAlloc();

private:
    BenchmarkMemoryTracker(const ALLOCATOR_HOOKS_API& hooks_api);
    void connectHooks();
    static void NewHook(const void* ptr, size_t);
    static void DeleteHook(const void* ptr);

    static std::atomic<BenchmarkMemoryTracker*> instance;
    static std::mutex instanceMutex;
    ALLOCATOR_HOOKS_API hooks_api;
    static std::atomic<size_t> maxTotalAllocation;
    static std::atomic<size_t> currentAlloc;
};
