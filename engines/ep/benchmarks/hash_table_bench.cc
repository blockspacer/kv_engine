/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2018 Couchbase, Inc
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

#include "configuration.h"
#include "hash_table.h"
#include "item.h"
#include "module_tests/test_helpers.h"
#include "stats.h"
#include "stored_value_factories.h"

#include <benchmark/benchmark.h>
#include <engines/ep/src/syncobject.h>
#include <gtest/gtest.h>
#include <spdlog/fmt/fmt.h>

// Benchmarks inserting items into a HashTable
class HashTableBench : public benchmark::Fixture {
public:
    HashTableBench()
        : ht(stats,
             std::make_unique<StoredValueFactory>(stats),
             Configuration().getHtSize(),
             Configuration().getHtLocks()) {
    }

    void SetUp(benchmark::State& state) {
        if (state.thread_index == 0) {
            ht.resize(numItems);
        }
    }

    void TearDown(benchmark::State& state) {
        if (state.thread_index == 0) {
            ht.clear();
        }
    }

    /**
     * Create numItems Items, givign each key the given prefix.
     * @param prefix String to prefix each key with.
     * @param pendingSyncWritesPcnt If non-zero, create additional
     *        pendingSyncWrites for the given percentage of items. For example
     *        a value of 20 will create an extra 20% of Items which are
     *        Prepared SyncWrites.
     */
    std::vector<Item> createItems(std::string prefix,
                                  int pendingSyncWritesPcnt = 0) {
        std::vector<Item> items;
        items.reserve(numItems);
        // Just use a minimal item (Blob) size - we are focusing on
        // benchmarking the HashTable's methods, don't really care about
        // cost of creating Item / StoredValue objects here.
        const size_t itemSize = 1;
        const auto data = std::string(itemSize, 'x');
        for (size_t i = 0; i < numItems; i++) {
            // Use fmtlib to format key with stack-local (non-heap) buffer to
            // minimise the cost of constructing keys for Items.
            fmt::memory_buffer keyBuf;
            format_to(keyBuf, "{}{}", prefix, i);
            DocKey key(keyBuf.data(), DocKeyEncodesCollectionId::No);
            items.emplace_back(key, 0, 0, data.data(), data.size());

            if (pendingSyncWritesPcnt > 0) {
                if (i % (100 / pendingSyncWritesPcnt) == 0) {
                    items.emplace_back(key, 0, 0, data.data(), data.size());
                    items.back().setPendingSyncWrite({});
                }
            }
        }

        return items;
    }

    EPStats stats;
    HashTable ht;
    static const size_t numItems = 100000;
    /// Shared vector of items for tests which want to use the same
    /// data across multiple threads.
    std::vector<Item> sharedItems;
    // Shared synchronization object and mutex, needed by some benchmarks to
    // coordinate their execution phases.
    std::mutex mutex;
    SyncObject syncObject;
    int waiters = 0;
};

// Benchmark finding items in the HashTable.
// Includes extra  50% of Items are prepared SyncWrites -  an unrealistically
// high percentage in a real-world, but want to measure any performance impact
// in having such items present in the HashTable.
BENCHMARK_DEFINE_F(HashTableBench, FindForRead)(benchmark::State& state) {
    // Populate the HashTable with numItems.
    if (state.thread_index == 0) {
        sharedItems = createItems(
                "Thread" + std::to_string(state.thread_index) + "::", 50);
        for (auto& item : sharedItems) {
            ASSERT_EQ(MutationStatus::WasClean, ht.set(item));
        }
    }

    // Benchmark - find them.
    while (state.KeepRunning()) {
        auto& key = sharedItems[state.iterations() % numItems].getKey();
        benchmark::DoNotOptimize(ht.findForRead(key));
    }

    state.SetItemsProcessed(state.iterations());
}

// Benchmark finding items (for write) in the HashTable.
// Includes extra  50% of Items are prepared SyncWrites -  an unrealistically
// high percentage in a real-world, but want to measure any performance impact
// in having such items present in the HashTable.
BENCHMARK_DEFINE_F(HashTableBench, FindForWrite)(benchmark::State& state) {
    // Populate the HashTable with numItems.
    if (state.thread_index == 0) {
        sharedItems = createItems(
                "Thread" + std::to_string(state.thread_index) + "::", 50);
        for (auto& item : sharedItems) {
            ASSERT_EQ(MutationStatus::WasClean, ht.set(item));
        }
    }

    // Benchmark - find them.
    while (state.KeepRunning()) {
        auto& key = sharedItems[state.iterations() % numItems].getKey();
        benchmark::DoNotOptimize(ht.findForWrite(key));
    }

    state.SetItemsProcessed(state.iterations());
}

// Benchmark inserting an item into the HashTable.
BENCHMARK_DEFINE_F(HashTableBench, Insert)(benchmark::State& state) {
    // To ensure we insert and not replace items, create a per-thread items
    // vector so each thread inserts a different set of items.
    auto items =
            createItems("Thread" + std::to_string(state.thread_index) + "::");

    while (state.KeepRunning()) {
        const auto index = state.iterations() % numItems;
        ASSERT_EQ(MutationStatus::WasClean, ht.set(items[index]));

        // Once a thread gets to the end of it's items; pause timing and let
        // the *last* thread clear them all - this is to avoid measuring any
        // of the ht.clear() cost indirectly when other threads are tyring to
        // insert.
        // Note: state.iterations() starts at 0; hence checking for
        // state.iterations() % numItems (aka 'index') is zero to represent we
        // wrapped.
        if (index == 0) {
            state.PauseTiming();
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (++waiters < state.threads) {
                    // Last thread to enter - clear HashTable and wake up the
                    // rest.
                    ht.clear();
                    waiters = 0;
                    syncObject.notify_all();
                } else {
                    // Not yet the last thread - wait for the last guy to do the
                    // cleanup.
                    syncObject.wait(lock, [this]() { return waiters == 0; });
                }
            }
            state.ResumeTiming();
        }
    }

    state.SetItemsProcessed(state.iterations());
}

// Benchmark replacing an existing item in the HashTable.
BENCHMARK_DEFINE_F(HashTableBench, Replace)(benchmark::State& state) {
    // Populate the HashTable with numItems.
    auto items =
            createItems("Thread" + std::to_string(state.thread_index) + "::");
    for (auto& item : items) {
        ASSERT_EQ(MutationStatus::WasClean, ht.set(item));
    }

    // Benchmark - update them.
    while (state.KeepRunning()) {
        ASSERT_EQ(MutationStatus::WasDirty,
                  ht.set(items[state.iterations() % numItems]));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_DEFINE_F(HashTableBench, Delete)(benchmark::State& state) {
    auto items =
            createItems("Thread" + std::to_string(state.thread_index) + "::");

    while (state.KeepRunning()) {
        const auto index = state.iterations() % numItems;

        // Populate the HashTable every numItems iterations.
        //
        // Once a thread deletes all of it's items; pause timing and let
        // the *last* thread re-populate the HashTable (so we can continue to
        // delete)
        // - this is to avoid measuring any of the re-populate cost while
        // other threads are trying to delete.
        if (index == 1) {
            state.PauseTiming();
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (++waiters < state.threads) {
                    // Last thread to enter - re-populate HashTable and wake up
                    // the rest.
                    for (auto& item : items) {
                        ASSERT_EQ(MutationStatus::WasClean, ht.set(item));
                    }
                    waiters = 0;
                    syncObject.notify_all();
                } else {
                    // Not yet the last thread - wait for the last guy to do the
                    // re-populate.
                    syncObject.wait(lock, [this]() { return waiters == 0; });
                }
            }
            state.ResumeTiming();
        }

        auto& key = items[index].getKey();
        {
            auto result = ht.findForWrite(key);
            ASSERT_TRUE(result.storedValue);
            ht.unlocked_del(result.lock, key);
        }
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(HashTableBench, FindForRead)
        ->ThreadPerCpu()
        ->Iterations(HashTableBench::numItems);
BENCHMARK_REGISTER_F(HashTableBench, FindForWrite)
        ->ThreadPerCpu()
        ->Iterations(HashTableBench::numItems);
BENCHMARK_REGISTER_F(HashTableBench, Insert)
        ->ThreadPerCpu()
        ->Iterations(HashTableBench::numItems);
BENCHMARK_REGISTER_F(HashTableBench, Replace)
        ->ThreadPerCpu()
        ->Iterations(HashTableBench::numItems);
BENCHMARK_REGISTER_F(HashTableBench, Delete)
        ->ThreadPerCpu()
        ->Iterations(HashTableBench::numItems);
