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

#pragma once

#include "kvstore_config.h"
#include "libmagma/magma.h"

class Configuration;

// This class represents the MagmaKVStore specific configuration.
// MagmaKVStore uses this in place of the KVStoreConfig base class.
class MagmaKVStoreConfig : public KVStoreConfig {
public:
    // Initialize the object from the central EPEngine Configuration
    MagmaKVStoreConfig(Configuration& config,
                       uint16_t numShards,
                       uint16_t shardid);

    size_t getBucketQuota() {
        return bucketQuota;
    }
    size_t getMagmaDeleteMemtableWritecache() const {
        return magmaDeleteMemtableWritecache;
    }
    float getMagmaDeleteFragRatio() const {
        return magmaDeleteFragRatio;
    }
    size_t getMagmaMaxCommitPoints() const {
        return magmaMaxCommitPoints;
    }
    size_t getMagmaCommitPointInterval() const {
        return magmaCommitPointInterval;
    }
    size_t getMagmaValueSeparationSize() const {
        return magmaValueSeparationSize;
    }
    size_t getMagmaMinWriteCache() const {
        return magmaMinWriteCache;
    }
    size_t getMagmaMaxWriteCache() const {
        return magmaMaxWriteCache;
    }
    float getMagmaMemQuotaRatio() const {
        return magmaMemQuotaRatio;
    }
    size_t getMagmaWalBufferSize() const {
        return magmaWalBufferSize;
    }
    size_t getMagmaWalNumBuffers() const {
        return magmaWalNumBuffers;
    }
    size_t getMagmaNumFlushers() const {
        return magmaNumFlushers;
    }
    size_t getMagmaNumCompactors() const {
        return magmaNumCompactors;
    }
    bool getMagmaCommitPointEveryBatch() const {
        return magmaCommitPointEveryBatch;
    }
    bool getMagmaEnableUpsert() const {
        return magmaEnableUpsert;
    }
    float getMagmaExpiryFragThreshold() const {
        return magmaExpiryFragThreshold;
    }
    float getMagmaTombstoneFragThreshold() const {
        return magmaTombstoneFragThreshold;
    }

    magma::Magma::Config magmaCfg;

private:
    // Bucket RAM Quota
    size_t bucketQuota;

    // Magma uses a lazy update model to maintain the sequence index. It
    // maintains a list of deleted seq #s that were deleted from the key Index.
    size_t magmaDeleteMemtableWritecache;

    // Magma compaction runs frequently and applies all methods of compaction
    // (removal of duplicates, expiry, tombstone removal) but it does not always
    // visit every sstable. In order to run compaction over less visited
    // sstables, magma uses a variety of methods to determine which range of
    // sstables need visited.
    //
    // This is the minimum fragmentation ratio for when magma will trigger
    // compaction based on the number of duplicate keys removed.
    float magmaDeleteFragRatio;

    // Magma keeps track of expiry histograms per sstable to determine
    // when an expiry compaction should be run. The fragmentation threshold
    // applies across all the kvstore but only specific sstables will be
    // visited.
    float magmaExpiryFragThreshold;

    // Magma keeps track of tombstone count to determine when a tombstone
    // compaction should be run. The fragmentation threshold applies across
    // all the kvstore but only specific sstables will be visited.
    float magmaTombstoneFragThreshold;

    // Max commit points that can be rolled back to
    int magmaMaxCommitPoints;

    // Time interval (in minutes) between commit points
    size_t magmaCommitPointInterval;

    // Magma minimum value for key value separation.
    // Values < magmaValueSeparationSize, value remains in key index.
    size_t magmaValueSeparationSize;

    // Magma uses a common skiplist to buffer all items at the shard level
    // called the write cache. The write cache contains items from all the
    // kvstores that are part of the shard and when it is flushed, each
    // kvstore will receive a few items each.
    //
    // A too large write cache size can lead to high space amplification.
    // A too small write cache size can lead to space amplfication issues.
    size_t magmaMinWriteCache;
    size_t magmaMaxWriteCache;

    // Magma Memory Quota as a ratio of Bucket Quota
    float magmaMemQuotaRatio;

    // Magma uses a write ahead log to quickly persist items during bg
    // flushing. This buffer contains the items along with control records
    // like begin/end transaction. It can be flushed many times for a batch
    // of items.
    size_t magmaWalBufferSize;

    // When batches of items are large, magma WAL can take advantage of double
    // buffering to speed up persistence.
    size_t magmaWalNumBuffers;

    // Number of background threads to flush filled memtables to disk
    size_t magmaNumFlushers;

    // Number of background compactor threads
    size_t magmaNumCompactors;

    // Used in testing to make sure each batch is flushed to disk to simulate
    // how couchstore flushes each batch to disk.
    bool magmaCommitPointEveryBatch;

    // When true, the kv_engine will utilize Magma's upsert capabiltiy
    // but accurate document counts for the data store or collections can
    // not be maintained.
    bool magmaEnableUpsert;
};
