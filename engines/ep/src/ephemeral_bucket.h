/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2016 Couchbase, Inc.
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

#include "kv_bucket.h"

/* Forward declarations */
class RollbackResult;

/**
 * Ephemeral Bucket
 *
 * A bucket type without any persistent data storage. Similar to memcache (default)
 * buckets, except with VBucket goodness - replication, rebalance, failover.
 */

class EphemeralBucket : public KVBucket {
public:
    EphemeralBucket(EventuallyPersistentEngine& theEngine);

    ~EphemeralBucket();

    bool initialize() override;

    /// Eviction not supported for Ephemeral buckets - without some backing
    /// storage, there is nowhere to evict /to/.
    protocol_binary_response_status evictKey(const DocKey& key,
                                             uint16_t vbucket,
                                             const char** msg) override {
        return PROTOCOL_BINARY_RESPONSE_NOT_SUPPORTED;
    }

    /// File stats not supported for Ephemeral buckets.
    ENGINE_ERROR_CODE getFileStats(const void* cookie,
                                   ADD_STAT add_stat) override {
        return ENGINE_KEY_ENOENT;
    }

    /// Disk stats not supported for Ephemeral buckets.
    ENGINE_ERROR_CODE getPerVBucketDiskStats(const void* cookie,
                                             ADD_STAT add_stat) override {
        return ENGINE_KEY_ENOENT;
    }

    /**
     * Creates an EphemeralVBucket
     */
    VBucketPtr makeVBucket(VBucket::id_type id,
                               vbucket_state_t state,
                               KVShard* shard,
                               std::unique_ptr<FailoverTable> table,
                               NewSeqnoCallback newSeqnoCb,
                               vbucket_state_t initState,
                               int64_t lastSeqno,
                               uint64_t lastSnapStart,
                               uint64_t lastSnapEnd,
                               uint64_t purgeSeqno,
                               uint64_t maxCas,
                               const std::string& collectionsManifest) override;

    /// Do nothing - no flusher to notify
    void notifyFlusher(const uint16_t vbid) override {
    }

    ENGINE_ERROR_CODE statsVKey(const DocKey& key,
                                uint16_t vbucket,
                                const void* cookie) override {
        return ENGINE_ENOTSUP;
    }

    void completeStatsVKey(const void* cookie,
                           const DocKey& key,
                           uint16_t vbid,
                           uint64_t bySeqNum) override;

    RollbackResult doRollback(uint16_t vbid, uint64_t rollbackSeqno) override;

    void rollbackUnpersistedItems(VBucket& vb, int64_t rollbackSeqno) override {
        // No op
    }

    size_t getNumPersistedDeletes(uint16_t vbid) override;

    void notifyNewSeqno(const uint16_t vbid,
                        const VBNotifyCtx& notifyCtx) override;

    /**
     * Enables the Ephemeral Tombstone purger task (if not already enabled).
     * This runs periodically, and based on memory pressure.
     */
    void enableTombstonePurgerTask();

    /**
     * Disables the Ephemeral Tombstone purger task (if enabled).
     */
    void disableTombstonePurgerTask();

    // Static methods /////////////////////////////////////////////////////////

    /** Apply necessary modifications to the Configuration for an Ephemeral
     *  bucket (e.g. disable features which are not applicable).
     * @param config Configuration to modify.
     */
    static void reconfigureForEphemeral(Configuration& config);

protected:
    std::unique_ptr<VBucketCountVisitor> makeVBCountVisitor(
            vbucket_state_t state) override;

    void appendAggregatedVBucketStats(VBucketCountVisitor& active,
                                      VBucketCountVisitor& replica,
                                      VBucketCountVisitor& pending,
                                      VBucketCountVisitor& dead,
                                      const void* cookie,
                                      ADD_STAT add_stat) override;

    // Protected member variables /////////////////////////////////////////////

    /// Task responsible for purging in-memory tombstones.
    ExTask tombstonePurgerTask;

private:
    /**
     * Task responsible for notifying high priority requests (usually during
     * rebalance)
     */
    class NotifyHighPriorityReqTask : public GlobalTask {
    public:
        NotifyHighPriorityReqTask(EventuallyPersistentEngine& e);

        bool run() override;

        cb::const_char_buffer getDescription() override;

        /**
         * Adds the connections to be notified by the task and then wakes up
         * the task.
         *
         * @param notifies Map of connections to be notified
         */
        void wakeup(std::map<const void*, ENGINE_ERROR_CODE> notifies);

    private:
        /* All the notifications to be called by the task */
        std::map<const void*, ENGINE_ERROR_CODE> toNotify;

        /* Serialize access to write/read of toNotify */
        std::mutex toNotifyLock;
    };

    // Private member variables ///////////////////////////////////////////////
    SingleThreadedRCPtr<NotifyHighPriorityReqTask> notifyHpReqTask;
};
